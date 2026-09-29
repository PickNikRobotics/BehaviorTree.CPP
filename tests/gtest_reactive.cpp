#include "test_helper.hpp"

#include "behaviortree_cpp/bt_factory.h"
#include "behaviortree_cpp/loggers/bt_observer.h"
#include "behaviortree_cpp/xml_parsing.h"

#include <gtest/gtest.h>

using BT::NodeStatus;
using std::chrono::milliseconds;

TEST(Reactive, RunningChildren)
{
  static const char* reactive_xml_text = R"(
<root BTCPP_format="4" >
  <BehaviorTree ID="MainTree">
    <ReactiveSequence>
      <Sequence name="first">
        <TestA/>
        <TestB/>
        <TestC/>
      </Sequence>
      <AsyncSequence name="second">
        <TestD/>
        <TestE/>
        <TestF/>
      </AsyncSequence>
    </ReactiveSequence>
  </BehaviorTree>
</root>
)";

  BT::BehaviorTreeFactory factory;
  std::array<int, 6> counters{};
  RegisterTestTick(factory, "Test", counters);

  auto tree = factory.createTreeFromText(reactive_xml_text);

  NodeStatus status = NodeStatus::IDLE;

  int count = 0;
  while(!BT::isStatusCompleted(status) && count < 100)
  {
    count++;
    status = tree.tickExactlyOnce();
  }

  ASSERT_NE(100, count);

  ASSERT_EQ(status, NodeStatus::SUCCESS);

  ASSERT_EQ(counters[0], 3);
  ASSERT_EQ(counters[1], 3);
  ASSERT_EQ(counters[2], 3);

  ASSERT_EQ(counters[3], 1);
  ASSERT_EQ(counters[4], 1);
  ASSERT_EQ(counters[5], 1);
}

TEST(Reactive, Issue587)
{
  // TestA should be executed only once, because of the variable "test"

  static const char* reactive_xml_text = R"(
<root BTCPP_format="4" >
  <BehaviorTree ID="Example_A">
    <Sequence>
      <Script code="test := false"/>
      <ReactiveSequence>
        <RetryUntilSuccessful name="Retry 1" num_attempts="-1" _skipIf="test ">
          <TestA name="Success 1" _onSuccess="test = true"/>
        </RetryUntilSuccessful>
        <RetryUntilSuccessful name="Retry 2" num_attempts="5">
          <AlwaysFailure name="Failure 2"/>
        </RetryUntilSuccessful>
      </ReactiveSequence>
    </Sequence>
  </BehaviorTree>
</root>
)";

  BT::BehaviorTreeFactory factory;
  std::array<int, 2> counters{};
  RegisterTestTick(factory, "Test", counters);

  auto tree = factory.createTreeFromText(reactive_xml_text);
  tree.tickWhileRunning();

  ASSERT_EQ(counters[0], 1);
}

TEST(Reactive, PreTickHooks)
{
  using namespace BT;

  static const char* reactive_xml_text = R"(
<root BTCPP_format="4" >
  <BehaviorTree ID="Main">
    <ReactiveSequence>
      <AlwaysFailure name="failureA"/>
      <AlwaysFailure name="failureB"/>
      <Sleep msec="100"/>
    </ReactiveSequence>
  </BehaviorTree>
</root>
)";

  BehaviorTreeFactory factory;

  auto tree = factory.createTreeFromText(reactive_xml_text);

  TreeNode::PreTickCallback callback = [](TreeNode& node) -> NodeStatus {
    std::cout << node.name() << " callback" << std::endl;
    return NodeStatus::SUCCESS;
  };

  tree.applyVisitor([&](TreeNode* node) -> void {
    if(auto dd = dynamic_cast<BT::AlwaysFailureNode*>(node))
    {
      dd->setPreTickFunction(callback);
    }
  });

  auto ret = tree.tickWhileRunning();
  ASSERT_EQ(ret, NodeStatus::SUCCESS);
}

TEST(Reactive, TestLogging)
{
  using namespace BT;

  static const char* reactive_xml_text = R"(
<root BTCPP_format="4" >
  <BehaviorTree ID="Main">
    <ReactiveSequence>
      <TestA name="testA"/>
      <AlwaysSuccess name="success"/>
      <Sleep msec="100"/>
    </ReactiveSequence>
  </BehaviorTree>
</root>
)";

  BehaviorTreeFactory factory;

  std::array<int, 1> counters{};
  RegisterTestTick(factory, "Test", counters);

  auto tree = factory.createTreeFromText(reactive_xml_text);
  TreeObserver observer(tree);

  auto ret = tree.tickWhileRunning();
  ASSERT_EQ(ret, NodeStatus::SUCCESS);

  int num_ticks = counters[0];
  ASSERT_GE(num_ticks, 5);

  ASSERT_EQ(observer.getStatistics("testA").success_count, num_ticks);
  ASSERT_EQ(observer.getStatistics("success").success_count, num_ticks);
}

