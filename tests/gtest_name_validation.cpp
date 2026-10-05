#include "behaviortree_cpp/basic_types.h"
#include "behaviortree_cpp/bt_factory.h"
#include "behaviortree_cpp/xml_parsing.h"

#include <algorithm>
#include <memory>

#include <gtest/gtest.h>

using namespace BT;

// ============== Tests for findForbiddenChar() ==============

TEST(NameValidation, ForbiddenCharDetection_ValidNames)
{
  // Valid ASCII names
  EXPECT_EQ(findForbiddenChar("ValidName"), '\0');
  EXPECT_EQ(findForbiddenChar("my_action"), '\0');
  EXPECT_EQ(findForbiddenChar("My-Action"), '\0');
  EXPECT_EQ(findForbiddenChar("action123"), '\0');
  EXPECT_EQ(findForbiddenChar("CamelCaseNode"), '\0');
  EXPECT_EQ(findForbiddenChar("snake_case_node"), '\0');
  EXPECT_EQ(findForbiddenChar("kebab-case-node"), '\0');
}

TEST(NameValidation, ForbiddenCharDetection_Unicode)
{
  // Unicode names should be allowed (UTF-8 multibyte sequences)
  EXPECT_EQ(findForbiddenChar("检查门状态"), '\0');    // Chinese
  EXPECT_EQ(findForbiddenChar("ドアを開ける"), '\0');  // Japanese
  EXPECT_EQ(findForbiddenChar("Tür_öffnen"), '\0');    // German with umlaut
  EXPECT_EQ(findForbiddenChar("проверка"), '\0');      // Russian
  EXPECT_EQ(findForbiddenChar("действие"), '\0');      // Russian
}

TEST(NameValidation, ForbiddenCharDetection_ForbiddenChars)
{
  // Space and whitespace
  EXPECT_EQ(findForbiddenChar("My Action"), ' ');
  EXPECT_EQ(findForbiddenChar("with\ttab"), '\t');
  EXPECT_EQ(findForbiddenChar("with\nnewline"), '\n');
  EXPECT_EQ(findForbiddenChar("with\rcarriage"), '\r');

  // XML special characters
  EXPECT_EQ(findForbiddenChar("My<Node>"), '<');
  EXPECT_EQ(findForbiddenChar("Node>End"), '>');
  EXPECT_EQ(findForbiddenChar("A&B"), '&');
  EXPECT_EQ(findForbiddenChar("say\"hello\""), '"');
  EXPECT_EQ(findForbiddenChar("it's"), '\'');

  // Filesystem problematic characters
  EXPECT_EQ(findForbiddenChar("path/to/node"), '/');
  EXPECT_EQ(findForbiddenChar("path\\to\\node"), '\\');
  EXPECT_EQ(findForbiddenChar("C:drive"), ':');
  EXPECT_EQ(findForbiddenChar("wild*card"), '*');
  EXPECT_EQ(findForbiddenChar("what?"), '?');
  EXPECT_EQ(findForbiddenChar("pipe|char"), '|');

  // Period (can cause issues)
  EXPECT_EQ(findForbiddenChar("request.name"), '.');
  EXPECT_EQ(findForbiddenChar("file.ext"), '.');
}

TEST(NameValidation, ForbiddenCharDetection_ControlChars)
{
  // Control characters should be forbidden
  std::string with_null = "test";
  with_null += '\0';
  with_null += "name";
  EXPECT_EQ(findForbiddenChar(with_null), '\0');  // null char detected

  // Bell character (ASCII 7) - use string concatenation to avoid hex digit issues
  std::string with_bell = "test";
  with_bell += '\x07';
  with_bell += "bell";
  EXPECT_EQ(findForbiddenChar(with_bell), '\x07');

  // DEL character (ASCII 127)
  std::string with_del = "test";
  with_del += '\x7F';
  with_del += "del";
  EXPECT_EQ(findForbiddenChar(with_del), '\x7F');
}

