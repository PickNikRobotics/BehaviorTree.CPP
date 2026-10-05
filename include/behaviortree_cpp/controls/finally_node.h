#pragma once

#include "behaviortree_cpp/control_node.h"

#include <exception>

namespace BT
{
/**
 * @brief The Finally node ticks its first child ("main") and then ticks its
 * second child ("cleanup"), like try/finally.
 *
 * - Cleanup runs after main returns SUCCESS, FAILURE or SKIPPED.
 * - If main throws (any type), main is halted, cleanup runs, and then the
 *   exception is rethrown.
 * - The node returns main's status, or FAILURE if cleanup fails.
 * - If this node is halted while main is RUNNING, main is halted and cleanup
 *   is ticked once, synchronously, on the thread calling halt(). Halt-time
 *   cleanup must therefore be synchronous: if it returns RUNNING, it is halted.
 *   A slow cleanup delays the parent, for example a ReactiveSequence whose
 *   condition changed. Call halt() from the thread that ticks the tree.
 * - If this node is halted while cleanup is RUNNING, cleanup is halted and
 *   does not finish.
 * - Exceptions thrown by cleanup propagate from tick(), and the next tick
 *   retries cleanup. If the tree is halted instead, cleanup is not retried.
 * - halt() never throws, because it also runs from ~Tree(). It prints
 *   exceptions from cleanup or from halting a child, and a cleanup FAILURE,
 *   to stderr.
 *
 * Requires exactly 2 children, checked when the XML is loaded and on tick.
 */
class FinallyNode : public ControlNode
{
public:
  FinallyNode(const std::string& name, const NodeConfig& config);

  ~FinallyNode() override = default;

  FinallyNode(const FinallyNode&) = delete;
  FinallyNode& operator=(const FinallyNode&) = delete;
  FinallyNode(FinallyNode&&) = delete;
  FinallyNode& operator=(FinallyNode&&) = delete;

  static PortsList providedPorts()
  {
    return {};
  }

  void halt() override;

private:
  bool in_cleanup_ = false;
  NodeStatus main_status_ = NodeStatus::IDLE;
  std::exception_ptr main_exception_;

  void haltChildNoThrow(size_t i);

  BT::NodeStatus tick() override;
};

}  // namespace BT
