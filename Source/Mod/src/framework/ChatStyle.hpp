#pragma once
#include <string>
#include <string_view>
namespace utility::chat_style {
inline constexpr auto aqua = "\xC2\xA7" "b";
inline constexpr auto gray = "\xC2\xA7" "7";
inline constexpr auto white = "\xC2\xA7" "f";
inline constexpr auto green = "\xC2\xA7" "a";
inline constexpr auto yellow = "\xC2\xA7" "e";
inline constexpr auto red = "\xC2\xA7" "c";
inline constexpr auto reset = "\xC2\xA7" "r";
inline std::string line(std::string_view text) {
    return std::string(gray) + "[" + aqua + "Utility" + gray + "] " + std::string(text) + reset;
}
}