// ============== Tests for IsAllowedPortName() ==============

TEST(NameValidation, IsAllowedPortName_Valid)
{
  EXPECT_TRUE(IsAllowedPortName("input"));
  EXPECT_TRUE(IsAllowedPortName("output_value"));
  EXPECT_TRUE(IsAllowedPortName("myPort123"));
  EXPECT_TRUE(IsAllowedPortName("Port_With_Underscore"));
}

TEST(NameValidation, IsAllowedPortName_Invalid)
{
  // Empty
  EXPECT_FALSE(IsAllowedPortName(""));

  // Starts with digit
  EXPECT_FALSE(IsAllowedPortName("1port"));
  EXPECT_FALSE(IsAllowedPortName("123"));

  // Starts with underscore (reserved)
  EXPECT_FALSE(IsAllowedPortName("_private"));

  // Reserved names
  EXPECT_FALSE(IsAllowedPortName("name"));
  EXPECT_FALSE(IsAllowedPortName("ID"));
  EXPECT_FALSE(IsAllowedPortName("_failureIf"));
  EXPECT_FALSE(IsAllowedPortName("_successIf"));
  EXPECT_FALSE(IsAllowedPortName("_skipIf"));
  EXPECT_FALSE(IsAllowedPortName("_while"));
  EXPECT_FALSE(IsAllowedPortName("_onSuccess"));
  EXPECT_FALSE(IsAllowedPortName("_onFailure"));
  EXPECT_FALSE(IsAllowedPortName("_onHalted"));
  EXPECT_FALSE(IsAllowedPortName("_post"));
  EXPECT_FALSE(IsAllowedPortName("_autoremap"));
}

// Fork divergence: port names follow the fork's 4.7.2 rules. They must start
// with a letter and not be reserved; CreatePort and <TreeNodesModel> also
// reject whitespace. Upstream 4.9.0's forbidden-character list does not apply.
TEST(NameValidation, IsAllowedPortName_ForbiddenCharsAcceptedByFork)
{
  EXPECT_TRUE(IsAllowedPortName("port.name"));
  EXPECT_TRUE(IsAllowedPortName("port:name"));
  EXPECT_TRUE(IsAllowedPortName("port/name"));
  // Whitespace is rejected by CreatePort and <TreeNodesModel>, not here.
  EXPECT_TRUE(IsAllowedPortName("port name"));
}

TEST(NameValidation, CppPortNameFollowsFork472Rules)
{
  BehaviorTreeFactory factory;
  factory.registerSimpleAction("ReadPose",
                               [](TreeNode& node) {
                                 return node.getInput<std::string>("goal.pose").value() ==
                                                "home" ?
                                            NodeStatus::SUCCESS :
                                            NodeStatus::FAILURE;
                               },
                               { InputPort<std::string>("goal.pose") });
  const char* xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="MainTree">
        <Sequence>
          <Script code="key := 'home'"/>
          <Action ID="ReadPose" goal.pose="home"/>
          <Action ID="ReadPose" goal.pose="{key}"/>
        </Sequence>
      </BehaviorTree>
    </root>)";
  Tree tree;
  ASSERT_NO_THROW(tree = factory.createTreeFromText(xml));
  EXPECT_EQ(tree.tickWhileRunning(), NodeStatus::SUCCESS);

  for(const char* name : { "goal pose", "goal\tpose", "goal\npose" })
  {
    try
    {
      (void)InputPort<std::string>(name);
      FAIL() << "Expected RuntimeError to be thrown for " << name;
    }
    catch(const RuntimeError& e)
    {
      EXPECT_NE(std::string(e.what()).find("must not contain whitespace"),
                std::string::npos)
          << e.what();
    }
  }
}

// ============== Tests for XML parsing validation ==============

class NameValidationXMLTest : public testing::Test
{
protected:
  BehaviorTreeFactory factory;
};

