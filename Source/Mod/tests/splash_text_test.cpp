#include "../src/integration/SplashTextHook.cpp"
#include <cassert>

int main() {
    // Mimic the game's release-layout string without crossing C++ allocators.
    alignas(16) std::array<std::byte, 0x30> object{};
    std::array<char, 64> storage{};
    storage.fill('#');
    char* buffer = storage.data();
    std::size_t size = 30, capacity = 63;
    std::memcpy(object.data() + 0x10, &buffer, sizeof(buffer));
    std::memcpy(object.data() + 0x20, &size, sizeof(size));
    std::memcpy(object.data() + 0x28, &capacity, sizeof(capacity));
    utility::integration::replace_splash_text(object.data());
    assert(std::strcmp(buffer, "made by Roundomegaboi") == 0);
    std::memcpy(&size, object.data() + 0x20, sizeof(size));
    assert(size == std::strlen("made by Roundomegaboi"));
    assert(storage[size + 1] == '#');

    // Short inline strings must never be overrun by the longer branding.
    object.fill(std::byte{});
    std::memcpy(object.data() + 0x10, "short", 6);
    size = 5; capacity = 15;
    std::memcpy(object.data() + 0x20, &size, sizeof(size));
    std::memcpy(object.data() + 0x28, &capacity, sizeof(capacity));
    auto before = object;
    utility::integration::replace_splash_text(object.data());
    assert(object == before);
    utility::integration::replace_splash_text(nullptr);
}
