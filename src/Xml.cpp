#include "Xml.hpp"
#include <cctype>
#include <stdexcept>
namespace sarah {
namespace {
class Parser {
public:
    explicit Parser(const std::string& content) : content_(content) {}
    XmlNode parse_document() {
        skip_bom();
        while (position_ < content_.size()) {
            if (starts_with("<?") || starts_with("<!--") || starts_with("<!")) {
                skip_declaration();
                continue;
            }
            if (content_[position_] == '<') {
                return parse_element();
            }
            ++position_;
        }
        throw std::runtime_error("xml document contains no root element");
    }

private:
    bool starts_with(const std::string& value) const {
        return content_.compare(position_, value.size(), value) == 0;
    }

    void skip_bom() {
        if (content_.size() >= 3 &&
            static_cast<unsigned char>(content_[0]) == 0xEF &&
            static_cast<unsigned char>(content_[1]) == 0xBB &&
            static_cast<unsigned char>(content_[2]) == 0xBF) {
            position_ = 3;
        }
    }

    void skip_whitespace() {
        while (position_ < content_.size() &&
               std::isspace(static_cast<unsigned char>(content_[position_]))) {
            ++position_;
        }
    }

    void skip_declaration() {
        if (starts_with("<!--")) {
            std::size_t end = content_.find("-->", position_ + 4);
            if (end == std::string::npos) {
                throw std::runtime_error("unterminated xml comment");
            }
            position_ = end + 3;
            return;
        }
        if (starts_with("<?")) {
            std::size_t end = content_.find("?>", position_ + 2);
            if (end == std::string::npos) {
                throw std::runtime_error("unterminated xml processing instruction");
            }
            position_ = end + 2;
            return;
        }
        if (starts_with("<![CDATA[")) {
            return;
        }
        int depth = 0;
        while (position_ < content_.size()) {
            if (content_[position_] == '<') {
                ++depth;
            } else if (content_[position_] == '>') {
                --depth;
                ++position_;
                if (depth == 0) {
                    return;
                }
                continue;
            }
            ++position_;
        }
        throw std::runtime_error("unterminated xml declaration");
    }

