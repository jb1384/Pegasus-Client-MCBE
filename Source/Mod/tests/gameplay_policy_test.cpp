// Exercise the real input queue and native string boundary without Minecraft.
#include "../src/modules/GameplayModules.cpp"
#include <cstdlib>
#include <cstdio>
using namespace utility::modules;
void check(bool ok,const char* text){if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
int eject_calls=0;
void __fastcall mock_eject(void*,void*,void*,void*,void*,void*,void*,bool,void*){++eject_calls;}
int main(){
    flags[static_cast<unsigned>(GameplayFeature::jetpack)]=true;
    flags[static_cast<unsigned>(GameplayFeature::esp)]=true;
    utility::integration::navigation_owns_controls=true;
    check(!on(GameplayFeature::jetpack)&&on(GameplayFeature::esp),"navigation ownership must suspend movement but preserve visuals");
    utility::integration::navigation_owns_controls=false;
    check(on(GameplayFeature::jetpack),"navigation ownership changed saved toggle");
    flags[static_cast<unsigned>(GameplayFeature::jetpack)]=false;
    flags[static_cast<unsigned>(GameplayFeature::esp)]=false;
    // Live camera sample: yaw 20.0704, pitch 18.7920. Looking along
    // Minecraft's independently computed yaw/pitch direction must hit center.
    Frame camera{};camera.origin={12,-51,3};
    const float basis[]{-0.939271867F,0,-0.343173981F,0,
        -0.110547982F,0.946694136F,0.302571297F,0,
        0.324880779F,0.322133899F,-0.889203131F,0,0,0,0,1};
    std::memcpy(camera.view,basis,sizeof(basis));
    const Frustum frustum{{-0.622975826F,0,-0.782241046F,0,0.622975826F,0,-0.782241046F,0,
        0,0.838670611F,-0.544639111F,0,0,-0.838670611F,-0.544639111F,0}};
    check(configure_projection(camera,frustum),"valid live camera");
    check(std::abs(camera.scale_x-0.7963988F)<0.0001F&&
          std::abs(camera.scale_y-1.539865F)<0.0001F,"camera-space frustum gives correct FOV");
    const float yaw=20.0703735F*0.01745329252F,pitch=18.7920227F*0.01745329252F;
    auto center=camera_space(camera,{camera.origin.x-10*std::sin(yaw)*std::cos(pitch),
        camera.origin.y-10*std::sin(pitch),camera.origin.z+10*std::cos(yaw)*std::cos(pitch)});
    check(std::abs(center.x)<0.001F&&std::abs(center.y)<0.001F&&std::abs(center.z-10)<0.001F,
        "pitched and rotated camera projects look direction onto crosshair");
    // Known camera-space offset: two blocks right and one up, ten ahead.
    auto offset=camera_space(camera,{camera.origin.x-5.2378995F,
        camera.origin.y-2.27464485F,camera.origin.z+8.50825465F});
    check(std::abs(offset.x-2)<0.001F&&std::abs(offset.y-1)<0.001F&&std::abs(offset.z-10)<0.001F,
        "off-center point preserves right/up/depth");
    auto behind=camera_space(camera,{camera.origin.x+3.24880779F,
        camera.origin.y+3.22133899F,camera.origin.z-8.89203131F});
    check(behind.z<0,"point behind camera has negative depth");
    camera.view[0]=1;camera.view[1]=camera.view[2]=camera.view[4]=camera.view[6]=camera.view[8]=camera.view[9]=0;
    camera.view[5]=camera.view[10]=1;
    check(configure_projection(camera,frustum)&&std::abs(camera.scale_x-0.7963988F)<0.0001F,
        "FOV does not vary with camera rotation");
    check(!configure_projection(camera,Frustum{}),"reject degenerate frustum");
    camera.view[0]=NAN;check(!configure_projection(camera,frustum),"reject invalid camera matrix");

    int actor_token{},player_token{},dimension_token{},other_dimension{};
    check(live_actor_slot(0x340008,0x340008,&dimension_token,&dimension_token),"live actor slot identity accepted");
    check(!live_actor_slot(0xFFFC000A,0x340008,&dimension_token,&dimension_token),"in-place deletion tombstone rejected");
    check(!live_actor_slot(0x340008,0x380008,&dimension_token,&dimension_token),"reused entity generation rejected");
    check(!live_actor_slot(0x340008,0x340008,&other_dimension,&dimension_token),"foreign registry actor pointer rejected");
    Box live_box{{1,2,3},{2,4,4}};
    check(esp_entity(&actor_token,&player_token,&dimension_token,&dimension_token,"minecraft:chicken",live_box),"live same-dimension mob retained");
    check(!esp_entity(&actor_token,&player_token,&other_dimension,&dimension_token,"minecraft:chicken",live_box),"foreign dimension ghost rejected");
    check(!esp_entity(&actor_token,&player_token,&dimension_token,&dimension_token,"",live_box),"unnamed ghost rejected");
    check(esp_entity(&actor_token,&player_token,&dimension_token,&dimension_token,"addon:custom_mob",live_box),"valid custom entities retained");
    check(!esp_entity(&player_token,&player_token,&dimension_token,&dimension_token,"minecraft:player",live_box),"local player excluded");
    live_box.movement={2,0,-1};
    check(std::abs(interpolated_offset(live_box,0.025).x+1)<0.0001F,"entity motion halfway between ticks");
    check(interpolated_offset(live_box,0.1).x==0,"late frame stops at current bounds rather than extrapolating");

    // Simulate a renderer updating while the entity snapshot remains unchanged.
    std::array<Byte,0xE00> fake_player{};
    std::array<Byte,0x558> fake_client{};
    std::array<Byte,0x470> fake_renderer{};
    std::array<Byte,0x680> fake_renderer_player{};
    auto write_ptr=[](Byte* at,void* value){std::memcpy(at,&value,sizeof(value));};
    image=reinterpret_cast<Byte*>(0x10000000);
    write_ptr(fake_player.data(),image+0xE820EC0);
    write_ptr(fake_player.data()+0xD70,fake_client.data());
    write_ptr(fake_player.data()+0x1C8,&dimension_token);
    write_ptr(fake_client.data()+0x1B8,fake_renderer.data());
    write_ptr(fake_renderer.data()+0x468,fake_renderer_player.data());
    std::memcpy(fake_client.data()+0x418,basis,sizeof(basis));
    std::memcpy(fake_client.data()+0x498,&frustum,sizeof(frustum));
    Frame fresh{};fresh.player=fake_player.data();fresh.dimension=&dimension_token;fresh.boxes.push_back(live_box);
    check(refresh_camera(fresh),"camera sampled independently of entity tick");
    Vec3 new_origin{23,45,67};std::memcpy(fake_renderer_player.data()+0x660,&new_origin,sizeof(new_origin));
    float straight[16]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    std::memcpy(fake_client.data()+0x418,straight,sizeof(straight));
    check(refresh_camera(fresh)&&fresh.origin.x==23&&fresh.view[0]==1&&fresh.boxes.size()==1,
        "next overlay frame uses new camera with same entity snapshot");
    CameraCache cache{};
    check(cached_camera(fresh,cache,1.0),"valid camera cached");
    const float invalid=NAN;std::memcpy(fake_client.data()+0x418,&invalid,sizeof(invalid));
    check(cached_camera(fresh,cache,1.01)&&fresh.origin.x==23,"one torn camera update does not blank all hitboxes");
    check(!cached_camera(fresh,cache,1.06),"persistent bad camera expires without prolonging ghosts");
    std::memcpy(fake_client.data()+0x418,straight,sizeof(straight));
    check(cached_camera(fresh,cache,2.0),"camera recovers after invalid sample");
    write_ptr(fake_player.data()+0x1C8,&other_dimension);
    check(!refresh_camera(fresh),"world transition rejects old entity snapshot");
    check(!cached_camera(fresh,cache,2.01),"cached camera cannot cross a dimension transition");
    fresh.player=reinterpret_cast<void*>(1);check(!refresh_camera(fresh),"unreadable retired player fails safely");
    // Replay the native in-place ActorOwner layout, including a deleted slot
    // whose payload still points at a perfectly valid named actor.
    std::array<Byte,0x80> registry_data{};
    std::array<Byte,32> pool_entry{};
    std::array<Byte,0x68> pool_data{};
    std::array<Byte,0x260> live_actor{},reused_actor{};
    std::uint32_t packed_ids[]{11,18,0xFFFC0003U,19};
    void* actor_page[128]{fake_player.data(),live_actor.data(),live_actor.data(),reused_actor.data()};
    void* actor_pages[]{actor_page};
    Vec3 bounds[]{{1,2,3},{2,4,4}};
    const GameString mob_name("minecraft:chicken");
    write_ptr(fake_player.data()+0x10,registry_data.data());
    write_ptr(fake_player.data()+0x1C8,&dimension_token);
    write_ptr(registry_data.data()+0x68,pool_entry.data());
    write_ptr(registry_data.data()+0x70,pool_entry.data()+32);
    const std::uint32_t owner_type=0x85B93800U;
    std::memcpy(pool_entry.data()+8,&owner_type,4);write_ptr(pool_entry.data()+16,pool_data.data());
    write_ptr(pool_data.data()+0x20,packed_ids);write_ptr(pool_data.data()+0x28,packed_ids+4);
    write_ptr(pool_data.data()+0x50,actor_pages);
    for(auto* actor:{live_actor.data(),reused_actor.data()}) {
        write_ptr(actor+0x10,registry_data.data());write_ptr(actor+0x1C8,&dimension_token);
        write_ptr(actor+0x220,bounds);std::memcpy(actor+0x240,&mob_name,sizeof(mob_name));
    }
    const std::uint32_t live_id=18,reused_id=0x40013;
    std::memcpy(live_actor.data()+0x18,&live_id,4);std::memcpy(reused_actor.data()+0x18,&reused_id,4);
    capture_esp(fake_player.data());
    check(frame.boxes.size()==1,"native capture retains live mob and excludes named tombstone and reused-generation ghost");
    image=nullptr;

    original_eject=&mock_eject;int local{},other{};local_state=&local;
    flags[static_cast<unsigned>(GameplayFeature::phase)]=true;
    eject_hook(nullptr,nullptr,nullptr,nullptr,nullptr,&local,nullptr,false,nullptr);
    check(eject_calls==0,"Phase suppresses local horizontal block ejection");
    eject_hook(nullptr,nullptr,nullptr,nullptr,nullptr,&other,nullptr,false,nullptr);
    check(eject_calls==1,"other entities retain native ejection");
    flags[static_cast<unsigned>(GameplayFeature::phase)]=false;
    eject_hook(nullptr,nullptr,nullptr,nullptr,nullptr,&local,nullptr,false,nullptr);
    check(eject_calls==2,"disabling Phase restores native ejection");

    static_assert(sizeof(GameString)==32);
    static_assert(sizeof(OptionalString)==40);
    GameplayModule jump(GameplayFeature::airjump),phase(GameplayFeature::phase),esp(GameplayFeature::esp);
    pending_keys=0;
    jump.on_key_down(VK_SPACE); // key may already be released before the tick
    check((pending_keys.exchange(0)&16)!=0,"short airborne jump survives until tick");
    check(pending_keys.exchange(0)==0,"short press consumed once");
    phase.on_key_down('W');phase.on_key_down('D');
    check(pending_keys.exchange(0)==9,"diagonal movement preserves both taps");
    phase.on_key_down('T');esp.on_key_down(VK_SPACE);
    check(pending_keys.exchange(0)==0,"unrelated keys and nonmovement modules do not queue movement");
    GameString short_text("0 -53 3"),long_text("Death position: 0, -53, 3");
    check(foreign_string(&short_text)=="0 -53 3","release inline string ABI");
    check(foreign_string(&long_text)=="Death position: 0, -53, 3","release external string ABI");
    long_text.size=257;check(foreign_string(&long_text).empty(),"reject corrupt native string lengths");
    esp.set_boolean_setting(true);check(esp.boolean_setting(),"ESP player filter setting");
    GameplayModule storage(GameplayFeature::chest_esp);
    utility::Module& storage_settings=storage;
    check(storage.name()=="ChestESP"&&storage.category()==utility::ModuleCategory::visual,"ChestESP in Visual");
    check(storage_settings.boolean_setting_count()==5,"all five storage settings exposed");
    for(unsigned i=0;i<5;++i){
        check(storage_settings.boolean_setting(i),"container types enabled by default");
        storage_settings.set_boolean_setting(i,false);
        for(unsigned j=0;j<5;++j)check(storage_settings.boolean_setting(j)==(j!=i),"each storage toggle independent");
        storage_settings.set_boolean_setting(i,true);
    }
    storage_settings.set_boolean_setting(99,false);
    check(storage_types==31&&!storage_settings.boolean_setting(99)&&storage_settings.boolean_setting_name(99).empty(),"invalid setting index ignored");
    utility::Module& entity_settings=esp;
    check(entity_settings.boolean_setting_count()==1&&entity_settings.boolean_setting_name(0)=="Players only","legacy ESP setting adapter");
    entity_settings.set_boolean_setting(0,false);check(!esp.boolean_setting(),"legacy ESP indexed toggle");
    using chest_esp::Kind;
    check(chest_esp::classify("minecraft:barrel")==Kind::barrel,"barrel classification");
    check(chest_esp::classify("minecraft:chest")==Kind::chest,"chest classification");
    check(chest_esp::classify("minecraft:trapped_chest")==Kind::trapped,"trapped chest classification");
    for(auto stage:{"","exposed_","weathered_","oxidized_"})for(auto wax:{"","waxed_"})
        check(chest_esp::classify(std::string("minecraft:")+wax+stage+"copper_chest")==Kind::copper,"every copper variant");
    for(auto color:{"undyed","white","orange","magenta","light_blue","yellow","lime","pink","gray","light_gray","cyan","purple","blue","brown","green","red","black"})
        check(chest_esp::classify(std::string("minecraft:")+color+"_shulker_box")==Kind::shulker,"every shulker variant");
    for(auto unknown:{"minecraft:ender_chest","minecraft:chest_boat","minecraft:fake_shulker_box","custom:chest","minecraft:stone",""})
        check(chest_esp::classify(unknown)==Kind::count,"unrequested block types excluded");
    check(chest_esp::belongs_to_chunk({-1,-60,-17},-1,-2,-64,320,0x40F0F),"negative chunk coordinates and packed position");
    check(!chest_esp::belongs_to_chunk({-1,-60,-17},0,-2,-64,320,0x40F0F),"wrong owning chunk rejected");
    check(!chest_esp::belongs_to_chunk({-1,-59,-17},-1,-2,-64,320,0x40F0F),"stale block actor position rejected");
    check(!chest_esp::belongs_to_chunk({-1,320,-17},-1,-2,-64,320,0x1800F0F),"height outside dimension rejected");
    std::puts("PASS: short input retention, one-shot consumption, diagonal input, native string ABI, ESP setting");
}
