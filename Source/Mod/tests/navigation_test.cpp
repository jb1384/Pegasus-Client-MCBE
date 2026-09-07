#include "../src/navigation/Navigation.hpp"
#include "../src/navigation/InventoryPolicy.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace utility::navigation;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
Cell air(){Cell c;c.known=true;c.id="minecraft:air";return c;}
Cell solid(std::string id="minecraft:stone",double height=1) {
    Cell c;c.known=true;c.id=std::move(id);c.collision={{{0,0,0},{1,height,1}}};
    c.breakable=c.harvestable=true;c.break_seconds=1;c.tool_slot=0;c.durability=100;return c;
}
std::shared_ptr<World> flat(int radius=9) {
    auto w=std::make_shared<World>();w->session=1;w->revision=1;
    for(int x=-radius;x<=radius;++x)for(int z=-radius;z<=radius;++z)for(int y=-4;y<=6;++y)
        w->cells[{x,y,z}]=y<0?solid():air();
    return w;
}
SearchRequest route(std::shared_ptr<World> w,BlockPos to) {
    SearchRequest r;r.world=std::move(w);r.start={0.5,0,0.5};r.goal.position=to;
    r.generation=3;r.timeout=std::chrono::milliseconds(2000);return r;
}
Frame frame(std::shared_ptr<World> w) {
    Frame f;f.world=std::move(w);f.feet={0.5,0,0.5};f.alive=f.connected=f.control=f.grounded=true;
    f.inventory.health=f.inventory.hunger=20;return f;
}
Command go(int x,int z) {Command c;c.kind=CommandKind::go;c.x.value=x;c.z.value=z;return c;}
struct Simulation final:Actions {
    Frame* f{};std::shared_ptr<World> w;
    bool reject{}, accept_move{true};int releases{}, moves{}, mines{}, placements{}, meals{};
    bool move(const Step& step,bool) override {++moves;if(reject)return false;if(accept_move)f->feet=step.to;return true;}
    bool mine(BlockPos p,int) override {++mines;if(reject)return false;w->cells[p]=air();++w->revision;return true;}
    bool place(BlockPos p,int) override {++placements;if(reject)return false;w->cells[p]=solid();++w->revision;return true;}
    bool open(BlockPos p) override {if(reject)return false;w->cells[p].collision.clear();w->cells[p].opened=true;return true;}
    bool eat(int) override {++meals;if(reject)return false;f->inventory.hunger=20;f->inventory.using_item=false;return true;}
    void release() noexcept override {++releases;}
};
void drive(Controller& c,Frame& f,Settings& s,Simulation& a,int iterations=150) {
    for(int i=0;i<iterations;++i) {
        c.tick(f,s,a,i*0.05);
        const auto state=c.status().state;
        if(state==JobState::complete||state==JobState::paused||state==JobState::idle)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
void parser_tests() {
    auto p=parse_command({"goto","~-2","~","+9"});require(p.command&&p.command->has_y,"relative XYZ parse");
    auto pos=resolve(*p.command,{-0.2,63.9,-4.1});require(pos&&*pos==BlockPos{-3,63,9},"negative coordinates must floor");
    require(!parse_command({"goto","1.2","4"}).command,"fractional coordinate accepted");
    require(!parse_command({"goto","2147483648","4"}).command,"overflow coordinate accepted");
    require(!parse_command({"goto","~+-2","4"}).command,"malformed relative coordinate accepted");
    require(!parse_command({"mine","0","stone"}).command,"zero quantity accepted");
    require(!parse_command({"mine","8"}).command,"missing block accepted");
    require(!parse_command({"mine","minecraft::ore"}).command,"invalid identifier accepted");
    p=parse_command({"mine","64","diamond_ore","minecraft:deepslate_diamond_ore","diamond_ore"});
    require(p.command&&p.command->quantity==64&&p.command->blocks.size()==2,"mine normalization");
    require(p.command->blocks[0]=="minecraft:diamond_ore","ore identity expanded incorrectly");
    require(!parse_command({"pause","extra"}).command,"pause arity");
    require(!parse_command({"something"}).handled,"unknown command stolen");
}
void planner_tests() {
    auto w=flat();auto r=route(w,{6,0,0});auto result=search(r);
    require(result.status==SearchStatus::complete&&!result.steps.empty(),"flat route failed");
    require(result.generation==3&&result.session==1,"search identity missing");
    require(result.steps.back().to.x==6.5,"wrong goal");
    std::cout<<"flat route: "<<result.expanded<<" nodes, "<<result.milliseconds<<" ms\n";
    w->cells[{2,0,0}]=solid();w->cells[{2,1,0}]=solid();w->cells[{2,2,0}]=solid();
    r.settings.allow_break=false;result=search(r);
    require(result.status==SearchStatus::complete,"wall detour failed");
    for(const auto& step:result.steps)require(step.breaking.empty(),"allowBreak=false ignored");
    auto liquid=air();liquid.id="minecraft:lava";liquid.liquid=liquid.hazard=true;
    w->cells[{1,0,0}]=liquid;result=search(r);
    require(result.status==SearchStatus::complete,"lava detour failed");
    for(const auto& step:result.steps)require(block_at(step.to)!=BlockPos{1,0,0},"route entered lava");
    require(!safe_to_break(*w,{2,0,0}),"liquid adjacency accepted for digging");
    w=flat();r=route(w,{3,0,0});
    r.world=w;w->cells[{1,0,0}]=solid("minecraft:slab",0.5);
    result=search(r);require(result.status==SearchStatus::complete,"slab route failed");
    require(body_clear(*w,{1.5,0.5,0.5}),"half-height standing collision");
    require(!body_clear(*w,{1.5,0,0.5}),"slab collision ignored");
    auto gravel=solid("minecraft:gravel");gravel.falling=true;
    w->cells[{3,0,0}]=solid();w->cells[{3,1,0}]=gravel;
    require(!safe_to_break(*w,{3,0,0}),"falling block trap accepted");
    w->cells.erase({2,0,0});require(!body_clear(*w,{2.5,0,0.5}),"unloaded cell treated as air");
    r=route(flat(),{100,0,0});result=search(r);
    require(result.status==SearchStatus::partial,"distant goal should return observed segment");
    for(const auto& step:result.steps)require(r.world->at(block_at(step.to)).known,"route entered unknown terrain");
    r.max_nodes=0;require(search(r).steps.empty(),"zero budget must not fabricate path");
    require(search(r,[]{return true;}).status==SearchStatus::cancelled,"search cancellation");
    auto invalid_start=r;invalid_start.start.x=NAN;require(search(invalid_start).steps.empty(),"nonfinite start accepted");
    w=flat();r=route(w,{0,0,0});r.goal.kind=GoalKind::mine;r.goal.targets={{3,0,0}};
    w->cells[{3,0,0}]=solid("minecraft:diamond_ore");
    result=search(r);require(result.status==SearchStatus::complete&&result.target==BlockPos{3,0,0},"mine goal failed");
    w->cells[{3,0,0}].durability=1;result=search(r);
    require(result.status!=SearchStatus::complete,"exhausted tool can satisfy mine goal");
    // A bridge must consume a block; without it the destination remains inaccessible.
    w=flat(5);
    for(int x=-5;x<=5;++x)for(int z=-5;z<=5;++z)for(int y=-4;y<0;++y)w->cells[{x,y,z}]=air();
    w->cells[{0,-1,0}]=solid();w->cells[{2,-1,0}]=solid();
    r=route(w,{2,0,0});r.settings.allow_break=false;r.settings.place_verified=true;
    result=search(r);require(result.status!=SearchStatus::complete,"bridge without blocks");
    r.placement_count=1;result=search(r);require(result.status==SearchStatus::complete,"bridge with one block failed");
    int used{};for(const auto& step:result.steps)used+=static_cast<int>(step.placing.size());require(used==1,"bridge resource count");
    r.placement_count=0;r.settings.parkour_verified=true;
    result=search(r);require(result.status==SearchStatus::complete,"verified gap jump failed");
    // The search must carry earlier placements into later edges, not require
    // every bridge block to be adjacent to the original world.
    w=flat(6);
    for(int x=-6;x<=6;++x)for(int z=-6;z<=6;++z)for(int y=-4;y<0;++y)w->cells[{x,y,z}]=air();
    w->cells[{0,-1,0}]=solid();w->cells[{4,-1,0}]=solid();
    r=route(w,{4,0,0});r.settings.allow_break=false;r.settings.place_verified=true;r.placement_count=3;
    result=search(r);require(result.status==SearchStatus::complete,"multi-block bridge lacks persistent edits");
    used=0;for(const auto& step:result.steps)used+=static_cast<int>(step.placing.size());require(used==3,"multi-block resource budget");
    require(w->at({1,-1,0}).air(),"planner mutated native snapshot");
    r=route(flat(),{3,0,0});result=search(r);
    auto changed=std::make_shared<World>(*r.world);
    require(step_still_valid(*changed,result.steps.front(),r.settings),"unchanged step rejected");
    const auto landing=block_at({result.steps.front().to.x,result.steps.front().to.y-0.01,result.steps.front().to.z});
    changed->cells[landing]=air();
    require(!step_still_valid(*changed,result.steps.front(),r.settings),"removed landing support not revalidated");
}
void worker_and_cache_tests() {
    SearchWorker worker;auto r=route(flat(),{8,0,8});r.generation=44;worker.submit(r);worker.cancel();
    r.generation=45;r.goal.position={0,0,0};worker.submit(r);
    std::optional<SearchResult> got;
    for(int i=0;i<100&&!got;++i){got=worker.take();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    require(got&&got->generation==45,"cancelled worker result escaped");
    WorldCache cache(2);World w;w.session=1;w.cells[{0,0,0}]=solid("minecraft:diamond_ore");cache.observe(w);
    require(cache.find({"minecraft:diamond_ore"}).size()==1,"buried target not cached");
    w.cells[{0,0,0}]=air();cache.observe(w);require(cache.find({"minecraft:diamond_ore"}).empty(),"removed ore cache stale");
    w.cells[{0,0,0}]=solid("minecraft:diamond_ore");cache.observe(w);w.cells.clear();w.session=2;cache.observe(w);
    require(cache.find({"minecraft:diamond_ore"}).empty(),"cross-world cache contamination");
}
void controller_tests() {
    Settings settings;auto w=flat();auto f=frame(w);Simulation a;a.f=&f;a.w=w;
    Controller c;
    auto known=[](const std::string& id){return id=="minecraft:diamond_ore";};
    f.terrain_ready=false;c.command(go(5,0),f,settings,known);
    for(int i=0;i<20;++i)c.tick(f,settings,a,i*0.05);
    require(c.status().state==JobState::planning&&a.moves==0&&c.status().message=="Waiting for nearby terrain","terrain warmup consumed retries or moved");
    f.terrain_ready=true;drive(c,f,settings,a);
    require(c.status().state==JobState::complete,"terrain readiness failed to resume planning");
    f.feet={0.5,0,0.5};f.terrain_ready=false;c.command(go(5,0),f,settings,known);
    c.tick(f,settings,a,100);c.tick(f,settings,a,115);
    require(c.status().state==JobState::paused,"missing terrain wait was unbounded");
    f.terrain_ready=true;
    c.command(go(5,0),f,settings,known);drive(c,f,settings,a);
    require(c.status().state==JobState::complete&&block_at(f.feet).x==5,"navigation lifecycle failed");
    f.feet={0.5,0,0.5};c.command(go(5,0),f,settings,known);
    const auto generation=c.status().generation;
    Command invalid;invalid.kind=CommandKind::mine;invalid.blocks={"minecraft:not_a_block"};
    c.command(invalid,f,settings,known);require(c.status().generation==generation,"invalid command replaced job");
    f.manual_input=true;c.tick(f,settings,a,0);require(c.status().state==JobState::paused&&!c.owns_controls(),"manual override failed");
    f.manual_input=false;Command resume;resume.kind=CommandKind::resume;c.command(resume,f,settings,known);drive(c,f,settings,a);
    require(c.status().state==JobState::complete,"resume failed");
    c.command(go(1,0),f,settings,known);f.control=false;c.tick(f,settings,a,1);
    require(c.status().state==JobState::paused,"focus loss failed");f.control=true;
    c.command(resume,f,settings,known);f.inventory.health=8;c.tick(f,settings,a,2);
    require(c.status().state==JobState::paused,"low health ignored");f.inventory.health=20;
    c.command(resume,f,settings,known);f.inventory.hunger=6;c.tick(f,settings,a,3);
    require(c.status().state==JobState::paused,"starvation ignored");
    f.inventory.hunger=14;f.inventory.food_slot=8;c.command(resume,f,settings,known);c.tick(f,settings,a,4);
    require(a.meals==1&&c.status().state==JobState::eating,"automatic eating not triggered");
    c.tick(f,settings,a,4.05);require(c.status().state==JobState::planning,"eating completion not observed");
    c.stop();require(!c.owns_controls()&&c.status().route.empty(),"stop retained controls");
    Command mine;mine.kind=CommandKind::mine;mine.blocks={"minecraft:diamond_ore"};mine.quantity=4;
    f.inventory.matching_items=4;c.command(mine,f,settings,known);c.tick(f,settings,a,5);
    require(c.status().state==JobState::complete&&a.mines==0,"existing quantity must satisfy goal");
    f.inventory.matching_items=0;f.inventory.target_capacity=false;
    c.command(mine,f,settings,known);c.tick(f,settings,a,6);require(c.status().state==JobState::paused,"inventory full ignored");
    f.inventory.target_capacity=true;c.command(resume,f,settings,known);f.alive=false;c.tick(f,settings,a,7);
    require(c.status().state==JobState::idle&&!c.owns_controls(),"death did not cancel");
    f.alive=true;c.command(go(4,0),f,settings,known);++w->session;c.tick(f,settings,a,8);
    require(c.status().state==JobState::idle,"dimension transition did not cancel");
    c.command(go(4,0),f,settings,known);f.connected=false;c.tick(f,settings,a,9);
    require(c.status().state==JobState::idle,"disconnect did not cancel");
    // Successful break observation alone must not satisfy an item quantity.
    w=flat();f=frame(w);a.f=&f;a.w=w;
    w->cells[{1,0,0}]=solid("minecraft:diamond_ore");mine.quantity=1;
    c.command(mine,f,settings,known);
    for(int i=0;i<60&&a.mines==0;++i){c.tick(f,settings,a,i*0.05);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    require(a.mines>0,"mining action was never requested");
    c.tick(f,settings,a,4);require(c.status().state!=JobState::complete&&c.status().collected==0,"break attempt counted as drop");
    f.inventory.matching_items=1;c.tick(f,settings,a,4.1);require(c.status().state==JobState::complete,"confirmed inventory quantity ignored");
    // Server refusal must lead to a bounded pause, not endless movement attempts.
    f.feet={0.5,0,0.5};a.reject=true;c.command(go(4,0),f,settings,known);drive(c,f,settings,a,300);
    require(c.status().state==JobState::paused&&!c.owns_controls(),"rejected actions retried without bound");
}
void inventory_tests() {
    Stack diamond;diamond.id="minecraft:diamond";diamond.slot=0;diamond.count=5;diamond.maximum_count=64;
    Stack ore;ore.id="minecraft:diamond_ore";ore.slot=1;ore.count=2;ore.maximum_count=64;
    Stack deepslate=ore;deepslate.id="minecraft:deepslate_diamond_ore";deepslate.slot=2;deepslate.count=3;
    std::vector<DropRule> rules{{"minecraft:diamond_ore",{"minecraft:diamond","minecraft:diamond_ore"},true},
        {"minecraft:deepslate_diamond_ore",{"minecraft:diamond","minecraft:deepslate_diamond_ore"},true}};
    auto count=count_drops({"minecraft:diamond_ore","minecraft:deepslate_diamond_ore"},rules,{diamond,ore,deepslate});
    require(count.verified&&count.count==10&&count.capacity,"shared normal drops counted twice or Silk Touch ignored");
    require(count_drops({"minecraft:unknown"},rules,{diamond}).verified==false,"unknown drop mapping guessed");
    diamond.count=diamond.maximum_count;ore.count=ore.maximum_count;
    require(!count_drops({"minecraft:diamond_ore"},rules,{diamond,ore}).capacity,"full stacks accepted");
    Stack food;food.id="minecraft:bread";food.count=1;food.slot=4;food.nutrition=5;food.safe_food=true;
    Stack special=food;special.id="minecraft:golden_apple";special.slot=0;
    require(food_slot({food,special},14)==4,"special food selected");
    special.id="minecraft:chorus_fruit";require(!usable_food(special),"chorus fruit selected");
    special.id="minecraft:rotten_flesh";special.safe_food=false;require(!usable_food(special),"unsafe food selected");
    Stack pick;pick.id="minecraft:diamond_pickaxe";pick.slot=2;pick.count=1;pick.damageable=true;pick.durability=1;pick.can_harvest=true;pick.break_seconds=0.1;
    Stack slower=pick;slower.slot=3;slower.durability=5;slower.break_seconds=0.5;
    require(tool_slot({pick,slower})==3,"tool preservation failed");
    slower.can_harvest=false;require(tool_slot({pick,slower})==-1,"unsuitable tool selected");
    require(throwaway("minecraft:dirt")&&!throwaway("minecraft:diamond_block"),"valuable placement material selected");
}
}
int main(){try{parser_tests();planner_tests();worker_and_cache_tests();inventory_tests();controller_tests();
    std::cout<<"Navigation policy, planner, worker and lifecycle tests passed (simulated world only).\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
