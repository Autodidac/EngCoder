#pragma once
#include <string>
#include <string_view>

namespace engcoder::core {
[[nodiscard]] std::string make_id(std::string_view prefix);
}