TEST_F(NameValidationXMLTest, ValidBehaviorTreeID)
{
  const char* xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="MainTree">
        <AlwaysSuccess/>
      </BehaviorTree>
    </root>)";
  EXPECT_NO_THROW(factory.createTreeFromText(xml));
}

TEST_F(NameValidationXMLTest, ValidBehaviorTreeID_WithUnderscore)
{
  const char* xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="My_Main_Tree">
        <AlwaysSuccess/>
      </BehaviorTree>
    </root>)";
  EXPECT_NO_THROW(factory.createTreeFromText(xml));
}

// Fork divergence: upstream rejects spaces in model names; this fork permits
// them because MoveIt Pro names every Objective in human-readable form
// ("Close Gripper", "Move to Pose"). See ModelNamesAreNotValidatedByFork.
TEST_F(NameValidationXMLTest, BehaviorTreeID_WithSpace_IsAcceptedByFork)
{
  const char* xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="Main Tree">
        <AlwaysSuccess/>
      </BehaviorTree>
    </root>)";
  EXPECT_NO_THROW(factory.createTreeFromText(xml));
}

// Fork divergence: see BehaviorTreeID_WithDot_IsAcceptedByFork below.
TEST_F(NameValidationXMLTest, BehaviorTreeID_WithPeriod_IsAcceptedByFork)
{
  const char* xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="Main.Tree">
        <AlwaysSuccess/>
      </BehaviorTree>
    </root>)";
  EXPECT_NO_THROW((void)factory.createTreeFromText(xml));
}

TEST_F(NameValidationXMLTest, ValidInstanceName)
{
  const char* xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="MainTree">
        <AlwaysSuccess name="my_success_node"/>
      </BehaviorTree>
    </root>)";
  EXPECT_NO_THROW(factory.createTreeFromText(xml));
}

TEST_F(NameValidationXMLTest, ValidInstanceName_WithSpace)
{
  // Instance names are XML attribute VALUES, so spaces are allowed
  const char* xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="MainTree">
        <AlwaysSuccess name="my success node"/>
      </BehaviorTree>
    </root>)";
  EXPECT_NO_THROW(factory.createTreeFromText(xml));
}

TEST_F(NameValidationXMLTest, ValidInstanceName_WithPeriod)
{
  // Instance names are XML attribute VALUES, so periods are allowed
  const char* xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="MainTree">
        <AlwaysSuccess name="node.name"/>
      </BehaviorTree>
    </root>)";
  EXPECT_NO_THROW(factory.createTreeFromText(xml));
}

TEST_F(NameValidationXMLTest, ValidSubTreeID)
{
  const char* xml = R"(
    <root BTCPP_format="4" main_tree_to_execute="MainTree">
      <BehaviorTree ID="MainTree">
        <SubTree ID="SubTree1"/>
      </BehaviorTree>
      <BehaviorTree ID="SubTree1">
        <AlwaysSuccess/>
      </BehaviorTree>
    </root>)";
  EXPECT_NO_THROW(factory.createTreeFromText(xml));
}

// Fork divergence: see BehaviorTreeID_WithSpace_IsAcceptedByFork above.
TEST_F(NameValidationXMLTest, SubTreeID_WithSpace_IsAcceptedByFork)
{
  const char* xml = R"(
    <root BTCPP_format="4" main_tree_to_execute="MainTree">
      <BehaviorTree ID="MainTree">
        <SubTree ID="Sub Tree"/>
      </BehaviorTree>
      <BehaviorTree ID="Sub Tree">
        <AlwaysSuccess/>
      </BehaviorTree>
    </root>)";
  EXPECT_NO_THROW(factory.createTreeFromText(xml));
}

