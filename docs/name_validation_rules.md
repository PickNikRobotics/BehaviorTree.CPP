# Name Validation Rules

This document describes the validation rules for names in Groot2 and BehaviorTree.CPP. These rules ensure XML compatibility while supporting Unicode characters (Chinese, Japanese, Korean, etc.). This fork keeps its 4.7.2 rules instead: model names are not validated, and port names are not checked against the forbidden characters below, except that C++ and `<TreeNodesModel>` port declarations reject whitespace. See [Validation Rules by Name Type](#validation-rules-by-name-type).

## Overview

The validation uses a **blacklist approach**: all characters are allowed except those explicitly forbidden. This enables Unicode support while blocking characters that would break XML serialization or cause path/filesystem issues.

## Forbidden Characters

`findForbiddenChar()` reports the following ASCII characters. Upstream rejects them in model and port names. This fork rejects only the whitespace ones, and only in C++ and `<TreeNodesModel>` port declarations:

| Category | Characters | Reason |
|----------|------------|--------|
| Whitespace | `space`, `\t`, `\n`, `\r` | Breaks XML element/attribute names |
| XML special | `<`, `>`, `&`, `"`, `'` | Reserved in XML |
| Path separators | `/`, `\`, `:` | Filesystem conflicts |
| Wildcards | `*`, `?`, `\|` | Shell/glob conflicts |
| Period | `.` | Ambiguous in port names (e.g., `request.name`) |
| Control chars | ASCII 0-31, 127 | Non-printable |

## Allowed Characters

| Category | Examples |
|----------|----------|
| ASCII letters | `a-z`, `A-Z` |
| Digits | `0-9` |
| Underscore | `_` |
| Hyphen | `-` |
| Unicode letters | `中文`, `日本語`, `한국어`, `Ümlauts` |

## Validation Rules by Name Type

### Model Name (Node Type Name)
This fork does not validate model names, as in 4.7.2. Tree IDs, SubTree IDs and node type names accept any character an XML attribute value can hold, including spaces, `"`, `&`, `/` and `Root`. A node type can only be written as an element, such as `<MyAction/>`, when its name is a valid XML name. Otherwise use `<Action ID="My Action"/>`.

### Port Name
This fork keeps its 4.7.2 rules.

Ports declared in C++ (`InputPort`, `OutputPort`, `BidirectionalPort`), and node attributes that the XML parser treats as ports:
- **Cannot be empty**
- **Must start with an ASCII letter**
- **Cannot be a reserved attribute**: `ID`, `name`, `_autoremap`, `_skipIf`, `_successIf`, `_failureIf`, `_while`, `_onSuccess`, `_onFailure`, `_onHalted`, `_post`
- C++ ports also **cannot contain whitespace**

Ports declared for a SubTree in `<TreeNodesModel>`:
- **Cannot contain whitespace**

### Instance Name
- **Can be empty** (defaults to model name)
- Instance names are XML attribute **values** (not element/attribute names), so most characters are allowed including spaces, periods, etc.
- Only invalid XML control characters are forbidden (ASCII 0-8, 11-12, 14-31, 127)

## Implementation

### C++ Reference Implementation

```cpp
#include <algorithm>
#include <array>
#include <string>

// Returns the forbidden character if found, or '\0' if valid
static char findForbiddenChar(const std::string& name)
{
  static constexpr std::array<char, 16> forbidden = {
      ' ', '\t', '\n', '\r', '<', '>', '&', '"', '\'', '/', '\\', ':', '*', '?', '|', '.'};

  for (unsigned char c : name)
  {
    // Allow UTF-8 multibyte sequences (high bit set)
    if (c >= 0x80)
    {
      continue;
    }
    // Block control characters
    if (c < 32 || c == 127)
    {
      return static_cast<char>(c);
    }
    // Check forbidden list
    if (std::find(forbidden.begin(), forbidden.end(), c) != forbidden.end())
    {
      return static_cast<char>(c);
    }
  }
  return '\0';
}
```

## Examples

### Valid Port Names
```
my_port
My-Port
request.name
goal:pose
Tür_öffnen      (non-ASCII after the first letter)
```

### Invalid Port Names
```
my port         (contains whitespace)
1port           (starts with a digit)
_private        (starts with an underscore)
检查门状态      (does not start with an ASCII letter)
name            (reserved)
```

### Valid Instance Names
Instance names have relaxed rules since they are XML attribute values:
```
My Action           (spaces allowed)
node.name           (periods allowed)
Success 1           (spaces allowed)
检查门状态          (Unicode allowed)
```

### Invalid Instance Names
```
name_with_null\0    (null character)
name_with_bell\x07  (control character)
```

## Related Issues

- [#59](https://github.com/BehaviorTree/Groot2/issues/59) - Unicode support in node names
- [#60](https://github.com/BehaviorTree/Groot2/issues/60) - i18n support request
- [#64](https://github.com/BehaviorTree/Groot2/issues/64) - Clear error for forbidden characters in port names

## Files Modified in BehaviorTree.CPP

- `include/behaviortree_cpp/basic_types.h` - `findForbiddenChar()` and `ThrowIfPortNameContainsWhitespace()` declarations
- `src/basic_types.cpp` - `findForbiddenChar()`, `IsAllowedPortName()` and `ThrowIfPortNameContainsWhitespace()`
- `src/xml_parsing.cpp` - whitespace check on `<TreeNodesModel>` ports, and instance-name validation
- `tests/gtest_name_validation.cpp` - tests for the rules above
