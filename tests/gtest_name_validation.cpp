#include "behaviortree_cpp/basic_types.h"
#include "behaviortree_cpp/bt_factory.h"
#include "behaviortree_cpp/xml_parsing.h"

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

  // Forbidden characters
  EXPECT_FALSE(IsAllowedPortName("port name"));  // space
  EXPECT_FALSE(IsAllowedPortName("port.name"));  // period
  EXPECT_FALSE(IsAllowedPortName("port<T>"));    // angle brackets
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

TEST_F(NameValidationXMLTest, InvalidBehaviorTreeID_Root)
{
  const char* xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="Root">
        <AlwaysSuccess/>
      </BehaviorTree>
    </root>)";
  EXPECT_THROW(factory.createTreeFromText(xml), RuntimeError);
}

TEST_F(NameValidationXMLTest, InvalidBehaviorTreeID_root_lowercase)
{
  const char* xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="root">
        <AlwaysSuccess/>
      </BehaviorTree>
    </root>)";
  EXPECT_THROW(factory.createTreeFromText(xml), RuntimeError);
}

// Fork divergence: upstream rejects spaces in model names; this fork permits
// them because MoveIt Pro names every Objective in human-readable form
// ("Close Gripper", "Move to Pose"). See validateModelName in xml_parsing.cpp.
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

// The space, the apostrophe and the dot are the ONLY relaxations. Every other
// forbidden character must still throw -- these are what the validation exists for.
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

TEST_F(NameValidationXMLTest, ForkStillRejectsOtherForbiddenCharsInModelName)
{
  // The trailing group puts the offender AFTER a carved-out character.
  // findForbiddenChar returns only the first offender, so a naive filter on its
  // result would accept every one of these.
  for(const char* bad_id :
      { "Main/Tree", "Main\\Tree", "Main:Tree", "Main*Tree", "Main?Tree", "Main|Tree",
        "Pick & Place", "Robot's Home/Left", "Robot's <Home>", "A B\\C",
        "Presoak 1.2|beta", "v1.2:final" })
  {
    const std::string xml = std::string(R"(<root BTCPP_format="4">
      <BehaviorTree ID=")") +
                            bad_id + R"(">
        <AlwaysSuccess/>
      </BehaviorTree>
    </root>)";
    EXPECT_THROW(factory.createTreeFromText(xml), RuntimeError) << bad_id;
  }
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

TEST_F(NameValidationXMLTest, InvalidSubTreePortName_Reserved)
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
  EXPECT_THROW(factory.createTreeFromText(xml), RuntimeError);
}

TEST_F(NameValidationXMLTest, InvalidSubTreePortName_StartsWithDigit)
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
  EXPECT_THROW(factory.createTreeFromText(xml), RuntimeError);
}

// A SubTree model may declare a port whose name IsAllowedPortName refuses.
// validatePortName only rejects a leading digit, so `_myPort` is accepted as a
// declaration, but the instance attribute that would remap it is diverted to
// other_attributes and the node reads its default instead. Silently.
TEST_F(NameValidationXMLTest, RemappingADeclaredUnusablePortNameThrows)
{
  const char* xml = R"(
    <root BTCPP_format="4" main_tree_to_execute="MainTree">
      <BehaviorTree ID="MainTree">
        <SubTree ID="MySubTree" _myPort="{outer}"/>
      </BehaviorTree>
      <BehaviorTree ID="MySubTree">
        <AlwaysSuccess/>
      </BehaviorTree>
      <TreeNodesModel>
        <SubTree ID="MySubTree">
          <input_port name="_myPort" default="unwired"/>
        </SubTree>
      </TreeNodesModel>
    </root>)";
  try
  {
    factory.createTreeFromText(xml);
    FAIL() << "expected a RuntimeError";
  }
  catch(const RuntimeError& err)
  {
    const std::string msg = err.what();
    EXPECT_NE(msg.find("_myPort"), std::string::npos) << msg;
    EXPECT_NE(msg.find("my_port"), std::string::npos) << msg;
  }
}

// The same underscore attribute on a SubTree that does not declare it is UI
// state, not a port. moveit_pro_example_ws carries 148 of these. Keep loading.
TEST_F(NameValidationXMLTest, UndeclaredUnderscoreAttributeIsStillIgnored)
{
  const char* xml = R"(
    <root BTCPP_format="4" main_tree_to_execute="MainTree">
      <BehaviorTree ID="MainTree">
        <SubTree ID="MySubTree" _collapsed="true"/>
      </BehaviorTree>
      <BehaviorTree ID="MySubTree">
        <AlwaysSuccess/>
      </BehaviorTree>
      <TreeNodesModel>
        <SubTree ID="MySubTree">
          <input_port name="goal" default="g"/>
        </SubTree>
      </TreeNodesModel>
    </root>)";
  EXPECT_NO_THROW(factory.createTreeFromText(xml));
}

// A declared-but-unusable port that nobody remaps keeps its default and keeps
// loading. This shape ships in moveit_pro_example_ws.
TEST_F(NameValidationXMLTest, DeclaredUnusablePortNameLoadsWhenNotRemapped)
{
  const char* xml = R"(
    <root BTCPP_format="4" main_tree_to_execute="MainTree">
      <BehaviorTree ID="MainTree">
        <SubTree ID="MySubTree"/>
      </BehaviorTree>
      <BehaviorTree ID="MySubTree">
        <AlwaysSuccess/>
      </BehaviorTree>
      <TreeNodesModel>
        <SubTree ID="MySubTree">
          <inout_port name="_collapsed" default="false"/>
        </SubTree>
      </TreeNodesModel>
    </root>)";
  EXPECT_NO_THROW(factory.createTreeFromText(xml));
}

namespace
{
struct UnusablePortAction : public SyncActionNode
{
  UnusablePortAction(const std::string& name, const NodeConfig& config)
    : SyncActionNode(name, config)
  {}
  NodeStatus tick() override
  {
    return NodeStatus::SUCCESS;
  }
};
}  // namespace

// The same trap reaches a registered node, not only a SubTree. `CreatePort`
// screens names through IsAllowedPortName, but the PortsList overload of
// registerNodeType takes the map as given, so a hand-built list can carry a key
// no instance attribute will ever bind.
TEST_F(NameValidationXMLTest, RemappingADeclaredUnusablePortOnARegisteredNodeThrows)
{
  factory.registerNodeType<UnusablePortAction>(
      "UnusablePortAction", PortsList{ { "_foo", PortInfo(PortDirection::INPUT) } });

  const char* xml = R"(
    <root BTCPP_format="4" main_tree_to_execute="MainTree">
      <BehaviorTree ID="MainTree">
        <UnusablePortAction _foo="{outer}"/>
      </BehaviorTree>
    </root>)";
  EXPECT_THROW(factory.createTreeFromText(xml), RuntimeError);
}
