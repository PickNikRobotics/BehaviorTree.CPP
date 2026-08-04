#include "behaviortree_cpp/basic_types.h"
#include "behaviortree_cpp/bt_factory.h"
#include "behaviortree_cpp/json_export.h"
#include "behaviortree_cpp/xml_parsing.h"

#include <gtest/gtest.h>

using namespace BT;

TEST(toStr, ConvertsIntVector)
{
  std::string val_str;
  ASSERT_NO_THROW(val_str = toStr(std::vector<int>{ 1, 2, 3, 4 }));
  EXPECT_EQ(val_str, "1;2;3;4");
}

TEST(toStr, ConvertsStringVector)
{
  std::string val_str;
  ASSERT_NO_THROW(val_str =
                      toStr(std::vector<std::string>{ "hello", "", " ", "world", "!" }));
  EXPECT_EQ(val_str, "hello;; ;world;!");
}

TEST(toStr, ConvertsEmptyVector)
{
  std::string val_str;
  ASSERT_NO_THROW(val_str = toStr(std::vector<std::string>{}));
  EXPECT_EQ(val_str, "");
}

class NodeWithPorts : public SyncActionNode
{
public:
  NodeWithPorts(const std::string& name, const NodeConfig& config)
    : SyncActionNode(name, config)
  {
    std::cout << "ctor" << std::endl;
  }

  NodeStatus tick() override
  {
    int val_A = 0;
    int val_B = 0;
    if(!getInput("in_port_A", val_A))
    {
      throw RuntimeError("missing input [in_port_A]");
    }
    if(!getInput("in_port_B", val_B))
    {
      throw RuntimeError("missing input [in_port_B]");
    }

    if(val_A == 42 && val_B == 66)
    {
      return NodeStatus::SUCCESS;
    }
    return NodeStatus::FAILURE;
  }

  static PortsList providedPorts()
  {
    return { BT::InputPort<int>("in_port_A", 42, "magic_number"),
             BT::InputPort<int>("in_port_B") };
  }
};

TEST(PortTest, WrongNodeConfig)
{
  NodeConfig config;
  config.input_ports["in_port_A"] = "42";
  // intentionally missing:
  // config.input_ports["in_port_B"] = "69";
  NodeWithPorts node("will_fail", config);
  ASSERT_ANY_THROW(node.tick());
}

TEST(PortTest, DefaultPorts)
{
  std::string xml_txt = R"(
    <root BTCPP_format="4" >
        <BehaviorTree ID="MainTree">
            <NodeWithPorts in_port_B="66" />
        </BehaviorTree>
    </root>)";

  BehaviorTreeFactory factory;
  factory.registerNodeType<NodeWithPorts>("NodeWithPorts");
  auto tree = factory.createTreeFromText(xml_txt);
  NodeStatus status = tree.tickWhileRunning();
  ASSERT_EQ(status, NodeStatus::SUCCESS);
}

TEST(PortTest, MissingPort)
{
  std::string xml_txt = R"(
    <root BTCPP_format="4" >
        <BehaviorTree ID="MainTree">
            <NodeWithPorts/>
        </BehaviorTree>
    </root>)";

  BehaviorTreeFactory factory;
  factory.registerNodeType<NodeWithPorts>("NodeWithPorts");
  auto tree = factory.createTreeFromText(xml_txt);
  ASSERT_ANY_THROW(tree.tickWhileRunning());
}

TEST(PortTest, WrongPort)
{
  std::string xml_txt = R"(
    <root BTCPP_format="4" >
        <BehaviorTree ID="MainTree">
            <NodeWithPorts da_port="66" />
        </BehaviorTree>
    </root>)";

  BehaviorTreeFactory factory;
  factory.registerNodeType<NodeWithPorts>("NodeWithPorts");

  EXPECT_ANY_THROW(auto tree = factory.createTreeFromText(xml_txt));
}

TEST(PortTest, Descriptions)
{
  std::string xml_txt = R"(
    <root BTCPP_format="4" >
        <BehaviorTree ID="MainTree" _description="this is my tree" >
            <Sequence>
                <NodeWithPorts name="first"  in_port_B="66" _description="this is my action" />
                <SubTree ID="mySubTree" name="second" _description="this is a subtree"/>
            </Sequence>
        </BehaviorTree>

        <BehaviorTree ID="mySubTree" _description="this is a subtree" >
            <NodeWithPorts name="third" in_port_B="99" />
        </BehaviorTree>

    </root>)";

  BehaviorTreeFactory factory;
  factory.registerNodeType<NodeWithPorts>("NodeWithPorts");

  factory.registerBehaviorTreeFromText(xml_txt);
  auto tree = factory.createTree("MainTree");

  NodeStatus status = tree.tickWhileRunning();
  while(status == NodeStatus::RUNNING)
  {
    status = tree.tickWhileRunning();
  }

  ASSERT_EQ(status, NodeStatus::FAILURE);  // failure because in_port_B="99"
}

