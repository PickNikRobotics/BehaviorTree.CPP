#include <gtest/gtest.h>

#include "behaviortree_cpp/xml_diff.h"

TEST(XMLDiff, UnchangedInputHasNoChanges)
{
  constexpr std::string_view xml = R"(
<root BTCPP_format="4">
  <BehaviorTree ID="Main">
    <Sequence name="root">
      <MoveBase goal="dock"/>
    </Sequence>
  </BehaviorTree>
</root>
)";

  EXPECT_EQ(BT::RenderXMLDiff(xml, xml), "## Behavior Tree XML Diff\n\nNo changes.\n");
}

TEST(XMLDiff, WrapperInsertionIsAdditionAndMove)
{
  constexpr std::string_view before = R"(
<root BTCPP_format="4">
  <BehaviorTree ID="Main">
    <Sequence name="root">
      <Sequence name="work">
        <MoveBase goal="dock"/>
        <Report result="done"/>
      </Sequence>
    </Sequence>
  </BehaviorTree>
</root>
)";
  constexpr std::string_view after = R"(
<root BTCPP_format="4">
  <BehaviorTree ID="Main">
    <Sequence name="root">
      <Fallback name="retry">
        <Sequence name="work">
          <MoveBase goal="dock"/>
          <Report result="done"/>
        </Sequence>
      </Fallback>
    </Sequence>
  </BehaviorTree>
</root>
)";

  const std::string diff = BT::RenderXMLDiff(before, after);
  EXPECT_NE(diff.find("### Added"), std::string::npos);
  EXPECT_NE(diff.find("+ <Fallback name=\"retry\">"), std::string::npos);
  EXPECT_NE(diff.find("### Moved"), std::string::npos);
  EXPECT_NE(diff.find("<Sequence name=\"work\">"), std::string::npos);
  EXPECT_EQ(diff.find("- <MoveBase"), std::string::npos);
  EXPECT_EQ(diff.find("+ <MoveBase"), std::string::npos);
}

TEST(XMLDiff, AttributeEditInsideMovedSubtreeIsMovedAndModified)
{
  constexpr std::string_view before = R"(
<root><BehaviorTree ID="Main"><Sequence name="root">
  <Sequence name="work"><MoveBase goal="dock" speed="slow"/></Sequence>
</Sequence></BehaviorTree></root>)";
  constexpr std::string_view after = R"(
<root><BehaviorTree ID="Main"><Sequence name="root"><RetryUntilSuccessful num_attempts="3">
  <Sequence name="work"><MoveBase goal="dock" speed="fast"/></Sequence>
</RetryUntilSuccessful></Sequence></BehaviorTree></root>)";

  const std::string diff = BT::RenderXMLDiff(before, after);
  EXPECT_NE(diff.find("### Moved and modified"), std::string::npos);
  EXPECT_NE(diff.find("-   <MoveBase goal=\"dock\" speed=\"slow\"/>"), std::string::npos);
  EXPECT_NE(diff.find("+   <MoveBase goal=\"dock\" speed=\"fast\"/>"), std::string::npos);
  EXPECT_EQ(diff.find("### Removed"), std::string::npos);
}

TEST(XMLDiff, MovedAncestorIgnoresModificationMovedOutsideItsMatch)
{
  constexpr std::string_view before =
      R"(<root><Left><Container name="p"><Leaf value="old"/></Container></Left><Right/></root>)";
  constexpr std::string_view after =
      R"(<root><Left><Leaf value="new"/></Left><Right><Container name="p"/></Right></root>)";

  const std::string diff = BT::RenderXMLDiff(before, after, BT::XMLDiffFormat::PlainText);
  const size_t moved_section = diff.find("Moved\n-----\n");
  const size_t container_move = diff.find("/root/Left[1]/Container[1] \xE2\x86\x92 "
                                          "/root/Right[1]/Container[1]");
  EXPECT_NE(moved_section, std::string::npos);
  EXPECT_NE(container_move, std::string::npos);
  EXPECT_GT(container_move, moved_section);
}

