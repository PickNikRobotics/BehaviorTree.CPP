#!/usr/bin/env python3

"""
Top-level module of the BehaviorTree.CPP Python bindings.
"""

# re-export
from btpy_cpp import (
    BehaviorTreeFactory,
    NodeStatus,
    StatefulActionNode,
    SyncActionNode,
    Tree,
)


def ports(inputs: list[str] = [], outputs: list[str] = []):
    """Decorator to specify input and outputs ports for an action node."""

    def specify_ports(cls):
        cls.input_ports = list(inputs)
        cls.output_ports = list(outputs)
        return cls

    return specify_ports


class AsyncActionNode(StatefulActionNode):
    """An abstract action node implemented via cooperative multitasking.

    Subclasses must implement the `run()` method as a generator.  Optionally,
    this method can return a final `NodeStatus` value to indicate its exit
    condition.

    Optionally, subclasses can override the `on_halted()` method which is called
    when the tree halts. The default implementation does nothing. The `run()`
    method will never be called again after a halt.

    Note:
        It is the responsibility of the action author to not block the main
        behavior tree loop with long-running tasks. `yield` calls should be
        placed whenever a pause is appropriate.
    """

    def __init__(self, name, config):
        """
        Initialize the AsyncActionNode with the given name and configuration.
        """
        super().__init__(name, config)

    def on_start(self) -> NodeStatus:
        """
        Initialize the coroutine and return RUNNING status.
        """
        self.coroutine = self.run()
        return NodeStatus.RUNNING

    def on_running(self) -> NodeStatus:
        """
        Resume the coroutine (generator). As long as the generator is not
        exhausted, keep this action in the RUNNING state.
        """
        # The library logic should never allow this to happen, but users can
        # still manually call `on_running` without an associated `on_start`
        # call. Make sure to print a useful error when this happens.
        if self.coroutine is None:
            raise ValueError("AsyncActionNode run without starting")

        # Resume the coroutine (generator). As long as the generator is not
        # exhausted, keep this action in the RUNNING state.
        try:
            next(self.coroutine)
            return NodeStatus.RUNNING
        except StopIteration as e:
            # If the action returns a status then propagate it upwards.
            if e.value is not None:
                return e.value
            # Otherwise, just assume the action finished successfully.
            else:
                return NodeStatus.SUCCESS

    def on_halted(self):
        """
        What to do when the action is halted. Does nothing by default.
        """
        # Default action: do nothing
        pass


# Specify the symbols to be imported with `from btpy import *`, as described in
# [1].
#
# [1]: https://docs.python.org/3/tutorial/modules.html#importing-from-a-package
__all__ = [
    "ports",
    "AsyncActionNode",
    "BehaviorTreeFactory",
    "NodeStatus",
    "StatefulActionNode",
    "SyncActionNode",
    "Tree",
]