TEST(PortsTest, NonPorts)
{
  std::string xml_txt =
      R"(
    <root BTCPP_format="4" >
        <BehaviorTree ID="MainTree">
            <Action ID="NodeWithPorts" name="NodeWithPortsName" in_port_B="66" _not_da_port="whateva" _skipIf="true" />
        </BehaviorTree>
    </root>)";

  BehaviorTreeFactory factory;
  factory.registerNodeType<NodeWithPorts>("NodeWithPorts");

  auto tree = factory.createTreeFromText(xml_txt);

  const TreeNode* root = tree.rootNode();
  ASSERT_NE(root, nullptr);
  ASSERT_EQ(root->type(), NodeType::ACTION);

  EXPECT_EQ(root->config().other_attributes.size(), 1);
  ASSERT_EQ(root->config().other_attributes.count("_not_da_port"), 1);
  EXPECT_EQ(root->config().other_attributes.at("_not_da_port"), "whateva");
}

struct MyType
{
  std::string value;
};

class NodeInPorts : public SyncActionNode
{
public:
  NodeInPorts(const std::string& name, const NodeConfig& config)
    : SyncActionNode(name, config)
  {}

  NodeStatus tick() override
  {
    int val_A = 0;
    MyType val_B;
    if(getInput("int_port", val_A) && getInput("any_port", val_B))
    {
      return NodeStatus::SUCCESS;
    }
    return NodeStatus::FAILURE;
  }

  static PortsList providedPorts()
  {
    return { BT::InputPort<int>("int_port"), BT::InputPort<MyType>("any_port") };
  }
};

class NodeOutPorts : public SyncActionNode
{
public:
  NodeOutPorts(const std::string& name, const NodeConfig& config)
    : SyncActionNode(name, config)
  {}

  NodeStatus tick() override
  {
    return NodeStatus::SUCCESS;
  }

  static PortsList providedPorts()
  {
    return { BT::OutputPort<int>("int_port"), BT::OutputPort<MyType>("any_port") };
  }
};

TEST(PortTest, EmptyPort)
{
  std::string xml_txt = R"(
    <root BTCPP_format="4" >
        <BehaviorTree ID="MainTree">
            <Sequence>
                <NodeInPorts  int_port="{ip}" any_port="{ap}" />
                <NodeOutPorts int_port="{ip}" any_port="{ap}" />
            </Sequence>
        </BehaviorTree>
    </root>)";

  BehaviorTreeFactory factory;
  factory.registerNodeType<NodeOutPorts>("NodeOutPorts");
  factory.registerNodeType<NodeInPorts>("NodeInPorts");

  auto tree = factory.createTreeFromText(xml_txt);

  NodeStatus status = tree.tickWhileRunning();
  // expect failure because port is not set yet
  ASSERT_EQ(status, NodeStatus::FAILURE);
}

class ActionVectorStringIn : public SyncActionNode
{
public:
  ActionVectorStringIn(const std::string& name, const NodeConfig& config,
                       std::vector<std::string>* states)
    : SyncActionNode(name, config), states_(states)
  {}

  NodeStatus tick() override
  {
    getInput("states", *states_);
    return NodeStatus::SUCCESS;
  }

  static PortsList providedPorts()
  {
    return { BT::InputPort<std::vector<std::string>>("states") };
  }

private:
  std::vector<std::string>* states_;
};

TEST(PortTest, SubtreeStringInput_StringVector)
{
  std::string xml_txt = R"(
    <root BTCPP_format="4" >
      <BehaviorTree ID="Main">
        <ActionVectorStringIn states="hello;world;with spaces"/>
      </BehaviorTree>
    </root>)";

  std::vector<std::string> states;

  BehaviorTreeFactory factory;
  factory.registerNodeType<ActionVectorStringIn>("ActionVectorStringIn", &states);

  factory.registerBehaviorTreeFromText(xml_txt);
  auto tree = factory.createTree("Main");

  NodeStatus status = tree.tickWhileRunning();

  ASSERT_EQ(status, NodeStatus::SUCCESS);
  ASSERT_EQ(3, states.size());
  ASSERT_EQ("hello", states[0]);
  ASSERT_EQ("world", states[1]);
  ASSERT_EQ("with spaces", states[2]);
}

struct Point2D
{
  int x = 0;
  int y = 0;
  bool operator==(const Point2D& other) const
  {
    return x == other.x && y == other.y;
  }
  bool operator!=(const Point2D& other) const
  {
    return !(*this == other);
  }
};

template <>
[[nodiscard]] Point2D BT::convertFromString<Point2D>(StringView str)
{
  if(StartWith(str, "json:"))
  {
    str.remove_prefix(5);
    return convertFromJSON<Point2D>(str);
  }
  const auto parts = BT::splitString(str, ',');
  if(parts.size() != 2)
  {
    throw BT::RuntimeError("invalid input)");
  }
  int x = convertFromString<int>(parts[0]);
  int y = convertFromString<int>(parts[1]);
  return { x, y };
}

template <>
[[nodiscard]] std::string BT::toStr<Point2D>(const Point2D& point)
{
  return std::to_string(point.x) + "," + std::to_string(point.y);
}

// NOLINTNEXTLINE(misc-use-anonymous-namespace,misc-use-internal-linkage)
BT_JSON_CONVERTER(Point2D, point)
{
  add_field("x", &point.x);
  add_field("y", &point.y);
}

class DefaultTestAction : public SyncActionNode
{
public:
  DefaultTestAction(const std::string& name, const NodeConfig& config)
    : SyncActionNode(name, config)
  {}

