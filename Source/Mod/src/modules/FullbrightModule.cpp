#include "FullbrightModule.hpp"
#include "../integration/TerrainLighting.hpp"
#include <algorithm>
#include <cmath>

namespace utility::modules {
void FullbrightModule::set_value(float level) noexcept {
    if(!std::isfinite(level))return;
    const int selected=static_cast<int>(std::round(std::clamp(level,9.0F,15.0F)));
    level_=selected;
    integration::terrain_lighting::set_fullbright_level(selected);
}
FullbrightModule::~FullbrightModule() { on_disable(); }
bool FullbrightModule::available() const noexcept { return integration::terrain_lighting::available(); }
void FullbrightModule::on_register(EventBus&) { integration::terrain_lighting::initialize(); }
void FullbrightModule::on_enable() { integration::terrain_lighting::set_fullbright(true); }
void FullbrightModule::on_disable() { integration::terrain_lighting::set_fullbright(false); }
}
