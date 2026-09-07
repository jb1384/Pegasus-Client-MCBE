#pragma once
#include <array>
#include <cstdint>
#include <mutex>

namespace utility::modules::airjump {
// One physical press may be consumed independently by the client and the
// integrated server. Repeated key-down messages do not create new presses.
class Requests {
public:
    void press(std::uint64_t now) {
        std::lock_guard guard(mutex_);
        if(held_)return;
        held_=true; ++sequence_; at_=now; entries_={};
    }
    void release() { std::lock_guard guard(mutex_);held_=false; }
    void reset() { std::lock_guard guard(mutex_);held_=false;at_=0;entries_={};++sequence_; }
    bool consume(std::uintptr_t registry,std::uint32_t entity,std::uint64_t now) {
        std::lock_guard guard(mutex_);
        if(!registry||!at_||now<at_||now-at_>500)return false;
        Entry* entry=nullptr;
        for(auto& candidate:entries_)if(candidate.registry==registry&&candidate.entity==entity){entry=&candidate;break;}
        if(!entry){for(auto& candidate:entries_)if(!candidate.registry){entry=&candidate;break;}}
        if(!entry)return false; // Only the two matching player replicas belong here.
        if(entry->registry&&entry->sequence==sequence_)return false;
        *entry={registry,entity,sequence_};return true;
    }
private:
    struct Entry { std::uintptr_t registry{};std::uint32_t entity{};std::uint64_t sequence{}; };
    std::mutex mutex_;
    bool held_{};
    std::uint64_t sequence_{},at_{};
    std::array<Entry,4> entries_{};
};
}