TEST(XMLDiff, AddedWrapperShowsFailureBranchAndRetainedContent)
{
  constexpr std::string_view before = R"(
<root><BehaviorTree ID="Main"><Sequence name="mission">
  <Sequence name="execute"><Plan/><Execute/></Sequence>
</Sequence></BehaviorTree></root>)";
  constexpr std::string_view after = R"(
<root><BehaviorTree ID="Main"><Sequence name="mission"><Fallback name="recoverable">
  <Sequence name="execute"><Plan/><Execute/></Sequence>
  <Sequence name="failure"><LogError code="planning_failed"/><ForceFailure/></Sequence>
</Fallback></Sequence></BehaviorTree></root>)";

  const std::string diff = BT::RenderXMLDiff(before, after);
  EXPECT_NE(diff.find("+ <Fallback name=\"recoverable\">"), std::string::npos);
  EXPECT_NE(diff.find("<!-- retained subtree -->"), std::string::npos);
  EXPECT_NE(diff.find("+   <Sequence name=\"failure\">"), std::string::npos);
  EXPECT_NE(diff.find("+     <LogError code=\"planning_failed\"/>"), std::string::npos);
  EXPECT_NE(diff.find("+     <ForceFailure/>"), std::string::npos);
  EXPECT_EQ(diff.find("+   <Sequence name=\"execute\">"), std::string::npos);
}

TEST(XMLDiff, RemovedSubtreeIsCoalescedUnderItsHighestRoot)
{
  constexpr std::string_view before = R"(
<root><BehaviorTree ID="Main"><Sequence name="mission">
  <Sequence name="keep"><A/><B/></Sequence>
  <Sequence name="obsolete"><C/><D><E/></D></Sequence>
</Sequence></BehaviorTree></root>)";
  constexpr std::string_view after = R"(
<root><BehaviorTree ID="Main"><Sequence name="mission">
  <Sequence name="keep"><A/><B/></Sequence>
</Sequence></BehaviorTree></root>)";

  const std::string diff = BT::RenderXMLDiff(before, after);
  EXPECT_NE(diff.find("### Removed"), std::string::npos);
  const size_t root = diff.find("- <Sequence name=\"obsolete\">");
  EXPECT_NE(root, std::string::npos);
  EXPECT_EQ(diff.find("- <Sequence name=\"obsolete\">", root + 1), std::string::npos);
  EXPECT_NE(diff.find("-   <C/>"), std::string::npos);
  EXPECT_NE(diff.find("-   <D>"), std::string::npos);
  EXPECT_NE(diff.find("-     <E/>"), std::string::npos);
}

TEST(XMLDiff, SimilarSiblingsUseStableAttributeAwareMatching)
{
  constexpr std::string_view before = R"(
<root><BehaviorTree ID="Main"><Sequence name="checks">
  <Check name="left" threshold="10"/>
  <Check name="right" threshold="20"/>
</Sequence></BehaviorTree></root>)";
  constexpr std::string_view after = R"(
<root><BehaviorTree ID="Main"><Sequence name="checks">
  <Check name="new" threshold="0"/>
  <Check name="left" threshold="11"/>
  <Check name="right" threshold="21"/>
</Sequence></BehaviorTree></root>)";

  const std::string first = BT::RenderXMLDiff(before, after);
  const std::string second = BT::RenderXMLDiff(before, after);
  EXPECT_EQ(first, second);
  EXPECT_NE(first.find("+ <Check name=\"new\" threshold=\"0\"/>"), std::string::npos);
  EXPECT_NE(first.find("- <Check name=\"left\" threshold=\"10\"/>\n"
                       "+ <Check name=\"left\" threshold=\"11\"/>"),
            std::string::npos);
  EXPECT_NE(first.find("- <Check name=\"right\" threshold=\"20\"/>\n"
                       "+ <Check name=\"right\" threshold=\"21\"/>"),
            std::string::npos);
}

