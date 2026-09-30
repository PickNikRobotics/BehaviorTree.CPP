#include "behaviortree_cpp/controls/finally_node.h"

#include <iostream>

namespace BT
{
FinallyNode::FinallyNode(const std::string& name, const NodeConfig& config)
  : ControlNode::ControlNode(name, config)
{
  setRegistrationID("Finally");
}

void FinallyNode::halt()
{
  if(!in_cleanup_ && isStatusActive(status()) && children_nodes_.size() == 2)
  {
    haltChild(0);
    if(children_nodes_[1]->executeTick() == NodeStatus::RUNNING)
    {
      haltChild(1);
    }
  }
  in_cleanup_ = false;
  ControlNode::halt();
}

NodeStatus FinallyNode::tick()
{
  if(children_nodes_.size() != 2)
  {
    throw LogicError("[", name(), "]: Finally requires exactly 2 children");
  }

  if(!isStatusActive(status()))
  {
    in_cleanup_ = false;
  }

  setStatus(NodeStatus::RUNNING);

  if(!in_cleanup_)
  {
    try
    {
      main_status_ = children_nodes_[0]->executeTick();
    }
    catch(const std::exception& ex)
    {
      std::cerr << "[" << name() << "]: Finally caught an exception from its main child, "
                << "running cleanup and returning FAILURE: " << ex.what() << std::endl;
      haltChild(0);
      main_status_ = NodeStatus::FAILURE;
    }

    if(main_status_ == NodeStatus::RUNNING)
    {
      return NodeStatus::RUNNING;
    }
    if(main_status_ == NodeStatus::IDLE)
    {
      throw LogicError("[", name(), "]: A child should not return IDLE");
    }
    in_cleanup_ = true;
  }

  const NodeStatus cleanup_status = children_nodes_[1]->executeTick();
  if(cleanup_status == NodeStatus::RUNNING)
  {
    return NodeStatus::RUNNING;
  }

  resetChildren();
  in_cleanup_ = false;
  return cleanup_status == NodeStatus::FAILURE ? NodeStatus::FAILURE : main_status_;
}

}  // namespace BT
