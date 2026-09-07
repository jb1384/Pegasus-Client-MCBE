#include "BaritoneModule.hpp"
#include "../framework/ChatStyle.hpp"
#include "../framework/Logger.hpp"
#include <Windows.h>
#include <algorithm>
#include <cmath>

namespace utility::modules {
BaritoneModule::~BaritoneModule() {
    integration::NavigationTickSink* expected=this;
    integration::navigation_sink.compare_exchange_strong(expected,nullptr);
    integration::navigation_owns_controls=false;
}
void BaritoneModule::on_register(EventBus&) {
    std::lock_guard lock(mutex_);adapter_.initialize();ready_=adapter_.available();
    integration::navigation_sink.store(this,std::memory_order_release);
    Logger::instance().info(ready_?"Baritone native adapter ready.":adapter_.unavailable_reason());
}
void BaritoneModule::on_disable() {stop_requested_=true;suspend_requested_=true;integration::navigation_owns_controls=false;}
void BaritoneModule::suspend_navigation() noexcept {suspend_requested_=true;integration::navigation_owns_controls=false;}
void BaritoneModule::on_tick() noexcept {
    // Framework ticks are not game ticks. Only request suspension here.
    if(GetTickCount64()-last_tick_.load()>500){stop_requested_=true;suspend_navigation();}
}
void BaritoneModule::native_tick(void* player) noexcept {
    try {
        std::lock_guard lock(mutex_);last_tick_=GetTickCount64();
        if(!ready_){integration::navigation_owns_controls=false;return;}
        last_frame_=adapter_.observe(player,controller_.mining_targets());
        if(stop_requested_.exchange(false)||!enabled()){commands_.clear();controller_.stop();adapter_.release();return;}
        if(suspend_requested_.exchange(false)){controller_.pause("Gameplay interrupted; use ,resume.");adapter_.release();}
        while(!commands_.empty()) {
            const auto command=std::move(commands_.front());commands_.pop_front();
            if(command.kind==navigation::CommandKind::mine)last_frame_=adapter_.observe(player,command.blocks);
            controller_.command(command,last_frame_,settings_,[&](const std::string& id){return adapter_.known_block(id);});
            last_frame_=adapter_.observe(player,controller_.mining_targets());
        }
        controller_.tick(last_frame_,settings_,adapter_,GetTickCount64()/1000.0);
        integration::navigation_owns_controls=controller_.owns_controls();
    } catch(...) {
        integration::navigation_owns_controls=false;stop_requested_=true;
        Logger::instance().info("Baritone tick failed; controls released and stop requested.");
    }
}
std::vector<std::string> BaritoneModule::command_help() const {
    return {",goto <x> <z> | <x> <y> <z> - Travel to coordinates; supports ~offset.",
        ",mine [quantity] <block> [block...] - Mine selected blocks.",
        ",pause - Pause the Baritone job.",",resume - Resume a paused Baritone job.",
        ",stop - Cancel the Baritone job.",",baritone status - Show job status or unavailable interfaces."};
}
bool BaritoneModule::handle_command(const std::vector<std::string>& args,std::vector<std::string>& replies) {
    const auto parsed=navigation::parse_command(args);if(!parsed.handled)return false;
    if(!parsed.command){replies.push_back(parsed.error);return true;}
    std::lock_guard lock(mutex_);const auto& command=*parsed.command;
    if(command.kind==navigation::CommandKind::status) {
        replies.push_back(std::string(chat_style::yellow)+(ready_?controller_.status().message:adapter_.unavailable_reason()));return true;
    }
    if(command.kind==navigation::CommandKind::stop) {
        commands_.clear();controller_.stop();stop_requested_=true;integration::navigation_owns_controls=false;
        replies.push_back(std::string(chat_style::green)+"Stopped Baritone.");return true;
    }
    if(!ready_){replies.push_back(adapter_.unavailable_reason());return true;}
    if(command.kind==navigation::CommandKind::go && !navigation::resolve(command,last_frame_.feet)) {
        replies.emplace_back("Coordinates are outside the supported world range.");return true;
    }
    if(command.kind==navigation::CommandKind::mine)for(const auto& id:command.blocks) {
        if(!adapter_.known_block(id)){replies.push_back("Unknown block: "+id);return true;}
    }
    if(command.kind==navigation::CommandKind::go||command.kind==navigation::CommandKind::mine) {
        if(!last_frame_.connected||!last_frame_.alive){replies.emplace_back("Enter a world before starting Baritone.");return true;}
        set_enabled(true);stop_requested_=false;
    }
    if(commands_.size()>=16){replies.emplace_back("Baritone command queue is full; try again next tick.");return true;}
    commands_.push_back(command);
    replies.push_back(std::string(chat_style::green)+"Baritone command queued.");return true;
}
std::string_view BaritoneModule::boolean_setting_name(std::size_t i) const noexcept {
    constexpr std::string_view labels[]{"Show route","Allow breaking","Allow placing","Allow sprinting","Allow parkour","Automatic eating"};
    return i<6?labels[i]:std::string_view{};
}
bool BaritoneModule::boolean_setting(std::size_t i) const noexcept {
    std::lock_guard lock(mutex_);const bool values[]{settings_.render_route,settings_.allow_break,settings_.allow_place,
        settings_.allow_sprint,settings_.allow_parkour,settings_.auto_eat};return i<6&&values[i];
}
void BaritoneModule::set_boolean_setting(std::size_t i,bool value) noexcept {
    std::lock_guard lock(mutex_);bool* values[]{&settings_.render_route,&settings_.allow_break,&settings_.allow_place,
        &settings_.allow_sprint,&settings_.allow_parkour,&settings_.auto_eat};if(i<6)*values[i]=value;
}
void BaritoneModule::draw_overlay(void* device,int width,int height) noexcept {
    if(!enabled())return;
    try {
        std::lock_guard lock(mutex_);const auto& status=controller_.status();
        auto dc=static_cast<HDC>(device);if(!dc)return;
        const auto text="Baritone: "+status.message+(status.requested>0?
            " ("+std::to_string(status.collected)+"/"+std::to_string(status.requested)+")":"");
        const int old_mode=SetBkMode(dc,TRANSPARENT);const auto old_color=SetTextColor(dc,RGB(60,220,240));
        TextOutA(dc,16,std::max(16,height-210),text.c_str(),static_cast<int>(text.size()));
        if(settings_.render_route&&!status.route.empty()) {
            // Compact north-up route map. Renderer reads copied positions only.
            const int cx=std::max(90,width-110),cy=std::max(90,height-110);
            double span=8;for(const auto& step:status.route)span=std::max(span,navigation::distance(last_frame_.feet,step.to));
            const double scale=80/span;auto pen=CreatePen(PS_SOLID,2,RGB(60,220,240));auto old=SelectObject(dc,pen);
            MoveToEx(dc,cx,cy,nullptr);
            for(const auto& step:status.route)LineTo(dc,cx+static_cast<int>((step.to.x-last_frame_.feet.x)*scale),
                cy+static_cast<int>((step.to.z-last_frame_.feet.z)*scale));
            Ellipse(dc,cx-3,cy-3,cx+3,cy+3);TextOutA(dc,cx-4,cy-90,"N",1);
            SelectObject(dc,old);DeleteObject(pen);
        }
        SetTextColor(dc,old_color);SetBkMode(dc,old_mode);
    }catch(...){}
}
} // namespace utility::modules