  NodeStatus tick() override
  {
    const int answer = getInput<int>("answer").value();
    if(answer != 42)
    {
      return NodeStatus::FAILURE;
    }

    const std::string greet = getInput<std::string>("greeting").value();
    if(greet != "hello")
    {
      return NodeStatus::FAILURE;
    }

    const Point2D point = getInput<Point2D>("pos").value();
    if(point.x != 1 || point.y != 2)
    {
      return NodeStatus::FAILURE;
    }

    return NodeStatus::SUCCESS;
  }

  static PortsList providedPorts()
  {
    return { BT::InputPort<int>("answer", 42, "the answer"),
             BT::InputPort<std::string>("greeting", "hello", "be polite"),
             BT::InputPort<Point2D>("pos", Point2D{ 1, 2 }, "where") };
  }
};

TEST(PortTest, DefaultInput)
{
  std::string xml_txt = R"(
    <root BTCPP_format="4" >
      <BehaviorTree>
        <DefaultTestAction/>
      </BehaviorTree>
    </root>)";

  BehaviorTreeFactory factory;
  factory.registerNodeType<DefaultTestAction>("DefaultTestAction");
  auto tree = factory.createTreeFromText(xml_txt);
  auto status = tree.tickOnce();
  ASSERT_EQ(status, NodeStatus::SUCCESS);
}

// NOTE: GetAny, SetAny classes and AnyPort test moved to gtest_port_type_rules.cpp

class NodeWithDefaultPoints : public SyncActionNode
{
public:
  NodeWithDefaultPoints(const std::string& name, const NodeConfig& config)
    : SyncActionNode(name, config)
  {}

  NodeStatus tick() override
  {
    Point2D pointA, pointB, pointC, pointD, pointE, input;

    if(!getInput("pointA", pointA) || pointA != Point2D{ 1, 2 })
    {
      throw std::runtime_error("failed pointA");
    }
    if(!getInput("pointB", pointB) || pointB != Point2D{ 3, 4 })
    {
      throw std::runtime_error("failed pointB");
    }
    if(!getInput("pointC", pointC) || pointC != Point2D{ 5, 6 })
    {
      throw std::runtime_error("failed pointC");
    }
    if(!getInput("pointD", pointD) || pointD != Point2D{ 7, 8 })
    {
      throw std::runtime_error("failed pointD");
    }
    if(!getInput("pointE", pointE) || pointE != Point2D{ 9, 10 })
    {
      throw std::runtime_error("failed pointE");
    }
    if(!getInput("input", input) || input != Point2D{ -1, -2 })
    {
      throw std::runtime_error("failed input");
    }
    return NodeStatus::SUCCESS;
  }

  static PortsList providedPorts()
  {
    return { BT::InputPort<Point2D>("input", "no default value"),
             BT::InputPort<Point2D>("pointA", Point2D{ 1, 2 }, "default value is [1,2]"),
             BT::InputPort<Point2D>("pointB", "{point}",
                                    "default value inside blackboard {point}"),
             BT::InputPort<Point2D>("pointC", "5,6", "default value is [5,6]"),
             BT::InputPort<Point2D>("pointD", "{=}",
                                    "default value inside blackboard {pointD}"),
             BT::InputPort<Point2D>("pointE", R"(json:{"x":9,"y":10})",
                                    "default value is [9,10]") };
  }
};

TEST(PortTest, DefaultInputPoint2D)
{
  std::string xml_txt = R"(
    <root BTCPP_format="4" >
      <BehaviorTree>
        <NodeWithDefaultPoints input="-1,-2"/>
      </BehaviorTree>
    </root>)";

  JsonExporter::get().addConverter<Point2D>();

  BehaviorTreeFactory factory;
  factory.registerNodeType<NodeWithDefaultPoints>("NodeWithDefaultPoints");
  auto tree = factory.createTreeFromText(xml_txt);

  tree.subtrees.front()->blackboard->set<Point2D>("point", Point2D{ 3, 4 });
  tree.subtrees.front()->blackboard->set<Point2D>("pointD", Point2D{ 7, 8 });

  BT::NodeStatus status = NodeStatus::IDLE;
  ASSERT_NO_THROW(status = tree.tickOnce());
  ASSERT_EQ(status, NodeStatus::SUCCESS);

  std::cout << writeTreeNodesModelXML(factory) << std::endl;
}

class NodeWithDefaultStrings : public SyncActionNode
{
public:
  NodeWithDefaultStrings(const std::string& name, const NodeConfig& config)
    : SyncActionNode(name, config)
  {}

  NodeStatus tick() override
  {
    std::string input, msgA, msgB, msgC;
    if(!getInput("input", input) || input != "from XML")
    {
      throw std::runtime_error("failed input");
    }
    if(!getInput("msgA", msgA) || msgA != "hello")
    {
      throw std::runtime_error("failed msgA");
    }
    if(!getInput("msgB", msgB) || msgB != "ciao")
    {
      throw std::runtime_error("failed msgB");
    }
    if(!getInput("msgC", msgC) || msgC != "hola")
    {
      throw std::runtime_error("failed msgC");
    }
    return NodeStatus::SUCCESS;
  }

  static PortsList providedPorts()
  {
    return { BT::InputPort<std::string>("input", "no default"),
             BT::InputPort<std::string>("msgA", "hello", "default value is 'hello'"),
             BT::InputPort<std::string>("msgB", "{msg}",
                                        "default value inside blackboard {msg}"),
             BT::InputPort<std::string>("msgC", "{=}",
                                        "default value inside blackboard {msgC}") };
  }
};

