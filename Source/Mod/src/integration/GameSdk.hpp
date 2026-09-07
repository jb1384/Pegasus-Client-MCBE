#pragma once

namespace utility::integration {

class GameSdk final {
public:
    // Intentionally inert. Future work should use documented, version-aware,
    // client-side interfaces and remain isolated behind this boundary.
    [[nodiscard]] bool available() const noexcept { return false; }
};

} // namespace utility::integration

