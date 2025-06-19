#include "behaviortree_cpp/python/types.h"

#include <pybind11/pybind11.h>
#include <pybind11/detail/typeid.h>

#include "behaviortree_cpp/json_export.h"
#include "behaviortree_cpp/contrib/json.hpp"

namespace BT
{

#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
    bool toPythonObject(const BT::Any& val, pybind11::object& dest)
{
  nlohmann::json json;
  if(JsonExporter::get().toJson(val, json))
  {
    dest = json;
    return true;
  }

  return false;
}

}  // namespace BT