TEST(PortTest, DefaultInputStrings)
{
  std::string xml_txt = R"(
    <root BTCPP_format="4" >
      <BehaviorTree>
        <NodeWithDefaultStrings input="from XML"/>
      </BehaviorTree>
    </root>)";

  BehaviorTreeFactory factory;
  factory.registerNodeType<NodeWithDefaultStrings>("NodeWithDefaultStrings");
  auto tree = factory.createTreeFromText(xml_txt);

  tree.subtrees.front()->blackboard->set<std::string>("msg", "ciao");
  tree.subtrees.front()->blackboard->set<std::string>("msgC", "hola");

  BT::NodeStatus status = NodeStatus::IDLE;
  ASSERT_NO_THROW(status = tree.tickOnce());
  ASSERT_EQ(status, NodeStatus::SUCCESS);

  std::cout << writeTreeNodesModelXML(factory) << std::endl;
}

struct TestStruct
{
  int a;
  double b;
  std::string c;
};

class NodeWithDefaultNullptr : public SyncActionNode
{
public:
  NodeWithDefaultNullptr(const std::string& name, const NodeConfig& config)
    : SyncActionNode(name, config)
  {}

  NodeStatus tick() override
  {
    return NodeStatus::SUCCESS;
  }

  static PortsList providedPorts()
  {
    return { BT::InputPort<std::shared_ptr<TestStruct>>("input", nullptr,
                                                        "default value is nullptr") };
  }
};

TEST(PortTest, Default_Issues_767)
{
  using namespace BT;

  ASSERT_NO_THROW(auto p = InputPort<std::optional<Point2D>>("opt_A", std::nullopt,
                                                             "default nullopt"));
  ASSERT_NO_THROW(auto p = InputPort<std::optional<std::string>>("opt_B", std::nullopt,
                                                                 "default nullopt"));

  ASSERT_NO_THROW(
      auto p = InputPort<std::shared_ptr<Point2D>>("ptr_A", nullptr, "default nullptr"));
  ASSERT_NO_THROW(auto p = InputPort<std::shared_ptr<std::string>>("ptr_B", nullptr,
                                                                   "default nullptr"));
}

TEST(PortTest, DefaultWronglyOverriden)
{
  BT::BehaviorTreeFactory factory;
  factory.registerNodeType<NodeWithDefaultNullptr>("NodeWithDefaultNullptr");

  std::string xml_txt_wrong = R"(
    <root BTCPP_format="4" >
      <BehaviorTree>
        <NodeWithDefaultNullptr input=""/>
      </BehaviorTree>
    </root>)";

  std::string xml_txt_correct = R"(
    <root BTCPP_format="4" >
      <BehaviorTree>
        <NodeWithDefaultNullptr/>
      </BehaviorTree>
    </root>)";

  // this should throw, because we are NOT using the default,
  // but overriding it with an empty string instead.
  // See issue 768 for reference
  ASSERT_ANY_THROW(auto tree = factory.createTreeFromText(xml_txt_wrong));
  // This is correct
  ASSERT_NO_THROW(auto tree = factory.createTreeFromText(xml_txt_correct));
}

// Issue #858: getInput should return the default value declared in
// providedPorts when the XML does not specify the port.
class ActionWithDefaultPort : public SyncActionNode
{
public:
  ActionWithDefaultPort(const std::string& name, const NodeConfig& config)
    : SyncActionNode(name, config)
  {}

  NodeStatus tick() override
  {
    auto res = getInput<std::string>("log_name");
    if(!res)
    {
      throw RuntimeError("getInput failed: " + res.error());
    }
    result = res.value();
    return NodeStatus::SUCCESS;
  }

  static PortsList providedPorts()
  {
    return { InputPort<std::string>("log_name", "my_default_logger", "Logger name"),
             InputPort<std::string>("message", "Message to be logged") };
  }

  std::string result;
};

TEST(PortTest, GetInputDefaultValue_Issue858)
{
  // XML does NOT specify "log_name" — should use the default from providedPorts
  std::string xml_txt = R"(
    <root BTCPP_format="4" >
      <BehaviorTree ID="Main">
        <ActionWithDefaultPort message="hello"/>
      </BehaviorTree>
    </root>)";

  BehaviorTreeFactory factory;
  factory.registerNodeType<ActionWithDefaultPort>("ActionWithDefaultPort");
  auto tree = factory.createTreeFromText(xml_txt);
  auto status = tree.tickWhileRunning();

  ASSERT_EQ(status, NodeStatus::SUCCESS);

  for(const auto& node : tree.subtrees.front()->nodes)
  {
    if(auto action = dynamic_cast<ActionWithDefaultPort*>(node.get()))
    {
      ASSERT_EQ("my_default_logger", action->result);
    }
  }
}

// Helper class used by Issue #969 and #1065 tests
class CollectDoubleAction : public SyncActionNode
{
public:
  CollectDoubleAction(const std::string& name, const NodeConfig& config,
                      std::vector<double>* collected)
    : SyncActionNode(name, config), collected_(collected)
  {}

  NodeStatus tick() override
  {
    double val = 0;
    if(getInput("value", val))
    {
      collected_->push_back(val);
      return NodeStatus::SUCCESS;
    }
    return NodeStatus::FAILURE;
  }

