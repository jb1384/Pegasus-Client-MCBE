// Exercise the production patch in an isolated image, never in Minecraft.
#include "../src/modules/GhostHandModule.cpp"
#include <cstdio>
#include <cstdlib>

using namespace utility::modules;
void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    auto* fixture = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x4800000,
        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    check(fixture != nullptr, "allocate fixture");
    std::memcpy(fixture + branch_rva - 6, branch_signature.data(), branch_signature.size());
    std::memcpy(fixture + 0x4F5E69, destination_signature.data(), destination_signature.size());
    check(!supported_host(GetModuleHandleW(nullptr)), "reject non-Minecraft host");
    check(!matches(nullptr, false), "reject missing image");
    fixture[0x4F5E69] ^= 1;
    check(!set_ghost_patch(fixture, true) && fixture[branch_rva] == vanilla_opcode,
        "mismatched destination rejects without writes");
    fixture[0x4F5E69] ^= 1;
    fixture[branch_rva + 1] ^= 1;
    check(!set_ghost_patch(fixture, true), "reject changed branch destination");
    fixture[branch_rva + 1] ^= 1;

    // ABI-safe wrapper runs the exact native movaps/cmp/branch sequence.
    // xmm0=ray length, xmm1=blocking distance, r8d=hit type. Substitute the
    // supplied distance for the native square root of the block-hit vector.
    auto run = [&](float ray, float block, int type) {
        auto* code = fixture;
        const unsigned char prefix[]{0x48,0x83,0xEC,0x28,0x0F,0x11,0x34,0x24,
            0x0F,0x11,0x7C,0x24,0x10,0x0F,0x28,0xF0,0x44,0x89,0xC0,0x83,0xE8,0x04};
        std::memcpy(code, prefix, sizeof(prefix));
        auto* sequence = code + sizeof(prefix);
        std::memcpy(sequence, fixture + branch_rva - 6, 8);
        const unsigned char block_path[]{0x0F,0x28,0xF9,0xEB,0x5C};
        std::memcpy(sequence + 8, block_path, sizeof(block_path));
        const unsigned char suffix[]{0x0F,0x28,0xC7,0x0F,0x10,0x34,0x24,
            0x0F,0x10,0x7C,0x24,0x10,0x48,0x83,0xC4,0x28,0xC3};
        std::memcpy(sequence + 8 + 0x61, suffix, sizeof(suffix));
        FlushInstructionCache(GetCurrentProcess(), code, 160);
        return reinterpret_cast<float(*)(float,float,int)>(code)(ray,block,type);
    };
    check(run(5,1,0) == 1, "vanilla block hit caps entity comparison distance");
    check(run(5,1,3) == 5, "vanilla miss uses full ray");
    for (int cycle = 0; cycle < 3; ++cycle) {
        check(set_ghost_patch(fixture,true), "enable patch");
        check(!set_ghost_patch(fixture,true), "reject duplicate patch ownership");
        for (float ray : {3.0F,5.0F,7.0F}) {
            check(run(ray,1,0) == ray, "intervening block no longer caps entity distance");
            check(run(ray,1,3) == ray, "miss keeps original ray");
        }
        check(set_ghost_patch(fixture,false), "disable patch");
        check(!std::memcmp(fixture + branch_rva - 6, branch_signature.data(), branch_signature.size()),
            "restore exact native bytes");
        check(run(5,1,0) == 1, "disable restores block occlusion");
    }
    GhostHandModule module;
    utility::Module& menu=module;
    check(menu.boolean_setting_count()==1 && menu.boolean_setting_name(0)=="Players only" &&
        !menu.boolean_setting(0), "players-only toggle defaults off");
    menu.set_boolean_setting(0,true);
    check(menu.boolean_setting(0), "menu toggle enables filter");
    unsigned char actor[0x280]{}, candidate[0x88]{}, block[0x88]{}, level[0x200]{}, table[0xB00]{};
    const auto field=[](void* target,std::size_t offset,const auto& value) {
        std::memcpy(static_cast<unsigned char*>(target)+offset,&value,sizeof(value));
    };
    field(level,0,static_cast<void*>(table));field(level,0x1E8,static_cast<void*>(block));
    field(table,0xA78,static_cast<void*>(fixture+0xD72560));
    field(candidate,0x2C,Point{0,0,2});field(block,0x2C,Point{0,0,1});
    const auto identifier=[&](const char* id) {
        field(actor,0x240,id);field(actor,0x250,std::strlen(id));field(actor,0x258,std::size_t{32});
    };
    filter_image=fixture;filter_active=true;
    identifier("minecraft:zombie");
    check(!filter_candidate(actor,candidate,level), "mob behind block rejected");
    field(block,0x2C,Point{0,0,3});
    check(filter_candidate(actor,candidate,level)==actor, "visible mob retains normal attacks");
    field(block,0x2C,Point{0,0,1});
    identifier("minecraft:player");
    check(filter_candidate(actor,candidate,level)==actor, "player behind block allowed");
    identifier("minecraft:player.remote");
    check(filter_candidate(actor,candidate,level)==actor, "player variant allowed");
    identifier("minecraft:player_fake");
    check(!filter_candidate(actor,candidate,level), "non-player prefix rejected");
    for (int miss:{2,3}) {
        field(block,0x18,miss);
        check(filter_candidate(actor,candidate,level)==actor,"no block leaves mobs attackable");
    }
    field(block,0x18,4);
    check(!filter_candidate(actor,candidate,level),"type-four blocking hit still obstructs mobs");
    field(block,0x18,0);
    check(!filter_candidate(actor,candidate,nullptr),"unknown block context fails closed for non-player");
    menu.set_boolean_setting(0,false);
    check(filter_candidate(actor,candidate,level)==actor,"toggle off restores all-entity Ghost Hand");
    menu.set_boolean_setting(0,true);filter_active=false;
    check(filter_candidate(actor,candidate,level)==actor,"disabled module passes through");

    // Install and execute the real call-site relay with a native resolver stub.
    filter_image=nullptr;
    std::memcpy(fixture+resolve_call_rva,resolve_call.data(),5);
    const unsigned char getter[]{0x48,0x8B,0x81,0xE8,0x01,0,0,0xC3};
    const unsigned char resolver[]{0x48,0x83,0xEC,0x48,0x48,0x8D,0x51,0x38,0x48,0x8D,0x4C,0x24,0x28};
    std::memcpy(fixture+0xD72560,getter,sizeof(getter));
    std::memcpy(fixture+0x47E8B00,resolver,sizeof(resolver));
    fixture[0xD72560]^=1;
    check(!install_filter(fixture),"filter rejects mismatched getter");
    check(!std::memcmp(fixture+resolve_call_rva,resolve_call.data(),5),"failed filter installs no call");
    fixture[0xD72560]^=1;
    check(install_filter(fixture),"install native filter relay");
    const unsigned char stub[]{0x48,0x8B,0x81,0x80,0,0,0,0xC3};
    std::memcpy(fixture+0x47E8B00,stub,sizeof(stub));
    field(candidate,0x80,static_cast<void*>(actor));
    // Entry sets the same r14 Level register as the native crosshair picker.
    const unsigned char prefix[]{0x41,0x56,0x48,0x83,0xEC,0x20,0x49,0x89,0xD6,0xE9};
    std::memcpy(fixture,prefix,sizeof(prefix));
    const auto offset=static_cast<std::int32_t>(resolve_call_rva-14);
    std::memcpy(fixture+10,&offset,4);
    const unsigned char tail[]{0x48,0x83,0xC4,0x20,0x41,0x5E,0xC3};
    std::memcpy(fixture+resolve_call_rva+5,tail,sizeof(tail));
    FlushInstructionCache(GetCurrentProcess(),fixture,0x4800000);
    auto execute=reinterpret_cast<void*(*)(void*,void*)>(fixture);
    filter_active=true;identifier("minecraft:zombie");
    check(!execute(candidate,level),"native relay rejects blocked mob and passes correct Level");
    identifier("minecraft:player");
    check(execute(candidate,level)==actor,"native relay returns blocked player");
    filter_active=false;identifier("minecraft:zombie");
    check(execute(candidate,level)==actor,"native relay passes through when disabled");
    menu.set_boolean_setting(0,false);
    check(module.name() == "Ghost Hand" && module.category() == utility::ModuleCategory::combat,
        "combat menu name");
    check(!module.enabled() && !module.available(), "starts disabled and unverified");
    module.set_enabled(true);
    check(!module.enabled(), "unavailable module cannot enable");
    VirtualFree(fixture, 0, MEM_RELEASE);
    std::puts("PASS: Ghost Hand native branch execution, signature rejection, toggles, restoration, menu metadata");
}
