#include "Navigation.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace utility::navigation {
namespace {
bool matching(const Command& job,const Cell& c) {
    return c.known && std::find(job.blocks.begin(),job.blocks.end(),c.id)!=job.blocks.end();
}
bool active(JobState state) {
    return state==JobState::planning||state==JobState::moving||state==JobState::mining||
        state==JobState::collecting||state==JobState::eating;
}
}
std::string Controller::command(const Command& command,const Frame& frame,const Settings&,
                              const std::function<bool(const std::string&)>& known_block) {
    switch(command.kind) {
    case CommandKind::status:return status_.message;
    case CommandKind::stop:stop();return "Stopped Baritone.";
    case CommandKind::pause:
        if(!job_)return "No active Baritone job.";
        pause("Paused by command; use ,resume.");return status_.message;
    case CommandKind::resume:
        if(!job_||status_.state!=JobState::paused)return "No paused Baritone job.";
        if(!frame.connected||!frame.alive||!frame.world||frame.world->session!=session_)
            return "Cannot resume outside the original live world.";
        status_.state=JobState::planning;status_.message="Resuming";
        ++generation_;status_.generation=generation_;retries_=0;return status_.message;
    default:break;
    }
    // All validation precedes mutation: bad commands cannot replace a valid job.
    if(!frame.connected||!frame.alive||!frame.world)return "Enter a world before starting Baritone.";
    Goal new_goal;
    if(command.kind==CommandKind::go) {
        const auto p=resolve(command,frame.feet);
        if(!p)return "Coordinates are invalid or outside the supported world range.";
        new_goal.kind=command.has_y?GoalKind::exact:GoalKind::xz;new_goal.position=*p;
    } else {
        for(const auto& id:command.blocks)if(!known_block(id))return "Unknown block: "+id;
        if(command.quantity>0&&!frame.inventory.drop_mapping_known)
            return "Cannot count these blocks' drops with the current item mapping.";
        new_goal.kind=GoalKind::mine;
    }
    stop();job_=command;goal_=std::move(new_goal);session_=frame.world->session;
    status_.state=JobState::planning;status_.message="Planning";status_.requested=command.quantity;
    if(command.kind==CommandKind::go)status_.goal=goal_.position;
    return command.kind==CommandKind::go?"Baritone navigation started.":"Baritone mining started.";
}
void Controller::stop(std::string reason) {
    worker_.cancel();searching_=false;++generation_;job_.reset();mine_target_.reset();action_target_.reset();
    blacklist_.clear();status_={};status_.generation=generation_;status_.message=std::move(reason);
    step_=0;retries_=0;action_started_=-1;eating_started_=-1;terrain_wait_started_=-1;
    next_segment_.reset();lookahead_=partial_route_=false;pending_goal_.reset();
}
void Controller::pause(std::string reason) {
    worker_.cancel();searching_=false;++generation_;status_.generation=generation_;
    status_.state=JobState::paused;status_.message=std::move(reason);status_.route.clear();
    step_=0;action_started_=-1;eating_started_=-1;terrain_wait_started_=-1;action_target_.reset();
    next_segment_.reset();lookahead_=partial_route_=false;
}
bool Controller::owns_controls() const noexcept { return active(status_.state); }
const std::vector<std::string>& Controller::mining_targets() const noexcept {
    static const std::vector<std::string> empty;
    return job_&&job_->kind==CommandKind::mine?job_->blocks:empty;
}
void Controller::replan(std::string reason) {
    worker_.cancel();searching_=false;++generation_;status_.generation=generation_;
    status_.route.clear();step_=0;action_started_=-1;action_target_.reset();
    next_segment_.reset();lookahead_=partial_route_=false;
    if(++retries_>4){pause("Repeated action failure: "+reason+" Use ,resume after resolving it.");return;}
    status_.state=JobState::planning;status_.message=std::move(reason);
}
void Controller::plan(const Frame& frame,const Settings& settings) {
    if(searching_)return;
    Goal next=goal_;
    if(job_->kind==CommandKind::mine) {
        next.kind=GoalKind::mine;next.targets.clear();
        auto candidates=cache_.find(job_->blocks);
        for(auto p:candidates) {
            const auto black=blacklist_.find(p);
            if(black!=blacklist_.end()&&black->second==frame.world->revision)continue;
            if(matching(*job_,frame.world->at(p)))next.targets.push_back(p);
        }
        std::sort(next.targets.begin(),next.targets.end(),[&](BlockPos a,BlockPos b){
            return distance(frame.feet,{a.x+0.5,static_cast<double>(a.y),a.z+0.5})<
                   distance(frame.feet,{b.x+0.5,static_cast<double>(b.y),b.z+0.5});
        });
        if(next.targets.size()>128)next.targets.resize(128);
        if(next.targets.empty()) {
            // Previously seen distant targets guide exploration, but actions still
            // require fresh cells. Otherwise extend a tunnel from current height.
            next.kind=GoalKind::xz;
            if(!candidates.empty()) {
                const auto nearest=std::min_element(candidates.begin(),candidates.end(),[&](BlockPos a,BlockPos b){
                    return distance(frame.feet,{a.x+0.5,static_cast<double>(a.y),a.z+0.5})<
                           distance(frame.feet,{b.x+0.5,static_cast<double>(b.y),b.z+0.5});});
                next.position=*nearest;
            } else {
                next.position=block_at(frame.feet);next.position.x+=32;
            }
            status_.message="Exploring for mining targets";
        } else status_.message="Planning mining route";
    }
    SearchRequest request{frame.world,frame.feet,next,settings,frame.inventory.placement_count,generation_};
    pending_goal_=next;
    worker_.submit(std::move(request));searching_=true;
}
bool Controller::survival(const Frame& frame,const Settings& settings,Actions& actions,double now) {
    const auto& inv=frame.inventory;
    if(inv.health<=8){pause("Low health (4 hearts or less); heal, then ,resume.");return false;}
    if(job_->kind==CommandKind::mine&&!inv.target_capacity){pause("No inventory capacity for target drops; empty space, then ,resume.");return false;}
    if(inv.hunger<=6&&(!settings.auto_eat||inv.food_slot<0)) {
        pause("Low hunger with no usable food; eat, then ,resume.");return false;
    }
    if(status_.state==JobState::eating) {
        if(inv.hunger>eating_hunger_&&!inv.using_item) {
            actions.release();status_.state=JobState::planning;status_.message="Replanning after eating";eating_started_=-1;return false;
        }
        if(now-eating_started_>8 || inv.food_slot<0 || !settings.auto_eat || !actions.eat(inv.food_slot)) {
            pause("Eating did not complete; check food, then ,resume.");return false;
        }
        return false;
    }
    if(settings.auto_eat&&inv.hunger<=14&&inv.food_slot>=0&&frame.grounded&&body_clear(*frame.world,frame.feet)) {
        actions.release();worker_.cancel();searching_=false;++generation_;status_.generation=generation_;
        next_segment_.reset();lookahead_=partial_route_=false;
        status_.route.clear();step_=0;action_started_=-1;
        eating_started_=now;eating_hunger_=inv.hunger;status_.state=JobState::eating;status_.message="Eating";
        if(!actions.eat(inv.food_slot))pause("Food use unavailable; eat, then ,resume.");
        return false;
    }
    return true;
}
void Controller::finish_segment(const Frame& frame) {
    status_.route.clear();step_=0;action_started_=-1;action_target_.reset();
    if(job_->kind==CommandKind::go) {
        const auto p=block_at(frame.feet);
        if(p.x==goal_.position.x&&p.z==goal_.position.z&&
           (goal_.kind==GoalKind::xz||std::abs(frame.feet.y-goal_.position.y)<0.15)) {
            status_.state=JobState::complete;status_.message="Destination reached";job_.reset();return;
        }
    }
    if(next_segment_) {
        auto next=std::move(*next_segment_);next_segment_.reset();
        if(next.generation==generation_&&next.session==session_&&
           (next.status==SearchStatus::complete||next.status==SearchStatus::partial)&&
           !next.steps.empty()&&distance(frame.feet,next.steps.front().from)<0.35) {
            status_.route=std::move(next.steps);partial_route_=next.status==SearchStatus::partial;
            mine_target_=next.target;status_.state=JobState::moving;status_.message="Following next segment";return;
        }
    }
    status_.state=mine_target_?JobState::mining:JobState::planning;
    status_.message=mine_target_?"Mining target":"Planning next segment";
}
bool Controller::execute(const Frame& frame,const Settings& settings,Actions& actions,double now) {
    if(step_>=status_.route.size()){actions.release();finish_segment(frame);return false;}
    const auto& step=status_.route[step_];
    if(distance(frame.feet,step.to)<0.25) {
        actions.release();++step_;retries_=0;action_started_=-1;action_target_.reset();return true;
    }
    if(distance(frame.feet,step.from)>distance(step.from,step.to)+2.0) {
        actions.release();replan("Player moved away from the route");return false;
    }
    if(!step_still_valid(*frame.world,step,settings)) {
        actions.release();replan("Route support, collision or movement settings changed");return false;
    }
    if(action_started_<0)action_started_=now;
    if(now-action_started_>std::max(8.0,step.seconds*3+3)) {
        actions.release();replan("Movement or interaction timed out");return false;
    }
    for(auto p:step.breaking) {
        const auto& c=frame.world->at(p);
        if(c.air())continue;
        if(!settings.allow_break||!safe_to_break(*frame.world,p)||!c.harvestable) {
            actions.release();replan("Breaking route is no longer safe");return false;
        }
        if(c.durability<=1){actions.release();pause("No suitable tool with durability remaining; replace it, then ,resume.");return false;}
        if(!actions.mine(p,c.tool_slot)){actions.release();replan("Mining action rejected");}
        return false;
    }
    for(auto p:step.opening) {
        if(frame.world->at(p).opened)continue;
        if(!actions.open(p)){actions.release();replan("Door or gate action rejected");}
        return false;
    }
    for(auto p:step.placing) {
        const auto& c=frame.world->at(p);
        if(c.known&&!c.collision.empty()&&!c.hazard&&!c.liquid)continue;
        if(!settings.allow_place||!settings.place_verified||!c.air()||frame.inventory.placement_count<=0) {
            actions.release();replan("Placement material or support unavailable");return false;
        }
        // Pillaring is a coordinated jump-and-place action; the adapter controls
        // the normal jump and submits placement only when the body has cleared it.
        if(step.kind==MoveKind::pillar&&!actions.move(step,false)) {
            actions.release();replan("Pillar jump rejected");return false;
        }
        if(!actions.place(p,frame.inventory.placement_slot)){actions.release();replan("Placement action rejected");}
        return false;
    }
    if(!body_clear(*frame.world,step.to) && step.kind!=MoveKind::swim && step.kind!=MoveKind::climb) {
        actions.release();replan("Route collision changed");return false;
    }
    if(!actions.move(step,settings.allow_sprint&&frame.inventory.hunger>6)) {
        actions.release();replan("Movement action rejected");return false;
    }
    status_.message="Following route";return true;
}
void Controller::tick(const Frame& frame,const Settings& settings,Actions& actions,double now) {
    if(!frame.connected||!frame.world) {stop("Disconnected");cache_.clear();actions.release();return;}
    if(!frame.alive || (job_&&frame.world->session!=session_)) {
        stop(frame.alive?"World or dimension changed":"Player died");cache_.clear();actions.release();return;
    }
    if(!job_||!active(status_.state)){actions.release();return;}
    if(!std::isfinite(frame.feet.x)||!std::isfinite(frame.feet.y)||!std::isfinite(frame.feet.z)||
       !std::isfinite(now)||std::abs(frame.feet.x)>30000000||std::abs(frame.feet.y)>30000000||std::abs(frame.feet.z)>30000000) {
        pause("Invalid player observation; use ,resume after it recovers.");actions.release();return;
    }
    if(!frame.control||frame.manual_input) {
        pause(frame.manual_input?"Manual input; use ,resume.":"Gameplay lost focus or an interface opened; use ,resume.");
        actions.release();return;
    }
    cache_.observe(*frame.world);
    status_.collected=frame.inventory.matching_items;
    if(job_->quantity>0 && !frame.inventory.drop_mapping_known) {
        pause("Drop mapping is unavailable; quantity cannot be verified.");actions.release();return;
    }
    if(job_->quantity>0&&status_.collected>=job_->quantity) {
        worker_.cancel();searching_=false;status_.state=JobState::complete;
        status_.message="Mining quantity reached: "+std::to_string(status_.collected);
        status_.route.clear();job_.reset();actions.release();return;
    }
    if(!survival(frame,settings,actions,now)){if(status_.state!=JobState::eating)actions.release();return;}
    if(status_.state==JobState::planning&&!frame.terrain_ready) {
        actions.release();
        if(terrain_wait_started_<0) {
            terrain_wait_started_=now;
            worker_.cancel();searching_=false;++generation_;status_.generation=generation_;
        }
        status_.message="Waiting for nearby terrain";
        if(now-terrain_wait_started_>=15)pause("Nearby terrain remained unavailable; use ,resume after it loads.");
        return;
    }
    terrain_wait_started_=-1;
    if(auto result=worker_.take()) {
        searching_=false;
        if(result->generation!=generation_||result->session!=session_)return;
        if(lookahead_&&status_.state==JobState::moving&&step_<status_.route.size()) {
            lookahead_=false;next_segment_=std::move(result);
        } else {
        lookahead_=false;
        if(result->status==SearchStatus::unreachable || result->status==SearchStatus::cancelled) {
            if(job_->kind==CommandKind::mine) {
                for(auto p:cache_.find(job_->blocks))blacklist_[p]=frame.world->revision;
            }
            actions.release();replan("No executable path found");return;
        }
        status_.route=std::move(result->steps);step_=0;mine_target_=result->target;
        partial_route_=result->status==SearchStatus::partial;
        status_.state=JobState::moving;action_started_=-1;
        if(status_.route.empty())finish_segment(frame);
        }
    }
    if(status_.state==JobState::planning){actions.release();plan(frame,settings);return;}
    if(status_.state==JobState::moving){
        if(partial_route_&&!searching_&&!next_segment_&&pending_goal_&&step_<status_.route.size()&&
           status_.route.size()-step_<=5) {
            int reserved{};for(auto i=step_;i<status_.route.size();++i)
                reserved+=static_cast<int>(status_.route[i].placing.size());
            worker_.submit({frame.world,status_.route.back().to,*pending_goal_,settings,
                std::max(0,frame.inventory.placement_count-reserved),generation_});
            searching_=true;lookahead_=true;
        }
        execute(frame,settings,actions,now);return;
    }
    if(status_.state==JobState::mining&&mine_target_) {
        const auto target=*mine_target_;const auto& c=frame.world->at(target);
        if(!matching(*job_,c)) {
            actions.release();mine_target_.reset();action_started_=now;
            status_.state=JobState::collecting;status_.message="Collecting drops";return;
        }
        if(!settings.allow_break||!safe_to_break(*frame.world,target)||!c.harvestable) {
            blacklist_[target]=frame.world->revision;mine_target_.reset();actions.release();replan("Target is unsafe to mine");return;
        }
        if(c.durability<=1){actions.release();pause("No suitable tool with durability remaining; replace it, then ,resume.");return;}
        if(action_started_<0)action_started_=now;
        if(now-action_started_>std::max(8.0,c.break_seconds*3+3)||!actions.mine(target,c.tool_slot)) {
            blacklist_[target]=frame.world->revision;mine_target_.reset();actions.release();replan("Target mining was rejected or timed out");
        }
        return;
    }
    if(status_.state==JobState::collecting) {
        // Route to visible matching drops instead of walking blindly into a shaft.
        if(!frame.matching_drops.empty()) {
            const auto p=*std::min_element(frame.matching_drops.begin(),frame.matching_drops.end(),[&](Vec3 a,Vec3 b){return distance(frame.feet,a)<distance(frame.feet,b);});
            if(distance(frame.feet,p)>0.8 && now-action_started_<5 && !searching_) {
                Goal drop;drop.kind=GoalKind::near;drop.position=block_at(p);drop.radius=0.8;
                worker_.submit({frame.world,frame.feet,drop,settings,frame.inventory.placement_count,generation_});searching_=true;
            }
        }
        if(now-action_started_>=1.5&&!searching_){status_.state=JobState::planning;status_.message="Searching for more targets";action_started_=-1;}
        actions.release();
    }
}
} // namespace utility::navigation
