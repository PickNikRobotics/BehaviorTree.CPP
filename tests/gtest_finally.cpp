#include "behaviortree_cpp/bt_factory.h"

#include <stdexcept>

#include <gtest/gtest.h>

using BT::NodeStatus;

// RUNNING until halted; throws on its second tick if `throw_on_running` is set.
class AsyncMain : public BT::StatefulActionNode
{
public:
  AsyncMain(const std::string& name, const BT::NodeConfig& config, int* halted,
            bool throw_on_running)
    : StatefulActionNode(name, config), halted_(halted), throw_(throw_on_running)
  {}
  static BT::PortsList providedPorts()
  {
    return {};
  }
  NodeStatus onStart() override
  {
    return NodeStatus::RUNNING;
  }
  NodeStatus onRunning() override
  {
    if(throw_)
    {
      throw std::runtime_error("boom");
    }
    return NodeStatus::RUNNING;
  }
  void onHalted() override
  {
    (*halted_)++;
  }

private:
  int* halted_;
  bool throw_;
};

class FinallyTest : public testing::Test
{
protected:
  BT::BehaviorTreeFactory factory;
  int cleanup_count = 0;
  int main_ticks = 0;
  int main_halted = 0;
  int cleanup_ticks = 0;

  void SetUp() override
  {
    factory.registerSimpleAction("Cleanup", [this](BT::TreeNode&) {
      cleanup_count++;
      return NodeStatus::SUCCESS;
    });
    factory.registerSimpleAction(
        "Throw", [](BT::TreeNode&) -> NodeStatus { throw std::runtime_error("boom"); });
    // RUNNING twice, then SUCCESS
    factory.registerSimpleCondition("RunTwice", [this](BT::TreeNode&) {
      return ++main_ticks < 3 ? NodeStatus::RUNNING : NodeStatus::SUCCESS;
    });
    factory.registerNodeType<AsyncMain>("AsyncMain", &main_halted, false);
    factory.registerNodeType<AsyncMain>("AsyncThrow", &main_halted, true);
    // Cleanup that is RUNNING on its first tick, then SUCCESS
    factory.registerSimpleCondition("AsyncCleanup", [this](BT::TreeNode&) {
      return ++cleanup_ticks < 2 ? NodeStatus::RUNNING : NodeStatus::SUCCESS;
    });
    factory.registerSimpleCondition("AlwaysRunning", [this](BT::TreeNode&) {
      main_ticks++;
      return NodeStatus::RUNNING;
    });
  }

  NodeStatus run(const std::string& main, const std::string& cleanup = "<Cleanup/>")
  {
    auto tree =
        factory.createTreeFromText(R"(<root BTCPP_format="4"><BehaviorTree><Finally>)" +
                                   main + cleanup + "</Finally></BehaviorTree></root>");
    return tree.tickWhileRunning();
  }
};

TEST_F(FinallyTest, MainSucceeds_CleanupRuns)
{
  EXPECT_EQ(run("<AlwaysSuccess/>"), NodeStatus::SUCCESS);
  EXPECT_EQ(cleanup_count, 1);
}

TEST_F(FinallyTest, MainFails_CleanupRuns)
{
  EXPECT_EQ(run("<AlwaysFailure/>"), NodeStatus::FAILURE);
  EXPECT_EQ(cleanup_count, 1);
}

TEST_F(FinallyTest, CleanupFails_ReturnsFailure)
{
  EXPECT_EQ(run("<AlwaysSuccess/>", "<AlwaysFailure/>"), NodeStatus::FAILURE);
}

TEST_F(FinallyTest, MainThrows_CleanupRunsAndReturnsFailure)
{
  EXPECT_EQ(run("<Throw/>"), NodeStatus::FAILURE);
  EXPECT_EQ(cleanup_count, 1);
}

TEST_F(FinallyTest, NestedMainThrows_CleanupRunsAndReturnsFailure)
{
  EXPECT_EQ(run("<Sequence><AlwaysSuccess/><Throw/></Sequence>"), NodeStatus::FAILURE);
  EXPECT_EQ(cleanup_count, 1);
}

TEST_F(FinallyTest, CleanupThrows_Propagates)
{
  EXPECT_THROW(run("<AlwaysSuccess/>", "<Throw/>"), BT::RuntimeError);
}

TEST_F(FinallyTest, AsyncMain_CleanupRunsOnceAfterMainCompletes)
{
  auto tree = factory.createTreeFromText(R"(
    <root BTCPP_format="4"><BehaviorTree>
      <Finally><RunTwice/><Cleanup/></Finally>
    </BehaviorTree></root>)");

  EXPECT_EQ(tree.tickOnce(), NodeStatus::RUNNING);
  EXPECT_EQ(tree.tickOnce(), NodeStatus::RUNNING);
  EXPECT_EQ(cleanup_count, 0);
  EXPECT_EQ(tree.tickOnce(), NodeStatus::SUCCESS);
  EXPECT_EQ(cleanup_count, 1);
}

