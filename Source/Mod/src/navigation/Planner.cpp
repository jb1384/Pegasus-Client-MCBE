#include "Navigation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>

namespace utility::navigation {
std::size_t PosHash::operator()(BlockPos p) const noexcept {
    auto mix=[](std::uint64_t v) { v ^= v >> 30; v *= 0xbf58476d1ce4e5b9ULL;
        v ^= v >> 27; v *= 0x94d049bb133111ebULL; return v ^ (v >> 31); };
    return static_cast<std::size_t>(mix(static_cast<std::uint32_t>(p.x)) ^
        mix(static_cast<std::uint32_t>(p.y) + 0x9e3779b97f4a7c15ULL) ^
        mix(static_cast<std::uint32_t>(p.z) + 0x517cc1b727220a95ULL));
}
BlockPos block_at(Vec3 p) { return {static_cast<int>(std::floor(p.x)),
    static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))}; }
double distance(Vec3 a, Vec3 b) { return std::hypot(std::hypot(a.x-b.x,a.z-b.z),a.y-b.y); }
const Cell& World::at(BlockPos p) const {
    static const Cell unknown;
    const auto it=cells.find(p); return it == cells.end() ? (parent?parent->at(p):unknown) : it->second;
}
namespace {
constexpr std::array<BlockPos,6> neighbors{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
BlockPos add(BlockPos a,BlockPos b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
bool contains(const std::vector<BlockPos>& values, BlockPos p) {
    return std::find(values.begin(),values.end(),p)!=values.end();
}
bool overlaps(const Bounds& a,const Bounds& b) {
    constexpr double e=0.001;
    return a.min.x < b.max.x-e && a.max.x > b.min.x+e &&
        a.min.y < b.max.y-e && a.max.y > b.min.y+e &&
        a.min.z < b.max.z-e && a.max.z > b.min.z+e;
}
Bounds translated(Bounds b, BlockPos p) {
    b.min.x+=p.x;b.max.x+=p.x;b.min.y+=p.y;b.max.y+=p.y;b.min.z+=p.z;b.max.z+=p.z;return b;
}
Bounds body(Vec3 p) { return {{p.x-0.3,p.y+0.001,p.z-0.3},{p.x+0.3,p.y+1.8,p.z+0.3}}; }
std::vector<BlockPos> intersected(Vec3 p) {
    const auto box=body(p); const auto lo=block_at(box.min);
    const auto hi=block_at({box.max.x-0.001,box.max.y-0.001,box.max.z-0.001});
    std::vector<BlockPos> result;
    for(int x=lo.x;x<=hi.x;++x)for(int y=lo.y;y<=hi.y;++y)for(int z=lo.z;z<=hi.z;++z)
        result.push_back({x,y,z});
    return result;
}
bool collides(const Cell& c, BlockPos p, Vec3 feet) {
    return std::any_of(c.collision.begin(),c.collision.end(),[&](const Bounds& box){
        return overlaps(body(feet),translated(box,p)); });
}
bool supported(const World& w,Vec3 feet) {
    auto p=block_at({feet.x,feet.y-0.01,feet.z}); const auto& c=w.at(p);
    if(!c.known || c.hazard || c.liquid || c.falling) return false;
    const Bounds sole{{feet.x-0.29,feet.y-0.04,feet.z-0.29},{feet.x+0.29,feet.y+0.001,feet.z+0.29}};
    return std::any_of(c.collision.begin(),c.collision.end(),[&](const Bounds& b){
        const auto box=translated(b,p);
        return std::abs(box.max.y-feet.y)<0.02 && overlaps(sole,box);
    });
}
// Nodes retain half-block standing heights, including slabs and stair landings.
struct NodeKey {
    int x{}, half_y{}, z{}, blocks{};
    std::uint64_t changes{};
    unsigned edits{};
    bool operator==(const NodeKey&) const = default;
};
struct NodeHash {
    std::size_t operator()(const NodeKey& p) const noexcept {
        return PosHash{}({p.x,p.half_y,p.z}) ^ (static_cast<std::size_t>(p.blocks)*0x9e3779b9) ^
            static_cast<std::size_t>(p.changes);
    }
};
Vec3 feet(NodeKey p) { return {p.x+0.5,p.half_y*0.5,p.z+0.5}; }
double heuristic(Vec3 p,const Goal& goal) {
    const auto to=[&](BlockPos b) { return distance(p,{b.x+0.5,static_cast<double>(b.y),b.z+0.5}); };
    if(goal.kind==GoalKind::xz) return std::hypot(p.x-goal.position.x-0.5,p.z-goal.position.z-0.5)/5.612;
    if(goal.kind==GoalKind::mine) {
        double best=std::numeric_limits<double>::infinity();
        for(auto t:goal.targets) best=std::min(best,std::max(0.0,to(t)-2.5));
        return best/5.612;
    }
    return std::max(0.0,to(goal.position)-(goal.kind==GoalKind::near?goal.radius:0.0))/5.612;
}
bool reached(Vec3 p,const Goal& goal,const World& w,std::optional<BlockPos>& target) {
    if(goal.kind==GoalKind::mine) {
        // Adjacent stand positions guarantee an exposed reachable face; the native
        // adapter performs the final eye raycast before holding destroy.
        for(auto t:goal.targets) {
            const double dx=std::abs(p.x-t.x-0.5),dz=std::abs(p.z-t.z-0.5);
            if(dx+dz<=1.01 && !(dx+dz<0.1 && p.y>t.y) && std::abs(p.y-t.y)<=1.01 && safe_to_break(w,t) &&
                w.at(t).harvestable && w.at(t).durability>1) { target=t;return true; }
        }
        return false;
    }
    if(goal.kind==GoalKind::near)
        return distance(p,{goal.position.x+0.5,static_cast<double>(goal.position.y),goal.position.z+0.5})<=goal.radius;
    return block_at(p).x==goal.position.x && block_at(p).z==goal.position.z &&
        (goal.kind==GoalKind::xz || std::abs(p.y-goal.position.y)<0.1);
}
bool prepare(const World& w, Vec3 p, Step& step,const Settings& s) {
    for(auto b:intersected(p)) {
        const auto& c=w.at(b);
        if(!c.known || c.hazard) return false;
        if(c.liquid && !(c.water && s.swim_verified))return false;
        if(!collides(c,b,p) || contains(step.breaking,b) || contains(step.opening,b)) continue;
        if(c.openable) {
            if(c.opened)return false;
            if(!contains(step.opening,b))step.opening.push_back(b);
        } else {
            if(!s.allow_break || !safe_to_break(w,b) || !c.harvestable || c.durability<=1) return false;
            if(!contains(step.breaking,b))step.breaking.push_back(b);
        }
    }
    return true;
}
bool sweep(const World& w,Step& step,const Settings& s) {
    const double span=distance(step.from,step.to);
    const int samples=std::max(4,static_cast<int>(std::ceil(span*8)));
    for(int i=0;i<=samples;++i) {
        const double t=static_cast<double>(i)/samples;
        Vec3 p{step.from.x+(step.to.x-step.from.x)*t,step.from.y+(step.to.y-step.from.y)*t,
               step.from.z+(step.to.z-step.from.z)*t};
        if(step.kind==MoveKind::jump || step.kind==MoveKind::ascend || step.kind==MoveKind::pillar)
            p.y+=std::sin(t*3.141592653589793)*1.05;
        // Descending must leave the ledge before lowering the body.
        if(step.kind==MoveKind::descend && t<0.8) p.y=step.from.y;
        if(!prepare(w,p,step,s))return false;
    }
    // Never dig out the starting or landing support to clear a swept body.
    const auto start_floor=block_at({step.from.x,step.from.y-0.01,step.from.z});
    const auto end_floor=block_at({step.to.x,step.to.y-0.01,step.to.z});
    return !contains(step.breaking,start_floor) && !contains(step.breaking,end_floor);
}
bool placement_supported(const World& w,BlockPos p) {
    for(auto d:neighbors) {
        const auto& c=w.at(add(p,d));
        if(c.known && !c.hazard && !c.liquid && !c.falling && !c.collision.empty())return true;
    }
    return false;
}
std::vector<Step> movements(const SearchRequest& r,NodeKey key) {
    std::vector<Step> result;
    const auto& w=*r.world;const auto& s=r.settings;const auto from=feet(key);
    const auto push=[&](Step step) {
        if(!sweep(w,step,s))return;
        step.seconds=distance(step.from,step.to)/(s.allow_sprint?5.612:4.317);
        if(step.kind!=MoveKind::walk)step.seconds+=0.35;
        for(auto b:step.breaking)step.seconds+=w.at(b).break_seconds;
        step.seconds+=step.placing.size()*1.0+step.opening.size()*0.25;
        if(std::isfinite(step.seconds) && step.seconds>0)result.push_back(std::move(step));
    };
    for(int dx=-1;dx<=1;++dx)for(int dz=-1;dz<=1;++dz) {
        if(!dx&&!dz)continue;
        const bool diagonal=dx&&dz;
        for(int dh=2;dh>=-s.max_fall*2;--dh) {
            Vec3 to{from.x+dx,from.y+dh*0.5,from.z+dz};
            if(!supported(w,to))continue;
            Step step{from,to,dh>0?MoveKind::ascend:(dh<0?MoveKind::descend:MoveKind::walk)};
            if(diagonal) {
                // Both corner corridors must be clear before a diagonal is usable.
                if(!body_clear(w,{from.x+dx,std::max(from.y,to.y),from.z}) ||
                   !body_clear(w,{from.x,std::max(from.y,to.y),from.z+dz}))continue;
            }
            push(std::move(step));
        }
        if(diagonal)continue;
        Vec3 to{from.x+dx,from.y,from.z+dz};
        const auto under=block_at({to.x,to.y-0.01,to.z});
        if(s.allow_place && s.place_verified && key.blocks>0 &&
            std::abs(from.y-std::round(from.y))<0.01 && w.at(under).air() &&
            placement_supported(w,under) && body_clear(w,to)) {
            Step step{from,to,MoveKind::bridge};step.placing.push_back(under);push(std::move(step));
        }
        const auto water_pos=block_at(to);
        if(s.swim_verified && w.at(water_pos).water && w.at(add(water_pos,{0,1,0})).air())
            push({from,to,MoveKind::swim});
        if(s.allow_parkour && s.parkour_verified && s.allow_sprint && supported(w,from)) {
            for(int length=2;length<=4;++length) {
                Vec3 landing{from.x+dx*length,from.y,from.z+dz*length};
                if(!supported(w,landing))continue;
                Step jump{from,landing,MoveKind::jump};
                Settings no_break=s;no_break.allow_break=false;
                if(sweep(w,jump,no_break) && jump.opening.empty())push(std::move(jump));
            }
        }
    }
    if(s.climb_verified && w.at(block_at(from)).climbable) {
        for(int dy:{-1,1}) {
            Vec3 to{from.x,from.y+dy,from.z};
            if(w.at(block_at(to)).climbable || supported(w,to))push({from,to,MoveKind::climb});
        }
    }
    if(s.allow_place && s.place_verified && key.blocks>0 && supported(w,from) &&
        w.at(block_at(from)).air() && std::abs(from.y-std::round(from.y))<0.01) {
        Step step{from,{from.x,from.y+1,from.z},MoveKind::pillar};
        step.placing.push_back(block_at(from));push(std::move(step));
    }
    return result;
}
} // namespace

bool safe_to_break(const World& w,BlockPos p) {
    const auto& c=w.at(p);
    if(!c.known || !c.breakable || c.hazard || c.liquid || c.openable ||
       !std::isfinite(c.break_seconds) || c.break_seconds<=0)return false;
    for(auto d:neighbors) {
        const auto& n=w.at(add(p,d));
        if(!n.known || n.liquid || n.hazard)return false;
    }
    // Falling columns are conservatively rejected until removed top-down.
    return !w.at(add(p,{0,1,0})).falling;
}
bool body_clear(const World& w,Vec3 feet,const std::vector<BlockPos>& ignored) {
    for(auto p:intersected(feet)) {
        const auto& c=w.at(p);
        if(!c.known || c.hazard || c.liquid)return false;
        if(!contains(ignored,p) && collides(c,p,feet))return false;
    }
    return true;
}
bool step_still_valid(const World& w,const Step& step,const Settings& settings) {
    if(step.kind==MoveKind::jump&&(!settings.allow_parkour||!settings.parkour_verified||!settings.allow_sprint))return false;
    if(step.kind==MoveKind::swim&&!settings.swim_verified)return false;
    if(step.kind==MoveKind::climb&&!settings.climb_verified)return false;
    if((step.kind==MoveKind::bridge||step.kind==MoveKind::pillar)&&!settings.place_verified)return false;
    Step fresh;fresh.from=step.from;fresh.to=step.to;fresh.kind=step.kind;
    if(!sweep(w,fresh,settings))return false;
    for(auto p:fresh.breaking)if(!contains(step.breaking,p))return false;
    for(auto p:fresh.opening)if(!contains(step.opening,p))return false;
    if(supported(w,step.to))return true;
    if(step.kind==MoveKind::swim) {
        const auto p=block_at(step.to);return w.at(p).water&&w.at(add(p,{0,1,0})).air();
    }
    if(step.kind==MoveKind::climb)return w.at(block_at(step.to)).climbable;
    const auto under=block_at({step.to.x,step.to.y-0.01,step.to.z});
    return settings.allow_place&&contains(step.placing,under)&&w.at(under).air()&&placement_supported(w,under);
}
SearchResult search(const SearchRequest& r,const std::function<bool()>& cancelled) {
    SearchResult out;out.generation=r.generation;
    if(!r.world || (r.goal.kind==GoalKind::mine && r.goal.targets.empty()))return out;
    if(!std::isfinite(r.start.x)||!std::isfinite(r.start.y)||!std::isfinite(r.start.z)||
       std::abs(r.start.x)>30000000||std::abs(r.start.y)>30000000||std::abs(r.start.z)>30000000)return out;
    out.session=r.world->session;out.revision=r.world->revision;
    const auto begin=std::chrono::steady_clock::now();
    struct Node { NodeKey key;double g{};std::size_t parent{};Step step;std::shared_ptr<const World> world; };
    struct Queue { double f{};std::size_t index{};bool operator<(const Queue& b)const {return f>b.f;} };
    std::vector<Node> nodes;
    std::priority_queue<Queue> open;
    std::unordered_map<NodeKey,double,NodeHash> best;
    NodeKey start{static_cast<int>(std::floor(r.start.x)),static_cast<int>(std::round(r.start.y*2)),
        static_cast<int>(std::floor(r.start.z)),std::clamp(r.placement_count,0,64)};
    nodes.push_back({start,0,0,{},r.world});best[start]=0;open.push({heuristic(feet(start),r.goal),0});
    std::size_t chosen=0;double best_progress=heuristic(feet(start),r.goal);
    while(!open.empty()) {
        if(cancelled && cancelled()){out.status=SearchStatus::cancelled;break;}
        if(out.expanded>=r.max_nodes || std::chrono::steady_clock::now()-begin>=r.timeout)break;
        const auto index=open.top().index;open.pop(); const auto node=nodes[index];
        if(node.g>best[node.key]+0.0005)continue;
        ++out.expanded;
        std::optional<BlockPos> target;
        if(reached(feet(node.key),r.goal,*node.world,target)) {
            chosen=index;out.target=target;out.status=SearchStatus::complete;break;
        }
        const auto h=heuristic(feet(node.key),r.goal);
        if(h<best_progress && distance(feet(start),feet(node.key))>=4) {best_progress=h;chosen=index;}
        auto local=r;local.world=node.world;
        for(auto step:movements(local,node.key)) {
            if(nodes.size()>=r.max_nodes)break;
            NodeKey next{static_cast<int>(std::floor(step.to.x)),static_cast<int>(std::round(step.to.y*2)),
                static_cast<int>(std::floor(step.to.z)),node.key.blocks-static_cast<int>(step.placing.size()),
                node.key.changes,node.key.edits};
            std::shared_ptr<const World> future=node.world;
            if(!step.breaking.empty()||!step.placing.empty()||!step.opening.empty()) {
                next.edits+=static_cast<unsigned>(step.breaking.size()+step.placing.size()+step.opening.size());
                if(next.edits>64)continue;
                auto edits=std::make_shared<World>();edits->parent=node.world;
                edits->session=r.world->session;edits->revision=r.world->revision;
                const auto changed=[&](BlockPos p,unsigned kind){
                    next.changes^=PosHash{}(p)+kind+0x9e3779b97f4a7c15ULL+(next.changes<<6)+(next.changes>>2);
                };
                for(auto p:step.breaking){Cell c;c.id="minecraft:air";c.known=true;edits->cells[p]=c;changed(p,1);}
                for(auto p:step.placing){Cell c;c.id="minecraft:cobblestone";c.known=true;
                    c.collision={{{0,0,0},{1,1,1}}};edits->cells[p]=c;changed(p,2);}
                for(auto p:step.opening){auto c=node.world->at(p);c.collision.clear();c.opened=true;edits->cells[p]=c;changed(p,3);}
                future=std::move(edits);
            }
            const double cost=node.g+step.seconds;
            const auto it=best.find(next);if(it!=best.end() && it->second<=cost+0.0005)continue;
            best[next]=cost;nodes.push_back({next,cost,index,std::move(step),std::move(future)});
            open.push({cost+heuristic(feet(next),r.goal),nodes.size()-1});
        }
    }
    if(out.status!=SearchStatus::cancelled) {
        // A partial route is useful both at search budgets and at observed frontiers.
        if(out.status!=SearchStatus::complete && chosen!=0)out.status=SearchStatus::partial;
        if(out.status==SearchStatus::complete || out.status==SearchStatus::partial) {
            for(auto i=chosen;i!=0;i=nodes[i].parent)out.steps.push_back(nodes[i].step);
            std::reverse(out.steps.begin(),out.steps.end());
        }
    }
    out.milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    return out;
}

SearchWorker::SearchWorker() : thread_([this](std::stop_token stop) {
    while(!stop.stop_requested()) {
        SearchRequest request;std::uint64_t ticket{};
        {
            std::unique_lock lock(mutex_);
            if(!changed_.wait(lock,stop,[&]{return pending_.has_value();}))return;
            request=std::move(*pending_);pending_.reset();ticket=revision_.load();
        }
        auto answer=search(request,[&]{return stop.stop_requested()||revision_.load()!=ticket;});
        std::lock_guard lock(mutex_);
        if(revision_.load()==ticket && !stop.stop_requested())result_=std::move(answer);
    }
}) {}
SearchWorker::~SearchWorker(){thread_.request_stop();changed_.notify_all();}
void SearchWorker::submit(SearchRequest request) {
    std::lock_guard lock(mutex_);++revision_;result_.reset();pending_=std::move(request);changed_.notify_all();
}
void SearchWorker::cancel(){std::lock_guard lock(mutex_);++revision_;pending_.reset();result_.reset();}
std::optional<SearchResult> SearchWorker::take(){std::lock_guard lock(mutex_);auto r=std::move(result_);result_.reset();return r;}

void WorldCache::observe(const World& world) {
    if(session_!=world.session){clear();session_=world.session;}
    ++clock_;
    for(const auto& [p,c]:world.cells)if(c.known)cells_[p]={c,clock_};
    if(cells_.size()<=maximum_)return;
    std::vector<std::pair<BlockPos,std::uint64_t>> ages;ages.reserve(cells_.size());
    for(const auto& [p,e]:cells_)ages.emplace_back(p,e.age);
    std::sort(ages.begin(),ages.end(),[](const auto& a,const auto& b){
        if(a.second!=b.second)return a.second<b.second;
        if(a.first.x!=b.first.x)return a.first.x<b.first.x;
        if(a.first.y!=b.first.y)return a.first.y<b.first.y;
        return a.first.z<b.first.z;
    });
    for(std::size_t i=0,n=cells_.size()-maximum_;i<n;++i)cells_.erase(ages[i].first);
}
std::vector<BlockPos> WorldCache::find(const std::vector<std::string>& ids) const {
    std::vector<BlockPos> result;
    for(const auto& [p,e]:cells_)if(std::find(ids.begin(),ids.end(),e.cell.id)!=ids.end())result.push_back(p);
    std::sort(result.begin(),result.end(),[](BlockPos a,BlockPos b){
        if(a.x!=b.x)return a.x<b.x;if(a.y!=b.y)return a.y<b.y;return a.z<b.z;});
    return result;
}
void WorldCache::clear(){cells_.clear();session_=0;clock_=0;}
} // namespace utility::navigation
