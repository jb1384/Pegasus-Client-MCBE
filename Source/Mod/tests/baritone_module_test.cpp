#include "../src/modules/BaritoneModule.hpp"
#include "../src/framework/ModuleManager.hpp"
#include "../src/framework/EventBus.hpp"
#include <iostream>
#include <stdexcept>

using namespace utility;
namespace {void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}}
int main(){try {
    ModuleManager modules;EventBus events;
    auto owned=std::make_unique<modules::BaritoneModule>();auto* module=owned.get();modules.add(std::move(owned));modules.initialize(events);
    require(!module->available(),"test host must not enable native actions");
    const auto call=[&](const char* text){std::vector<std::string> replies;
        require(modules.commands().execute(text,modules,replies),"local command leaked");
        require(!replies.empty(),"no command feedback");return replies;};
    require(call(",help").size()==10,"navigation help registration");
    require(call(",goto 10 20")[0].find("unavailable")!=std::string::npos,"missing native capabilities hidden");
    require(call(",baritone status")[0].find("supported executable")!=std::string::npos,"capability diagnostics missing");
    require(call(",mine -1 stone")[0].find("positive")!=std::string::npos,"syntax not checked before availability");
    require(call(",stop")[0].find("Stopped")!=std::string::npos,"stop unavailable module");
    module->set_enabled(true);require(!module->enabled(),"unsupported module enabled");
    for(std::size_t i=0;i<6;++i){require(!module->boolean_setting_name(i).empty(),"setting label missing");
        module->set_boolean_setting(i,false);require(!module->boolean_setting(i),"setting false");
        module->set_boolean_setting(i,true);require(module->boolean_setting(i),"setting true");}
    require(!module->boolean_setting(100),"invalid setting index");
    integration::NavigationCapabilities caps;caps.executable=caps.player_observation=caps.block_identifiers=true;
    require(!caps.complete()&&caps.missing().find("movement input")!=std::string::npos,"incomplete adapter accepted");
    std::cout<<"Baritone command routing, settings and fail-closed availability passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
