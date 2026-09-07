#pragma once

#include <Windows.h>

namespace utility::integration {

class SplashTextHook final {
public:
    bool install() noexcept;
    void uninstall() noexcept;
    [[nodiscard]] bool installed() const noexcept;

private:
    void* target_{};
    void* trampoline_{};
    unsigned char original_[19]{};
    bool installed_{};
};

} // namespace utility::integration