// Fork divergence: MoveIt Pro puts no validation on Objective names, and its
// REST suite pins a name with an apostrophe. An apostrophe needs no escaping in
// the double-quoted attribute value BT.CPP writes.
// Fork divergence: a customer workspace names an Objective `Test Presoak 1.2`.
// Version-suffixed names are natural and '.' is not structural in a model name.
TEST_F(NameValidationXMLTest, BehaviorTreeID_WithDot_IsAcceptedByFork)
{
  const char* xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="Test Presoak 1.2">
        <AlwaysSuccess/>
      </BehaviorTree>
    </root>)";
  EXPECT_NO_THROW((void)factory.createTreeFromText(xml));
}

TEST_F(NameValidationXMLTest, BehaviorTreeID_WithApostrophe_IsAcceptedByFork)
{
  const char* xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="Robot's Home">
        <AlwaysSuccess/>
      </BehaviorTree>
    </root>)";
  EXPECT_NO_THROW((void)factory.createTreeFromText(xml));
}

// Fork divergence: model names are not validated, as in 4.7.2. MoveIt Pro does
// not use them as XML element names, and tinyxml2 escapes them as attribute
// values, so every character survives a load and a WriteTreeToXML round trip.
TEST(NameValidation, ModelNamesAreNotValidatedByFork)
{
  const auto escape = [](const std::string& value) {
    std::string out;
    for(const char c : value)
    {
      switch(c)
      {
        case '&':
          out += "&amp;";
          break;
        case '<':
          out += "&lt;";
          break;
        case '>':
          out += "&gt;";
          break;
        case '"':
          out += "&quot;";
          break;
        default:
          out += c;
      }
    }
    return out;
  };

  for(const std::string id :
      { "Main/Tree", "Main\\Tree", "Main:Tree", "Main*Tree", "Main?Tree", "Main|Tree",
        "Pick & Place", "Robot's <Home>", "Say \"hi\"", "Root", "root" })
  {
    const std::string xml = R"(<root BTCPP_format="4" main_tree_to_execute="Main">
      <BehaviorTree ID="Main"><SubTree ID=")" +
                            escape(id) + R"("/></BehaviorTree>
      <BehaviorTree ID=")" + escape(id) +
                            R"("><AlwaysSuccess/></BehaviorTree>
    </root>)";

    BehaviorTreeFactory factory;
    Tree tree;
    ASSERT_NO_THROW(tree = factory.createTreeFromText(xml)) << id;
    const auto has_subtree =
        std::any_of(tree.subtrees.begin(), tree.subtrees.end(),
                    [&id](const auto& subtree) { return subtree->tree_ID == id; });
    EXPECT_TRUE(has_subtree) << id;

    const std::string written = WriteTreeToXML(tree, false, false);
    BehaviorTreeFactory reloaded;
    ASSERT_NO_THROW(reloaded.registerBehaviorTreeFromText(written)) << written;
    const auto trees = reloaded.registeredBehaviorTrees();
    EXPECT_NE(std::find(trees.begin(), trees.end(), id), trees.end()) << written;
  }
}

// Fork divergence: node type names are not validated either. `Root` and a name
// with ':' are valid XML element names that upstream rejects.
TEST(NameValidation, NodeTypeNameIsNotValidatedByFork)
{
  BehaviorTreeFactory factory;
  factory.registerSimpleAction("Root", [](TreeNode&) { return NodeStatus::SUCCESS; });
  factory.registerSimpleAction("My:Action",
                               [](TreeNode&) { return NodeStatus::SUCCESS; });
  const char* xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="MainTree">
        <Sequence>
          <Root/>
          <My:Action/>
        </Sequence>
      </BehaviorTree>
    </root>)";
  Tree tree;
  ASSERT_NO_THROW(tree = factory.createTreeFromText(xml));
  EXPECT_EQ(tree.tickWhileRunning(), NodeStatus::SUCCESS);
}

