#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace utility::navigation {

struct BlockPos {
    int x{}, y{}, z{};
    bool operator==(const BlockPos&) const = default;
};
struct PosHash {
    std::size_t operator()(BlockPos p) const noexcept;
};
struct Vec3 { double x{}, y{}, z{}; };
BlockPos block_at(Vec3 p);
double distance(Vec3 a, Vec3 b);
struct Bounds { Vec3 min{}, max{}; };

// Collision boxes use block-local coordinates. Unknown cells are never air.
struct Cell {
    std::string id;
    std::vector<Bounds> collision;
    bool known{}, liquid{}, water{}, hazard{}, climbable{}, openable{}, falling{}, opened{};
    bool breakable{}, harvestable{};
    double break_seconds{};
    int tool_slot{-1}, durability{};
    bool air() const noexcept { return known && id == "minecraft:air"; }
};
struct World {
    std::uint64_t session{}, revision{};
    std::unordered_map<BlockPos, Cell, PosHash> cells;
    // Planner-only persistent overlays model blocks broken/placed by earlier edges.
    // Native snapshots have no parent and are never modified by search.
    std::shared_ptr<const World> parent;
    const Cell& at(BlockPos p) const;
};
// Only copied values cross the game-thread/worker boundary.
struct Inventory {
    double health{}, hunger{};
    int placement_count{}, placement_slot{-1}, food_slot{-1};
    bool target_capacity{true}, using_item{};
    int matching_items{};
    bool drop_mapping_known{true};
};
struct Frame {
    std::shared_ptr<const World> world;
    Vec3 feet{};
    Inventory inventory;
    bool connected{}, alive{}, control{}, manual_input{}, grounded{};
    bool terrain_ready{true};
    std::vector<Vec3> matching_drops;
};
struct Settings {
    bool render_route{true}, allow_break{true}, allow_place{true}, allow_sprint{true};
    bool allow_parkour{true}, auto_eat{true};
    // The native adapter must validate these before enabling the respective edges.
    bool parkour_verified{}, swim_verified{}, climb_verified{}, place_verified{};
    int max_fall{3};
};
enum class GoalKind { exact, xz, mine, near };
struct Goal {
    GoalKind kind{GoalKind::exact};
    BlockPos position{};
    std::vector<BlockPos> targets;
    double radius{1.0};
};
enum class MoveKind { walk, ascend, descend, jump, swim, climb, bridge, pillar };
struct Step {
    Vec3 from{}, to{};
    MoveKind kind{MoveKind::walk};
    std::vector<BlockPos> breaking, placing, opening;
    double seconds{};
};
enum class SearchStatus { complete, partial, unreachable, cancelled };
struct SearchResult {
    SearchStatus status{SearchStatus::unreachable};
    std::uint64_t generation{}, session{}, revision{};
    std::vector<Step> steps;
    std::optional<BlockPos> target;
    std::size_t expanded{};
    double milliseconds{};
};
struct SearchRequest {
    std::shared_ptr<const World> world;
    Vec3 start{};
    Goal goal;
    Settings settings;
    int placement_count{};
    std::uint64_t generation{};
    std::size_t max_nodes{30000};
    std::chrono::milliseconds timeout{150};
};
bool safe_to_break(const World&, BlockPos);
bool body_clear(const World&, Vec3 feet, const std::vector<BlockPos>& ignored = {});
bool step_still_valid(const World&, const Step&, const Settings&);
SearchResult search(const SearchRequest&, const std::function<bool()>& cancelled = {});

class SearchWorker final {
public:
    SearchWorker();
    ~SearchWorker();
    void submit(SearchRequest request);
    void cancel();
    std::optional<SearchResult> take();
private:
    std::mutex mutex_;
    std::condition_variable_any changed_;
    std::optional<SearchRequest> pending_;
    std::optional<SearchResult> result_;
    std::atomic<std::uint64_t> revision_{};
    std::jthread thread_;
};

struct Coordinate { int value{}; bool relative{}; };
enum class CommandKind { go, mine, pause, resume, stop, status };
struct Command {
    CommandKind kind{CommandKind::status};
    Coordinate x{}, y{}, z{};
    bool has_y{};
    int quantity{};
    std::vector<std::string> blocks;
};
struct ParseResult {
    bool handled{};
    std::optional<Command> command;
    std::string error;
};
ParseResult parse_command(const std::vector<std::string>& args);
std::optional<BlockPos> resolve(const Command&, Vec3 feet);

// A session cache contains only observed blocks. Eviction is bounded and deterministic.
class WorldCache final {
public:
    explicit WorldCache(std::size_t maximum_cells = 262144) : maximum_(maximum_cells) {}
    void observe(const World& world);
    std::vector<BlockPos> find(const std::vector<std::string>& ids) const;
    void clear();
private:
    struct Entry { Cell cell; std::uint64_t age{}; };
    std::uint64_t session_{}, clock_{};
    std::size_t maximum_;
    std::unordered_map<BlockPos, Entry, PosHash> cells_;
};

enum class JobState { idle, planning, moving, mining, collecting, eating, paused, complete, failed };
struct Status {
    JobState state{JobState::idle};
    std::string message{"Idle"};
    int collected{}, requested{};
    std::uint64_t generation{};
    std::vector<Step> route;
    std::optional<BlockPos> goal;
};

// Calls below occur exclusively on the game thread. Implementations must never
// teleport, write world blocks, or synthesize success in place of server observation.
class Actions {
public:
    virtual ~Actions() = default;
    virtual void release() noexcept = 0;
    virtual bool move(const Step&, bool sprint) = 0;
    virtual bool mine(BlockPos, int tool_slot) = 0;
    virtual bool place(BlockPos, int slot) = 0;
    virtual bool open(BlockPos) = 0;
    virtual bool eat(int slot) = 0;
};
class Controller final {
public:
    // Caller serializes all access. Module/UI queues never invoke native actions.
    std::string command(const Command&, const Frame&, const Settings&,
                        const std::function<bool(const std::string&)>& known_block);
    void tick(const Frame&, const Settings&, Actions&, double now);
    void stop(std::string reason = "Stopped");
    void pause(std::string reason);
    const Status& status() const noexcept { return status_; }
    bool owns_controls() const noexcept;
    const std::vector<std::string>& mining_targets() const noexcept;
private:
    void plan(const Frame&, const Settings&);
    bool survival(const Frame&, const Settings&, Actions&, double);
    bool execute(const Frame&, const Settings&, Actions&, double);
    void replan(std::string reason);
    void finish_segment(const Frame&);
    SearchWorker worker_;
    WorldCache cache_;
    Status status_;
    std::optional<Command> job_;
    Goal goal_;
    std::uint64_t session_{}, generation_{};
    bool searching_{};
    std::size_t step_{};
    double action_started_{-1}, eating_started_{-1}, terrain_wait_started_{-1};
    int retries_{};
    double eating_hunger_{};
    std::optional<BlockPos> mine_target_, action_target_;
    std::unordered_map<BlockPos, std::uint64_t, PosHash> blacklist_;
    std::optional<SearchResult> next_segment_;
    bool lookahead_{};
    bool partial_route_{};
    std::optional<Goal> pending_goal_;
};

} // namespace utility::navigation