TEST(XMLDiff, ModifiedLeafBecomingContainerUsesSourceAccurateTags)
{
  constexpr std::string_view before = R"(<root><A state="leaf"/></root>)";
  constexpr std::string_view after = R"(<root><A state="container"><B/></A></root>)";

  const std::string diff = BT::RenderXMLDiff(before, after, BT::XMLDiffFormat::PlainText);
  EXPECT_NE(diff.find("- <A state=\"leaf\"/>\n+ <A state=\"container\">"),
            std::string::npos);
  EXPECT_NE(diff.find("+ </A>"), std::string::npos);
}

TEST(XMLDiff, ModifiedContainerBecomingLeafUsesSourceAccurateTags)
{
  constexpr std::string_view before = R"(<root><A state="container"><B/></A></root>)";
  constexpr std::string_view after = R"(<root><A state="leaf"/></root>)";

  const std::string diff = BT::RenderXMLDiff(before, after, BT::XMLDiffFormat::PlainText);
  EXPECT_NE(diff.find("- <A state=\"container\">\n+ <A state=\"leaf\"/>"),
            std::string::npos);
  EXPECT_NE(diff.find("- </A>"), std::string::npos);
}

TEST(XMLDiff, ExactDuplicateMatchingUsesGlobalPairCost)
{
  constexpr std::string_view before = R"(<root><A x="1"/><A x="1"/></root>)";
  constexpr std::string_view after = R"(<root><A x="2"/><A x="1"/></root>)";

  const std::string diff = BT::RenderXMLDiff(before, after);
  EXPECT_NE(diff.find("### Modified"), std::string::npos);
  EXPECT_NE(diff.find("- <A x=\"1\"/>\n+ <A x=\"2\"/>"), std::string::npos);
  EXPECT_EQ(diff.find("### Moved"), std::string::npos);
}

TEST(XMLDiff, MatchesTwoThousandSameIdentityModifiedSiblings)
{
  std::string before = "<root>";
  std::string after = "<root>";
  for(size_t index = 0; index < 2000; ++index)
  {
    before += "<A name=\"node_" + std::to_string(index) + "\" value=\"old\"/>";
    after += "<A name=\"node_" + std::to_string(index) + "\" value=\"new\"/>";
  }
  before += "</root>";
  after += "</root>";

  const std::string diff = BT::RenderXMLDiff(before, after, BT::XMLDiffFormat::PlainText);
  size_t modification_count = 0;
  for(size_t offset = 0; (offset = diff.find("- <A ", offset)) != std::string::npos;
      offset += 5)
  {
    ++modification_count;
  }
  EXPECT_EQ(modification_count, 2000);
  EXPECT_EQ(diff.find("Added\n-----"), std::string::npos);
  EXPECT_EQ(diff.find("Removed\n-------"), std::string::npos);
  EXPECT_EQ(diff.find("Moved\n-----"), std::string::npos);
}

TEST(XMLDiff, MatchesThreeThousandExactDuplicateSiblings)
{
  std::string before = "<root revision=\"old\">";
  std::string after = "<root revision=\"new\">";
  for(size_t index = 0; index < 3000; ++index)
  {
    before += "<A/>";
    after += "<A/>";
  }
  before += "</root>";
  after += "</root>";

  const std::string diff = BT::RenderXMLDiff(before, after, BT::XMLDiffFormat::PlainText);
  EXPECT_NE(diff.find("revision=\"old\""), std::string::npos);
  EXPECT_NE(diff.find("revision=\"new\""), std::string::npos);
  EXPECT_EQ(diff.find("Added\n-----"), std::string::npos);
  EXPECT_EQ(diff.find("Removed\n-------"), std::string::npos);
  EXPECT_EQ(diff.find("Moved\n-----"), std::string::npos);
}

TEST(XMLDiff, CompactAndGenericNodeFormsShareRegistrationIdentity)
{
  constexpr std::string_view compact = R"(
<root><BehaviorTree ID="Main"><Sequence>
  <Navigate goal="dock" server="nav"/>
</Sequence></BehaviorTree></root>)";
  constexpr std::string_view generic = R"(