  static PortsList providedPorts()
  {
    return { BT::InputPort<double>("value") };
  }

private:
  std::vector<double>* collected_;
};

// Issue #969: LoopNode<T> uses SharedQueue<T> (shared_ptr<deque<T>>) for its queue
// port, but upstream nodes often produce std::vector<T>. This type mismatch causes
// tree creation to fail.
class ProduceVectorDoubleAction : public SyncActionNode
{
public:
  ProduceVectorDoubleAction(const std::string& name, const NodeConfig& config)
    : SyncActionNode(name, config)
  {}

  NodeStatus tick() override
  {
    std::vector<double> vec = { 10.0, 20.0, 30.0 };
    setOutput("numbers", vec);
    return NodeStatus::SUCCESS;
  }

  static PortsList providedPorts()
  {
    return { BT::OutputPort<std::vector<double>>("numbers") };
  }
};

TEST(PortTest, LoopNodeAcceptsVector_Issue969)
{
  // An upstream node outputs std::vector<double>, and LoopDouble should be
  // able to iterate over it without requiring manual conversion to SharedQueue.
  std::string xml_txt = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="MainTree">
        <Sequence>
          <ProduceVectorDouble numbers="{nums}" />
          <LoopDouble queue="{nums}" value="{val}">
            <CollectDouble value="{val}" />
          </LoopDouble>
        </Sequence>
      </BehaviorTree>
    </root>
  )";

  std::vector<double> collected;

  BehaviorTreeFactory factory;
  factory.registerNodeType<ProduceVectorDoubleAction>("ProduceVectorDouble");
  factory.registerNodeType<CollectDoubleAction>("CollectDouble", &collected);
  auto tree = factory.createTreeFromText(xml_txt);
  auto status = tree.tickWhileRunning();

  ASSERT_EQ(status, NodeStatus::SUCCESS);
  ASSERT_EQ(collected.size(), 3u);
  EXPECT_DOUBLE_EQ(collected[0], 10.0);
  EXPECT_DOUBLE_EQ(collected[1], 20.0);
  EXPECT_DOUBLE_EQ(collected[2], 30.0);
}

// Issue #982: A port of type std::vector<std::string> with a default empty value {}
// gets initialized with the literal string "json:[]" instead of being empty.
// This happens because toStr() converts {} to "json:[]", and the vector
// convertFromString specialization doesn't handle the "json:" prefix.
class ActionWithDefaultEmptyVector : public SyncActionNode
{
public:
  ActionWithDefaultEmptyVector(const std::string& name, const NodeConfig& config,
                               std::vector<std::string>* out_vec)
    : SyncActionNode(name, config), out_vec_(out_vec)
  {}

  NodeStatus tick() override
  {
    if(!getInput("string_vector", *out_vec_))
    {
      return NodeStatus::FAILURE;
    }
    return NodeStatus::SUCCESS;
  }

  static PortsList providedPorts()
  {
    return { BT::InputPort<std::vector<std::string>>("string_vector", {},
                                                     "A string vector") };
  }

private:
  std::vector<std::string>* out_vec_;
};

TEST(PortTest, DefaultEmptyVector_Issue982)
{
  // Port has default value {} (empty vector) and no input specified in XML.
  // The vector should be empty, not contain "json:[]".
  std::string xml_txt = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="MainTree">
        <ActionWithDefaultEmptyVector />
      </BehaviorTree>
    </root>
  )";

  std::vector<std::string> result;

  BehaviorTreeFactory factory;
  factory.registerNodeType<ActionWithDefaultEmptyVector>("ActionWithDefaultEmptyVector",
                                                         &result);
  auto tree = factory.createTreeFromText(xml_txt);
  auto status = tree.tickWhileRunning();

  ASSERT_EQ(status, NodeStatus::SUCCESS);
  ASSERT_TRUE(result.empty()) << "Expected empty vector, but got " << result.size()
                              << " element(s). First element: \""
                              << (result.empty() ? "" : result[0]) << "\"";
}

// Issue #989: JsonExporter::addConverter(std::function) had a use-after-move
// bug where `converter` was moved into to_json_converters_ before
// `vector_converter` captured it, causing bad_function_call at runtime.
namespace
{
struct TestPoint989
{
  double x = 0;
  double y = 0;
};

void TestPoint989ToJson(const TestPoint989& p, nlohmann::json& j)
{
  j["x"] = p.x;
  j["y"] = p.y;
}

void TestPoint989FromJson(const nlohmann::json& j, TestPoint989& p)
{
  p.x = j.at("x").get<double>();
  p.y = j.at("y").get<double>();
}
}  // namespace