// Fork divergence: WriteTreeToXML writes every node as <Action ID="...">,
// <Control ID="..."> and so on, so node type names that are not valid XML
// element names still produce XML that loads.
TEST(NameValidation, WriteTreeToXMLUsesExplicitForm)
{
  const auto make_factory = [] {
    auto factory = std::make_unique<BehaviorTreeFactory>();
    factory->registerSimpleAction("Pick / Place",
                                  [](TreeNode&) { return NodeStatus::SUCCESS; });
    factory->registerSimpleCondition("Is / Ready",
                                     [](TreeNode&) { return NodeStatus::SUCCESS; });
    return factory;
  };
  const char* xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="MainTree">
        <Sequence>
          <Condition ID="Is / Ready"/>
          <Inverter>
            <Inverter>
              <Action ID="Pick / Place" name="pick"/>
            </Inverter>
          </Inverter>
        </Sequence>
      </BehaviorTree>
    </root>)";
  const auto factory = make_factory();
  const Tree tree = factory->createTreeFromText(xml);

  // The arguments FileLogger2, SqliteLogger and Groot2Publisher use.
  const std::string written = WriteTreeToXML(tree, true, true);
  const std::string tree_part = written.substr(0, written.find("<TreeNodesModel"));
  for(const char* expected :
      { R"(<Action ID="Pick / Place")", R"(<Condition ID="Is / Ready")",
        R"(<Decorator ID="Inverter")", R"(<Control ID="Sequence")" })
  {
    EXPECT_NE(tree_part.find(expected), std::string::npos) << expected << "\n" << written;
  }
  for(const char* compact : { "<Pick / Place", "<Is / Ready", "<Inverter", "<Sequence" })
  {
    EXPECT_EQ(tree_part.find(compact), std::string::npos) << compact << "\n" << written;
  }

  const auto reloaded = make_factory();
  ASSERT_NO_THROW(reloaded->registerBehaviorTreeFromText(written)) << written;
  Tree again;
  ASSERT_NO_THROW(again = reloaded->createTree("MainTree")) << written;
  EXPECT_EQ(again.tickWhileRunning(), NodeStatus::SUCCESS);
}

// A node registered with an UNDEFINED manifest has no tag of its own, so it is
// written as <Action ID="...">, which loads it.
TEST(NameValidation, WriteTreeToXMLWritesUndefinedNodeAsAction)
{
  class UndefinedNode : public TreeNode
  {
  public:
    UndefinedNode(const std::string& name, const NodeConfig& config)
      : TreeNode(name, config)
    {}
    NodeType type() const override
    {
      return NodeType::UNDEFINED;
    }
    NodeStatus tick() override
    {
      return NodeStatus::SUCCESS;
    }
    void halt() override
    {}
  };
  const auto make_factory = [] {
    auto factory = std::make_unique<BehaviorTreeFactory>();
    TreeNodeManifest manifest;
    manifest.type = NodeType::UNDEFINED;
    manifest.registration_ID = "Odd / Name";
    factory->registerBuilder(manifest,
                             [](const std::string& name, const NodeConfig& config) {
                               return std::make_unique<UndefinedNode>(name, config);
                             });
    return factory;
  };
  const char* xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="MainTree">
        <Action ID="Odd / Name"/>
      </BehaviorTree>
    </root>)";
  const Tree tree = make_factory()->createTreeFromText(xml);
  const std::string written = WriteTreeToXML(tree, true, true);
  EXPECT_NE(written.find(R"(<Action ID="Odd / Name")"), std::string::npos) << written;

  const auto reloaded = make_factory();
  ASSERT_NO_THROW(reloaded->registerBehaviorTreeFromText(written)) << written;
  Tree again;
  ASSERT_NO_THROW(again = reloaded->createTree("MainTree")) << written;
  EXPECT_EQ(again.tickWhileRunning(), NodeStatus::SUCCESS);
}

// Fork divergence: an XSD can only describe node types used as element names,
// which this fork does not support.
TEST(NameValidation, WriteTreeXSDIsDisabledByFork)
{
  const BehaviorTreeFactory factory;
  EXPECT_THROW((void)writeTreeXSD(factory), RuntimeError);
}