<root><BehaviorTree ID="Main"><Control ID="Sequence">
  <Action ID="Navigate" goal="dock" server="nav"/>
</Control></BehaviorTree></root>)";

  EXPECT_EQ(BT::RenderXMLDiff(compact, generic), "## Behavior Tree XML Diff\n\nNo "
                                                 "changes.\n");
}

TEST(XMLDiff, MalformedInputIdentifiesBeforeAndAfterSides)
{
  constexpr std::string_view valid = "<root><BehaviorTree "
                                     "ID=\"Main\"><A/></BehaviorTree></root>";

  try
  {
    static_cast<void>(BT::RenderXMLDiff("<root>", valid));
    FAIL() << "Expected malformed before XML to throw";
  }
  catch(const BT::RuntimeError& error)
  {
    EXPECT_NE(std::string(error.what()).find("before"), std::string::npos);
  }

  try
  {
    static_cast<void>(BT::RenderXMLDiff(valid, "<root>"));
    FAIL() << "Expected malformed after XML to throw";
  }
  catch(const BT::RuntimeError& error)
  {
    EXPECT_NE(std::string(error.what()).find("after"), std::string::npos);
  }
}

TEST(XMLDiff, ExtraTopLevelElementIdentifiesBeforeAndAfterSides)
{
  constexpr std::string_view valid = "<root/>";

  EXPECT_THROW(
      {
        try
        {
          static_cast<void>(BT::RenderXMLDiff("<root/><extra/>", valid));
        }
        catch(const BT::RuntimeError& error)
        {
          EXPECT_NE(std::string(error.what()).find("before"), std::string::npos);
          throw;
        }
      },
      BT::RuntimeError);
  EXPECT_THROW(
      {
        try
        {
          static_cast<void>(BT::RenderXMLDiff(valid, "<root/><extra/>"));
        }
        catch(const BT::RuntimeError& error)
        {
          EXPECT_NE(std::string(error.what()).find("after"), std::string::npos);
          throw;
        }
      },
      BT::RuntimeError);
}

TEST(XMLDiff, TopLevelTextIdentifiesBeforeAndAfterSides)
{
  constexpr std::string_view valid = "<root/>";

  EXPECT_THROW(
      {
        try
        {
          static_cast<void>(BT::RenderXMLDiff("unexpected<root/>", valid));
        }
        catch(const BT::RuntimeError& error)
        {
          EXPECT_NE(std::string(error.what()).find("before"), std::string::npos);
          throw;
        }
      },
      BT::RuntimeError);
  EXPECT_THROW(
      {
        try
        {
          static_cast<void>(BT::RenderXMLDiff(valid, "<root/>unexpected"));
        }
        catch(const BT::RuntimeError& error)
        {
          EXPECT_NE(std::string(error.what()).find("after"), std::string::npos);
          throw;
        }
      },
      BT::RuntimeError);
}

TEST(XMLDiff, TopLevelDeclarationCommentsAndWhitespaceAreAllowed)
{
  constexpr std::string_view decorated = "<?xml version=\"1.0\"?>\n<!-- before "
                                         "-->\n<root/>\n<!-- after -->\n";

  EXPECT_EQ(BT::RenderXMLDiff(decorated, "<root/>"), "## Behavior Tree XML Diff\n\nNo "
                                                     "changes.\n");
}

TEST(XMLDiff, CommentsWhitespaceAndGeneratedMetadataAreIgnored)
{
  constexpr std::string_view before = R"(
<root BTCPP_format="4">
  <!-- editor note -->
  <BehaviorTree ID="Main">
    <Sequence _uid="17" _fullPath="Main/root">
      <Action ID="Navigate" goal="dock"/>
    </Sequence>
  </BehaviorTree>
</root>)";
  constexpr std::string_view after = R"(<root BTCPP_format="4"><BehaviorTree ID="Main">

<Sequence _uid="99" _fullpath="generated/path"><!-- moved comment -->
<Navigate goal="dock" />
</Sequence></BehaviorTree></root>)";

  EXPECT_EQ(BT::RenderXMLDiff(before, after), "## Behavior Tree XML Diff\n\nNo "
                                              "changes.\n");
}