TEST(Reactive, TwoAsyncNodesInReactiveSequence)
{
  static const char* reactive_xml_text = R"(
<root BTCPP_format="4" >
  <BehaviorTree ID="MainTree">
    <ReactiveSequence>
      <AsyncSequence name="first">
        <TestA/>
        <TestB/>
        <TestC/>
      </AsyncSequence>
      <AsyncSequence name="second">
        <TestD/>
        <TestE/>
        <TestF/>
      </AsyncSequence>
    </ReactiveSequence>
  </BehaviorTree>
</root>
)";

  BT::BehaviorTreeFactory factory;
  std::array<int, 6> counters{};
  RegisterTestTick(factory, "Test", counters);

  EXPECT_ANY_THROW(auto tree = factory.createTreeFromText(reactive_xml_text));
}

// ============ Phase 4: Additional Reactive Tests ============

TEST(Reactive, ReactiveSequence_FirstChildFails)
{
  // When first child fails, ReactiveSequence should return FAILURE immediately
  BT::BehaviorTreeFactory factory;
  std::array<int, 2> counters{};
  RegisterTestTick(factory, "Test", counters);

  static const char* xml_text = R"(
<root BTCPP_format="4">
  <BehaviorTree>
    <ReactiveSequence>
      <AlwaysFailure/>
      <TestA/>
    </ReactiveSequence>
  </BehaviorTree>
</root>
)";

  auto tree = factory.createTreeFromText(xml_text);
  auto status = tree.tickWhileRunning();

  ASSERT_EQ(status, NodeStatus::FAILURE);
  ASSERT_EQ(counters[0], 0);  // TestA should never be ticked
}

TEST(Reactive, ReactiveSequence_HaltOnConditionChange)
{
  // Test that running children are halted when an earlier condition changes
  BT::BehaviorTreeFactory factory;

  bool condition_result = true;
  int child_tick_count = 0;
  bool child_was_halted = false;

  factory.registerSimpleCondition("DynamicCondition", [&condition_result](BT::TreeNode&) {
    return condition_result ? NodeStatus::SUCCESS : NodeStatus::FAILURE;
  });

  factory.registerBuilder<BT::StatefulActionNode>(
      "TrackingAction", [&child_tick_count, &child_was_halted](
                            const std::string& name, const BT::NodeConfig& config) {
        class TrackingAction : public BT::StatefulActionNode
        {
        public:
          int& tick_count_;
          bool& was_halted_;
          TrackingAction(const std::string& name, const BT::NodeConfig& config,
                         int& tick_count, bool& was_halted)
            : BT::StatefulActionNode(name, config)
            , tick_count_(tick_count)
            , was_halted_(was_halted)
          {}
          NodeStatus onStart() override
          {
            tick_count_++;
            return NodeStatus::RUNNING;
          }
          NodeStatus onRunning() override
          {
            tick_count_++;
            return NodeStatus::RUNNING;
          }
          void onHalted() override
          {
            was_halted_ = true;
          }
        };
        return std::make_unique<TrackingAction>(name, config, child_tick_count,
                                                child_was_halted);
      });

  static const char* xml_text = R"(
<root BTCPP_format="4">
  <BehaviorTree>
    <ReactiveSequence>
      <DynamicCondition/>
      <TrackingAction/>
    </ReactiveSequence>
  </BehaviorTree>
</root>
)";

  auto tree = factory.createTreeFromText(xml_text);

  // First tick - condition passes, action starts
  auto status = tree.tickOnce();
  ASSERT_EQ(status, NodeStatus::RUNNING);
  ASSERT_GE(child_tick_count, 1);
  ASSERT_FALSE(child_was_halted);

  // Tick again while condition is still true
  status = tree.tickOnce();
  ASSERT_EQ(status, NodeStatus::RUNNING);

  // Now change condition to false - child should be halted
  condition_result = false;
  status = tree.tickOnce();
  ASSERT_EQ(status, NodeStatus::FAILURE);
  ASSERT_TRUE(child_was_halted);
}