TEST(PortTest, JsonExporterVectorConverter_Issue989)
{
  auto& exporter = JsonExporter::get();
  exporter.addConverter<TestPoint989>(&TestPoint989ToJson);
  exporter.addConverter<TestPoint989>(&TestPoint989FromJson);

  // Single element conversion should work
  {
    BT::Any any_val(TestPoint989{ 1.0, 2.0 });
    nlohmann::json j;
    ASSERT_TRUE(exporter.toJson(any_val, j));
    EXPECT_DOUBLE_EQ(j["x"].get<double>(), 1.0);
    EXPECT_DOUBLE_EQ(j["y"].get<double>(), 2.0);
  }

  // Vector conversion must not throw bad_function_call
  {
    std::vector<TestPoint989> vec = { { 1.0, 2.0 }, { 3.0, 4.0 } };
    BT::Any any_vec(vec);
    nlohmann::json j;
    ASSERT_NO_THROW(ASSERT_TRUE(exporter.toJson(any_vec, j)));
    ASSERT_EQ(j.size(), 2u);
    EXPECT_DOUBLE_EQ(j[0]["x"].get<double>(), 1.0);
    EXPECT_DOUBLE_EQ(j[0]["y"].get<double>(), 2.0);
    EXPECT_DOUBLE_EQ(j[1]["x"].get<double>(), 3.0);
    EXPECT_DOUBLE_EQ(j[1]["y"].get<double>(), 4.0);
  }
}

// Issue #1065: passing a string literal like "1;2;3" through a SubTree port
// to a LoopDouble node should work, but fails because the subtree remapping
// stores the value as a plain std::string in the blackboard without converting
// it to SharedQueue<double>.
TEST(PortTest, SubtreeStringLiteralToLoopDouble_Issue1065)
{
  // The main tree passes a string literal "1;2;3" to the subtree port "queue".
  // Inside the subtree, LoopDouble should parse it and iterate over the values.
  std::string xml_txt = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="MainTree">
        <SubTree ID="LoopSubTree" queue="1;2;3" />
      </BehaviorTree>

      <BehaviorTree ID="LoopSubTree">
        <LoopDouble queue="{queue}" value="{number}">
          <CollectDouble value="{number}" />
        </LoopDouble>
      </BehaviorTree>
    </root>
  )";

  std::vector<double> collected;

  BehaviorTreeFactory factory;
  factory.registerNodeType<CollectDoubleAction>("CollectDouble", &collected);
  factory.registerBehaviorTreeFromText(xml_txt);

  auto tree = factory.createTree("MainTree");
  auto status = tree.tickWhileRunning();

  ASSERT_EQ(status, NodeStatus::SUCCESS);
  ASSERT_EQ(collected.size(), 3u);
  EXPECT_DOUBLE_EQ(collected[0], 1.0);
  EXPECT_DOUBLE_EQ(collected[1], 2.0);
  EXPECT_DOUBLE_EQ(collected[2], 3.0);
}

class OutputVectorStringNode : public SyncActionNode
{
public:
  OutputVectorStringNode(const std::string& name, const NodeConfig& config)
    : SyncActionNode(name, config)
  {}
  static PortsList providedPorts()
  {
    return { InputPort<std::string>("string1", "val1", "First string"),
             InputPort<std::string>("string2", "val2", "Second string"),
             OutputPort<std::vector<std::string>>("string_vector", "{string_vector}",
                                                  "Vector of strings.") };
  }

  NodeStatus tick() override
  {
    auto string1 = getInput<std::string>("string1");
    auto string2 = getInput<std::string>("string2");

    std::vector<std::string> out = { string1.value(), string2.value() };
    setOutput("string_vector", out);
    return NodeStatus::SUCCESS;
  }
};

class InputVectorStringNode : public SyncActionNode
{
public:
  InputVectorStringNode(const std::string& name, const NodeConfig& config)
    : SyncActionNode(name, config)
  {}
  static PortsList providedPorts()
  {
    return { InputPort<std::vector<std::string>>("string_vector", "{string_vector}",
                                                 "Vector of strings.") };
  }

  NodeStatus tick() override
  {
    std::vector<std::string> expected_vec = { "val1", "val2" };
    std::vector<std::string> actual_vec;

    if(!getInput<std::vector<std::string>>("string_vector", actual_vec))
    {
      return NodeStatus::FAILURE;
    }
    if(expected_vec == actual_vec)
    {
      return NodeStatus::SUCCESS;
    }
    else
    {
      return NodeStatus::FAILURE;
    }
  }
};

class InputVectorDoubleNode : public SyncActionNode
{
public:
  InputVectorDoubleNode(const std::string& name, const NodeConfig& config)
    : SyncActionNode(name, config)
  {}
  static PortsList providedPorts()
  {
    return { InputPort<std::vector<double>>("double_vector", "{double_vector}",
                                            "Vector of doubles.") };
  }

  NodeStatus tick() override
  {
    std::vector<double> expected_vec = { 1.0, 2.0 };
    std::vector<double> actual_vec;

    if(!getInput<std::vector<double>>("double_vector", actual_vec))
    {
      return NodeStatus::FAILURE;
    }
    if(expected_vec == actual_vec)
    {
      return NodeStatus::SUCCESS;
    }
    else
    {
      return NodeStatus::FAILURE;
    }
  }
};

