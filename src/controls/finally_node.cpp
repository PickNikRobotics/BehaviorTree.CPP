#include "behaviortree_cpp/controls/finally_node.h"

#include <exception>
#include <iostream>
#include <string_view>
#include <utility>

namespace BT
{
namespace
{
void printException(std::string_view node_name, const char* context,
                    const std::exception_ptr& exception)
{
  std::cerr << "[" << node_name << "]: " << context << ": ";
  try
  {
    std::rethrow_exception(exception);
  }
  catch(const std::exception& ex)
  {
    std::cerr << ex.what() << std::endl;
  }
  catch(...)
  {
    std::cerr << "non-std exception" << std::endl;
  }
}
}  // namespace

FinallyNode::FinallyNode(const std::string& name, const NodeConfig& config)
  : ControlNode::ControlNode(name, config)
{
  setRegistrationID("Finally");
}

void FinallyNode::halt()
{
  // halt() also runs from ~Tree(), where a propagating exception terminates, so nothing here throws.
  if(!in_cleanup_ && status() == NodeStatus::RUNNING && children_nodes_.size() == 2)
  {
    haltChildNoThrow(0);
    try
    {
      if(children_nodes_[1]->executeTick() == NodeStatus::FAILURE)
      {
        std::cerr << "[" << name() << "]: cleanup returned FAILURE during halt"
                  << std::endl;
      }
    }
    catch(...)
    {
      printException(name(), "cleanup threw during halt", std::current_exception());
    }
  }
  for(size_t i = 0; i < children_nodes_.size(); i++)
  {
    haltChildNoThrow(i);
  }
  in_cleanup_ = false;
  main_status_ = NodeStatus::IDLE;
  main_exception_ = nullptr;
  resetStatus();
}

void FinallyNode::haltChildNoThrow(size_t i)
{
  try
  {
    haltChild(i);
  }
  catch(...)
  {
    printException(name(), "a child threw while being halted", std::current_exception());
  }
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
    main_exception_ = nullptr;
  }

  setStatus(NodeStatus::RUNNING);

  if(!in_cleanup_)
  {
    try
    {
      main_status_ = children_nodes_[0]->executeTick();
    }
    catch(...)
    {
      main_exception_ = std::current_exception();
      haltChildNoThrow(0);
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
  if(main_exception_)
  {
    // executeTick() keeps our RUNNING status when tick() throws, and halt() would rerun cleanup.
    resetStatus();
    std::rethrow_exception(std::exchange(main_exception_, nullptr));
  }
  if(cleanup_status == NodeStatus::FAILURE)
  {
    return NodeStatus::FAILURE;
  }
  if(main_status_ == NodeStatus::SKIPPED)
  {
    // executeTick() keeps our RUNNING status on SKIPPED, and halt() would rerun cleanup.
    resetStatus();
  }
  return main_status_;
}

}  // namespace BT
