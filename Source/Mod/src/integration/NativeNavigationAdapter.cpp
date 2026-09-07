#include "NativeNavigationAdapter.hpp"
#include "GameContext.hpp"
#include "NavigationBridge.hpp"
#include "NavigationNativeLayout.hpp"
#include "NavigationWorldAccess.hpp"
#include "NavigationInventoryAccess.hpp"
#include "NavigationSnapshotPolicy.hpp"
#include <Windows.h>
#include <cmath>
#include <cstring>

namespace utility::integration {
namespace {
template<class T> T read(const void* object,std::size_t offset=0) {
    T value{};
    if(object && readable_game_memory(static_cast<const std::byte*>(object)+offset,sizeof(T)))
        std::memcpy(&value,static_cast<const std::byte*>(object)+offset,sizeof(T));
    return value;
}
std::string native_string(const void* object) {
    const auto size=read<std::size_t>(object,16),capacity=read<std::size_t>(object,24);
    if(!size||size>256||capacity<size||capacity>65536)return {};
    const char* data=capacity<16?static_cast<const char*>(object):read<const char*>(object);
    return readable_game_memory(data,size)?std::string(data,size):std::string{};
}
bool controls_active() {
    DWORD process{};GetWindowThreadProcessId(GetForegroundWindow(),&process);
    CURSORINFO cursor{sizeof(cursor)};
    return process==GetCurrentProcessId()&&GetCursorInfo(&cursor)&&!(cursor.flags&CURSOR_SHOWING);
}
bool physical_input() {
    for(const auto key:std::array<int,10>{'W','S','A','D',VK_SPACE,VK_SHIFT,VK_CONTROL,VK_LBUTTON,VK_RBUTTON,VK_ESCAPE})
        if(GetAsyncKeyState(key)&0x8000)return true;
    return false;
}
bool safe_copy(std::uintptr_t at,void* output,std::size_t size) noexcept {
    __try {if(!at)return false;std::memcpy(output,reinterpret_cast<void*>(at),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool safe_write(std::uintptr_t at,const void* input,std::size_t size) noexcept {
    __try {if(!at)return false;std::memcpy(reinterpret_cast<void*>(at),input,size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool native_mining_call(std::uintptr_t function,void* mode,const navigation::BlockPos& position,
                       unsigned char face,const float* hit,bool start) noexcept {
    __try {
        bool destroyed{};
        if(start)reinterpret_cast<bool(__fastcall*)(void*,const navigation::BlockPos*,unsigned char,bool*)>(function)(mode,&position,face,&destroyed);
        else reinterpret_cast<bool(__fastcall*)(void*,const navigation::BlockPos*,unsigned char,const float*,bool*)>(function)(mode,&position,face,hit,&destroyed);
        // Completion is established by the next world snapshot, not this flag.
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void native_mining_abort(std::uintptr_t function,void* mode,const navigation::BlockPos& position) noexcept {
    __try {reinterpret_cast<void(__fastcall*)(void*,const navigation::BlockPos*)>(function)(mode,&position);}
    __except(EXCEPTION_EXECUTE_HANDLER){}
}
}
bool NavigationCapabilities::complete() const noexcept {
    return executable&&player_observation&&block_identifiers&&world_snapshot&&movement&&aiming&&
        mining&&placement&&interaction&&inventory&&survival&&food&&drops;
}
std::string NavigationCapabilities::missing() const {
    std::string result;
    const auto add=[&](bool present,const char* name){if(!present){if(!result.empty())result+=", ";result+=name;}};
    add(executable,"supported executable");add(player_observation,"player observation");
    add(block_identifiers,"block registry");add(world_snapshot,"collision/chunk snapshots");
    add(movement,"movement input");add(aiming,"aim/raycast");add(mining,"mining lifecycle");
    add(placement,"placement");add(interaction,"doors/gates");add(inventory,"inventory transactions");
    add(survival,"health/hunger/durability");add(food,"food use");add(drops,"drop accounting");
    return result;
}
void NativeNavigationAdapter::initialize() {
    capabilities_={};identifiers_.clear();
    base_=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto* image=reinterpret_cast<const std::byte*>(base_);
    const auto dos=read<IMAGE_DOS_HEADER>(image);
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0 || dos.e_lfanew>0x100000)return;
    const auto nt=read<IMAGE_NT_HEADERS64>(image,static_cast<std::size_t>(dos.e_lfanew));
    capabilities_.executable=nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64&&
        nt.FileHeader.TimeDateStamp==navigation_native::timestamp&&nt.OptionalHeader.SizeOfImage==navigation_native::image_size;
    if(!capabilities_.executable)return;
    struct Signature {std::uintptr_t rva;unsigned char bytes[8];};
    constexpr Signature signatures[]{
        {0x2D2D640,{0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54}},
        {0x2D2E310,{0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41}},
        {0x2D2EF30,{0x56,0x48,0x83,0xEC,0x20,0x48,0x89,0xCE}},
        {0x2D33570,{0x48,0x83,0xEC,0x28,0x80,0xB9,0xC8,0x00}}};
    mining_interfaces_=true;
    for(const auto& signature:signatures) {
        unsigned char bytes[8]{};
        mining_interfaces_=safe_copy(base_+signature.rva,bytes,8)&&!std::memcmp(bytes,signature.bytes,8)&&mining_interfaces_;
    }
    // Already researched player/BlockGraphics layouts; no new hooks or writes.
    capabilities_.player_observation=readable_game_memory(image+0xE820EC0,8);
    const auto first=read<std::uintptr_t>(image,0x11A73A28),last=read<std::uintptr_t>(image,0x11A73A30);
    if(first&&last>first&&(last-first)%8==0&&(last-first)/8<=16384&&
        readable_game_memory(reinterpret_cast<void*>(first),last-first)) {
        for(auto at=first;at<last;at+=8) {
            auto* graphics=read<void*>(reinterpret_cast<void*>(at));
            auto* block=read<void*>(graphics,8);auto* type=read<std::byte*>(block,0x68);
            if(!type)continue;
            auto id=native_string(type+0xE8);if(id.empty())continue;
            if(id.find(':')==id.npos)id="minecraft:"+id;
            identifiers_.insert(std::move(id));
        }
    }
    capabilities_.block_identifiers=identifiers_.contains("minecraft:air")&&identifiers_.contains("minecraft:stone");
    // Remaining flags deliberately remain false. The existing Autotool hooks
    // identify start/continue mining, but do not verify abort, placement, input,
    // authoritative collision, health attributes or server-confirmed inventory.
    // See docs/baritone-validation.md for the native completion checklist.
}
std::string NativeNavigationAdapter::unavailable_reason() const {
    return "Baritone unavailable: native interfaces not verified ("+capabilities_.missing()+").";
}
navigation::Frame NativeNavigationAdapter::observe(void* player,const std::vector<std::string>&) {
    input_observation_={};
    navigation::Frame frame;
    if(!capabilities_.executable || read<std::uintptr_t>(player)!=base_+navigation_native::player_vtable) {
        action_player_=nullptr;input_owned_=false;mining_owned_=false;return frame;
    }
    auto* shape=read<void*>(player,0x220);auto* dimension=read<void*>(player,0x1C8);
    struct NativeVec {float x{},y{},z{};};
    if(!dimension||!readable_game_memory(shape,sizeof(NativeVec)*2))return frame;
    const auto lower=read<NativeVec>(shape),upper=read<NativeVec>(shape,sizeof(NativeVec));
    if(!std::isfinite(lower.x)||!std::isfinite(lower.y)||!std::isfinite(lower.z)||
       !std::isfinite(upper.x)||!std::isfinite(upper.y)||!std::isfinite(upper.z)||
       upper.x<=lower.x||upper.z<=lower.z||upper.y<=lower.y)return frame;
    if(player!=previous_player_||dimension!=previous_dimension_) {
        input_owned_=false;mining_owned_=false;view_known_=false;aim_owned_=false;
        ++session_;previous_player_=player;previous_dimension_=dimension;
        observed_world_={};observed_hashes_.clear();published_world_.reset();scan_cursor_=0;snapshot_time_=0;
    }
    frame.feet={(lower.x+upper.x)*0.5,lower.y,(lower.z+upper.z)*0.5};
    action_player_=player;action_feet_=frame.feet;
    const auto copy=[](std::uintptr_t at,void* out,std::size_t bytes) {
        if(!readable_game_memory(reinterpret_cast<void*>(at),bytes))return false;
        std::memcpy(out,reinterpret_cast<void*>(at),bytes);return true;
    };
    const auto input=navigation_native::component(read<std::uintptr_t>(player,0x10),
        read<std::uint32_t>(player,0x18),navigation_native::raw_input_hash,navigation_native::raw_input_size,copy);
    std::array<unsigned char,navigation_native::raw_input_size> bytes{};
    // Failure to inspect manual activity must not silently authorize control.
    frame.manual_input=true;
    if(input&&copy(input,bytes.data(),bytes.size())) {
        std::uint32_t flags{};float sideways{},forward{};
        std::memcpy(&flags,bytes.data(),4);std::memcpy(&sideways,bytes.data()+16,4);std::memcpy(&forward,bytes.data()+20,4);
        input_observation_.flags=flags;input_observation_.sideways=sideways;input_observation_.forward=forward;
        input_observation_.physical=physical_input();input_observation_.owned=input_owned_;
        const bool valid=std::isfinite(sideways)&&std::isfinite(forward)&&std::abs(sideways)<=1.001f&&std::abs(forward)<=1.001f;
        if(input_owned_&&navigation_native::owned_axes(sideways,forward,submitted_)) {
            flags&=~submitted_.buttons;sideways=0;forward=0;
        }
        if(valid)frame.manual_input=physical_input()||flags!=0||sideways!=0||forward!=0;
    }
    const auto rotation=read<std::uintptr_t>(player,0x228);float view[2]{};
    if(copy(rotation,view,sizeof(view))&&std::isfinite(view[0])&&std::isfinite(view[1])) {
        input_observation_.pitch=view[0];input_observation_.yaw=view[1];
        input_observation_.expected_pitch=last_view_[0];input_observation_.expected_yaw=last_view_[1];
        input_observation_.aim_owned=aim_owned_;input_observation_.view_known=view_known_;
        if(view_known_&&(input_owned_||mining_owned_||aim_owned_)&&
           (std::abs(view[0]-last_view_[0])>0.1f||std::abs(navigation_native::angle_delta(view[1]-last_view_[1]))>0.1f))frame.manual_input=true;
        std::memcpy(last_view_,view,sizeof(view));view_known_=true;
    }else frame.manual_input=true;
    const auto attributes=navigation_native::component(read<std::uintptr_t>(player,0x10),
        read<std::uint32_t>(player,0x18),navigation_native::attributes_hash,navigation_native::attributes_size,copy);
    float health{},hunger{};
    const bool health_known=navigation_native::attribute(attributes,
        read<std::uint32_t>(reinterpret_cast<void*>(base_),navigation_native::health_definition+4),health,copy);
    const bool hunger_known=navigation_native::attribute(attributes,
        read<std::uint32_t>(reinterpret_cast<void*>(base_),navigation_native::hunger_definition+4),hunger,copy);
    if(health_known&&hunger_known&&health>=0&&health<=20&&hunger>=0&&hunger<=20) {
        frame.inventory.health=health;frame.inventory.hunger=hunger;
        frame.alive=health>0&&readable_game_memory(static_cast<std::byte*>(player)+0x269,1)&&
            read<unsigned char>(player,0x269)==0;
    }
    frame.control=frame.alive&&controls_active();
    navigation_native::NativeInventory inventory;
    if(navigation_native::capture_inventory(base_,reinterpret_cast<std::uintptr_t>(player),inventory)) {
        for(unsigned slot=0;slot<inventory.count;++slot) {
            const auto& stack=inventory.slots[slot];const std::string_view id=stack.identifier;
            if(id=="minecraft:cobblestone"||id=="minecraft:dirt"||id=="minecraft:netherrack") {
                frame.inventory.placement_count+=stack.count;
                if(slot<9&&frame.inventory.placement_slot<0)frame.inventory.placement_slot=static_cast<int>(slot);
            }
        }
    } else frame.inventory.target_capacity=false;
    frame.grounded=navigation_native::component(read<std::uintptr_t>(player,0x10),read<std::uint32_t>(player,0x18),0xC29078A0,0,copy)!=0;
    frame.world=snapshot_world(player);
    frame.terrain_ready=static_cast<bool>(frame.world);
    const auto center=navigation::block_at(frame.feet);
    if(frame.world)for(int x=-2;x<=2;++x)for(int z=-2;z<=2;++z)for(int y=-1;y<=2;++y)
        if(!frame.world->at({center.x+x,center.y+y,center.z+z}).known)frame.terrain_ready=false;
    // Partial observation is diagnostic only. Never substitute invented health,
    // air cells or control state for missing native information.
    frame.connected=true;frame.inventory.drop_mapping_known=false;
    return frame;
}
std::shared_ptr<const navigation::World> NativeNavigationAdapter::snapshot_world(void* player) {
    const auto now=GetTickCount64();
    if(published_world_&&now-snapshot_time_<25)return published_world_;
    snapshot_time_=now;
    const auto region=read<std::uintptr_t>(previous_dimension_,0xF0);
    const auto center=navigation::block_at(action_feet_);
    observed_world_.session=session_;
    const auto sample=[&](navigation::BlockPos position) {
        navigation_native::NativeCell native;
        if(!navigation_native::capture_cell(base_,region,{position.x,position.y,position.z},native)) {
            if(observed_world_.cells.erase(position)){observed_hashes_.erase(position);++observed_world_.revision;}
            return;
        }
        std::uint64_t hash=14695981039346656037ull;
        for(std::size_t i=0;i<sizeof(native);++i){hash^=reinterpret_cast<unsigned char*>(&native)[i];hash*=1099511628211ull;}
        const auto previous=observed_hashes_.find(position);
        if(previous!=observed_hashes_.end()&&previous->second==hash)return;
        navigation::Cell cell;cell.id=native.identifier;cell.known=true;
        for(unsigned i=0;i<native.count;++i) {
            const auto* box=native.boxes[i].bounds;
            cell.collision.push_back({{box[0]-position.x,box[1]-position.y,box[2]-position.z},
                                      {box[3]-position.x,box[4]-position.y,box[5]-position.z}});
        }
        const auto& id=cell.id;
        cell.water=id=="minecraft:water"||id=="minecraft:flowing_water";
        cell.liquid=cell.water||id=="minecraft:lava"||id=="minecraft:flowing_lava";
        cell.hazard=id=="minecraft:lava"||id=="minecraft:flowing_lava"||id=="minecraft:fire"||id=="minecraft:soul_fire"||
            id=="minecraft:magma"||id=="minecraft:cactus"||id=="minecraft:powder_snow"||id=="minecraft:sweet_berry_bush"||
            id=="minecraft:campfire"||id=="minecraft:soul_campfire"||id=="minecraft:pointed_dripstone"||id=="minecraft:bubble_column"||
            id=="minecraft:portal"||id=="minecraft:end_portal"||id=="minecraft:end_gateway"||id=="minecraft:wither_rose";
        cell.falling=id=="minecraft:sand"||id=="minecraft:red_sand"||id=="minecraft:gravel"||id.ends_with("_concrete_powder")||id.ends_with("anvil");
        cell.climbable=id=="minecraft:ladder"||id=="minecraft:vine";
        // Opening, harvesting, and break cost are not inferred from identifiers.
        observed_world_.cells.insert_or_assign(position,std::move(cell));observed_hashes_[position]=hash;++observed_world_.revision;
    };
    // Prioritize body clearance and immediate support on every game tick.
    for(int x=-1;x<=1;++x)for(int z=-1;z<=1;++z)for(int y=-1;y<=2;++y)sample({center.x+x,center.y+y,center.z+z});
    constexpr unsigned side=25,height=17,total=side*side*height;
    LARGE_INTEGER begin{},frequency{};QueryPerformanceCounter(&begin);QueryPerformanceFrequency(&frequency);
    for(unsigned count=0;count<128;++count) {
        const auto index=scan_cursor_++%total;
        const auto& offset=navigation_native::snapshot_offsets()[index];
        sample({center.x+offset[0],center.y+offset[1],center.z+offset[2]});
        LARGE_INTEGER current{};QueryPerformanceCounter(&current);
        if(static_cast<double>(current.QuadPart-begin.QuadPart)/frequency.QuadPart>0.003)break;
    }
    if(observed_world_.cells.size()>32768) {
        for(auto it=observed_world_.cells.begin();it!=observed_world_.cells.end();) {
            const auto p=it->first;
            if(std::abs(p.x-center.x)>24||std::abs(p.z-center.z)>24||std::abs(p.y-center.y)>12) {
                observed_hashes_.erase(p);it=observed_world_.cells.erase(it);
            }else ++it;
        }
        for(auto it=observed_world_.cells.begin();it!=observed_world_.cells.end()&&observed_world_.cells.size()>32768;) {
            const auto p=it->first;
            if(std::abs(p.x-center.x)>1||std::abs(p.z-center.z)>1||p.y<center.y-1||p.y>center.y+2) {
                observed_hashes_.erase(p);it=observed_world_.cells.erase(it);
            }else ++it;
        }
    }
    if(!published_world_||published_world_->revision!=observed_world_.revision)
        published_world_=std::make_shared<const navigation::World>(observed_world_);
    (void)player;return published_world_;
}
bool NativeNavigationAdapter::known_block(const std::string& id) const {return identifiers_.contains(id);}
bool NativeNavigationAdapter::write_movement(navigation_native::Steering input) noexcept {
    if(!action_player_||read<std::uintptr_t>(action_player_)!=base_+navigation_native::player_vtable||
        read<void*>(action_player_,0x1C8)!=previous_dimension_)return false;
    const auto registry=read<std::uintptr_t>(action_player_,0x10);const auto entity=read<std::uint32_t>(action_player_,0x18);
    const auto movement=navigation_native::component(registry,entity,navigation_native::move_input_hash,navigation_native::move_input_size,safe_copy);
    const auto raw=navigation_native::component(registry,entity,navigation_native::raw_input_hash,navigation_native::raw_input_size,safe_copy);
    if(!movement||!raw)return false;
    bool success=true;
    for(const auto at:std::array<std::uintptr_t,3>{movement,movement+16,raw}) {
        std::uint32_t flags{};
        if(!safe_copy(at,&flags,4)){success=false;continue;}
        flags=(flags&~navigation_native::direction_mask)|input.buttons;
        success=safe_write(at,&flags,4)&&success;
    }
    const float axes[]{input.sideways,input.forward};
    success=safe_write(movement+0x24,axes,sizeof(axes))&&success;
    success=safe_write(raw+16,axes,sizeof(axes))&&success;
    return success;
}
void NativeNavigationAdapter::release() noexcept {
    stop_mining();
    aim_owned_=false;
    if(input_owned_&&action_player_&&read<std::uintptr_t>(action_player_)==base_+navigation_native::player_vtable&&
       read<void*>(action_player_,0x1C8)==previous_dimension_) {
        const auto registry=read<std::uintptr_t>(action_player_,0x10);const auto entity=read<std::uint32_t>(action_player_,0x18);
        const auto movement=navigation_native::component(registry,entity,navigation_native::move_input_hash,navigation_native::move_input_size,safe_copy);
        const auto raw=navigation_native::component(registry,entity,navigation_native::raw_input_hash,navigation_native::raw_input_size,safe_copy);
        if(movement&&raw) {
            auto mask=submitted_.buttons;
            if(GetAsyncKeyState('W')&0x8000)mask&=~0x2000u;
            if(GetAsyncKeyState('S')&0x8000)mask&=~0x4000u;
            if(GetAsyncKeyState('A')&0x8000)mask&=~0x8000u;
            if(GetAsyncKeyState('D')&0x8000)mask&=~0x10000u;
            for(const auto at:std::array<std::uintptr_t,3>{movement,movement+16,raw}) {
                std::uint32_t flags{};
                if(safe_copy(at,&flags,4)){flags&=~mask;safe_write(at,&flags,4);}
            }
            for(const auto at:std::array<std::uintptr_t,2>{movement+0x24,raw+16}) {
                float axes[2]{};
                if(safe_copy(at,axes,8)) {
                    if(navigation_native::owned_axes(axes[0],axes[1],submitted_)) {
                        if(!(GetAsyncKeyState('A')&0x8000)&&!(GetAsyncKeyState('D')&0x8000))axes[0]=0;
                        if(!(GetAsyncKeyState('W')&0x8000)&&!(GetAsyncKeyState('S')&0x8000))axes[1]=0;
                    }
                    safe_write(at,axes,8);
                }
            }
        }
    }
    input_owned_=false;submitted_={};navigation_owns_controls=false;
}
bool NativeNavigationAdapter::move(const navigation::Step& step,bool sprint) {
    stop_mining();
    // Walking path verified by the bounded forward-input trial. Advanced movement
    // remains gated until separate native trials establish its execution contract.
    (void)sprint; // Sprint permission permits walking until sprint execution is verified.
    if(!controls_active()||physical_input()||
       (step.kind!=navigation::MoveKind::walk&&step.kind!=navigation::MoveKind::descend&&
          !(step.kind==navigation::MoveKind::ascend&&step.to.y-step.from.y<=0.6))) {release();return false;}
    // Direct actor-angle writes are restored by the camera between ticks.
    // Walk relative to the observed view until native look input is verified.
    const auto rotation=read<void*>(action_player_,0x228);
    if(!readable_game_memory(rotation,8))return false;
    const auto input=navigation_native::steer(step.to.x-action_feet_.x,step.to.z-action_feet_.z,read<float>(rotation,4));
    if(!input.valid){release();return true;}
    submitted_=input;input_owned_=true;
    if(!write_movement(input)){release();return false;}
    navigation_owns_controls=true;return true;
}
void NativeNavigationAdapter::stop_mining() noexcept {
    if(mining_owned_&&mining_interfaces_&&action_player_&&
       read<std::uintptr_t>(action_player_)==base_+navigation_native::player_vtable&&
       read<void*>(action_player_,0x1C8)==previous_dimension_) {
        const auto mode=read<void*>(action_player_,0xAA0);
        const auto table=read<std::uintptr_t>(mode);
        if((table==base_+0xE81A090||table==base_+0xE81A130)&&read<void*>(mode,8)==action_player_)
            native_mining_abort(base_+0x2D2EF30,mode,mining_target_);
    }
    mining_owned_=false;
}
bool NativeNavigationAdapter::mine(navigation::BlockPos target,int slot) {
    if(!mining_interfaces_||!action_player_||!controls_active()||physical_input()) {release();return false;}
    if(input_owned_)release();
    const auto mode=read<void*>(action_player_,0xAA0);const auto table=read<std::uintptr_t>(mode);
    if((table!=base_+0xE81A090&&table!=base_+0xE81A130)||read<void*>(mode,8)!=action_player_)return false;
    // Tool selection and aiming must be confirmed before starting a break.
    const auto supplies=read<void*>(action_player_,0x5B8);
    if(slot<0||slot>8||read<int>(supplies,0x10)!=slot)return false;
    const auto level=read<void*>(action_player_,0x1D8);const auto hit=read<void*>(level,0x1E8);
    if(!readable_game_memory(hit,0x38))return false;
    if(read<int>(hit,0x18)!=0||read<navigation::BlockPos>(hit,0x20)!=target) {
        stop_mining();return aim_at({target.x+0.5,target.y+0.5,target.z+0.5});
    }
    const auto face=read<unsigned char>(hit,0x1C);float point[3]{};
    std::memcpy(point,static_cast<std::byte*>(hit)+0x2C,12);
    const double dx=point[0]-action_feet_.x,dy=point[1]-(action_feet_.y+1.62),dz=point[2]-action_feet_.z;
    if(face>5||!std::isfinite(dx+dy+dz)||dx*dx+dy*dy+dz*dz>4.5*4.5)return false;
    if(mining_owned_&&mining_target_!=target)stop_mining();
    const bool start=!mining_owned_;mining_target_=target;mining_owned_=true;
    navigation_owns_controls=true;
    const auto function=start?(table==base_+0xE81A130?0x2D33570:0x2D2D640):0x2D2E310;
    if(!native_mining_call(base_+function,mode,target,face,point,start)){release();return false;}
    return true;
}
bool NativeNavigationAdapter::aim_at(navigation::Vec3 target) {
    if(!action_player_||!view_known_||!controls_active()||physical_input())return false;
    const auto desired=navigation_native::look_at(target.x-action_feet_.x,target.y-action_feet_.y-1.62,
        target.z-action_feet_.z,last_view_[0],last_view_[1]);
    if(!desired.valid)return false;
    const auto movement=navigation_native::component(read<std::uintptr_t>(action_player_,0x10),
        read<std::uint32_t>(action_player_,0x18),navigation_native::move_input_hash,navigation_native::move_input_size,safe_copy);
    const auto rotation=read<std::uintptr_t>(action_player_,0x228);if(!movement||!rotation)return false;
    const float angles[]{desired.pitch,desired.yaw};
    if(!safe_write(rotation,angles,sizeof(angles))||!safe_write(movement+0x34,angles,sizeof(angles)))return false;
    std::memcpy(last_view_,angles,sizeof(angles));aim_owned_=true;navigation_owns_controls=true;return true;
}
bool NativeNavigationAdapter::place(navigation::BlockPos,int){return false;}
bool NativeNavigationAdapter::open(navigation::BlockPos){return false;}
bool NativeNavigationAdapter::eat(int){return false;}
} // namespace utility::integration
