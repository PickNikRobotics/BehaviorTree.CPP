#include "behaviortree_cpp/xml_diff.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "behaviortree_cpp/exceptions.h"
#include "tinyxml2/tinyxml2.h"

namespace BT
{
namespace
{

using Attributes = std::vector<std::pair<std::string, std::string>>;

struct Node
{
  std::string tag;
  std::string identity;
  Attributes attributes;
  Attributes semantic_attributes;
  std::vector<Node*> children;
  Node* parent = nullptr;
  Node* match = nullptr;
  std::string fingerprint;
  size_t sibling_index = 0;
  size_t preorder_index = 0;
  size_t subtree_size = 1;
  bool reordered = false;
};

struct Document
{
  std::vector<std::unique_ptr<Node>> storage;
  std::vector<Node*> preorder;
  Node* root = nullptr;
};

bool IsIgnoredAttribute(const std::string& name)
{
  return name == "_uid" || name == "_fullPath" || name == "_fullpath";
}

bool IsGenericNodeTag(const std::string& tag)
{
  static const std::set<std::string> generic_tags = { "Action", "Condition", "Control",
                                                      "Decorator" };
  return generic_tags.count(tag) != 0;
}

std::string EscapeXML(std::string_view text)
{
  std::string escaped;
  escaped.reserve(text.size());
  for(const unsigned char ch : text)
  {
    switch(ch)
    {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&apos;";
        break;
      case '\t':
        escaped += "&#9;";
        break;
      case '\n':
        escaped += "&#10;";
        break;
      case '\r':
        escaped += "&#13;";
        break;
      default:
        if(ch < 0x20 || ch == 0x7F)
        {
          escaped += "&#" + std::to_string(ch) + ";";
        }
        else
        {
          escaped += static_cast<char>(ch);
        }
        break;
    }
  }
  return escaped;
}

std::string LengthPrefixed(std::string_view text)
{
  return std::to_string(text.size()) + ":" + std::string(text);
}

size_t LongestBacktickRun(std::string_view text)
{
  size_t longest_run = 0;
  size_t current_run = 0;
  for(const char ch : text)
  {
    current_run = ch == '`' ? current_run + 1 : 0;
    longest_run = std::max(longest_run, current_run);
  }
  return longest_run;
}

Node* BuildNode(const tinyxml2::XMLElement& element, Node* parent, size_t sibling_index,
                Document& document)
{
  auto node = std::make_unique<Node>();
  node->tag = element.Name();
  node->parent = parent;
  node->sibling_index = sibling_index;
  node->preorder_index = document.preorder.size();

  const char* generic_id = nullptr;
  for(const tinyxml2::XMLAttribute* attribute = element.FirstAttribute(); attribute;
      attribute = attribute->Next())
  {
    const std::string name = attribute->Name();
    const std::string value = attribute->Value();
    if(IsIgnoredAttribute(name))
    {
      continue;
    }
    node->attributes.emplace_back(name, value);
    if(name == "ID")
    {
      generic_id = attribute->Value();
    }
  }

  node->identity = IsGenericNodeTag(node->tag) && generic_id ? generic_id : node->tag;
  node->semantic_attributes = node->attributes;
  if(IsGenericNodeTag(node->tag) && generic_id)
  {
    node->semantic_attributes.erase(
        std::remove_if(node->semantic_attributes.begin(), node->semantic_attributes.end(),
                       [](const auto& attribute) { return attribute.first == "ID"; }),
        node->semantic_attributes.end());
  }
  std::sort(node->semantic_attributes.begin(), node->semantic_attributes.end());

  Node* result = node.get();
  document.storage.push_back(std::move(node));
  document.preorder.push_back(result);

  size_t child_index = 0;
  for(const tinyxml2::XMLElement* child = element.FirstChildElement(); child;
      child = child->NextSiblingElement())
  {
    result->children.push_back(BuildNode(*child, result, child_index++, document));
  }

  std::string fingerprint = LengthPrefixed(result->identity);
  for(const auto& [name, value] : result->semantic_attributes)
  {
    fingerprint += "A" + LengthPrefixed(name) + LengthPrefixed(value);
  }
  for(const Node* child : result->children)
  {
    fingerprint += "C" + LengthPrefixed(child->fingerprint);
    result->subtree_size += child->subtree_size;
  }
  result->fingerprint = std::move(fingerprint);
  return result;
}

bool IsXMLCharacter(uint32_t codepoint)
{
  return codepoint == '\t' || codepoint == '\n' || codepoint == '\r' ||
         (codepoint >= 0x20 && codepoint <= 0xD7FF) ||
         (codepoint >= 0xE000 && codepoint <= 0xFFFD) ||
         (codepoint >= 0x10000 && codepoint <= 0x10FFFF);
}

void ValidateXMLCharacters(std::string_view xml, std::string_view side)
{
  for(size_t index = 0; index < xml.size();)
  {
    const auto first = static_cast<unsigned char>(xml[index]);
    size_t length = 1;
    uint32_t codepoint = first;
    if(first >= 0xC2 && first <= 0xDF)
    {
      length = 2;
      codepoint &= 0x1F;
    }
    else if(first >= 0xE0 && first <= 0xEF)
    {
      length = 3;
      codepoint &= 0x0F;
    }
    else if(first >= 0xF0 && first <= 0xF4)
    {
      length = 4;
      codepoint &= 0x07;
    }
    else if(first >= 0x80)
    {
      throw RuntimeError("Malformed ", side, " XML: invalid UTF-8 at byte ",
                         std::to_string(index));
    }

    if(length > xml.size() - index)
    {
      throw RuntimeError("Malformed ", side, " XML: truncated UTF-8 at byte ",
                         std::to_string(index));
    }
    for(size_t offset = 1; offset < length; ++offset)
    {
      const auto continuation = static_cast<unsigned char>(xml[index + offset]);
      if((continuation & 0xC0) != 0x80)
      {
        throw RuntimeError("Malformed ", side, " XML: invalid UTF-8 at byte ",
                           std::to_string(index));
      }
      codepoint = (codepoint << 6) | (continuation & 0x3F);
    }

    if((length == 2 && codepoint < 0x80) || (length == 3 && codepoint < 0x800) ||
       (length == 4 && codepoint < 0x10000) || !IsXMLCharacter(codepoint))
    {
      throw RuntimeError("Malformed ", side, " XML: invalid character at byte ",
                         std::to_string(index));
    }
    index += length;
  }
}

void ValidateXMLCharacterReferences(std::string_view xml, std::string_view side)
{
  for(size_t index = 0; index < xml.size(); ++index)
  {
    // Character references in comments, CDATA and processing instructions
    // are literal text rather than references to decoded XML characters.
    size_t end = std::string_view::npos;
    size_t closing_length = 0;
    if(xml.compare(index, 4, "<!--") == 0)
    {
      end = xml.find("-->", index + 4);
      closing_length = 3;
    }
    else if(xml.compare(index, 9, "<![CDATA[") == 0)
    {
      end = xml.find("]]>", index + 9);
      closing_length = 3;
    }
    else if(xml.compare(index, 2, "<?") == 0)
    {
      end = xml.find("?>", index + 2);
      closing_length = 2;
    }
    if(closing_length)
    {
      if(end == std::string_view::npos)
      {
        break;  // The XML parser reports the unterminated section.
      }
      index = end + closing_length - 1;
      continue;
    }
    if(xml.compare(index, 2, "&#") != 0)
    {
      continue;
    }
    size_t digit = index + 2;
    const bool hexadecimal =
        digit < xml.size() && (xml[digit] == 'x' || xml[digit] == 'X');
    digit += hexadecimal ? 1 : 0;
    const size_t start = digit;
    uint32_t codepoint = 0;
    const uint32_t base = hexadecimal ? 16 : 10;
    while(digit < xml.size() && xml[digit] != ';')
    {
      const char ch = xml[digit];
      const int value = ch >= '0' && ch <= '9'                ? ch - '0' :
                        hexadecimal && ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 :
                        hexadecimal && ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 :
                                                                -1;
      if(value < 0 || codepoint > (0x10FFFF - static_cast<uint32_t>(value)) / base)
      {
        throw RuntimeError("Malformed ", side,
                           " XML: invalid character reference at byte ",
                           std::to_string(index));
      }
      codepoint = codepoint * base + static_cast<uint32_t>(value);
      ++digit;
    }
    if(digit == start || digit == xml.size() || !IsXMLCharacter(codepoint))
    {
      throw RuntimeError("Malformed ", side, " XML: invalid character reference at byte ",
                         std::to_string(index));
    }
    index = digit;
  }
}

Document ParseDocument(std::string_view xml, std::string_view side)
{
  ValidateXMLCharacters(xml, side);
  ValidateXMLCharacterReferences(xml, side);
  tinyxml2::XMLDocument xml_document;
  const tinyxml2::XMLError error = xml_document.Parse(xml.data(), xml.size());
  if(error != tinyxml2::XML_SUCCESS || !xml_document.RootElement())
  {
    throw RuntimeError("Malformed ", side, " XML: ", xml_document.ErrorStr());
  }
  if(xml_document.RootElement()->NextSiblingElement())
  {
    throw RuntimeError("Malformed ", side, " XML: multiple top-level elements");
  }
  for(const tinyxml2::XMLNode* node = xml_document.FirstChild(); node;
      node = node->NextSibling())
  {
    const tinyxml2::XMLText* text = node->ToText();
    if(text)
    {
      const std::string_view value = text->Value();
      const bool has_non_whitespace =
          std::any_of(value.begin(), value.end(), [](const char ch) {
            return !std::isspace(static_cast<unsigned char>(ch));
          });
      if(has_non_whitespace)
      {
        throw RuntimeError("Malformed ", side, " XML: non-whitespace top-level text");
      }
    }
  }

  Document document;
  document.root = BuildNode(*xml_document.RootElement(), nullptr, 0, document);
  return document;
}

void PairSubtrees(Node* before, Node* after)
{
  before->match = after;
  after->match = before;
  for(size_t index = 0; index < before->children.size(); ++index)
  {
    PairSubtrees(before->children[index], after->children[index]);
  }
}

bool SubtreeIsUnmatched(const Node* node)
{
  if(node->match)
  {
    return false;
  }
  return std::all_of(node->children.begin(), node->children.end(), SubtreeIsUnmatched);
}

std::tuple<int, size_t, size_t> PairingCost(const Node* before, const Node* after)
{
  int parent_cost = 1;
  if(!before->parent && !after->parent)
  {
    parent_cost = 0;
  }
  else if(before->parent && before->parent->match == after->parent)
  {
    parent_cost = 0;
  }
  const size_t sibling_cost = before->sibling_index > after->sibling_index ?
                                  before->sibling_index - after->sibling_index :
                                  after->sibling_index - before->sibling_index;
  const size_t preorder_cost = before->preorder_index > after->preorder_index ?
                                   before->preorder_index - after->preorder_index :
                                   after->preorder_index - before->preorder_index;
  return { parent_cost, sibling_cost, preorder_cost };
}

void MatchExactSubtrees(Document& before, Document& after)
{
  struct FingerprintGroup
  {
    size_t subtree_size;
    size_t first_preorder_index;
    std::vector<Node*> before_nodes;
    std::vector<Node*> after_nodes;
  };

  std::map<std::string, FingerprintGroup> groups;
  for(Node* node : before.preorder)
  {
    const auto group =
        groups
            .try_emplace(
                node->fingerprint,
                FingerprintGroup{ node->subtree_size, node->preorder_index, {}, {} })
            .first;
    group->second.before_nodes.push_back(node);
  }
  for(Node* node : after.preorder)
  {
    const auto group = groups.find(node->fingerprint);
    if(group != groups.end())
    {
      group->second.after_nodes.push_back(node);
    }
  }

  std::vector<FingerprintGroup*> ordered_groups;
  for(auto& entry : groups)
  {
    FingerprintGroup& group = entry.second;
    if(!group.after_nodes.empty())
    {
      ordered_groups.push_back(&group);
    }
  }
  std::stable_sort(ordered_groups.begin(), ordered_groups.end(),
                   [](const FingerprintGroup* lhs, const FingerprintGroup* rhs) {
                     if(lhs->subtree_size != rhs->subtree_size)
                     {
                       return lhs->subtree_size > rhs->subtree_size;
                     }
                     return lhs->first_preorder_index < rhs->first_preorder_index;
                   });

  using Position = std::pair<size_t, size_t>;
  using PositionIndex = std::map<Position, Node*>;
  for(const FingerprintGroup* group : ordered_groups)
  {
    PositionIndex remaining;
    std::map<Node*, PositionIndex> by_parent;
    for(Node* node : group->after_nodes)
    {
      if(!SubtreeIsUnmatched(node))
      {
        continue;
      }
      const Position position{ node->sibling_index, node->preorder_index };
      remaining.emplace(position, node);
      by_parent[node->parent].emplace(position, node);
    }

    const auto nearest = [](const PositionIndex& index,
                            const Node* before_node) -> Node* {
      if(index.empty())
      {
        return nullptr;
      }
      const auto next = index.lower_bound({ before_node->sibling_index, 0 });
      Node* best = nullptr;
      const auto consider = [&](const PositionIndex::const_iterator it) {
        if(it != index.end() && (!best || PairingCost(before_node, it->second) <
                                              PairingCost(before_node, best)))
        {
          best = it->second;
        }
      };
      consider(next);
      if(next != index.begin())
      {
        consider(std::prev(next));
      }
      return best;
    };
    const auto pair = [&](Node* before_node, Node* after_node) {
      const Position position{ after_node->sibling_index, after_node->preorder_index };
      remaining.erase(position);
      by_parent.at(after_node->parent).erase(position);
      PairSubtrees(before_node, after_node);
    };
    // Preserve unchanged sibling positions before considering a move.
    for(Node* node : group->before_nodes)
    {
      if(!SubtreeIsUnmatched(node))
      {
        continue;
      }
      Node* parent = node->parent ? node->parent->match : nullptr;
      if(node->parent && !parent)
      {
        continue;
      }
      const auto bucket = by_parent.find(parent);
      if(bucket == by_parent.end())
      {
        continue;
      }
      const auto exact = bucket->second.lower_bound({ node->sibling_index, 0 });
      if(exact != bucket->second.end() && exact->first.first == node->sibling_index)
      {
        pair(node, exact->second);
      }
    }
    for(Node* node : group->before_nodes)
    {
      if(!SubtreeIsUnmatched(node))
      {
        continue;
      }
      Node* parent = node->parent ? node->parent->match : nullptr;
      if(node->parent && !parent)
      {
        continue;
      }
      const auto bucket = by_parent.find(parent);
      if(bucket != by_parent.end())
      {
        if(Node* candidate = nearest(bucket->second, node))
        {
          pair(node, candidate);
        }
      }
    }
    for(Node* node : group->before_nodes)
    {
      // An ambiguous fingerprint beneath an as-yet-unmatched parent has no
      // reliable cross-parent cost. Leave it for parent-first similar matching.
      if(SubtreeIsUnmatched(node) &&
         ((group->before_nodes.size() == 1 && group->after_nodes.size() == 1) ||
          (node->parent && node->parent->match)))
      {
        if(Node* candidate = nearest(remaining, node))
        {
          pair(node, candidate);
        }
      }
    }
  }
}

size_t MatchedChildCount(const Node* before, const Node* after)
{
  size_t count = 0;
  for(const Node* child : before->children)
  {
    if(child->match && child->match->parent == after)
    {
      ++count;
    }
  }
  return count;
}

int AttributeSimilarity(const Node& before, const Node& after)
{
  int score = 0;
  for(const auto& entry : before.semantic_attributes)
  {
    const std::string& before_name = entry.first;
    const std::string& before_value = entry.second;
    const auto after_attribute = std::find_if(
        after.semantic_attributes.begin(), after.semantic_attributes.end(),
        [&](const auto& attribute) { return attribute.first == before_name; });
    if(after_attribute == after.semantic_attributes.end())
    {
      continue;
    }

    score += 20;
    if(after_attribute->second == before_value)
    {
      score += 80;
      if(before_name == "name")
      {
        score += 400;
      }
    }
  }
  return score;
}

bool ConflictingContainerNames(const Node& before, const Node& after)
{
  if(before.children.empty() && after.children.empty())
  {
    return false;
  }

  const auto find_name = [](const Node& node) -> const std::string* {
    const auto attribute =
        std::find_if(node.semantic_attributes.begin(), node.semantic_attributes.end(),
                     [](const auto& item) { return item.first == "name"; });
    return attribute == node.semantic_attributes.end() ? nullptr : &attribute->second;
  };

  const std::string* before_name = find_name(before);
  const std::string* after_name = find_name(after);
  return before_name && after_name && *before_name != *after_name &&
         MatchedChildCount(&before, &after) == 0;
}

std::tuple<int, int, size_t, size_t> SimilarityRank(const Node* before, const Node* after)
{
  int score = AttributeSimilarity(*before, *after);
  if(before->semantic_attributes == after->semantic_attributes)
  {
    score += 100;
  }
  if((!before->parent && !after->parent) ||
     (before->parent && before->parent->match == after->parent))
  {
    score += 200;
  }
  score += static_cast<int>(MatchedChildCount(before, after) * 50);

  const auto [parent_cost, sibling_cost, preorder_cost] = PairingCost(before, after);
  return { -score, parent_cost, sibling_cost, preorder_cost };
}

void MatchSimilarNodes(Document& before, Document& after)
{
  using Position = std::pair<size_t, size_t>;
  using PositionIndex = std::map<Position, Node*>;
  const auto node_name = [](const Node& node) -> const std::string* {
    const auto it =
        std::find_if(node.semantic_attributes.begin(), node.semantic_attributes.end(),
                     [](const auto& attribute) { return attribute.first == "name"; });
    return it == node.semantic_attributes.end() ? nullptr : &it->second;
  };
  struct IdentityGroup
  {
    PositionIndex all;
    PositionIndex unnamed_or_leaf;
    std::map<Node*, PositionIndex> by_parent;
    std::map<Node*, PositionIndex> unnamed_or_leaf_by_parent;
    std::map<std::string, PositionIndex> by_name;
    std::map<std::pair<Node*, std::string>, PositionIndex> by_parent_and_name;
  };
  std::map<std::string, IdentityGroup> groups;
  for(Node* node : after.preorder)
  {
    if(node->match)
    {
      continue;
    }
    auto& group = groups[node->identity];
    const Position position{ node->sibling_index, node->preorder_index };
    group.all.emplace(position, node);
    group.by_parent[node->parent].emplace(position, node);
    if(const std::string* name = node_name(*node))
    {
      group.by_name[*name].emplace(position, node);
      group.by_parent_and_name[{ node->parent, *name }].emplace(position, node);
    }
    if(node->children.empty() || !node_name(*node))
    {
      group.unnamed_or_leaf.emplace(position, node);
      group.unnamed_or_leaf_by_parent[node->parent].emplace(position, node);
    }
  }

  const auto nearest = [](const PositionIndex& index, const Node* node) -> Node* {
    if(index.empty())
    {
      return nullptr;
    }
    const auto next = index.lower_bound({ node->sibling_index, 0 });
    Node* best = nullptr;
    const auto consider = [&](const PositionIndex::const_iterator it) {
      if(it != index.end() &&
         (!best || SimilarityRank(node, it->second) < SimilarityRank(node, best)))
      {
        best = it->second;
      }
    };
    consider(next);
    if(next != index.begin())
    {
      consider(std::prev(next));
    }
    return best;
  };

  // Preorder establishes parent matches before their modified children. In
  // particular, lexical identity order must not pair children across parents.
  for(Node* node : before.preorder)
  {
    if(node->match)
    {
      continue;
    }
    const auto found = groups.find(node->identity);
    if(found == groups.end())
    {
      continue;
    }
    IdentityGroup& group = found->second;
    Node* parent = node->parent ? node->parent->match : nullptr;
    const std::string* name = node_name(*node);
    Node* candidate = nullptr;
    // A retained child is a stronger anchor than a changed container name.
    for(Node* child : node->children)
    {
      if(child->match && child->match->parent &&
         child->match->parent->identity == node->identity && !child->match->parent->match)
      {
        candidate = child->match->parent;
        break;
      }
    }
    if(!candidate && name && (!node->parent || parent))
    {
      const auto bucket = group.by_parent_and_name.find({ parent, *name });
      if(bucket != group.by_parent_and_name.end())
      {
        candidate = nearest(bucket->second, node);
      }
    }
    if(!candidate && name)
    {
      const auto bucket = group.by_name.find(*name);
      if(bucket != group.by_name.end())
      {
        candidate = nearest(bucket->second, node);
      }
    }
    const bool restrict_names =
        name && !node->children.empty() &&
        std::none_of(node->children.begin(), node->children.end(),
                     [](const Node* child) { return child->match; });
    if(!candidate && (!node->parent || parent))
    {
      const auto& index =
          restrict_names ? group.unnamed_or_leaf_by_parent : group.by_parent;
      const auto bucket = index.find(parent);
      if(bucket != index.end())
      {
        candidate = nearest(bucket->second, node);
      }
    }
    if(!candidate)
    {
      candidate = nearest(restrict_names ? group.unnamed_or_leaf : group.all, node);
    }
    if(!candidate || ConflictingContainerNames(*node, *candidate))
    {
      continue;
    }

    const Position position{ candidate->sibling_index, candidate->preorder_index };
    group.all.erase(position);
    group.by_parent.at(candidate->parent).erase(position);
    if(const std::string* candidate_name = node_name(*candidate))
    {
      group.by_name.at(*candidate_name).erase(position);
      group.by_parent_and_name.at({ candidate->parent, *candidate_name }).erase(position);
    }
    if(candidate->children.empty() || !node_name(*candidate))
    {
      group.unnamed_or_leaf.erase(position);
      group.unnamed_or_leaf_by_parent.at(candidate->parent).erase(position);
    }
    node->match = candidate;
    candidate->match = node;
  }
}

std::string OpeningTag(const Node& node, bool self_closing)
{
  std::string line = "<" + node.tag;
  for(const auto& [name, value] : node.attributes)
  {
    line += " " + name + "=\"" + EscapeXML(value) + "\"";
  }
  line += self_closing ? "/>" : ">";
  return line;
}

std::string Path(const Node& node)
{
  std::vector<const Node*> lineage;
  for(const Node* current = &node; current; current = current->parent)
  {
    lineage.push_back(current);
  }
  std::reverse(lineage.begin(), lineage.end());

  std::string path;
  for(const Node* current : lineage)
  {
    path += "/" + current->identity;
    if(current->parent)
    {
      size_t occurrence = 1;
      for(const Node* sibling : current->parent->children)
      {
        if(sibling == current)
        {
          break;
        }
        if(sibling->identity == current->identity)
        {
          ++occurrence;
        }
      }
      path += "[" + std::to_string(occurrence) + "]";
    }
  }
  return path;
}

void RenderWholeNode(const Node& node, size_t indent, std::ostringstream& output)
{
  const std::string spaces(indent, ' ');
  if(node.children.empty())
  {
    output << spaces << OpeningTag(node, true) << '\n';
    return;
  }

  output << spaces << OpeningTag(node, false) << '\n';
  for(const Node* child : node.children)
  {
    RenderWholeNode(*child, indent + 2, output);
  }
  output << spaces << "</" << node.tag << ">\n";
}

void RenderAddedNode(const Node& node, size_t indent, std::ostringstream& output)
{
  const std::string spaces(indent, ' ');
  if(node.match)
  {
    if(node.children.empty())
    {
      output << "  " << spaces << OpeningTag(node, true) << '\n';
    }
    else
    {
      output << "  " << spaces << OpeningTag(node, false) << '\n';
      output << "  " << spaces << "  <!-- retained subtree -->\n";
      output << "  " << spaces << "</" << node.tag << ">\n";
    }
    return;
  }

  if(node.children.empty())
  {
    output << "+ " << spaces << OpeningTag(node, true) << '\n';
    return;
  }

  output << "+ " << spaces << OpeningTag(node, false) << '\n';
  for(const Node* child : node.children)
  {
    RenderAddedNode(*child, indent + 2, output);
  }
  output << "+ " << spaces << "</" << node.tag << ">\n";
}

void RenderRemovedNode(const Node& node, size_t indent, std::ostringstream& output)
{
  const std::string spaces(indent, ' ');
  if(node.match)
  {
    if(node.children.empty())
    {
      output << "  " << spaces << OpeningTag(node, true) << '\n';
    }
    else
    {
      output << "  " << spaces << OpeningTag(node, false) << '\n';
      output << "  " << spaces << "  <!-- retained subtree -->\n";
      output << "  " << spaces << "</" << node.tag << ">\n";
    }
    return;
  }

  if(node.children.empty())
  {
    output << "- " << spaces << OpeningTag(node, true) << '\n';
    return;
  }

  output << "- " << spaces << OpeningTag(node, false) << '\n';
  for(const Node* child : node.children)
  {
    RenderRemovedNode(*child, indent + 2, output);
  }
  output << "- " << spaces << "</" << node.tag << ">\n";
}

std::vector<Node*> HighestUnmatchedRoots(const std::vector<Node*>& preorder)
{
  std::vector<Node*> roots;
  for(Node* node : preorder)
  {
    if(!node->match && (!node->parent || node->parent->match))
    {
      roots.push_back(node);
    }
  }
  return roots;
}

void MarkReorderedChildren(Document& before)
{
  for(Node* parent : before.preorder)
  {
    if(!parent->match)
    {
      continue;
    }

    std::vector<Node*> children;
    std::vector<size_t> after_indices;
    for(Node* child : parent->children)
    {
      if(child->match && child->match->parent == parent->match)
      {
        children.push_back(child);
        after_indices.push_back(child->match->sibling_index);
      }
    }
    if(children.size() < 2)
    {
      continue;
    }

    struct BestPrefix
    {
      size_t length = 0;
      size_t earliest_index = 0;
    };
    std::vector<BestPrefix> prefixes(parent->match->children.size() + 1);
    std::vector<int> previous(children.size(), -1);
    size_t best_end = 0;
    size_t longest = 0;
    for(size_t index = 0; index < children.size(); ++index)
    {
      // Fenwick prefix query: find the earliest predecessor among the longest
      // sequences ending at a strictly smaller after-position.
      BestPrefix best;
      for(size_t cursor = after_indices[index]; cursor > 0; cursor -= cursor & -cursor)
      {
        const BestPrefix& entry = prefixes[cursor];
        if(entry.length > best.length || (entry.length == best.length && entry.length &&
                                          entry.earliest_index < best.earliest_index))
        {
          best = entry;
        }
      }
      if(best.length)
      {
        previous[index] = static_cast<int>(best.earliest_index);
      }
      const BestPrefix updated{ best.length + 1, index };
      if(updated.length > longest)
      {
        longest = updated.length;
        best_end = index;
      }
      for(size_t cursor = after_indices[index] + 1; cursor < prefixes.size();
          cursor += cursor & -cursor)
      {
        BestPrefix& entry = prefixes[cursor];
        if(updated.length > entry.length ||
           (updated.length == entry.length &&
            updated.earliest_index < entry.earliest_index))
        {
          entry = updated;
        }
      }
    }

    std::vector<bool> retained(children.size(), false);
    for(int index = static_cast<int>(best_end); index >= 0; index = previous[index])
    {
      retained[static_cast<size_t>(index)] = true;
      if(previous[static_cast<size_t>(index)] < 0)
      {
        break;
      }
    }
    for(size_t index = 0; index < children.size(); ++index)
    {
      children[index]->reordered = !retained[index];
    }
  }
}

bool IsMoved(const Node& before)
{
  if(!before.match)
  {
    return false;
  }
  if(before.reordered)
  {
    return true;
  }
  if(!before.parent || !before.match->parent)
  {
    return before.parent != nullptr || before.match->parent != nullptr;
  }
  return before.parent->match != before.match->parent;
}

bool IsModified(const Node& before)
{
  return before.match && before.semantic_attributes != before.match->semantic_attributes;
}

bool HasModification(const Node& before)
{
  if(IsModified(before))
  {
    return true;
  }
  return std::any_of(before.children.begin(), before.children.end(),
                     [&](const Node* child) {
                       return child->match && child->match->parent == before.match &&
                              HasModification(*child);
                     });
}

std::vector<Node*> MovedRoots(const Document& before)
{
  std::vector<Node*> moved;
  for(Node* node : before.preorder)
  {
    if(!IsMoved(*node))
    {
      continue;
    }
    if(node->parent && IsMoved(*node->parent) &&
       node->parent->match == node->match->parent)
    {
      continue;
    }
    moved.push_back(node);
  }
  return moved;
}

std::vector<Node*> ModifiedRoots(const Document& before)
{
  std::vector<Node*> modified;
  for(Node* node : before.preorder)
  {
    if(!IsModified(*node) || IsMoved(*node))
    {
      continue;
    }

    bool inside_rendered_ancestor = false;
    for(Node* ancestor = node->parent; ancestor; ancestor = ancestor->parent)
    {
      if(IsMoved(*ancestor) || IsModified(*ancestor))
      {
        inside_rendered_ancestor = true;
        break;
      }
    }
    if(!inside_rendered_ancestor)
    {
      modified.push_back(node);
    }
  }
  return modified;
}

void RenderNodeDiff(const Node& before, const Node& after, size_t indent,
                    std::ostringstream& output)
{
  const std::string spaces(indent, ' ');
  const bool modified = IsModified(before);
  if(modified)
  {
    if(before.children.empty() && after.children.empty())
    {
      output << "- " << spaces << OpeningTag(before, true) << '\n';
      output << "+ " << spaces << OpeningTag(after, true) << '\n';
      return;
    }
    output << "- " << spaces << OpeningTag(before, before.children.empty()) << '\n';
    output << "+ " << spaces << OpeningTag(after, after.children.empty()) << '\n';
    if(after.children.empty())
    {
      output << "- " << spaces << "</" << before.tag << ">\n";
      return;
    }
  }
  else
  {
    output << "  " << spaces << OpeningTag(after, after.children.empty()) << '\n';
    if(after.children.empty())
    {
      return;
    }
  }

  for(const Node* before_child : before.children)
  {
    if(before_child->match && before_child->match->parent == &after &&
       HasModification(*before_child))
    {
      RenderNodeDiff(*before_child, *before_child->match, indent + 2, output);
    }
  }

  if(modified && before.children.empty())
  {
    output << "+ " << spaces << "</" << after.tag << ">\n";
  }
  else if(modified && before.tag != after.tag)
  {
    output << "- " << spaces << "</" << before.tag << ">\n";
    output << "+ " << spaces << "</" << after.tag << ">\n";
  }
  else
  {
    output << "  " << spaces << "</" << after.tag << ">\n";
  }
}

std::string DisplayPath(const Node& node, XMLDiffFormat format)
{
  const std::string path = EscapeXML(Path(node));
  if(format != XMLDiffFormat::Markdown)
  {
    return path;
  }

  const std::string delimiter(std::max<size_t>(1, LongestBacktickRun(path) + 1), '`');
  return delimiter + " " + path + " " + delimiter;
}

std::string ReorderPositionNote(const Node& before)
{
  if(!before.reordered)
  {
    return {};
  }
  return " (sibling position " + std::to_string(before.sibling_index + 1) +
         " \xE2\x86\x92 " + std::to_string(before.match->sibling_index + 1) + ")";
}

void RenderCodeBlock(XMLDiffFormat format, std::string_view language,
                     const std::string& content, std::ostringstream& output)
{
  if(format == XMLDiffFormat::Markdown)
  {
    const std::string delimiter(std::max<size_t>(3, LongestBacktickRun(content) + 1),
                                '`');
    output << delimiter << language << '\n' << content << delimiter << "\n\n";
  }
  else
  {
    output << content << '\n';
  }
}

}  // namespace

std::string RenderXMLDiff(std::string_view before_xml, std::string_view after_xml,
                          XMLDiffFormat format)
{
  Document before = ParseDocument(before_xml, "before");
  Document after = ParseDocument(after_xml, "after");

  MatchExactSubtrees(before, after);
  MatchSimilarNodes(before, after);
  MarkReorderedChildren(before);

  const std::vector<Node*> added = HighestUnmatchedRoots(after.preorder);
  const std::vector<Node*> removed = HighestUnmatchedRoots(before.preorder);
  const std::vector<Node*> moved = MovedRoots(before);
  const std::vector<Node*> modified = ModifiedRoots(before);

  std::vector<Node*> moved_and_modified;
  std::vector<Node*> moved_only;
  for(Node* node : moved)
  {
    (HasModification(*node) ? moved_and_modified : moved_only).push_back(node);
  }

  std::ostringstream output;
  if(format == XMLDiffFormat::Markdown)
  {
    output << "## Behavior Tree XML Diff\n\n";
  }
  else
  {
    output << "Behavior Tree XML Diff\n======================\n\n";
  }

  if(added.empty() && removed.empty() && moved.empty() && modified.empty())
  {
    output << "No changes.\n";
    return output.str();
  }

  if(!added.empty())
  {
    output << (format == XMLDiffFormat::Markdown ? "### Added\n\n" : "Added\n-----\n");
    for(const Node* node : added)
    {
      output << "At " << DisplayPath(*node, format) << ":\n";
      std::ostringstream content;
      RenderAddedNode(*node, 0, content);
      RenderCodeBlock(format, "diff", content.str(), output);
    }
  }

  if(!removed.empty())
  {
    output << (format == XMLDiffFormat::Markdown ? "### Removed\n\n" :
                                                   "Removed\n-------\n");
    for(const Node* node : removed)
    {
      output << "From " << DisplayPath(*node, format) << ":\n";
      std::ostringstream content;
      RenderRemovedNode(*node, 0, content);
      RenderCodeBlock(format, "diff", content.str(), output);
    }
  }

  if(!moved_and_modified.empty())
  {
    output << (format == XMLDiffFormat::Markdown ? "### Moved and modified\n\n" :
                                                   "Moved and "
                                                   "modified\n------------------\n");
    for(const Node* node : moved_and_modified)
    {
      output << DisplayPath(*node, format) << " \xE2\x86\x92 "
             << DisplayPath(*node->match, format) << ReorderPositionNote(*node) << '\n';
      std::ostringstream content;
      RenderNodeDiff(*node, *node->match, 0, content);
      RenderCodeBlock(format, "diff", content.str(), output);
    }
  }

  if(!modified.empty())
  {
    output << (format == XMLDiffFormat::Markdown ? "### Modified\n\n" :
                                                   "Modified\n--------\n");
    for(const Node* node : modified)
    {
      output << "At " << DisplayPath(*node->match, format) << ":\n";
      std::ostringstream content;
      RenderNodeDiff(*node, *node->match, 0, content);
      RenderCodeBlock(format, "diff", content.str(), output);
    }
  }

  if(!moved_only.empty())
  {
    output << (format == XMLDiffFormat::Markdown ? "### Moved\n\n" : "Moved\n-----\n");
    for(const Node* node : moved_only)
    {
      output << DisplayPath(*node, format) << " \xE2\x86\x92 "
             << DisplayPath(*node->match, format) << ReorderPositionNote(*node) << '\n';
      std::ostringstream content;
      RenderWholeNode(*node->match, 0, content);
      RenderCodeBlock(format, "xml", content.str(), output);
    }
  }

  return output.str();
}

}  // namespace BT