TEST(XMLDiff, MarkdownUsesFencesAndPlainTextAvoidsMarkdownSyntax)
{
  constexpr std::string_view before = "<root><BehaviorTree ID=\"Main\"><Action "
                                      "ID=\"Navigate\" goal=\"old\"/>"
                                      "</BehaviorTree></root>";
  constexpr std::string_view after = "<root><BehaviorTree ID=\"Main\"><Action "
                                     "ID=\"Navigate\" goal=\"new\"/>"
                                     "</BehaviorTree></root>";

  const std::string markdown =
      BT::RenderXMLDiff(before, after, BT::XMLDiffFormat::Markdown);
  EXPECT_NE(markdown.find("### Modified"), std::string::npos);
  EXPECT_NE(markdown.find("```diff\n"), std::string::npos);
  EXPECT_NE(markdown.find("\n```"), std::string::npos);

  const std::string plain =
      BT::RenderXMLDiff(before, after, BT::XMLDiffFormat::PlainText);
  EXPECT_NE(plain.find("Modified\n--------"), std::string::npos);
  EXPECT_NE(plain.find("- <Action ID=\"Navigate\" goal=\"old\"/>"), std::string::npos);
  EXPECT_NE(plain.find("+ <Action ID=\"Navigate\" goal=\"new\"/>"), std::string::npos);
  EXPECT_EQ(plain.find("```"), std::string::npos);
  EXPECT_EQ(plain.find("###"), std::string::npos);
  EXPECT_EQ(plain.find('`'), std::string::npos);
}

TEST(XMLDiff, MarkdownEscapesControlWhitespaceAndBacktickRuns)
{
  constexpr std::string_view before =
      R"(<root><Action ID="Tick````&#10;Node" note="old&#10;&#9;&#13;````"/></root>)";
  constexpr std::string_view after =
      R"(<root><Action ID="Tick````&#10;Node" note="new&#10;&#9;&#13;```"/></root>)";

  const std::string markdown =
      BT::RenderXMLDiff(before, after, BT::XMLDiffFormat::Markdown);
  EXPECT_NE(markdown.find("At ````` /root/Tick````&#10;Node[1] `````:"),
            std::string::npos);
  EXPECT_NE(markdown.find("`````diff\n"), std::string::npos);
  EXPECT_NE(markdown.find("note=\"old&#10;&#9;&#13;````\""), std::string::npos);
  EXPECT_NE(markdown.find("note=\"new&#10;&#9;&#13;```\""), std::string::npos);
  EXPECT_NE(markdown.find("\n`````\n"), std::string::npos);
  EXPECT_EQ(markdown.find("note=\"old\n"), std::string::npos);
  EXPECT_EQ(markdown.find("note=\"new\n"), std::string::npos);

  const std::string plain =
      BT::RenderXMLDiff(before, after, BT::XMLDiffFormat::PlainText);
  EXPECT_NE(plain.find("note=\"old&#10;&#9;&#13;````\""), std::string::npos);
  EXPECT_NE(plain.find("note=\"new&#10;&#9;&#13;```\""), std::string::npos);
  EXPECT_EQ(plain.find("note=\"old\n"), std::string::npos);
  EXPECT_EQ(plain.find("note=\"new\n"), std::string::npos);
}

TEST(XMLDiff, RepresentativeNestedRefactorRemainsReviewable)
{
  constexpr std::string_view before = R"(
<root BTCPP_format="4"><BehaviorTree ID="Main"><Sequence name="mission">
  <Sequence name="execute">
    <ComputePathToPose goal="{goal}" path="{path}" planner_id="GridBased"/>
    <FollowPath path="{path}" controller_id="FollowPath" speed="0.5"/>
  </Sequence>
  <Sequence name="legacy_recovery"><ClearCostmap/><Wait msec="1000"/></Sequence>
</Sequence></BehaviorTree></root>)";
  constexpr std::string_view after = R"(