TEST(Reactive, ReactiveFallback_FirstChildSucceeds)
{
  // When first child succeeds, ReactiveFallback should return SUCCESS immediately
  BT::BehaviorTreeFactory factory;
  std::array<int, 2> counters{};
  RegisterTestTick(factory, "Test", counters);

  static const char* xml_text = R"(
<root BTCPP_format="4">
  <BehaviorTree>
    <ReactiveFallback>
      <AlwaysSuccess/>
      <TestA/>
    </ReactiveFallback>
  </BehaviorTree>
</root>
)";

  auto tree = factory.createTreeFromText(xml_text);
  auto status = tree.tickWhileRunning();

  ASSERT_EQ(status, NodeStatus::SUCCESS);
  ASSERT_EQ(counters[0], 0);  // TestA should never be ticked
}

TEST(Reactive, ReactiveFallback_AllChildrenFail)
{
  BT::BehaviorTreeFactory factory;

  static const char* xml_text = R"(
<root BTCPP_format="4">
  <BehaviorTree>
    <ReactiveFallback>
      <AlwaysFailure/>
      <AlwaysFailure/>
      <AlwaysFailure/>
    </ReactiveFallback>
  </BehaviorTree>
</root>
)";

  auto tree = factory.createTreeFromText(xml_text);
  auto status = tree.tickWhileRunning();

  ASSERT_EQ(status, NodeStatus::FAILURE);
}

TEST(Reactive, ReactiveFallback_SecondChildSucceeds)
{
  BT::BehaviorTreeFactory factory;
  std::array<int, 2> counters{};
  RegisterTestTick(factory, "Test", counters);

  static const char* xml_text = R"(
<root BTCPP_format="4">
  <BehaviorTree>
    <ReactiveFallback>
      <AlwaysFailure/>
      <TestA/>
    </ReactiveFallback>
  </BehaviorTree>
</root>
)";

  auto tree = factory.createTreeFromText(xml_text);
  auto status = tree.tickWhileRunning();

  ASSERT_EQ(status, NodeStatus::SUCCESS);
  ASSERT_EQ(counters[0], 1);  // TestA ticked once and succeeded
}

TEST(Reactive, ReactiveSequence_AllChildrenSucceed)
{
  BT::BehaviorTreeFactory factory;
  std::array<int, 3> counters{};
  RegisterTestTick(factory, "Test", counters);

  static const char* xml_text = R"(
<root BTCPP_format="4">
  <BehaviorTree>
    <ReactiveSequence>
      <TestA/>
      <TestB/>
      <TestC/>
    </ReactiveSequence>
  </BehaviorTree>
</root>
)";

  auto tree = factory.createTreeFromText(xml_text);
  auto status = tree.tickWhileRunning();

  ASSERT_EQ(status, NodeStatus::SUCCESS);
  ASSERT_EQ(counters[0], 1);
  ASSERT_EQ(counters[1], 1);
  ASSERT_EQ(counters[2], 1);
}

TEST(Reactive, ReactiveSequence_ReEvaluatesOnEveryTick)
{
  // Verify that conditions are re-evaluated on every tick in ReactiveSequence
  BT::BehaviorTreeFactory factory;

  int condition_tick_count = 0;
  factory.registerSimpleCondition("CountingCondition",
                                  [&condition_tick_count](BT::TreeNode&) {
                                    condition_tick_count++;
                                    return NodeStatus::SUCCESS;
                                  });

  static const char* xml_text = R"(
<root BTCPP_format="4">
  <BehaviorTree>
    <ReactiveSequence>
      <CountingCondition/>
      <Sleep msec="50"/>
    </ReactiveSequence>
  </BehaviorTree>
</root>
)";

  auto tree = factory.createTreeFromText(xml_text);
  auto status = tree.tickWhileRunning();

  ASSERT_EQ(status, NodeStatus::SUCCESS);
  // Condition should be ticked multiple times (re-evaluated while Sleep is running)
  ASSERT_GE(condition_tick_count, 2);
}

