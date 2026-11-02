/*  Copyright (C) 2022-2025 Davide Faconti -  All Rights Reserved
*
*   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"),
*   to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
*   and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
*   The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*
*   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
*   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include "behaviortree_cpp/scripting/any_types.hpp"
#include "behaviortree_cpp/scripting/script_parser.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Naive implementation of an AST with simple evaluation function.
namespace BT::Ast
{
using SimpleString = SafeAny::SimpleString;

using expr_ptr = std::shared_ptr<struct ExprBase>;

namespace detail
{
// 2^53: every integer up to here is exact as a double.
constexpr double kMaxExactDouble =
    static_cast<double>(int64_t{ 1 } << std::numeric_limits<double>::digits);

inline std::string_view TrimSpaces(std::string_view str)
{
  const auto is_space = [](char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
  };
  while(!str.empty() && is_space(str.front()))
  {
    str.remove_prefix(1);
  }
  while(!str.empty() && is_space(str.back()))
  {
    str.remove_suffix(1);
  }
  return str;
}

// Whether str spells a decimal number: digits, an optional leading '-', one '.', and an
// exponent whose sign may be '+'. Checked before parsing so every standard library rejects the
// same strings ("0x10", "nan", "+5").
inline bool IsDecimalNumberText(std::string_view str)
{
  bool digit_seen = false;
  bool dot_seen = false;
  bool exponent_seen = false;
  for(size_t i = 0; i < str.size(); ++i)
  {
    const char c = str[i];
    const bool after_exponent = i > 0 && (str[i - 1] == 'e' || str[i - 1] == 'E');
    if(std::isdigit(static_cast<unsigned char>(c)) != 0)
    {
      digit_seen = true;
    }
    else if(c == '-' && (i == 0 || after_exponent))
    {
    }
    else if(c == '+' && after_exponent)
    {
    }
    else if(c == '.' && !dot_seen && !exponent_seen)
    {
      dot_seen = true;
    }
    else if((c == 'e' || c == 'E') && digit_seen && !exponent_seen)
    {
      exponent_seen = true;
      digit_seen = false;
    }
    else
    {
      return false;
    }
  }
  return digit_seen;
}

// Locale-independent parsing for standard libraries without floating-point std::from_chars.
inline std::optional<double> StreamParseNumber(std::string_view str)
{
  double out = 0;
  std::istringstream stream{ std::string(str) };
  stream.imbue(std::locale::classic());
  stream >> out;
  if(stream.fail() || stream.peek() != std::char_traits<char>::eof())
  {
    return std::nullopt;
  }
  return out;
}

// Locale-independent shortest %g form that reads back exactly.
inline std::string StreamFormatNumber(double number)
{
  std::string text;
  for(int precision = std::numeric_limits<double>::digits10;
      precision <= std::numeric_limits<double>::max_digits10; ++precision)
  {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(precision) << number;
    text = stream.str();
    if(StreamParseNumber(text) == number)
    {
      break;
    }
  }
  return text;
}

// The number a string holds, if all of it apart from surrounding spaces is a finite decimal
// number: "12", " 5 " and "-1.5e3" are numbers; "3 apples", "+5", "0x10", "nan" and "" are
// not. Any::stringToNumber accepts a numeric prefix, so scripts don't use it.
inline std::optional<double> ParseWholeNumber(std::string_view text)
{
  const auto str = TrimSpaces(text);
  if(!IsDecimalNumberText(str))
  {
    return std::nullopt;
  }
  double out = 0;
#if __cpp_lib_to_chars >= 201611L
  const char* end = str.data() + str.size();
  const auto [ptr, err] = std::from_chars(str.data(), end, out);
  if(err != std::errc() || ptr != end)
  {
    return std::nullopt;
  }
#else
  if(auto parsed = StreamParseNumber(str))
  {
    out = *parsed;
  }
  else
  {
    return std::nullopt;
  }
#endif
  if(!std::isfinite(out))
  {
    return std::nullopt;
  }
  // Too small for a double: reject it on every standard library, as from_chars does.
  const auto mantissa = str.substr(0, str.find_first_of("eE"));
  if(out == 0 && mantissa.find_first_of("123456789") != std::string_view::npos)
  {
    return std::nullopt;
  }
  return out;
}

// An integer string parsed exactly, so values above 2^53 keep every digit.
inline std::optional<int64_t> ParseWholeInteger(std::string_view text)
{
  const auto str = TrimSpaces(text);
  int64_t out = 0;
  const char* end = str.data() + str.size();
  const auto [ptr, err] = std::from_chars(str.data(), end, out);
  if(str.empty() || err != std::errc() || ptr != end)
  {
    return std::nullopt;
  }
  return out;
}

// The number a string holds for arithmetic. Integer text beyond 2^53 is an error, as it is for
// an int64 entry, because a double would silently round it.
inline std::optional<double> ArithmeticString(std::string_view text)
{
  const auto number = ParseWholeNumber(text);
  const auto str = TrimSpaces(text);
  if(number && str.find_first_not_of("-0123456789") == std::string_view::npos)
  {
    // Checked on the exact integer: the double has already rounded it.
    constexpr auto kMaxExactInteger = static_cast<int64_t>(kMaxExactDouble);
    const auto exact = ParseWholeInteger(str);
    if(!exact || *exact > kMaxExactInteger || *exact < -kMaxExactInteger)
    {
      throw RuntimeError(StrCat("Number ['", std::string(str),
                                "'] is too large to compute with exactly"));
    }
  }
  return number;
}

inline std::string_view StringView(const SimpleString& str)
{
  return { str.data(), str.size() };
}

inline std::string ShownOperand(const Any& operand)
{
  return operand.isString() ? StrCat("'", operand.cast<std::string>(), "'") :
                              demangle(operand.type());
}

inline std::string NotANumberError(const Any& operand, const char* op_str)
{
  auto msg = StrCat("Operand [", ShownOperand(operand), "] of operator [", op_str,
                    "] is not a number");
  if(std::string_view(op_str) == "+" || std::string_view(op_str) == "+=")
  {
    msg += ". Use [..] to join a string with a number";
  }
  return msg;
}

// A number as a double, reported as a script error when a double can't hold it exactly
// (for example an int64 above 2^53).
inline double NumberToDouble(const Any& value)
{
  try
  {
    return value.cast<double>();
  }
  catch(const std::exception& err)
  {
    throw RuntimeError(err.what());
  }
}

// The value of an arithmetic operand: a number, or a string that is entirely a number.
inline std::optional<double> ArithmeticOperand(const Any& value)
{
  if(value.isString())
  {
    return ArithmeticString(StringView(value.cast<SimpleString>()));
  }
  if(value.isNumber())
  {
    return NumberToDouble(value);
  }
  if(auto number = value.tryCast<double>())
  {
    return number.value();
  }
  return std::nullopt;
}

// The value of a bitwise operand as an exact integer.
inline int64_t BitwiseOperand(const Any& value, const char* op_str)
{
  try
  {
    if(value.isString())
    {
      const auto owned = value.cast<SimpleString>();
      const auto str = StringView(owned);
      if(auto exact = ParseWholeInteger(str))
      {
        return *exact;
      }
      if(auto number = ParseWholeNumber(str);
         number && std::abs(*number) <= kMaxExactDouble)
      {
        return Any(*number).cast<int64_t>();
      }
    }
    else if(value.isNumber())
    {
      return value.cast<int64_t>();
    }
  }
  catch(const std::exception&)
  {
    throw RuntimeError("Binary operators are not allowed if "
                       "one of the operands is not an integer");
  }
  throw RuntimeError(NotANumberError(value, op_str));
}

// A string or number as `..` joins it. Whole doubles print as integers ("2", not "2.000000")
// up to 2^53; other doubles in the shortest %g form that reads back exactly ("0.0001"), the
// same on every standard library.
inline std::string ConcatOperandString(const Any& value)
{
  if(value.castedType() != typeid(double))
  {
    return value.cast<std::string>();
  }
  const auto number = value.cast<double>();
  if(std::trunc(number) == number && std::abs(number) <= kMaxExactDouble)
  {
    return std::to_string(static_cast<int64_t>(number));
  }
  return StreamFormatNumber(number);
}
}  // namespace detail

// extended string to number that consider enums and booleans
inline double StringToDouble(const Any& value, const Environment& env)
{
  const auto str = value.cast<std::string>();
  if(str == "true")
  {
    return 1.0;
  }
  if(str == "false")
  {
    return 0.0;
  }
  if(env.enums)
  {
    auto it = env.enums->find(str);
    if(it != env.enums->end())
    {
      return it->second;
    }
  }
  if(auto number = detail::ArithmeticString(str))
  {
    return *number;
  }
  throw RuntimeError(StrCat("String ['", str, "'] is not a number"));
}

struct ExprBase
{
  using Ptr = std::shared_ptr<ExprBase>;

  virtual ~ExprBase() = default;
  virtual Any evaluate(Environment& env) const = 0;
};

inline std::string ErrorNotInit(const char* side, const char* op_str)
{
  return StrCat("The ", side, " operand of the operator [", op_str,
                "] is not initialized");
}

struct ExprLiteral : ExprBase
{
  Any value;

  ExprLiteral(Any v) : value(v)
  {}

  Any evaluate(Environment&) const override
  {
    return value;
  }
};

struct ExprName : ExprBase
{
  std::string name;

  explicit ExprName(std::string n) : name(std::move(n))
  {}

  Any evaluate(Environment& env) const override
  {
    //search first in the enums table
    if(env.enums)
    {
      auto enum_ptr = env.enums->find(name);
      if(enum_ptr != env.enums->end())
      {
        return Any(double(enum_ptr->second));
      }
    }
    // search now in the variables table
    auto any_ref = env.vars->getAnyLocked(name);
    if(!any_ref)
    {
      throw RuntimeError(StrCat("Variable not found: ", name));
    }
    return *any_ref.get();
  }
};

struct ExprUnaryArithmetic : ExprBase
{
  enum op_t
  {
    negate,
    complement,
    logical_not
  } op;
  expr_ptr rhs;

  explicit ExprUnaryArithmetic(op_t op, expr_ptr e) : op(op), rhs(std::move(e))
  {}

  Any evaluate(Environment& env) const override
  {
    auto rhs_v = rhs->evaluate(env);
    if(op == complement && (rhs_v.isString() || rhs_v.isIntegral()))
    {
      return Any(static_cast<double>(~detail::BitwiseOperand(rhs_v, "~")));
    }
    if(rhs_v.isString())
    {
      // A numeric string, for example from a SubTree port, takes part as the number it holds.
      const auto number =
          detail::ArithmeticString(detail::StringView(rhs_v.cast<SimpleString>()));
      if(!number)
      {
        throw RuntimeError(StrCat("Operand [", detail::ShownOperand(rhs_v),
                                  "] of a unary operator is not a number"));
      }
      rhs_v = Any(*number);
    }
    if(rhs_v.isNumber())
    {
      const double rv = detail::NumberToDouble(rhs_v);
      switch(op)
      {
        case negate:
          return Any(-rv);
        case complement:
          if(rv > static_cast<double>(std::numeric_limits<int64_t>::max()) ||
             rv < static_cast<double>(std::numeric_limits<int64_t>::min()))
          {
            throw RuntimeError("Number out of range for bitwise operation");
          }
          return Any(static_cast<double>(~static_cast<int64_t>(rv)));
        case logical_not:
          return Any(static_cast<double>(!static_cast<bool>(rv)));
      }
    }
    throw RuntimeError("ExprUnaryArithmetic: undefined");
  }
};

struct ExprBinaryArithmetic : ExprBase
{
  enum op_t
  {
    plus,
    minus,
    times,
    div,
    concat,

    bit_and,
    bit_or,
    bit_xor,

    logic_and,
    logic_or
  } op;

  const char* opStr() const
  {
    switch(op)
    {
      case plus:
        return "+";
      case minus:
        return "-";
      case times:
        return "*";
      case div:
        return "/";
      case concat:
        return "..";
      case bit_and:
        return "&";
      case bit_or:
        return "|";
      case bit_xor:
        return "^";
      case logic_and:
        return "&&";
      case logic_or:
        return "||";
    }
    return "";
  }

  expr_ptr lhs, rhs;

  explicit ExprBinaryArithmetic(expr_ptr lhs, op_t op, expr_ptr rhs)
    : op(op), lhs(std::move(lhs)), rhs(std::move(rhs))
  {}

  Any evaluate(Environment& env) const override
  {
    auto lhs_v = lhs->evaluate(env);
    auto rhs_v = rhs->evaluate(env);

    if(lhs_v.empty())
    {
      throw RuntimeError(ErrorNotInit("left", opStr()));
    }
    if(rhs_v.empty())
    {
      throw RuntimeError(ErrorNotInit("right", opStr()));
    }

    if(op == concat)
    {
      if((lhs_v.isString() || lhs_v.isNumber()) && (rhs_v.isString() || rhs_v.isNumber()))
      {
        return Any(detail::ConcatOperandString(lhs_v) +
                   detail::ConcatOperandString(rhs_v));
      }
      const auto& bad = (lhs_v.isString() || lhs_v.isNumber()) ? rhs_v : lhs_v;
      throw RuntimeError(StrCat("Operand [", detail::ShownOperand(bad),
                                "] of operator [..] is neither a string nor a number"));
    }

    // `+` joins two strings until PickNikRobotics/moveit_pro#24181 removes it.
    if(op == plus && lhs_v.isString() && rhs_v.isString())
    {
      return Any(lhs_v.cast<std::string>() + rhs_v.cast<std::string>());
    }

    if(op == bit_and || op == bit_or || op == bit_xor)
    {
      const int64_t li = detail::BitwiseOperand(lhs_v, opStr());
      const int64_t ri = detail::BitwiseOperand(rhs_v, opStr());
      switch(op)
      {
        case bit_and:
          return Any(static_cast<double>(li & ri));
        case bit_or:
          return Any(static_cast<double>(li | ri));
        default:
          return Any(static_cast<double>(li ^ ri));
      }
    }

    const auto lv = detail::ArithmeticOperand(lhs_v);
    if(!lv)
    {
      throw RuntimeError(detail::NotANumberError(lhs_v, opStr()));
    }
    const auto rv = detail::ArithmeticOperand(rhs_v);
    if(!rv)
    {
      throw RuntimeError(detail::NotANumberError(rhs_v, opStr()));
    }
    switch(op)
    {
      case plus:
        return Any(*lv + *rv);
      case minus:
        return Any(*lv - *rv);
      case times:
        return Any(*lv * *rv);
      case div:
        return Any(*lv / *rv);
      default: {
      }
    }

    if(op == logic_or || op == logic_and)
    {
      try
      {
        auto lb = (lhs_v.isNumber() ? lhs_v : Any(*lv)).cast<bool>();
        auto rb = (rhs_v.isNumber() ? rhs_v : Any(*rv)).cast<bool>();
        switch(op)
        {
          case logic_or:
            return Any(static_cast<double>(lb || rb));
          case logic_and:
            return Any(static_cast<double>(lb && rb));
          default: {
          }
        }
      }
      catch(...)
      {
        throw RuntimeError("Logic operators are not allowed if "
                           "one of the operands is not castable to bool");
      }
    }

    throw RuntimeError("Operation not permitted");
  }
};

template <typename T>
bool IsSame(const T& lv, const T& rv)
{
  if constexpr(std::is_same_v<double, T>)
  {
    constexpr double EPS = static_cast<double>(std::numeric_limits<float>::epsilon());
    return std::abs(lv - rv) <= EPS;
  }
  else
  {
    return (lv == rv);
  }
}

struct ExprComparison : ExprBase
{
  enum op_t
  {
    equal,
    not_equal,
    less,
    greater,
    less_equal,
    greater_equal
  };

  const char* opStr(op_t op) const
  {
    switch(op)
    {
      case equal:
        return "==";
      case not_equal:
        return "!=";
      case less:
        return "<";
      case greater:
        return ">";
      case less_equal:
        return "<=";
      case greater_equal:
        return ">=";
    }
    return "";
  }

  std::vector<op_t> ops;
  std::vector<expr_ptr> operands;

  Any evaluate(Environment& env) const override
  {
    auto SwitchImpl = [&](const auto& lv, const auto& rv, op_t op) {
      switch(op)
      {
        case equal:
          if(!IsSame(lv, rv))
            return false;
          break;
        case not_equal:
          if(IsSame(lv, rv))
            return false;
          break;
        case less:
          if(lv >= rv)
            return false;
          break;
        case greater:
          if(lv <= rv)
            return false;
          break;
        case less_equal:
          if(lv > rv)
            return false;
          break;
        case greater_equal:
          if(lv < rv)
            return false;
          break;
      }
      return true;
    };

    auto lhs_v = operands[0]->evaluate(env);
    for(auto i = 0u; i != ops.size(); ++i)
    {
      auto rhs_v = operands[i + 1]->evaluate(env);

      if(lhs_v.empty())
      {
        throw RuntimeError(ErrorNotInit("left", opStr(ops[i])));
      }
      if(rhs_v.empty())
      {
        throw RuntimeError(ErrorNotInit("right", opStr(ops[i])));
      }
      const Any False(0.0);

      if(lhs_v.isNumber() && rhs_v.isNumber())
      {
        auto lv = detail::NumberToDouble(lhs_v);
        auto rv = detail::NumberToDouble(rhs_v);
        if(!SwitchImpl(lv, rv, ops[i]))
        {
          return False;
        }
      }
      else if(lhs_v.isString() && rhs_v.isString())
      {
        auto lv = lhs_v.cast<SimpleString>();
        auto rv = rhs_v.cast<SimpleString>();
        // Two strings that are both numbers, for example from SubTree ports, compare as numbers,
        // exactly: integers as int64, others as long double, which IsSame compares without the
        // tolerance it applies to double. Strings compare as text only if one isn't a number.
        const auto lview = detail::StringView(lv);
        const auto rview = detail::StringView(rv);
        const auto li = detail::ParseWholeInteger(lview);
        const auto ri = detail::ParseWholeInteger(rview);
        const auto ln = detail::ParseWholeNumber(lview);
        const auto rn = detail::ParseWholeNumber(rview);
        bool holds = false;
        if(li && ri)
        {
          holds = SwitchImpl(*li, *ri, ops[i]);
        }
        else if(ln && rn && std::abs(*ln) <= detail::kMaxExactDouble &&
                std::abs(*rn) <= detail::kMaxExactDouble)
        {
          holds = SwitchImpl(static_cast<long double>(*ln), static_cast<long double>(*rn),
                             ops[i]);
        }
        else if(ln && rn)
        {
          throw RuntimeError(StrCat("Numbers ['", lv.toStdString(), "'] and ['",
                                    rv.toStdString(),
                                    "'] are too large to compare exactly"));
        }
        else
        {
          holds = SwitchImpl(lv, rv, ops[i]);
        }
        if(!holds)
        {
          return False;
        }
      }
      else if(lhs_v.isString() && rhs_v.isNumber())
      {
        auto lv = StringToDouble(lhs_v, env);
        auto rv = detail::NumberToDouble(rhs_v);
        if(!SwitchImpl(lv, rv, ops[i]))
        {
          return False;
        }
      }
      else if(lhs_v.isNumber() && rhs_v.isString())
      {
        auto lv = detail::NumberToDouble(lhs_v);
        auto rv = StringToDouble(rhs_v, env);
        if(!SwitchImpl(lv, rv, ops[i]))
        {
          return False;
        }
      }
      else
      {
        throw RuntimeError(StrCat("Can't mix different types in Comparison. "
                                  "Left operand [",
                                  BT::demangle(lhs_v.type()), "] right operand [",
                                  BT::demangle(rhs_v.type()), "]"));
      }
      lhs_v = rhs_v;
    }
    return Any(1.0);
  }
};

struct ExprIf : ExprBase
{
  expr_ptr condition, then, else_;

  explicit ExprIf(expr_ptr condition, expr_ptr then, expr_ptr else_)
    : condition(std::move(condition)), then(std::move(then)), else_(std::move(else_))
  {}

  Any evaluate(Environment& env) const override
  {
    const auto& v = condition->evaluate(env);
    double number = 0;
    if(v.isString())
    {
      const auto parsed =
          detail::ArithmeticString(detail::StringView(v.cast<SimpleString>()));
      if(!parsed)
      {
        throw RuntimeError(
            StrCat("Condition [", detail::ShownOperand(v), "] of [?:] is not a number"));
      }
      number = *parsed;
    }
    else
    {
      number = detail::NumberToDouble(v);
    }
    if(number != 0.0)
    {
      return then->evaluate(env);
    }
    else
    {
      return else_->evaluate(env);
    }
  }
};

struct ExprAssignment : ExprBase
{
  enum op_t
  {
    assign_create,
    assign_existing,
    assign_plus,
    assign_minus,
    assign_times,
    assign_div
  } op;

  const char* opStr() const
  {
    switch(op)
    {
      case assign_create:
        return ":=";
      case assign_existing:
        return "=";
      case assign_plus:
        return "+=";
      case assign_minus:
        return "-=";
      case assign_times:
        return "*=";
      case assign_div:
        return "/=";
    }
    return "";
  }

  expr_ptr lhs, rhs;

  explicit ExprAssignment(expr_ptr _lhs, op_t op, expr_ptr _rhs)
    : op(op), lhs(std::move(_lhs)), rhs(std::move(_rhs))
  {}

  Any evaluate(Environment& env) const override
  {
    auto varname = dynamic_cast<ExprName*>(lhs.get());
    if(!varname)
    {
      throw RuntimeError("Assignment left operand not a blackboard entry");
    }
    const auto& key = varname->name;

    auto entry = env.vars->getEntry(key);
    if(!entry)
    {
      // variable doesn't exist, create it if using operator assign_create
      if(op == assign_create)
      {
        env.vars->createEntry(key, PortInfo());
        entry = env.vars->getEntry(key);
        if(!entry)
        {
          throw LogicError("Bug: report");
        }
      }
      else
      {
        // fail otherwise
        auto msg = StrCat("The blackboard entry [", key,
                          "] doesn't exist, yet.\n"
                          "If you want to create a new one, "
                          "use the operator "
                          "[:=] instead of [=]");
        throw RuntimeError(msg);
      }
    }
    auto value = rhs->evaluate(env);

    std::scoped_lock lock(entry->entry_mutex);
    auto* dst_ptr = &entry->value;

    auto errorPrefix = [dst_ptr, &key]() {
      return StrCat("Error assigning a value to entry [", key, "] with type [",
                    BT::demangle(dst_ptr->type()), "]. ");
    };

    if(value.empty())
    {
      throw RuntimeError(ErrorNotInit("right", opStr()));
    }

    if(op == assign_create || op == assign_existing)
    {
      // the very fist assignment can come from any type.
      // In the future, type check will be done by Any::copyInto
      if(dst_ptr->empty() && entry->info.type() == typeid(AnyTypeAllowed))
      {
        *dst_ptr = value;
      }
      else if(value.isString() && !dst_ptr->isString())
      {
        // special case: string to other type.
        // Check if we can use the StringConverter
        auto const str = value.cast<std::string>();
        const auto* entry_info = env.vars->entryInfo(key);

        if(auto converter = entry_info->converter())
        {
          *dst_ptr = converter(str);
        }
        else if(dst_ptr->isNumber())
        {
          double num_value = 0;
          try
          {
            num_value = StringToDouble(value, env);
          }
          catch(const RuntimeError& err)
          {
            throw RuntimeError(StrCat(errorPrefix(), err.what()));
          }
          *dst_ptr = Any(num_value);
        }
        else
        {
          auto msg = StrCat(errorPrefix(),
                            "\nThe right operand is a string, "
                            "can't convert to ",
                            demangle(dst_ptr->type()));
          throw RuntimeError(msg);
        }
      }
      else
      {
        try
        {
          value.copyInto(*dst_ptr);
        }
        catch(std::exception&)
        {
          auto msg = StrCat(errorPrefix(), "\nThe right operand has type [",
                            BT::demangle(value.type()), "] and can't be converted to [",
                            BT::demangle(dst_ptr->type()), "]");
          throw RuntimeError(msg);
        }
      }
      entry->sequence_id++;
      entry->stamp = std::chrono::steady_clock::now().time_since_epoch();
      return *dst_ptr;
    }

    if(dst_ptr->empty())
    {
      throw RuntimeError(ErrorNotInit("left", opStr()));
    }

    // temporary use
    Any temp_variable = *dst_ptr;

    // `+=` joins two strings until PickNikRobotics/moveit_pro#24181 removes it.
    if(op == assign_plus && temp_variable.isString() && value.isString())
    {
      temp_variable = Any(temp_variable.cast<std::string>() + value.cast<std::string>());
    }
    else
    {
      const auto current = detail::ArithmeticOperand(temp_variable);
      if(!current)
      {
        throw RuntimeError(StrCat(errorPrefix(), "Its value is not a number, so [",
                                  opStr(), "] can't be applied"));
      }
      const auto operand = detail::ArithmeticOperand(value);
      if(!operand)
      {
        throw RuntimeError(
            StrCat(errorPrefix(), detail::NotANumberError(value, opStr())));
      }

      const auto lv = *current;
      const auto rv = *operand;
      switch(op)
      {
        case assign_plus:
          temp_variable = Any(lv + rv);
          break;
        case assign_minus:
          temp_variable = Any(lv - rv);
          break;
        case assign_times:
          temp_variable = Any(lv * rv);
          break;
        case assign_div:
          temp_variable = Any(lv / rv);
          break;
        default: {
        }
      }
    }

    if(temp_variable.isNumber() && dst_ptr->isString())
    {
      // The entry held a numeric string, so it keeps holding a string.
      *dst_ptr = Any(detail::ConcatOperandString(temp_variable));
    }
    else
    {
      temp_variable.copyInto(*dst_ptr);
    }
    entry->sequence_id++;
    entry->stamp = std::chrono::steady_clock::now().time_since_epoch();
    return *dst_ptr;
  }
};
}  // namespace BT::Ast

namespace BT::Scripting
{

/// Parse a script string into a list of AST expression nodes.
/// Throws std::runtime_error on parse failure.
std::vector<Ast::expr_ptr> parseStatements(const std::string& script);

}  // namespace BT::Scripting