TEST_F(NameValidationXMLTest, EmptySubTreeIDIsRejected)
{
  const char* xml = R"(
    <root BTCPP_format="4" main_tree_to_execute="MainTree">
      <BehaviorTree ID="MainTree">
        <SubTree ID=""/>
      </BehaviorTree>
    </root>)";
  EXPECT_THROW((void)factory.createTreeFromText(xml), RuntimeError);
}

// ============== Tests for Unicode support ==============

TEST_F(NameValidationXMLTest, UnicodeTreeID_Chinese)
{
  const char* xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="检查门">
        <AlwaysSuccess/>
      </BehaviorTree>
    </root>)";
  EXPECT_NO_THROW(factory.createTreeFromText(xml));
}

TEST_F(NameValidationXMLTest, UnicodeInstanceName_Japanese)
{
  const char* xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="MainTree">
        <AlwaysSuccess name="成功ノード"/>
      </BehaviorTree>
    </root>)";
  EXPECT_NO_THROW(factory.createTreeFromText(xml));
}

TEST_F(NameValidationXMLTest, UnicodeTreeID_German)
{
  const char* xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="Türöffner">
        <AlwaysSuccess/>
      </BehaviorTree>
    </root>)";
  EXPECT_NO_THROW(factory.createTreeFromText(xml));
}

// ============== Tests for SubTree port validation ==============

TEST_F(NameValidationXMLTest, ValidSubTreePortName)
{
  const char* xml = R"(
    <root BTCPP_format="4" main_tree_to_execute="MainTree">
      <BehaviorTree ID="MainTree">
        <SubTree ID="MySubTree" input_value="{value}"/>
      </BehaviorTree>
      <BehaviorTree ID="MySubTree">
        <AlwaysSuccess/>
      </BehaviorTree>
      <TreeNodesModel>
        <SubTree ID="MySubTree">
          <input_port name="input_value"/>
        </SubTree>
      </TreeNodesModel>
    </root>)";
  EXPECT_NO_THROW(factory.createTreeFromText(xml));
}

TEST_F(NameValidationXMLTest, InvalidSubTreePortName_WithSpace)
{
  const char* xml = R"(
    <root BTCPP_format="4" main_tree_to_execute="MainTree">
      <BehaviorTree ID="MainTree">
        <AlwaysSuccess/>
      </BehaviorTree>
      <TreeNodesModel>
        <SubTree ID="MySubTree">
          <input_port name="input value"/>
        </SubTree>
      </TreeNodesModel>
    </root>)";
  EXPECT_THROW(factory.createTreeFromText(xml), RuntimeError);
}

// Fork divergence: as in the fork's 4.7.2, <TreeNodesModel> ports only reject
// whitespace.
TEST_F(NameValidationXMLTest, SubTreePortName_Reserved_IsAcceptedByFork)
{
  const char* xml = R"(
    <root BTCPP_format="4" main_tree_to_execute="MainTree">
      <BehaviorTree ID="MainTree">
        <AlwaysSuccess/>
      </BehaviorTree>
      <TreeNodesModel>
        <SubTree ID="MySubTree">
          <input_port name="ID"/>
        </SubTree>
      </TreeNodesModel>
    </root>)";
  EXPECT_NO_THROW((void)factory.createTreeFromText(xml));
}

TEST_F(NameValidationXMLTest, SubTreePortName_StartsWithDigit_IsAcceptedByFork)
{
  const char* xml = R"(
    <root BTCPP_format="4" main_tree_to_execute="MainTree">
      <BehaviorTree ID="MainTree">
        <AlwaysSuccess/>
      </BehaviorTree>
      <TreeNodesModel>
        <SubTree ID="MySubTree">
          <input_port name="1port"/>
        </SubTree>
      </TreeNodesModel>
    </root>)";
  EXPECT_NO_THROW((void)factory.createTreeFromText(xml));
}