namespace
{
struct ReactiveChildCase
{
  const char* name;
  const char* xml;
};

class ReactiveGenericChild : public testing::TestWithParam<ReactiveChildCase>
{
};

TEST_P(ReactiveGenericChild, ValidatesConstructsAndTicksSuccessfully)
{
  // GIVEN a ReactiveSequence containing a registered child in either XML form.
  BT::BehaviorTreeFactory factory;
  factory.registerSimpleCondition("Check",
                                  [](BT::TreeNode&) { return NodeStatus::SUCCESS; });
  const std::string xml =
      std::string(R"(<root BTCPP_format="4" main_tree_to_execute="Test">
        <BehaviorTree ID="Test"><Control ID="ReactiveSequence">)") +
      GetParam().xml +
      "</Control></BehaviorTree>"
      R"(<BehaviorTree ID="ChildTree"><AlwaysSuccess/></BehaviorTree></root>)";
  std::unordered_map<std::string, BT::NodeType> registered_nodes;
  for(const auto& [name, manifest] : factory.manifests())
  {
    registered_nodes.emplace(name, manifest.type);
  }

  // WHEN validation and construction consume the same XML and node catalog.
  ASSERT_NO_THROW(BT::VerifyXML(xml, registered_nodes));
  auto tree = factory.createTreeFromText(xml);

  // THEN the resolved child executes successfully.
  EXPECT_EQ(tree.tickExactlyOnce(), NodeStatus::SUCCESS);
}

INSTANTIATE_TEST_SUITE_P(
    XmlForms, ReactiveGenericChild,
    testing::Values(
        ReactiveChildCase{ "GenericAction", R"(<Action ID="AlwaysSuccess"/>)" },
        ReactiveChildCase{ "NativeAction", "<AlwaysSuccess/>" },
        ReactiveChildCase{ "SubTree", R"(<SubTree ID="ChildTree"/>)" },
        ReactiveChildCase{ "GenericCondition", R"(<Condition ID="Check"/>)" },
        ReactiveChildCase{ "GenericControl",
                           R"(<Control ID="Sequence"><AlwaysSuccess/></Control>)" },
        ReactiveChildCase{ "GenericDecorator",
                           R"(<Decorator ID="Inverter"><AlwaysFailure/></Decorator>)" }),
    [](const testing::TestParamInfo<ReactiveChildCase>& info) {
      return info.param.name;
    });
}  // namespace

TEST(Reactive, GenericAsyncChildrenPreserveMultipleAsyncRejection)
{
  // GIVEN two asynchronous control children expressed with generic tags.
  BT::BehaviorTreeFactory factory;
  const std::string xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="Test">
        <ReactiveSequence>
          <Control ID="AsyncSequence"><AlwaysSuccess/></Control>
          <Control ID="AsyncSequence"><AlwaysSuccess/></Control>
        </ReactiveSequence>
      </BehaviorTree>
    </root>)";

  // WHEN registering the tree.
  // THEN the async-child guard still rejects it for the intended reason.
  try
  {
    factory.registerBehaviorTreeFromText(xml);
    FAIL() << "Expected multiple async children to be rejected";
  }
  catch(const BT::RuntimeError& error)
  {
    EXPECT_NE(std::string(error.what()).find("more than one async child"),
              std::string::npos)
        << error.what();
  }
}

TEST(Reactive, UnknownGenericChildIdIsRejected)
{
  // GIVEN a generic Action referencing an unregistered Behavior.
  BT::BehaviorTreeFactory factory;
  const std::string xml = R"(
    <root BTCPP_format="4">
      <BehaviorTree ID="Test">
        <ReactiveSequence><Action ID="MissingBehavior"/></ReactiveSequence>
      </BehaviorTree>
    </root>)";

  // WHEN registering the tree.
  // THEN the unresolved ID remains an error and identifies the missing Behavior.
  try
  {
    factory.registerBehaviorTreeFromText(xml);
    FAIL() << "Expected unknown child ID to be rejected";
  }
  catch(const BT::RuntimeError& error)
  {
    EXPECT_NE(std::string(error.what()).find("MissingBehavior"), std::string::npos)
        << error.what();
  }
}

TEST(Reactive, MissingOrEmptyGenericChildIdIsRejected)
{
  // GIVEN generic child tags without a usable registered ID.
  for(const char* child : { "<Action/>", R"(<Action ID=""/>)" })
  {
    BT::BehaviorTreeFactory factory;
    const std::string xml = std::string(R"(<root BTCPP_format="4"><BehaviorTree ID="Test">
          <ReactiveSequence>)") +
                            child + "</ReactiveSequence></BehaviorTree></root>";

    // WHEN registering the malformed tree.
    // THEN the generic tag cannot stand in for a registered Behavior.
    EXPECT_THROW(factory.registerBehaviorTreeFromText(xml), BT::RuntimeError) << child;
  }
}
