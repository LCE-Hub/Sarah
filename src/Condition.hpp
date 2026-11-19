#pragma once
#include <map>
#include <string>
namespace sarah {
using Variables = std::map<std::string, std::string>;
std::string expand_variables(const std::string& value, const Variables& variables);
bool evaluate_condition(const std::string& condition, const Variables& variables);
}