TEST(PortTest, VectorAny)
{
  BT::BehaviorTreeFactory factory;
  factory.registerNodeType<OutputVectorStringNode>("OutputVectorStringNode");
  factory.registerNodeType<InputVectorStringNode>("InputVectorStringNode");
  factory.registerNodeType<InputVectorDoubleNode>("InputVectorDoubleNode");

  std::string xml_txt_good = R"(
    <root BTCPP_format="4" >
      <BehaviorTree>
        <Sequence name="root_sequence">
          <OutputVectorStringNode/>
          <InputVectorStringNode/>
        </Sequence>
      </BehaviorTree>
    </root>)";

  std::string xml_txt_bad = R"(
    <root BTCPP_format="4" >
      <BehaviorTree>
        <Sequence name="root_sequence">
          <OutputVectorStringNode/>
          <InputVectorDoubleNode double_vector="{string_vector}"/>
        </Sequence>
      </BehaviorTree>
    </root>)";

  // Test that setting and retrieving a vector<string> works.
  BT::Tree tree;
  ASSERT_NO_THROW(tree = factory.createTreeFromText(xml_txt_good));

  BT::NodeStatus status;
  ASSERT_NO_THROW(status = tree.tickOnce());
  ASSERT_EQ(status, NodeStatus::SUCCESS);

  // Test that setting a port as a vector<string> and attempting to retrie it as a vector<double> fails.
  ASSERT_NO_THROW(tree = factory.createTreeFromText(xml_txt_bad));

  ASSERT_NO_THROW(status = tree.tickOnce());
  ASSERT_EQ(status, NodeStatus::FAILURE);
}

TEST(PortTest, WhitespaceInPortName)
{
  ASSERT_ANY_THROW(BT::InputPort<std::string>("port name"));
  ASSERT_ANY_THROW(BT::InputPort<std::string>("port\tname"));
  ASSERT_ANY_THROW(BT::InputPort<std::string>("port\nname"));
  ASSERT_ANY_THROW(BT::InputPort<std::string>(" leading"));
  ASSERT_ANY_THROW(BT::InputPort<std::string>("trailing "));
  ASSERT_NO_THROW(BT::InputPort<std::string>("valid_port_name"));
}

// ----------------------------------------------------------------------------
// Diagnostic-aware input port reads.
//
// getInputStampedWithDiagnostic() returns a structured PortInputError alongside
// the human-readable message so callers can tell unwired ports apart from
// blackboard wiring that was never populated (the latter is a likely user bug
// that should be surfaced).
// ----------------------------------------------------------------------------

class DiagnosticPortNode : public SyncActionNode
{
public:
  DiagnosticPortNode(const std::string& name, const NodeConfig& config)
    : SyncActionNode(name, config)
  {}

  NodeStatus tick() override
  {
    return NodeStatus::SUCCESS;
  }

  static PortsList providedPorts()
  {
    // Two input ports: one with no default ("required" from the manifest's
    // perspective) and one with a default of 7.
    return { BT::InputPort<int>("no_default"),
             BT::InputPort<int>("with_default", 7, "default seven") };
  }
};

TEST(PortTest, DiagnosticBlackboardKeyNotFound)
{
  // Port is wired to {missing_key}, but the blackboard has no such entry.
  // This is the bug case the diagnostic enum exists to surface.
  auto bb = Blackboard::create();
  NodeConfig config;
  config.blackboard = bb;
  config.input_ports["no_default"] = "{missing_key}";
  TreeNodeManifest manifest;
  manifest.ports = DiagnosticPortNode::providedPorts();
  config.manifest = &manifest;

  DiagnosticPortNode node("diag", config);

  int value = 0;
  auto res = node.getInputStampedWithDiagnostic<int>("no_default", value);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, PortError::BlackboardKeyNotFound);
  EXPECT_FALSE(res.error().message.empty());
}

TEST(PortTest, DiagnosticBlackboardEntryEmpty)
{
  // The blackboard key exists, but its entry is empty (created without a value).
  auto bb = Blackboard::create();
  bb->createEntry("empty_key", TypeInfo::Create<int>());

  NodeConfig config;
  config.blackboard = bb;
  config.input_ports["no_default"] = "{empty_key}";
  TreeNodeManifest manifest;
  manifest.ports = DiagnosticPortNode::providedPorts();
  config.manifest = &manifest;

  DiagnosticPortNode node("diag", config);

  int value = 0;
  auto res = node.getInputStampedWithDiagnostic<int>("no_default", value);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, PortError::BlackboardEntryEmpty);
  EXPECT_FALSE(res.error().message.empty());
}

TEST(PortTest, DiagnosticManifestKeyMissing)
{
  // Ask for a key that is not in the XML and not in the manifest.
  auto bb = Blackboard::create();
  NodeConfig config;
  config.blackboard = bb;
  TreeNodeManifest manifest;
  manifest.ports = DiagnosticPortNode::providedPorts();
  config.manifest = &manifest;

  DiagnosticPortNode node("diag", config);

  int value = 0;
  auto res = node.getInputStampedWithDiagnostic<int>("totally_unknown", value);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, PortError::ManifestKeyMissing);
  EXPECT_FALSE(res.error().message.empty());
}

TEST(PortTest, DiagnosticNoDefaultNoWiring)
{
  // The manifest has the key but the default is empty AND nothing wired it.
  auto bb = Blackboard::create();
  NodeConfig config;
  config.blackboard = bb;
  TreeNodeManifest manifest;
  manifest.ports = DiagnosticPortNode::providedPorts();
  config.manifest = &manifest;

  DiagnosticPortNode node("diag", config);

  int value = 0;
  auto res = node.getInputStampedWithDiagnostic<int>("no_default", value);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, PortError::NoDefaultNoWiring);
  EXPECT_FALSE(res.error().message.empty());
}

