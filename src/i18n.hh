#pragma once
#include <string>

namespace i18n {

void init(const std::string &lang_override);
const char *t(const char *key);

} // namespace i18n
