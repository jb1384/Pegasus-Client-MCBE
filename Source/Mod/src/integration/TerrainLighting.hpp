#pragma once
namespace utility::integration::terrain_lighting {
bool initialize();
bool available() noexcept;
void set_fullbright(bool enabled) noexcept;
void set_fullbright_level(int level) noexcept;
}
