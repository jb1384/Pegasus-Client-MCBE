// Compile the real patch implementation into an isolated process, not Minecraft.
#include "../src/modules/ReachModule.cpp"
#include <cstdio>
#include <cstdlib>

using namespace utility::modules;
void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    // Extending one reach must not extend the other; reducing block reach must
    // still leave a long enough ray for normal Creative entity selection.
    for(float vanilla : {5.0F,7.0F}) {
        auto r=pick_ranges(vanilla,true,7,false,3);
        check(r.ray==7 && r.block==vanilla,"entity-only leaves blocks vanilla");
        check(final_range(0,r.ray,r.block,true,7)==vanilla,"final block gate remains vanilla");
        r=pick_ranges(vanilla,false,7,true,3);
        check(r.ray==vanilla && r.block==3,"short block range preserves vanilla entity query");
        check(final_range(1,r.ray,r.block,false,7)==vanilla,"disabled entity module leaves native range");
        r=pick_ranges(vanilla,true,3,true,7);
        check(r.ray==7 && final_range(1,r.ray,r.block,true,3)==3,"long blocks do not extend configured entities");
        check(final_range(3,9,r.block,true,3)==9,"unrelated hit types retain native range");
        r=pick_ranges(vanilla,false,7,false,7);
        check(r.ray==vanilla && r.block==vanilla,"disabling both restores vanilla ranges");
    }

    auto* fixture = static_cast<std::byte*>(VirtualAlloc(nullptr, 0x500000,
        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    check(fixture != nullptr, "fixture allocation");
    for (const auto& site : entity_range_reads)
        std::memcpy(fixture + site.rva, site.bytes.data(), site.size);
    EntityRangeStorage patch;
    // A mismatched build must not partially install any of the three reads.
    fixture[entity_range_reads[2].rva] = std::byte{0x90};
    check(!patch.install(fixture), "reject mismatched signature");
    check(!std::memcmp(fixture + entity_range_reads[0].rva,
        entity_range_reads[0].bytes.data(), entity_range_reads[0].size), "no partial writes");
    fixture[entity_range_reads[2].rva] = std::byte{0x0F};
    check(patch.install(fixture), "install all entity range reads");

    // Execute each exact native SSE instruction in a tiny ABI-safe wrapper.
    // This checks both comparison gates and the clamp, including the 7/8-byte
    // instruction-length distinction when relocating RIP-relative operands.
    auto prepare = [&](std::size_t index) {
        const auto& site = entity_range_reads[index];
        auto* code = reinterpret_cast<unsigned char*>(fixture + index * 128);
        const unsigned char prefix[]{0x48,0x83,0xEC,0x18,0x0F,0x11,0x3C,0x24,
            0x0F,0x28,0xF8,0x0F,0x28,0xD8}; // save xmm7; xmm7=xmm0; xmm3=xmm0
        std::memcpy(code, prefix, sizeof(prefix));
        auto* instruction = code + sizeof(prefix);
        std::memcpy(instruction, fixture + site.rva, site.size);
        std::int32_t displacement{};
        std::memcpy(&displacement, instruction + site.size - 4, 4);
        const auto target = reinterpret_cast<std::intptr_t>(fixture + site.rva + site.size) + displacement;
        const auto relocated = target - reinterpret_cast<std::intptr_t>(instruction + site.size);
        check(relocated >= INT32_MIN && relocated <= INT32_MAX, "fixture displacement fits");
        displacement = static_cast<std::int32_t>(relocated);
        std::memcpy(instruction + site.size - 4, &displacement, 4);
        auto* tail = instruction + site.size;
        if (index == 1) { const unsigned char result[]{0x0F,0x28,0xC7}; std::memcpy(tail,result,3); }
        else { const unsigned char result[]{0x0F,0x96,0xC0}; std::memcpy(tail,result,3); }
        const unsigned char suffix[]{0x0F,0x10,0x3C,0x24,0x48,0x83,0xC4,0x18,0xC3};
        std::memcpy(tail + 3, suffix, sizeof(suffix));
        FlushInstructionCache(GetCurrentProcess(), code, 128);
        return code;
    };
    auto first = reinterpret_cast<bool(*)(float)>(prepare(0));
    auto clamp = reinterpret_cast<float(*)(float)>(prepare(1));
    auto final = reinterpret_cast<bool(*)(float)>(prepare(2));
    for (float distance : {3.0F, 3.5F, 5.0F, 7.0F, 3.0F}) {
        patch.set(distance);
        check(first(distance) && final(distance), "inclusive entity reach boundary");
        check(!first(distance + 0.01F) && !final(distance + 0.01F), "reject beyond configured range");
        check(clamp(0) == distance, "clamp matches both rejection checks");
    }
    patch.set(7.0F);
    check(patch.uninstall(), "uninstall succeeds");
    check(clamp(0) == 3.0F, "disable restores vanilla backing value");
    for (const auto& site : entity_range_reads)
        check(!std::memcmp(fixture + site.rva, site.bytes.data(), site.size), "restore exact native instruction");
    check(patch.install(fixture) && patch.uninstall(), "repeat installation and restoration");
    VirtualFree(fixture, 0, MEM_RELEASE);
    std::puts("PASS: native Survival entity cap, clamp, final rejection, slider distances, signature rejection, restoration");
}