<root BTCPP_format="4"><BehaviorTree ID="Main"><Sequence name="mission">
  <Fallback name="execute_or_report_failure">
    <Sequence name="execute">
      <ComputePathToPose goal="{goal}" path="{path}" planner_id="GridBased"/>
      <FollowPath path="{path}" controller_id="FollowPath" speed="0.8"/>
    </Sequence>
    <Sequence name="report_failure">
      <SetBlackboard output_key="failure_reason" value="navigation_failed"/>
      <ForceFailure/>
    </Sequence>
  </Fallback>
</Sequence></BehaviorTree></root>)";

  const std::string diff = BT::RenderXMLDiff(before, after);
  EXPECT_NE(diff.find("### Added"), std::string::npos);
  EXPECT_NE(diff.find("+ <Fallback name=\"execute_or_report_failure\">"),
            std::string::npos);
  EXPECT_NE(diff.find("+   <Sequence name=\"report_failure\">"), std::string::npos);
  EXPECT_NE(diff.find("### Removed"), std::string::npos);
  EXPECT_NE(diff.find("- <Sequence name=\"legacy_recovery\">"), std::string::npos);
  EXPECT_NE(diff.find("### Moved and modified"), std::string::npos);
  EXPECT_NE(diff.find("-   <FollowPath path=\"{path}\" controller_id=\"FollowPath\" "
                      "speed=\"0.5\"/>\n"
                      "+   <FollowPath path=\"{path}\" controller_id=\"FollowPath\" "
                      "speed=\"0.8\"/>"),
            std::string::npos);
  EXPECT_EQ(diff.find("- <ComputePathToPose"), std::string::npos);
  EXPECT_EQ(diff.find("+ <ComputePathToPose"), std::string::npos);
}

TEST(XMLDiff, SiblingOrderChangeIsRenderedAsMove)
{
  constexpr std::string_view before = R"(
<root><BehaviorTree ID="Main"><Sequence name="ordered">
  <A/><B/><C/>
</Sequence></BehaviorTree></root>)";
  constexpr std::string_view after = R"(
<root><BehaviorTree ID="Main"><Sequence name="ordered">
  <B/><A/><C/>
</Sequence></BehaviorTree></root>)";

  const std::string diff = BT::RenderXMLDiff(before, after);
  EXPECT_NE(diff.find("### Moved"), std::string::npos);
  EXPECT_NE(diff.find("<B/>"), std::string::npos);
  EXPECT_EQ(diff.find("### Added"), std::string::npos);
  EXPECT_EQ(diff.find("### Removed"), std::string::npos);
}

TEST(XMLDiff, ReorderedDuplicateSiblingsReportAbsolutePosition)
{
  constexpr std::string_view before = R"(<root><A/><A/><B/></root>)";
  constexpr std::string_view after = R"(<root><A/><B/><A/></root>)";

  const std::string diff = BT::RenderXMLDiff(before, after, BT::XMLDiffFormat::PlainText);
  EXPECT_NE(diff.find("/root/B[1] \xE2\x86\x92 /root/B[1] (sibling position 3 "
                      "\xE2\x86\x92 2)"),
            std::string::npos);
}

TEST(XMLDiff, ModifiedChildrenStayWithTheirNamedParentsAfterReorder)
{
  constexpr std::string_view before = R"(<root>
  <Z name="left"><A name="leaf" value="old-left"/></Z>
  <Z name="right"><A name="leaf" value="old-right"/></Z>
</root>)";
  constexpr std::string_view after = R"(<root>
  <Z name="right"><A name="leaf" value="new-right"/></Z>
  <Z name="left"><A name="leaf" value="new-left"/></Z>
</root>)";

  const std::string diff = BT::RenderXMLDiff(before, after, BT::XMLDiffFormat::PlainText);
  EXPECT_NE(diff.find("old-left\"/>\n+ <A name=\"leaf\" value=\"new-left"),
            std::string::npos)
      << diff;
  EXPECT_NE(diff.find("old-right\"/>\n+   <A name=\"leaf\" value=\"new-right"),
            std::string::npos)
      << diff;
  EXPECT_EQ(diff.find("old-left\"/>\n+ <A name=\"leaf\" value=\"new-right"),
            std::string::npos)
      << diff;
}