TEST(PortTest, DiagnosticManifestMissing)
{
  // The node has no manifest attached and no input_ports entry for the key.
  auto bb = Blackboard::create();
  NodeConfig config;
  config.blackboard = bb;
  config.manifest = nullptr;

  DiagnosticPortNode node("diag", config);

  int value = 0;
  auto res = node.getInputStampedWithDiagnostic<int>("no_default", value);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, PortError::ManifestMissing);
  EXPECT_FALSE(res.error().message.empty());
}

TEST(PortTest, DiagnosticConversionFailed)
{
  // A literal (non-blackboard) value that cannot be parsed as int.
  auto bb = Blackboard::create();
  NodeConfig config;
  config.blackboard = bb;
  config.input_ports["no_default"] = "not_an_int";
  TreeNodeManifest manifest;
  manifest.ports = DiagnosticPortNode::providedPorts();
  config.manifest = &manifest;

  DiagnosticPortNode node("diag", config);

  int value = 0;
  auto res = node.getInputStampedWithDiagnostic<int>("no_default", value);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, PortError::ConversionFailed);
  EXPECT_FALSE(res.error().message.empty());
}

TEST(PortTest, DiagnosticInvalidBlackboard)
{
  // Port is wired to {foo} but no blackboard is attached.
  NodeConfig config;
  config.blackboard = nullptr;
  config.input_ports["no_default"] = "{foo}";
  TreeNodeManifest manifest;
  manifest.ports = DiagnosticPortNode::providedPorts();
  config.manifest = &manifest;

  DiagnosticPortNode node("diag", config);

  int value = 0;
  auto res = node.getInputStampedWithDiagnostic<int>("no_default", value);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, PortError::InvalidBlackboard);
  EXPECT_FALSE(res.error().message.empty());
}

TEST(PortTest, DiagnosticSuccessFromBlackboard)
{
  // Happy path: blackboard has the entry, returns the value, no error.
  auto bb = Blackboard::create();
  bb->set("my_value", 42);

  NodeConfig config;
  config.blackboard = bb;
  config.input_ports["no_default"] = "{my_value}";
  TreeNodeManifest manifest;
  manifest.ports = DiagnosticPortNode::providedPorts();
  config.manifest = &manifest;

  DiagnosticPortNode node("diag", config);

  int value = 0;
  auto res = node.getInputStampedWithDiagnostic<int>("no_default", value);
  ASSERT_TRUE(res.has_value()) << (res ? "" : res.error().message);
  EXPECT_EQ(value, 42);
}

TEST(PortTest, DiagnosticSuccessFromDefault)
{
  // Happy path: nothing wired, manifest default fires.
  auto bb = Blackboard::create();
  NodeConfig config;
  config.blackboard = bb;
  TreeNodeManifest manifest;
  manifest.ports = DiagnosticPortNode::providedPorts();
  config.manifest = &manifest;

  DiagnosticPortNode node("diag", config);

  int value = 0;
  auto res = node.getInputStampedWithDiagnostic<int>("with_default", value);
  ASSERT_TRUE(res.has_value()) << (res ? "" : res.error().message);
  EXPECT_EQ(value, 7);
}

TEST(PortTest, GetInputStampedDelegatesToDiagnosticMessage)
{
  // The original getInputStamped() must keep its existing message-on-failure
  // contract so callers that relied on the string still work.
  auto bb = Blackboard::create();
  NodeConfig config;
  config.blackboard = bb;
  config.input_ports["no_default"] = "{missing_key}";
  TreeNodeManifest manifest;
  manifest.ports = DiagnosticPortNode::providedPorts();
  config.manifest = &manifest;

  DiagnosticPortNode node("diag", config);

  int value = 0;
  auto res = node.getInputStamped<int>("no_default", value);
  ASSERT_FALSE(res.has_value());
  EXPECT_FALSE(res.error().empty());
}

TEST(PortTest, GetInputWithDiagnosticReturnsValueOnSuccess)
{
  // Happy path: getInputWithDiagnostic<T>(key) should return the port value
  // directly, without the caller having to stage a destination variable.
  auto bb = Blackboard::create();
  bb->set("my_value", 42);

  NodeConfig config;
  config.blackboard = bb;
  config.input_ports["no_default"] = "{my_value}";
  TreeNodeManifest manifest;
  manifest.ports = DiagnosticPortNode::providedPorts();
  config.manifest = &manifest;

  DiagnosticPortNode node("diag", config);

  auto res = node.getInputWithDiagnostic<int>("no_default");
  ASSERT_TRUE(res.has_value()) << (res ? "" : res.error().message);
  EXPECT_EQ(*res, 42);
}

TEST(PortTest, GetInputWithDiagnosticPropagatesBlackboardKeyNotFound)
{
  // Failure path: getInputWithDiagnostic<T>(key) should surface the same
  // structured PortInputError the stamped variant produces. This is the
  // error code MoveIt Pro's getOptionalInputs() relies on to distinguish a
  // wiring bug from a legitimate "no value" optional port.
  auto bb = Blackboard::create();
  NodeConfig config;
  config.blackboard = bb;
  config.input_ports["no_default"] = "{missing_key}";
  TreeNodeManifest manifest;
  manifest.ports = DiagnosticPortNode::providedPorts();
  config.manifest = &manifest;

  DiagnosticPortNode node("diag", config);

  auto res = node.getInputWithDiagnostic<int>("no_default");
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error().code, PortError::BlackboardKeyNotFound);
  EXPECT_FALSE(res.error().message.empty());
}
