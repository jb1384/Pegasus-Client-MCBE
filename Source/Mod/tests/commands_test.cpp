#include "../src/integration/ChatCommands.cpp"
#include <iostream>
#include <stdexcept>

using namespace utility;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
class TestModule final : public Module {
public:
    std::string_view name() const noexcept override { return label; }
    bool available() const noexcept override { return supported; }
    bool supported{true};
    std::string label{"Test Module"};
};
std::vector<std::string> displayed, sent;
void __fastcall mock_display(void*,const integration::NativeString* text,const integration::OptionalString*,bool) {
    displayed.emplace_back(text->capacity>=16?text->data.pointer:text->data.text,text->size);
}
void __fastcall mock_submit(void* controller) {
    auto* value=reinterpret_cast<integration::NativeString*>(static_cast<unsigned char*>(controller)+0xD60);
    if (value->size) sent.emplace_back(value->capacity>=16?value->data.pointer:value->data.text,value->size);
}
}
int main() {
    try {
        ModuleManager modules;
        auto owned=std::make_unique<TestModule>();auto* module=owned.get();modules.add(std::move(owned));
        auto& commands=modules.commands();
        const auto plain=[](std::string text) {
            for (std::size_t at=0;(at=text.find("\xC2\xA7",at))!=std::string::npos;) text.erase(at,3);
            return text;
        };
        auto execute=[&](std::string_view text) {
            std::vector<std::string> replies;
            require(commands.execute(text,modules,replies),"command must be consumed");
            require(!replies.empty(),"command needs feedback");
            for (auto& line:replies) {
                require(line.starts_with("\xC2\xA7") && line.ends_with(chat_style::reset),"response formatting missing");
                line=plain(line);
            }
            return replies;
        };
        std::vector<std::string> ordinary;
        require(!commands.execute("hello, world",modules,ordinary)&&ordinary.empty(),"normal chat changed");
        require(!commands.execute("/help",modules,ordinary),"server command changed");
        require(!commands.execute(" ,help",modules,ordinary),"only leading comma is a command");
        const auto help=execute(",help");
        require(help.size()==4,"help must contain exactly one row per command");
        require(help[0].starts_with(",help") && help[1].starts_with(",keybind") && help[2].starts_with(",unbind") && help[3].starts_with(",eject"),"help registry");
        for (const auto& line:help) require(line.find('\n')==std::string::npos && line.size()<140,"help must stay concise");
        unsigned eject_calls{};
        commands.set_eject_handler([&]{++eject_calls;return false;});
        require(execute(",eject extra")[0].starts_with("Usage:"),"eject arity");
        require(eject_calls==0,"invalid eject called handler");
        require(execute(",eject")[0].starts_with("Could not"),"eject scheduling failure");
        require(eject_calls==1,"eject failure path");
        commands.set_eject_handler([&]{++eject_calls;return true;});
        require(execute(",EJECT")[0].starts_with("Ejecting"),"eject success");
        require(execute(",eject")[0].find("already")!=std::string::npos && eject_calls==2,"duplicate eject scheduled");
        commands.set_eject_handler({});
        require(execute(",help extra")[0].starts_with("Usage:"),"help arity");
        require(execute(",unknown")[0].find("Unrecognized")!=std::string::npos,"unknown command");
        execute(",");execute(",keybind");execute(",keybind \"Test Module G toggle");
        execute(",keybind Missing G toggle");execute(",keybind \"Test Module\" tab toggle");
        execute(",keybind \"Test Module\" G bad");execute(",keybind \"Test Module\" F25 toggle");
        execute(",keybind \"Test Module\" G toggle extra");
        commands.key('G',true,true);require(!module->enabled(),"invalid binding was installed");commands.key('G',false,true);
        execute(",KEYBIND \"test module\" g TOGGLE");
        commands.key('G',true,true);require(module->enabled(),"toggle on");
        commands.key('G',true,true);require(module->enabled(),"repeat toggled");
        commands.key('G',false,true);commands.key('G',true,true);require(!module->enabled(),"toggle off");
        commands.key('G',false,true);commands.key('G',true,false);commands.key('G',true,true);
        require(!module->enabled(),"typing or held key activated on resume");commands.key('G',false,true);
        execute(",keybind \"Test Module\" H keyhold");
        commands.key('G',true,true);require(!module->enabled(),"old binding remains");commands.key('G',false,true);
        commands.key('H',true,true);require(module->enabled(),"hold on");
        commands.key('H',false,false);require(!module->enabled(),"hold release after focus loss");
        commands.key('H',true,true);commands.suspend();require(!module->enabled(),"suspend release");
        commands.key('H',true,true);require(!module->enabled(),"resume repeat");commands.key('H',false,true);
        commands.key('H',true,true);execute(",keybind \"Test Module\" F6 toggle");require(!module->enabled(),"rebind release");
        commands.key('H',false,true);commands.key(VK_F6,true,true);require(module->enabled(),"F key binding");commands.key(VK_F6,false,true);
        execute(",keybind \"Test Module\" shift keyhold");require(!module->enabled(),"hold assignment resets enabled module");
        commands.key(VK_LSHIFT,true,true);commands.key(VK_RSHIFT,true,true);commands.key(VK_LSHIFT,false,true);
        require(module->enabled(),"generic modifier released while other side held");commands.key(VK_RSHIFT,false,true);require(!module->enabled(),"modifier release");
        module->supported=false;require(execute(",keybind \"Test Module\" J toggle")[0].starts_with("Module unavailable"),"unavailable module");module->supported=true;

        require(execute(",unbind")[0].starts_with("Usage: ,unbind"),"unbind missing argument");
        require(execute(",unbind \"Test Module\" G")[0].starts_with("Usage: ,unbind"),"unbind extra argument");
        require(execute(",unbind Missing")[0].starts_with("Unknown module:"),"unbind unknown module");
        require(execute(",unbind \"Test Module")[0].find("quote")!=std::string::npos,"unbind invalid quote");
        execute(",keybind \"Test Module\" G toggle");
        commands.key('G',true,true);require(module->enabled(),"unbind toggle setup");commands.key('G',false,true);
        require(execute(",UNBIND \"test module\"")[0].starts_with("Unbound"),"case insensitive unbind");
        require(module->enabled(),"unbinding a toggle must preserve enabled state");
        commands.key('G',true,true);require(module->enabled(),"unbound key still toggles");commands.key('G',false,true);
        require(execute(",unbind \"Test Module\"")[0].find("no key bindings")!=std::string::npos,"already unbound feedback");
        execute(",keybind \"Test Module\" H keyhold");commands.key('H',true,true);
        require(module->enabled(),"unbind hold setup");
        module->supported=false;execute(",unbind \"Test Module\"");
        require(!module->enabled(),"unbind must release hold even if module becomes unavailable");module->supported=true;
        commands.key('H',true,true);commands.key('H',false,true);commands.key('H',true,true);
        require(!module->enabled(),"removed hold reactivated");commands.key('H',false,true);
        // A shared key must keep controlling other modules after removing one binding.
        {
            ModuleManager shared;
            auto first=std::make_unique<TestModule>();auto* a=first.get();shared.add(std::move(first));
            auto second=std::make_unique<TestModule>();second->label="Other";auto* b=second.get();shared.add(std::move(second));
            std::vector<std::string> replies;
            shared.commands().execute(",keybind \"Test Module\" J toggle",shared,replies);
            shared.commands().execute(",keybind Other J toggle",shared,replies);
            shared.commands().execute(",unbind \"Test Module\"",shared,replies);
            shared.commands().key('J',true,true);
            require(!a->enabled() && b->enabled(),"unbind affected another module's shared key");
            shared.shutdown();
        }

        // Exercise the real native adapter against release-ABI chat/controller fixtures.
        // The mocked native sender must never see any comma-prefixed command.
        std::array<unsigned char,0xE00> controller{};
        std::array<unsigned char,0x60> model{};
        std::array<unsigned char,0x650> client{};
        bool control=true;void* control_ptr=&control;void* model_ptr=model.data();void* client_ptr=client.data();void* chat_ptr=&control;
        std::memcpy(controller.data()+0xD48,&model_ptr,8);std::memcpy(model.data()+0x40,&control_ptr,8);
        std::memcpy(model.data()+0x50,&client_ptr,8);std::memcpy(client.data()+0x648,&chat_ptr,8);
        integration::manager=&modules;integration::original_submit=mock_submit;integration::display_message=mock_display;
        auto submit=[&](const std::string& text) {
            integration::NativeString value;std::string heap=text;value.size=text.size();
            if(text.size()<16)std::memcpy(value.data.text,text.c_str(),text.size()+1);
            else {value.data.pointer=heap.data();value.capacity=heap.size();}
            std::memcpy(controller.data()+0xD60,&value,sizeof(value));
            integration::submit_hook(controller.data());
        };
        submit("normal message");submit("/help");submit("hello, world");submit(" ,help");
        require(sent==std::vector<std::string>({"normal message","/help","hello, world"," ,help"}),"normal native chat changed");
        submit(",help");submit(",keybind \"Test Module\" G toggle");submit(",unknown");submit(",");submit(",help extra");
        submit(",keybind \"Test Module\" G bad");submit(","+std::string(33000,'x'));
        submit(",unbind \"Test Module\"");submit(",unbind Missing");submit(",unbind");
        require(sent.size()==4,"command leaked into native sender");require(displayed.size()>=7,"missing local feedback");
        unsigned native_ejects{};
        commands.set_eject_handler([&]{++native_ejects;return true;});
        submit(",eject bad");require(native_ejects==0,"native eject arity");
        submit(",eject");require(native_ejects==1 && sent.size()==4,"eject leaked or did not schedule");
        require(displayed.back().starts_with(std::string(chat_style::gray)+"["+chat_style::aqua+"Utility"),"colored local prefix missing");
        require(displayed.back().find(chat_style::green)!=std::string::npos,"success must be green");
        integration::stop_chat_commands();submit(",help");require(sent.size()==4,"shutdown command leaked");
        integration::stop_chat_commands(true);
        const auto reply_count=displayed.size();submit(",help");submit("ordinary after eject");
        require(sent.size()==6 && sent[4]==",help" && displayed.size()==reply_count,"eject did not restore ordinary chat");
        module->set_enabled(true);
        modules.deactivate();require(!module->enabled() && modules.modules().size()==1,"deactivation must preserve native callback objects");
        commands.clear();module->set_enabled(false);commands.key('G',false,true);commands.key('G',true,true);
        require(!module->enabled(),"cleared bindings remained");
        modules.shutdown();
        std::cout<<"Command parser, keybind transitions and native chat isolation passed.\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
