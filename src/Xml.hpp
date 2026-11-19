#pragma once
#include <string>
#include <string_view>
#include <vector>
namespace sarah { //neo: no im not using pugixml i dont wanna okay
struct XmlNode {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<XmlNode> children;
    std::string text;
    const std::string* attribute(std::string_view key) const;
    bool has_attribute(std::string_view key) const;
    std::vector<XmlNode> children_named(std::string_view child_name) const;
    const XmlNode* first_child(std::string_view child_name) const;
};

XmlNode parse_xml(const std::string& content);
}
