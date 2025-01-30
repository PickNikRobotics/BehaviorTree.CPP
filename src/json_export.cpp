#include "behaviortree_cpp/json_export.h"

namespace
{
constexpr std::string_view kTypeField = "__type";
constexpr std::string_view kValueField = "value";
}  // namespace

namespace BT
{

JsonExporter& JsonExporter::get()
{
  static JsonExporter global_instance;
  return global_instance;
}

bool JsonExporter::toJson(const Any& any, nlohmann::json& dst) const
{
  auto const& type = any.castedType();

  if(any.isString())
  {
    dst[kTypeField] = "string";
    dst[kValueField] = any.cast<std::string>();
  }
  else if(type == typeid(int64_t))
  {
    dst[kTypeField] = "int64_t";
    dst[kValueField] = any.cast<int64_t>();
  }
  else if(type == typeid(uint64_t))
  {
    dst[kTypeField] = "uint64_t";
    dst[kValueField] = any.cast<uint64_t>();
  }
  else if(type == typeid(double))
  {
    dst[kTypeField] = "double";
    dst[kValueField] = any.cast<double>();
  }
  else if(type == typeid(std::vector<double>))
  {
    dst = any.cast<std::vector<double>>();
  }
  else if(type == typeid(std::vector<int>))
  {
    dst = any.cast<std::vector<int>>();
  }
  else if(type == typeid(std::vector<std::string>))
  {
    dst = any.cast<std::vector<std::string>>();
  }
  else if(type == typeid(std::vector<bool>))
  {
    dst = any.cast<std::vector<bool>>();
  }
  else
  {
    auto it = to_json_converters_.find(type);
    if(it != to_json_converters_.end())
    {
      it->second(any, dst);
    }
    else
    {
      return false;
    }
  }
  return true;
}

JsonExporter::ExpectedEntry JsonExporter::fromJson(const nlohmann::json& source) const
{
  if(source.is_null())
  {
    return Entry{ BT::Any(), BT::TypeInfo::Create<std::nullptr_t>() };
  }
  // Fork divergence: toJson() above serializes primitives in the tagged form
  // {"__type": <name>, "value": <primitive>} so they match how custom types are
  // represented. Accept that form here, then fall through to upstream's
  // bare-primitive handling so both wire formats round-trip.
  // Dispatch on __type naming a primitive, not merely on a "value" field being
  // present: a registered custom type is free to have its own "value" member, and
  // toJson() also emits {__type, value} for types with no converter registered.
  // Keying off presence alone would silently import either as a std::string.
  if(source.is_object() && source.contains(kTypeField) && source[kTypeField].is_string())
  {
    const auto type_name = source[kTypeField].get<std::string>();
    const auto source_value_it = source.find(kValueField);
    if(source_value_it != source.end())
    {
      if(type_name == "string" && source_value_it->is_string())
      {
        return Entry{ BT::Any(source_value_it->get<std::string>()),
                      BT::TypeInfo::Create<std::string>() };
      }
      if(type_name == "uint64_t" && source_value_it->is_number_unsigned())
      {
        return Entry{ BT::Any(source_value_it->get<uint64_t>()),
                      BT::TypeInfo::Create<uint64_t>() };
      }
      if(type_name == "int64_t" && source_value_it->is_number_integer())
      {
        return Entry{ BT::Any(source_value_it->get<int64_t>()),
                      BT::TypeInfo::Create<int64_t>() };
      }
      if(type_name == "double" && source_value_it->is_number_float())
      {
        return Entry{ BT::Any(source_value_it->get<double>()),
                      BT::TypeInfo::Create<double>() };
      }
      if(type_name == "bool" && source_value_it->is_boolean())
      {
        return Entry{ BT::Any(source_value_it->get<bool>()),
                      BT::TypeInfo::Create<bool>() };
      }
    }
  }

  if(source.is_string())
  {
    return Entry{ BT::Any(source.get<std::string>()),
                  BT::TypeInfo::Create<std::string>() };
  }
  if(source.is_number_integer())
  {
    return Entry{ BT::Any(source.get<int>()), BT::TypeInfo::Create<int>() };
  }
  if(source.is_number_float())
  {
    return Entry{ BT::Any(source.get<double>()), BT::TypeInfo::Create<double>() };
  }
  if(source.is_boolean())
  {
    return Entry{ BT::Any(source.get<bool>()), BT::TypeInfo::Create<bool>() };
  }

  // basic vectors
  if(source.is_array() && source.size() > 0 && !source.contains("__type"))
  {
    const auto first_element = source[0];
    if(first_element.is_string())
    {
      return Entry{ BT::Any(source.get<std::vector<std::string>>()),
                    BT::TypeInfo::Create<std::vector<std::string>>() };
    }
    if(first_element.is_number_integer())
    {
      return Entry{ BT::Any(source.get<std::vector<int>>()),
                    BT::TypeInfo::Create<std::vector<int>>() };
    }
    if(first_element.is_number_float())
    {
      return Entry{ BT::Any(source.get<std::vector<double>>()),
                    BT::TypeInfo::Create<std::vector<double>>() };
    }
    if(first_element.is_boolean())
    {
      return Entry{ BT::Any(source.get<std::vector<bool>>()),
                    BT::TypeInfo::Create<std::vector<bool>>() };
    }
  }

  if(source.is_array())
  {
    if(source.empty())
      return nonstd::make_unexpected("Missing field '__type'");
    const auto& first = source[0];
    if(!first.is_object() || !first.contains("__type"))
      return nonstd::make_unexpected("Missing field '__type'");
    if(!first["__type"].is_string())
      return nonstd::make_unexpected("Invalid '__type' (must be string)");
  }
  else
  {
    if(!source.is_object() || !source.contains("__type") || !source["__type"].is_string())
      return nonstd::make_unexpected("Missing field '__type'");
  }

  auto& from_converters =
      source.is_array() ? from_json_array_converters_ : from_json_converters_;
  auto type_field = source.is_array() ? source[0]["__type"] : source["__type"];

  auto type_it = type_names_.find(type_field);
  if(type_it == type_names_.end())
  {
    return nonstd::make_unexpected("Type not found in registered list");
  }
  auto func_it = from_converters.find(type_it->second.type());
  if(func_it == from_converters.end())
  {
    return nonstd::make_unexpected("Type not found in registered list");
  }
  return func_it->second(source);
}

JsonExporter::ExpectedEntry JsonExporter::fromJson(const nlohmann::json& source,
                                                   std::type_index type) const
{
  auto func_it = from_json_converters_.find(type);
  if(func_it == from_json_converters_.end())
  {
    return nonstd::make_unexpected("Type not found in registered list");
  }
  return func_it->second(source);
}

}  // namespace BT
