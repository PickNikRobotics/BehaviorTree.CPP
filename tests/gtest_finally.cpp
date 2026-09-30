#include "behaviortree_cpp/bt_factory.h"

#include <stdexcept>

#include <gtest/gtest.h>

using BT::NodeStatus;

class FinallyTest : public testing::Test
{
protected:
  BT::BehaviorTreeFactory factory;
  int cleanup_count = 0;
  int main_ticks = 0;

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