    static void append_utf8(std::string& output, unsigned int code) {
        if (code <= 0x7F) {
            output += static_cast<char>(code);
        } else if (code <= 0x7FF) {
            output += static_cast<char>(0xC0 | (code >> 6));
            output += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code <= 0xFFFF) {
            output += static_cast<char>(0xE0 | (code >> 12));
            output += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            output += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            output += static_cast<char>(0xF0 | (code >> 18));
            output += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            output += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            output += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    std::string decode(const std::string& value) const {
        std::string result;
        result.reserve(value.size());
        std::size_t index = 0;
        while (index < value.size()) {
            if (value[index] != '&') {
                result += value[index];
                ++index;
                continue;
            }
            std::size_t end = value.find(';', index + 1);
            if (end == std::string::npos) {
                result += value[index];
                ++index;
                continue;
            }
            std::string entity = value.substr(index + 1, end - index - 1);
            if (entity == "amp") {
                result += '&';
            } else if (entity == "lt") {
                result += '<';
            } else if (entity == "gt") {
                result += '>';
            } else if (entity == "quot") {
                result += '"';
            } else if (entity == "apos") {
                result += '\'';
            } else if (!entity.empty() && entity[0] == '#') {
                unsigned int code = 0;
                bool valid = false;
                if (entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X')) {
                    for (std::size_t position = 2; position < entity.size(); ++position) {
                        char character = entity[position];
                        if (!std::isxdigit(static_cast<unsigned char>(character))) {
                            valid = false;
                            break;
                        }
                        code = code * 16 + static_cast<unsigned int>(std::isdigit(static_cast<unsigned char>(character))
                                                                         ? character - '0'
                                                                         : std::tolower(static_cast<unsigned char>(character)) - 'a' + 10);
                        valid = true;
                    }
                } else {
                    for (std::size_t position = 1; position < entity.size(); ++position) {
                        char character = entity[position];
                        if (!std::isdigit(static_cast<unsigned char>(character))) {
                            valid = false;
                            break;
                        }
                        code = code * 10 + static_cast<unsigned int>(character - '0');
                        valid = true;
                    }
                }
                if (valid) {
                    append_utf8(result, code);
                } else {
                    result += value.substr(index, end - index + 1);
                }
            } else {
                result += value.substr(index, end - index + 1);
            }
            index = end + 1;
        }
        return result;
    }

    std::string read_name() {
        std::size_t start = position_;
        while (position_ < content_.size()) {
            char character = content_[position_];
            if (std::isalnum(static_cast<unsigned char>(character)) ||
                character == '_' || character == ':' || character == '-' || character == '.') {
                ++position_;
            } else {
                break;
            }
        }
        if (position_ == start) {
            throw std::runtime_error("xml element has an empty name");
        }
        return content_.substr(start, position_ - start);
    }

    XmlNode parse_element() {
        if (position_ >= content_.size() || content_[position_] != '<') {
            throw std::runtime_error("expected xml element");
        }
        ++position_;
        XmlNode node;
        node.name = read_name();
        while (position_ < content_.size()) {
            skip_whitespace();
            if (starts_with("/>")) {
                position_ += 2;
                return node;
            }
            if (starts_with(">")) {
                ++position_;
                break;
            }
            std::string attribute_name = read_name();
            skip_whitespace();
            if (position_ >= content_.size() || content_[position_] != '=') {
                throw std::runtime_error("xml attribute is missing a value");
            }
            ++position_;
            skip_whitespace();
            if (position_ >= content_.size() ||
                (content_[position_] != '"' && content_[position_] != '\'')) {
                throw std::runtime_error("xml attribute value is not quoted");
            }
            char quote = content_[position_];
            ++position_;
            std::size_t value_start = position_;
            while (position_ < content_.size() && content_[position_] != quote) {
                ++position_;
            }
            if (position_ >= content_.size()) {
                throw std::runtime_error("unterminated xml attribute value");
            }
            std::string raw_value = content_.substr(value_start, position_ - value_start);
            ++position_;
            node.attributes.emplace_back(std::move(attribute_name), decode(raw_value));
        }

        while (position_ < content_.size()) {
            if (starts_with("</")) {
                position_ += 2;
                std::string closing = read_name();
                if (closing != node.name) {
                    throw std::runtime_error("mismatched xml closing tag for " + node.name);
                }
                skip_whitespace();
                if (position_ >= content_.size() || content_[position_] != '>') {
                    throw std::runtime_error("unterminated xml closing tag");
                }
                ++position_;
                return node;
            }
            if (starts_with("<!--")) {
                std::size_t end = content_.find("-->", position_ + 4);
                if (end == std::string::npos) {
                    throw std::runtime_error("unterminated xml comment");
                }
                position_ = end + 3;
                continue;
            }
            if (starts_with("<![CDATA[")) {
                position_ += 9;
                std::size_t end = content_.find("]]>", position_);
                if (end == std::string::npos) {
                    throw std::runtime_error("unterminated xml cdata section");
                }
                node.text += content_.substr(position_, end - position_);
                position_ = end + 3;
                continue;
            }
            if (starts_with("<?")) {
                std::size_t end = content_.find("?>", position_ + 2);
                if (end == std::string::npos) {
                    throw std::runtime_error("unterminated xml processing instruction");
                }
                position_ = end + 2;
                continue;
            }
            if (content_[position_] == '<') {
                node.children.push_back(parse_element());
                continue;
            }
            std::size_t text_start = position_;
            while (position_ < content_.size() && content_[position_] != '<') {
                ++position_;
            }
            node.text += decode(content_.substr(text_start, position_ - text_start));
        }
        throw std::runtime_error("unterminated xml element " + node.name);
    }

    const std::string& content_;
    std::size_t position_ = 0;
};

}

const std::string* XmlNode::attribute(std::string_view key) const {
    for (const auto& entry : attributes) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}

bool XmlNode::has_attribute(std::string_view key) const {
    return attribute(key) != nullptr;
}

std::vector<XmlNode> XmlNode::children_named(std::string_view child_name) const {
    std::vector<XmlNode> result;
    for (const auto& child : children) {
        if (child.name == child_name) {
            result.push_back(child);
        }
    }
    return result;
}

const XmlNode* XmlNode::first_child(std::string_view child_name) const {
    for (const auto& child : children) {
        if (child.name == child_name) {
            return &child;
        }
    }
    return nullptr;
}

XmlNode parse_xml(const std::string& content) {
    Parser parser(content);
    return parser.parse_document();
}
}
