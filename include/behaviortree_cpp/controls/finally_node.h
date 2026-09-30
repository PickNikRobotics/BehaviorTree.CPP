#pragma once

#include "behaviortree_cpp/control_node.h"

namespace BT
{
/**
 * @brief The Finally node ticks its first child ("main") and then always ticks
 * its second child ("cleanup"), like try/finally.
 *
 * - Cleanup runs after main returns SUCCESS, FAILURE or SKIPPED.
 * - If main throws, the exception is printed to stderr, main is halted,
 *   cleanup runs, and this node returns FAILURE.
 * - If this node is halted while main is RUNNING, main is halted and cleanup
 *   is ticked once synchronously. If cleanup returns RUNNING, it is halted.
 * - The node returns main's status, or FAILURE if cleanup fails.
 * - Exceptions thrown by cleanup propagate.
 *
 * Requires exactly 2 children.
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

  BT::NodeStatus tick() override;
};

}  // namespace BT