TEST_F(FinallyTest, HaltWhileMainRunning_CleanupRuns)
{
  auto tree = factory.createTreeFromText(R"(
    <root BTCPP_format="4"><BehaviorTree>
      <Finally><AlwaysRunning/><Cleanup/></Finally>
    </BehaviorTree></root>)");

  EXPECT_EQ(tree.tickOnce(), NodeStatus::RUNNING);
  tree.haltTree();
  EXPECT_EQ(cleanup_count, 1);
  EXPECT_EQ(tree.rootNode()->status(), NodeStatus::IDLE);

  // A second halt on an idle node does not run cleanup again.
  tree.haltTree();
  EXPECT_EQ(cleanup_count, 1);
}

TEST_F(FinallyTest, TickedAgain_CleanupRunsEachTime)
{
  auto tree = factory.createTreeFromText(R"(
    <root BTCPP_format="4"><BehaviorTree>
      <Finally><AlwaysSuccess/><Cleanup/></Finally>
    </BehaviorTree></root>)");

  EXPECT_EQ(tree.tickWhileRunning(), NodeStatus::SUCCESS);
  EXPECT_EQ(tree.tickWhileRunning(), NodeStatus::SUCCESS);
  EXPECT_EQ(cleanup_count, 2);
}

TEST_F(FinallyTest, WrongChildCount_RejectedAtLoad)
{
  EXPECT_THROW((void)factory.createTreeFromText(R"(
    <root BTCPP_format="4"><BehaviorTree>
      <Finally><AlwaysSuccess/></Finally>
    </BehaviorTree></root>)"),
               BT::RuntimeError);
  EXPECT_THROW((void)factory.createTreeFromText(R"(
    <root BTCPP_format="4"><BehaviorTree>
      <Finally><AlwaysSuccess/><Cleanup/><Cleanup/></Finally>
    </BehaviorTree></root>)"),
               BT::RuntimeError);
}

TEST_F(FinallyTest, MainSkipped_CleanupRunsAndReturnsSkipped)
{
  auto tree = factory.createTreeFromText(R"(
    <root BTCPP_format="4"><BehaviorTree>
      <Finally><AlwaysSuccess _skipIf="true"/><Cleanup/></Finally>
    </BehaviorTree></root>)");

  EXPECT_EQ(tree.tickWhileRunning(), NodeStatus::SKIPPED);
  tree.haltTree();
  EXPECT_EQ(cleanup_count, 1);
}

TEST_F(FinallyTest, AsyncCleanup_ReturnsMainStatusWhenCleanupCompletes)
{
  EXPECT_EQ(run("<AlwaysFailure/>", "<AsyncCleanup/>"), NodeStatus::FAILURE);
  EXPECT_EQ(cleanup_ticks, 2);
}

TEST_F(FinallyTest, AsyncMainThrows_MainHaltedAndCleanupRuns)
{
  EXPECT_EQ(run("<AsyncThrow/>"), NodeStatus::FAILURE);
  EXPECT_EQ(main_halted, 1);
  EXPECT_EQ(cleanup_count, 1);
}

TEST_F(FinallyTest, HaltWhileMainRunning_MainHalted)
{
  auto tree = factory.createTreeFromText(R"(
    <root BTCPP_format="4"><BehaviorTree>
      <Finally><AsyncMain/><Cleanup/></Finally>
    </BehaviorTree></root>)");

  EXPECT_EQ(tree.tickOnce(), NodeStatus::RUNNING);
  tree.haltTree();
  EXPECT_EQ(main_halted, 1);
  EXPECT_EQ(cleanup_count, 1);
}

TEST_F(FinallyTest, HaltWhileCleanupRunning_CleanupNotRestarted)
{
  auto tree = factory.createTreeFromText(R"(
    <root BTCPP_format="4"><BehaviorTree>
      <Finally><AlwaysSuccess/><AsyncCleanup/></Finally>
    </BehaviorTree></root>)");

  EXPECT_EQ(tree.tickOnce(), NodeStatus::RUNNING);
  tree.haltTree();
  EXPECT_EQ(cleanup_ticks, 1);
}

TEST_F(FinallyTest, HaltCleanupRunning_CleanupHalted)
{
  auto tree = factory.createTreeFromText(R"(
    <root BTCPP_format="4"><BehaviorTree>
      <Finally><AlwaysRunning/><AsyncCleanup/></Finally>
    </BehaviorTree></root>)");

  EXPECT_EQ(tree.tickOnce(), NodeStatus::RUNNING);
  tree.haltTree();
  EXPECT_EQ(cleanup_ticks, 1);
  EXPECT_EQ(tree.rootNode()->status(), NodeStatus::IDLE);
}

TEST_F(FinallyTest, CleanupThrowsDuringHalt_DoesNotPropagate)
{
  auto tree = factory.createTreeFromText(R"(
    <root BTCPP_format="4"><BehaviorTree>
      <Finally><AlwaysRunning/><Throw/></Finally>
    </BehaviorTree></root>)");

  EXPECT_EQ(tree.tickOnce(), NodeStatus::RUNNING);
  EXPECT_NO_THROW(tree.haltTree());
  EXPECT_EQ(tree.rootNode()->status(), NodeStatus::IDLE);
}