TEST(XMLDiff, RenamedContainerWithRetainedChildIsModification)
{
  constexpr std::string_view before =
      R"(<root><Z name="old"><DistinctiveLeaf value="same"/></Z></root>)";
  constexpr std::string_view after =
      R"(<root><Z name="new"><DistinctiveLeaf value="same"/></Z></root>)";
  const std::string diff = BT::RenderXMLDiff(before, after, BT::XMLDiffFormat::PlainText);
  EXPECT_NE(diff.find("name=\"old\""), std::string::npos);
  EXPECT_NE(diff.find("name=\"new\""), std::string::npos);
  EXPECT_NE(diff.find("Modified\n--------"), std::string::npos) << diff;
  EXPECT_EQ(diff.find("Added\n-----"), std::string::npos) << diff;
  EXPECT_EQ(diff.find("Removed\n-------"), std::string::npos) << diff;
}

TEST(XMLDiff, ModifiedParentIncludesChildEditOnlyOnce)
{
  const std::string diff =
      BT::RenderXMLDiff(R"(<root><Z state="old"><A value="old"/></Z></root>)",
                        R"(<root><Z state="new"><A value="new"/></Z></root>)",
                        BT::XMLDiffFormat::PlainText);
  const size_t first = diff.find("-   <A value=\"old\"/>");
  ASSERT_NE(first, std::string::npos) << diff;
  EXPECT_EQ(diff.find("-   <A value=\"old\"/>", first + 1), std::string::npos) << diff;
  EXPECT_EQ(diff.find("- <A value=\"old\"/>"), std::string::npos) << diff;
}

TEST(XMLDiff, RejectsEmbeddedNulAndTrailingDocument)
{
  std::string malformed = "<root/>";
  malformed.push_back('\0');
  malformed += "<extra/>";
  EXPECT_THROW(static_cast<void>(BT::RenderXMLDiff(malformed, "<root/>")),
               BT::RuntimeError);
  EXPECT_THROW(static_cast<void>(BT::RenderXMLDiff("<root/>", malformed)),
               BT::RuntimeError);
}

TEST(XMLDiff, RejectsInvalidUTF8InsteadOfEmittingIt)
{
  for(const std::string_view invalid :
      { std::string_view("\xFF", 1), std::string_view("\xC3", 1),
        std::string_view("\xC0\xAF", 2), std::string_view("\xED\xA0\x80", 3),
        std::string_view("\xF4\x90\x80\x80", 4) })
  {
    const std::string malformed =
        "<root><A name=\"" + std::string(invalid) + "\"/></root>";
    EXPECT_THROW(static_cast<void>(BT::RenderXMLDiff("<root/>", malformed)),
                 BT::RuntimeError);
    EXPECT_THROW(static_cast<void>(BT::RenderXMLDiff(malformed, "<root/>")),
                 BT::RuntimeError);
  }
  EXPECT_NE(BT::RenderXMLDiff("<root/>", "<root><A name=\"caf\xC3\xA9\"/></root>")
                .find("caf\xC3\xA9"),
            std::string::npos);
}

TEST(XMLDiff, RejectsInvalidXMLCharacterReferences)
{
  for(const std::string_view reference : { "&#0;", "&#x1F;", "&#xD800;", "&#x110000;" })
  {
    const std::string malformed =
        "<root><A value=\"" + std::string(reference) + "\"/></root>";
    EXPECT_THROW(static_cast<void>(BT::RenderXMLDiff(malformed, "<root/>")),
                 BT::RuntimeError)
        << reference;
  }
  EXPECT_EQ(BT::RenderXMLDiff("<root><!-- &#0; --><![CDATA[&#0;]]></root>", "<root/>"),
            "## Behavior Tree XML Diff\n\nNo changes.\n");
}
