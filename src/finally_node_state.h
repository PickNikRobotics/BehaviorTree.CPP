#pragma once

#include "behaviortree_cpp/tree_node.h"

#include <atomic>
#include <thread>

namespace BT
{

struct TreeNode::FinallyState
{
  std::atomic<std::thread::id> tick_thread{};
  std::atomic<bool> halting{ false };
  bool main_started = false;
};

}  // namespace BT
