#pragma once

#include <string>
#include <string_view>

#include "behaviortree_cpp/exceptions.h"

namespace BT
{

enum class XMLDiffFormat
{
  PlainText,
  Markdown
};

/**
 * @brief Render a structural review diff between two Behavior Tree XML documents.
 *
 * @throws RuntimeError if either document is malformed XML.
 */
[[nodiscard]] std::string RenderXMLDiff(std::string_view before_xml,
                                        std::string_view after_xml,
                                        XMLDiffFormat format = XMLDiffFormat::Markdown);

}  // namespace BT
