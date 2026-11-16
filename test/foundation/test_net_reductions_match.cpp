// The match of the reductions library and its nets: each plan of Reduce.h,
// Scan.h and Sort.h as an interaction net, the checks of the net, and a run
// of each implementation on the agents of the net.
//
//   The net of a plan     a num agent for each leaf, an add for each step of a
//                         combine, a cmp for each compare-exchange, a chain of
//                         dup agents for each value with more than one use, a
//                         boundary result for each output, and a boundary drop
//                         for a leaf that no step and no output reads
//   The checks            wiring, dataflow and the vicious circle for each
//                         net, the forest for each tree, and the value table
//                         of rules, which allocates nothing
//   The symbolic run      the implementation runs on symbols: the value of a
//                         node is the node, and a combine of a and b finds the
//                         add whose left port takes a and whose right port
//                         takes b.  Each combine of the implementation is one
//                         add of the net, in port order, one time each
//   The ties              sort_by_network sorts tickets by a key with ties,
//                         and it gives the tags of the evaluation of the net
//   The bits              sum_by_tree and the sum scans, with each tested
//                         count of lanes, give the bits of the evaluation of
//                         the net on Philox draws
//
// Three controls show that the tests can refuse a wrong net.  A tree plan
// with the operands of one step swapped passes each structural check, and the
// symbolic run refuses it.  A sort net with the inputs of its first cmp
// crossed sorts the keys, and the tie test refuses it.  The net of a left
// fold gives bits that sum_by_tree does not give.

#include <foundation/algebra/net/Net.h>
#include <foundation/algebra/net/Rules.h>
#include <foundation/core/Array.h>
#include <foundation/core/Reduce.h>
#include <foundation/core/Scalar.h>
#include <foundation/core/Scan.h>
#include <foundation/core/Sort.h>

#include "../test_assert.h"
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>

namespace {

namespace net = foundation::algebra::net;
namespace core = foundation::core;

// ── The value signature ─────────────────────────────────────────────────

inline constexpr net::TypeId value{0};

inline constexpr net::KindId num_kind{0};
inline constexpr net::KindId add_kind{1};
inline constexpr net::KindId dup_kind{2};
inline constexpr net::KindId cmp_kind{3};
inline constexpr net::KindId result_kind{4};
inline constexpr net::KindId drop_kind{5};

inline constexpr net::NetSignature<1, 6> value_kinds{
    .type_names = {"Value"},
    .kinds = {
        {.name = "num", .port_names = {"value"}, .port_types = {net::give(value)}},
        {.name = "add",
         .port_names = {"left", "right", "sum"},
         .port_types = {net::take(value), net::take(value), net::give(value)}},
        {.name = "dup",
         .port_names = {"in", "first", "second"},
         .port_types = {net::take(value), net::give(value), net::give(value)}},
        {.name = "cmp",
         .port_names = {"low", "high", "min", "max"},
         .port_types = {net::take(value), net::take(value), net::give(value), net::give(value)}},
        {.name = "result", .port_names = {"in"}, .port_types = {net::take(value)}, .is_boundary = true},
        {.name = "drop", .port_names = {"in"}, .port_types = {net::take(value)}, .is_boundary = true},
    }};

// ── The net of a combine plan ───────────────────────────────────────────
//
// A combine plan has Leaves leaves, Steps steps and Outputs outputs.  Node n
// is leaf n below Leaves, and step n - Leaves above.  An output is a node, or
// empty_plan_node for the empty value of an exclusive scan, which a num agent
// of its own gives.

template <std::size_t Leaves, std::size_t Steps, std::size_t Outputs>
struct CombinePlan {
    core::FixedArray<core::TreeStep, Steps> steps{};
    core::FixedArray<core::PlanNode, Outputs> outputs{};
};

// The element at a position that the plan bounds.
template <class T, std::size_t N>
[[nodiscard]] constexpr T& slot_at(core::FixedArray<T, N>& array, std::size_t position) noexcept {
    return array.at(core::Index<N>::check(position).expect("a position that the plan bounds"));
}

template <class T, std::size_t N>
[[nodiscard]] constexpr T const& slot_at(core::FixedArray<T, N> const& array, std::size_t position) noexcept {
    return array.at(core::Index<N>::check(position).expect("a position that the plan bounds"));
}

// The uses of each node: one for each step that reads it and one for each
// output.  The last node counts the outputs of the empty value.
template <std::size_t Leaves, std::size_t Steps, std::size_t Outputs>
[[nodiscard]] consteval core::FixedArray<std::uint32_t, Leaves + Steps + 1>
count_uses(CombinePlan<Leaves, Steps, Outputs> const& plan) noexcept {
    core::FixedArray<std::uint32_t, Leaves + Steps + 1> uses{};
    for (core::TreeStep const& step : plan.steps) {
        ++slot_at(uses, static_cast<std::size_t>(step.left));
        ++slot_at(uses, static_cast<std::size_t>(step.right));
    }
    for (core::PlanNode const output : plan.outputs) {
        std::size_t const node = output == core::empty_plan_node ? Leaves + Steps : static_cast<std::size_t>(output);
        ++slot_at(uses, node);
    }
    return uses;
}

// The agents of the net: a num for each leaf, an add for each step, a num for
// the empty value when an output takes it, a result for each output, a dup
// for each use past the first, and a drop for each leaf of no use.
template <std::size_t Leaves, std::size_t Steps, std::size_t Outputs>
[[nodiscard]] consteval std::size_t count_agents(CombinePlan<Leaves, Steps, Outputs> const& plan) noexcept {
    auto const uses = count_uses(plan);
    std::size_t agents = Leaves + Steps + 1 + Outputs;
    for (std::uint32_t const count : uses)
        agents += count == 0 ? 1 : count - 1;
    return agents;
}

// The wires of the net: a node of no use has one wire to its drop, and a node
// of u uses has 2u - 1 wires through its chain of dups.
template <std::size_t Leaves, std::size_t Steps, std::size_t Outputs>
[[nodiscard]] consteval std::size_t count_wires(CombinePlan<Leaves, Steps, Outputs> const& plan) noexcept {
    auto const uses = count_uses(plan);
    std::size_t wires = 0;
    for (std::uint32_t const count : uses)
        wires += count == 0 ? 1 : 2 * count - 1;
    return wires;
}

[[nodiscard]] constexpr net::PortRef port_of(std::uint32_t agent, std::uint32_t port) noexcept {
    return net::PortRef{net::AgentId{agent}, port};
}

// The net of a combine plan.  The agents: the nums of the leaves, the adds of
// the steps, the num of the empty value, the results of the outputs, and then
// the dups and the drops.  Each value goes from its producer through a chain
// of dups to its consumers, in the order of the steps and then the outputs.
// Complexity: O(nodes * (steps + outputs)), for the small plans of a test.
template <std::size_t Agents, std::size_t Wires, std::size_t Leaves, std::size_t Steps, std::size_t Outputs>
[[nodiscard]] consteval net::Net<1, 6, Agents, Wires>
build_combine_net(CombinePlan<Leaves, Steps, Outputs> const& plan) noexcept {
    constexpr auto leaves = static_cast<std::uint32_t>(Leaves);
    constexpr auto steps = static_cast<std::uint32_t>(Steps);
    constexpr auto outputs = static_cast<std::uint32_t>(Outputs);
    constexpr std::uint32_t empty_agent = leaves + steps;
    net::Net<1, 6, Agents, Wires> graph{};
    graph.signature = value_kinds;
    for (std::uint32_t agent = 0; agent < leaves; ++agent)
        graph.agents[agent] = num_kind;
    for (std::uint32_t agent = leaves; agent < empty_agent; ++agent)
        graph.agents[agent] = add_kind;
    graph.agents[empty_agent] = num_kind;
    for (std::uint32_t agent = empty_agent + 1; agent <= empty_agent + outputs; ++agent)
        graph.agents[agent] = result_kind;
    std::uint32_t next_agent = empty_agent + 1 + outputs;
    std::uint32_t next_wire = 0;
    auto const uses = count_uses(plan);
    for (std::uint32_t node = 0; node <= empty_agent; ++node) {
        net::PortRef giver = port_of(node, node < leaves || node == empty_agent ? 0u : 2u);
        std::uint32_t const count = slot_at(uses, node);
        if (count == 0) {
            graph.agents[next_agent] = drop_kind;
            graph.wires[next_wire] = net::Wire{giver, port_of(next_agent, 0)};
            ++next_agent;
            ++next_wire;
            continue;
        }
        std::uint32_t seen = 0;
        auto const feed = [&](net::PortRef taker) constexpr noexcept {
            ++seen;
            if (seen < count) {
                graph.agents[next_agent] = dup_kind;
                graph.wires[next_wire] = net::Wire{giver, port_of(next_agent, 0)};
                graph.wires[next_wire + 1] = net::Wire{port_of(next_agent, 1), taker};
                giver = port_of(next_agent, 2);
                ++next_agent;
                next_wire += 2;
            } else {
                graph.wires[next_wire] = net::Wire{giver, taker};
                ++next_wire;
            }
        };
        std::uint32_t step = 0;
        for (core::TreeStep const& one : plan.steps) {
            if (static_cast<std::uint32_t>(one.left) == node) feed(port_of(leaves + step, 0));
            if (static_cast<std::uint32_t>(one.right) == node) feed(port_of(leaves + step, 1));
            ++step;
        }
        std::uint32_t output = 0;
        for (core::PlanNode const out : plan.outputs) {
            std::uint32_t const read = out == core::empty_plan_node ? empty_agent : static_cast<std::uint32_t>(out);
            if (read == node) feed(port_of(empty_agent + 1 + output, 0));
            ++output;
        }
    }
    return graph;
}

// The node whose value reaches a port that takes a value: the wire at the
// port, back through each dup.  Complexity: O(W) for each step back.
template <std::size_t Agents, std::size_t Wires>
[[nodiscard]] consteval std::uint32_t source_of(net::Net<1, 6, Agents, Wires> const& graph,
                                                net::PortRef taker) noexcept {
    for (;;) {
        net::PortRef giver{};
        for (net::Wire const& wire : graph.wires) {
            if (wire.right.agent == taker.agent && wire.right.port == taker.port) giver = wire.left;
        }
        if (graph.agents[net::index_of(giver.agent)] != dup_kind) return net::index_of(giver.agent);
        taker = port_of(net::index_of(giver.agent), 0);
    }
}

// ── The symbolic run ────────────────────────────────────────────────────

// The value of a node in a symbolic run.
struct Symbol {
    std::uint32_t node = 0;

    [[nodiscard]] friend constexpr bool operator==(Symbol const& left, Symbol const& right) noexcept {
        return left.node == right.node;
    }
};

inline constexpr std::uint32_t no_add = 0xFFFFFFFFu;

// The adds of a net with the nodes that reach their two ports, and the count
// of the combines of the run that each add took.
template <std::size_t Steps>
struct Matcher {
    core::FixedArray<std::uint32_t, Steps> left{};
    core::FixedArray<std::uint32_t, Steps> right{};
    core::FixedArray<std::uint32_t, Steps> fired{};
    std::uint32_t leaves = 0;
    bool is_stray = false;

    // The node of the add whose left port takes a and whose right port takes
    // b.  A combine that no add takes, or that an add took before, marks the
    // run as stray.
    [[nodiscard]] constexpr Symbol combine(Symbol a, Symbol b) noexcept {
        std::uint32_t step = 0;
        for (std::uint32_t& count : fired) {
            if (slot_at(left, step) == a.node && slot_at(right, step) == b.node && count == 0) {
                count = 1;
                return Symbol{leaves + step};
            }
            ++step;
        }
        is_stray = true;
        return Symbol{no_add};
    }

    // Each add took one combine.
    [[nodiscard]] constexpr bool is_each_add_fired() const noexcept {
        for (std::uint32_t const count : fired) {
            if (count != 1) return false;
        }
        return !is_stray;
    }
};

template <std::size_t Leaves, std::size_t Steps, std::size_t Agents, std::size_t Wires>
[[nodiscard]] consteval Matcher<Steps> matcher_of(net::Net<1, 6, Agents, Wires> const& graph) noexcept {
    Matcher<Steps> matcher{};
    matcher.leaves = static_cast<std::uint32_t>(Leaves);
    for (std::uint32_t step = 0; step < Steps; ++step) {
        slot_at(matcher.left, step) = source_of(graph, port_of(static_cast<std::uint32_t>(Leaves) + step, 0));
        slot_at(matcher.right, step) = source_of(graph, port_of(static_cast<std::uint32_t>(Leaves) + step, 1));
    }
    return matcher;
}

template <std::size_t Leaves>
[[nodiscard]] consteval core::FixedArray<Symbol, Leaves> symbols_of_leaves() noexcept {
    core::FixedArray<Symbol, Leaves> symbols{};
    std::uint32_t node = 0;
    for (Symbol& symbol : symbols) {
        symbol.node = node;
        ++node;
    }
    return symbols;
}

// ── The tree reduce ─────────────────────────────────────────────────────

template <std::size_t Leaves>
[[nodiscard]] consteval CombinePlan<Leaves, Leaves - 1, 1> tree_combine_plan() noexcept {
    CombinePlan<Leaves, Leaves - 1, 1> plan{};
    plan.steps = core::plan_tree<Leaves>();
    plan.outputs = core::FixedArray<core::PlanNode, 1>{core::PlanNode{static_cast<std::uint32_t>(2 * Leaves - 2)}};
    return plan;
}

// The same plan with the operands of its first step swapped.
template <std::size_t Leaves>
[[nodiscard]] consteval CombinePlan<Leaves, Leaves - 1, 1> swapped_tree_combine_plan() noexcept {
    CombinePlan<Leaves, Leaves - 1, 1> plan = tree_combine_plan<Leaves>();
    for (core::TreeStep& step : plan.steps) {
        core::TreeStep const swapped{step.right, step.left};
        step = swapped;
        break;
    }
    return plan;
}

// The left fold of the leaves: a plan with the size of the tree plan and a
// different association, a control of the bit test.
template <std::size_t Leaves>
[[nodiscard]] consteval CombinePlan<Leaves, Leaves - 1, 1> fold_combine_plan() noexcept {
    CombinePlan<Leaves, Leaves - 1, 1> plan{};
    auto previous = static_cast<std::uint32_t>(0);
    auto next_node = static_cast<std::uint32_t>(Leaves);
    std::uint32_t leaf = 1;
    for (core::TreeStep& step : plan.steps) {
        step = core::TreeStep{core::PlanNode{previous}, core::PlanNode{leaf}};
        previous = next_node;
        ++next_node;
        ++leaf;
    }
    plan.outputs = core::FixedArray<core::PlanNode, 1>{core::PlanNode{previous}};
    return plan;
}

template <std::size_t Leaves>
inline constexpr auto fold_net_of =
    build_combine_net<count_agents(fold_combine_plan<Leaves>()), count_wires(fold_combine_plan<Leaves>())>(
        fold_combine_plan<Leaves>());

static_assert(net::check_forest(fold_net_of<37>).is_admitted());

template <std::size_t Leaves, bool IsSwapped>
inline constexpr auto tree_plan_of = IsSwapped ? swapped_tree_combine_plan<Leaves>() : tree_combine_plan<Leaves>();

template <std::size_t Leaves, bool IsSwapped>
inline constexpr auto tree_net_of =
    build_combine_net<count_agents(tree_plan_of<Leaves, IsSwapped>), count_wires(tree_plan_of<Leaves, IsSwapped>)>(
        tree_plan_of<Leaves, IsSwapped>);

// tree_reduce combines the agents of the net of its plan, in port order, one
// time each, and its result is the node that the result agent takes.
template <std::size_t Leaves, bool IsSwapped>
[[nodiscard]] consteval bool does_tree_reduce_follow() noexcept {
    auto const& graph = tree_net_of<Leaves, IsSwapped>;
    auto matcher = matcher_of<Leaves, Leaves - 1>(graph);
    auto const leaves = symbols_of_leaves<Leaves>();
    Symbol const root = core::reduce_by_tree(leaves.view(), [&](Symbol const& a, Symbol const& b) noexcept {
                            return matcher.combine(a, b);
                        }).expect("a run of leaves");
    std::uint32_t const result = source_of(graph, port_of(static_cast<std::uint32_t>(2 * Leaves), 0));
    return matcher.is_each_add_fired() && root.node == result;
}

static_assert(net::check_forest(tree_net_of<6, false>).is_admitted());
static_assert(net::check_dataflow(tree_net_of<6, false>).is_admitted());
static_assert(net::check_vicious(tree_net_of<6, false>).is_admitted());
static_assert(does_tree_reduce_follow<6, false>());
static_assert(does_tree_reduce_follow<13, false>());

// The swapped plan passes each structural check, and the run refuses it.
static_assert(net::check_forest(tree_net_of<6, true>).is_admitted());
static_assert(net::check_dataflow(tree_net_of<6, true>).is_admitted());
static_assert(!does_tree_reduce_follow<6, true>());

// ── The scans ───────────────────────────────────────────────────────────

template <std::size_t Count, core::ScanKind Kind>
inline constexpr std::size_t scan_steps_of = core::count_scan_plan_steps(Count, Kind).expect("a scan of a short run");

template <std::size_t Count, core::ScanKind Kind>
[[nodiscard]] consteval CombinePlan<Count, scan_steps_of<Count, Kind>, Count> scan_combine_plan() noexcept {
    CombinePlan<Count, scan_steps_of<Count, Kind>, Count> plan{};
    bool is_written = false;
    plan.steps = core::with_mut(std::move(plan.steps), [&](auto steps) noexcept {
        plan.outputs = core::with_mut(std::move(plan.outputs), [&](auto outputs) noexcept {
            is_written = core::write_scan_plan(Kind, std::move(steps), std::move(outputs)).is_ok();
        });
    });
    if (!is_written) core::fatal("the counts of the scan plan agree");
    return plan;
}

template <std::size_t Count, core::ScanKind Kind>
inline constexpr auto scan_plan_of = scan_combine_plan<Count, Kind>();

template <std::size_t Count, core::ScanKind Kind>
inline constexpr auto scan_net_of =
    build_combine_net<count_agents(scan_plan_of<Count, Kind>), count_wires(scan_plan_of<Count, Kind>)>(
        scan_plan_of<Count, Kind>);

// The generic scan combines the agents of the net of its plan, in port
// order, one time each, and each output is the node that its result agent
// takes.
template <std::size_t Count, core::ScanKind Kind>
[[nodiscard]] consteval bool does_scan_follow() noexcept {
    constexpr std::size_t steps = scan_steps_of<Count, Kind>;
    auto const& graph = scan_net_of<Count, Kind>;
    auto matcher = matcher_of<Count, steps>(graph);
    auto const leaves = symbols_of_leaves<Count>();
    core::FixedArray<Symbol, Count> out{};
    auto const combine = [&](Symbol const& a, Symbol const& b) noexcept { return matcher.combine(a, b); };
    constexpr auto empty_node = static_cast<std::uint32_t>(Count + steps);
    bool is_written = false;
    out = core::with_mut(std::move(out), [&](auto written) noexcept {
        if constexpr (Kind == core::ScanKind::Inclusive) {
            is_written =
                core::scan_inclusive(core::View<Symbol const>{leaves.view()}, std::move(written), combine).is_ok();
        } else {
            is_written = core::scan_exclusive(core::View<Symbol const>{leaves.view()}, std::move(written),
                                              Symbol{empty_node}, combine)
                             .is_ok();
        }
    });
    bool is_each_output = is_written;
    std::uint32_t position = 0;
    for (Symbol const& symbol : out) {
        is_each_output = is_each_output && symbol.node == source_of(graph, port_of(empty_node + 1 + position, 0));
        ++position;
    }
    return is_each_output && matcher.is_each_add_fired();
}

static_assert(net::check_wiring(scan_net_of<8, core::ScanKind::Inclusive>).is_admitted());
static_assert(net::check_dataflow(scan_net_of<8, core::ScanKind::Inclusive>).is_admitted());
static_assert(net::check_vicious(scan_net_of<8, core::ScanKind::Inclusive>).is_admitted());
static_assert(net::check_dataflow(scan_net_of<13, core::ScanKind::Exclusive>).is_admitted());
static_assert(does_scan_follow<8, core::ScanKind::Inclusive>());
static_assert(does_scan_follow<13, core::ScanKind::Inclusive>());
static_assert(does_scan_follow<8, core::ScanKind::Exclusive>());
static_assert(does_scan_follow<13, core::ScanKind::Exclusive>());

// A value with two uses needs its copies, and the copies meet again, so the
// scan net is no forest.
static_assert(!net::check_forest(scan_net_of<8, core::ScanKind::Inclusive>).is_admitted());

// ── The sorting network ─────────────────────────────────────────────────
//
// The agents: a num for each position, a cmp for each comparator, and a
// result for each position.  A cmp takes the value of its low position on
// its principal port, and it gives the minimum to the low position and the
// maximum to the high one.  The crossed net gives the first cmp the value of
// its high position on its principal port: a control that the structural
// checks admit.

template <std::size_t Count>
inline constexpr std::size_t sort_steps_of = core::sort_network_size<Count>;

template <std::size_t Count, bool IsCrossed>
[[nodiscard]] consteval net::Net<1, 6, 2 * Count + sort_steps_of<Count>, Count + 2 * sort_steps_of<Count>>
build_sort_net() noexcept {
    constexpr auto count = static_cast<std::uint32_t>(Count);
    constexpr auto steps = static_cast<std::uint32_t>(sort_steps_of<Count>);
    net::Net<1, 6, 2 * Count + sort_steps_of<Count>, Count + 2 * sort_steps_of<Count>> graph{};
    graph.signature = value_kinds;
    core::FixedArray<net::PortRef, Count> giver{};
    for (std::uint32_t position = 0; position < count; ++position) {
        graph.agents[position] = num_kind;
        slot_at(giver, position) = port_of(position, 0);
    }
    std::uint32_t agent = count;
    std::uint32_t wire = 0;
    for (core::Comparator const& step : core::plan_sort_network<Count>()) {
        bool const is_crossed = IsCrossed && agent == count;
        graph.agents[agent] = cmp_kind;
        graph.wires[wire] = net::Wire{slot_at(giver, is_crossed ? step.high : step.low), port_of(agent, 0)};
        graph.wires[wire + 1] = net::Wire{slot_at(giver, is_crossed ? step.low : step.high), port_of(agent, 1)};
        slot_at(giver, step.low) = port_of(agent, 2);
        slot_at(giver, step.high) = port_of(agent, 3);
        ++agent;
        wire += 2;
    }
    for (std::uint32_t position = 0; position < count; ++position) {
        graph.agents[count + steps + position] = result_kind;
        graph.wires[wire] = net::Wire{slot_at(giver, position), port_of(count + steps + position, 0)};
        ++wire;
    }
    return graph;
}

template <std::size_t Count, bool IsCrossed = false>
inline constexpr auto sort_net_of = build_sort_net<Count, IsCrossed>();

static_assert(net::check_dataflow(sort_net_of<4>).is_admitted() && net::check_vicious(sort_net_of<4>).is_admitted());
static_assert(net::check_dataflow(sort_net_of<7>).is_admitted() && net::check_vicious(sort_net_of<7>).is_admitted());
static_assert(net::check_dataflow(sort_net_of<16>).is_admitted());
static_assert(!net::check_forest(sort_net_of<4>).is_admitted());

// ── The rules of the value agents ───────────────────────────────────────
//
// add_left holds the left operand of an add, and cmp_high holds the low value
// of a cmp.  Each rule keeps the count of the agents or makes it smaller, so
// a reduction of each net above allocates nothing.

inline constexpr net::KindId add_left_kind{6};
inline constexpr net::KindId cmp_high_kind{7};

inline constexpr net::NetSignature<1, 8> rule_kinds{
    .type_names = {"Value"},
    .kinds = {value_kinds.kinds[0],
              value_kinds.kinds[1],
              value_kinds.kinds[2],
              value_kinds.kinds[3],
              value_kinds.kinds[4],
              value_kinds.kinds[5],
              {.name = "add_left", .port_names = {"right", "sum"}, .port_types = {net::take(value), net::give(value)}},
              {.name = "cmp_high",
               .port_names = {"high", "min", "max"},
               .port_types = {net::take(value), net::give(value), net::give(value)}}}};

[[nodiscard]] consteval net::RuleWire join_ends(net::RuleEnd left, net::RuleEnd right) noexcept {
    return net::RuleWire{left, right};
}

inline constexpr net::RightSide one_num{.agents = {num_kind},
                                        .wires = {join_ends(net::point_at_agent(0, 0), net::point_at_interface(0))},
                                        .agent_count = 1,
                                        .wire_count = 1};
inline constexpr net::RightSide two_nums{.agents = {num_kind, num_kind},
                                         .wires = {join_ends(net::point_at_agent(0, 0), net::point_at_interface(0)),
                                                   join_ends(net::point_at_agent(1, 0), net::point_at_interface(1))},
                                         .agent_count = 2,
                                         .wire_count = 2};

inline constexpr net::RuleTable<1, 8, 5> value_rules{
    .signature = rule_kinds,
    .rules = {net::Rule{"add_meets_num", add_kind, num_kind,
                        net::RightSide{.agents = {add_left_kind},
                                       .wires = {join_ends(net::point_at_agent(0, 0), net::point_at_interface(0)),
                                                 join_ends(net::point_at_agent(0, 1), net::point_at_interface(1))},
                                       .agent_count = 1,
                                       .wire_count = 2}},
              net::Rule{"add_left_meets_num", add_left_kind, num_kind, one_num},
              net::Rule{"dup_meets_num", dup_kind, num_kind, two_nums},
              net::Rule{"cmp_meets_num", cmp_kind, num_kind,
                        net::RightSide{.agents = {cmp_high_kind},
                                       .wires = {join_ends(net::point_at_agent(0, 0), net::point_at_interface(0)),
                                                 join_ends(net::point_at_agent(0, 1), net::point_at_interface(1)),
                                                 join_ends(net::point_at_agent(0, 2), net::point_at_interface(2))},
                                       .agent_count = 1,
                                       .wire_count = 3}},
              net::Rule{"cmp_high_meets_num", cmp_high_kind, num_kind, two_nums}}};

inline constexpr net::RuleVerdict value_rules_verdict = net::check_rules(value_rules);
static_assert(value_rules_verdict.is_admitted(), value_rules_verdict);
static_assert(value_rules_verdict.allocates_nothing());

// ── The evaluation of a value net ───────────────────────────────────────
//
// The value at each port that gives a value: a num gives its leaf, or the
// empty value for the num of no leaf, and an add gives the combine of its
// left port and its right port, in that order.  A dup gives the value that it
// takes at its two ports.  A cmp makes the one compare of Sort.h, and it gives
// the minimum at min and the maximum at max.  The evaluation reads the wires
// of the net and not the plan, and it keeps each value that it computes.
// Complexity: O(A + W).

template <class T, std::size_t Agents>
struct NetValues {
    core::FixedArray<net::PortRef, Agents * net::max_agent_ports> giver{};
    core::FixedArray<T, Agents * net::max_agent_ports> value{};
    core::FixedArray<bool, Agents * net::max_agent_ports> is_known{};
};

[[nodiscard]] constexpr std::size_t slot_of(net::PortRef port) noexcept {
    return static_cast<std::size_t>(net::index_of(port.agent)) * net::max_agent_ports + port.port;
}

template <class T, std::size_t Agents, std::size_t Wires, std::size_t Leaves, class Combine, class Less>
[[nodiscard]] constexpr T value_at(net::Net<1, 6, Agents, Wires> const& graph,
                                   core::FixedArray<T, Leaves> const& leaves, T const& empty, Combine const& combine,
                                   Less const& less, NetValues<T, Agents>& values, net::PortRef taker) noexcept {
    net::PortRef const giver = slot_at(values.giver, slot_of(taker));
    std::size_t const slot = slot_of(giver);
    if (slot_at(values.is_known, slot)) return slot_at(values.value, slot);
    std::uint32_t const agent = net::index_of(giver.agent);
    net::KindId const kind = graph.agents[agent];
    T computed{};
    if (kind == num_kind) {
        computed = agent < Leaves ? slot_at(leaves, agent) : empty;
    } else if (kind == dup_kind) {
        computed = value_at(graph, leaves, empty, combine, less, values, port_of(agent, 0));
    } else {
        T const left = value_at(graph, leaves, empty, combine, less, values, port_of(agent, 0));
        T const right = value_at(graph, leaves, empty, combine, less, values, port_of(agent, 1));
        bool const is_swapped = kind == cmp_kind && less(right, left);
        if (kind == add_kind) {
            computed = combine(left, right);
        } else if (giver.port == 2) {
            computed = is_swapped ? right : left;
        } else {
            computed = is_swapped ? left : right;
        }
    }
    slot_at(values.value, slot) = computed;
    slot_at(values.is_known, slot) = true;
    return computed;
}

// The values that the result agents take, in the order of the agents.  The
// giver of a wire is the end whose port gives a value.
template <std::size_t Outputs, class T, std::size_t Agents, std::size_t Wires, std::size_t Leaves, class Combine,
          class Less>
[[nodiscard]] constexpr core::FixedArray<T, Outputs>
evaluate_net(net::Net<1, 6, Agents, Wires> const& graph, core::FixedArray<T, Leaves> const& leaves, T const& empty,
             Combine const& combine, Less const& less) noexcept {
    NetValues<T, Agents> values{};
    for (net::Wire const& wire : graph.wires) {
        net::AgentKind const& left_kind =
            graph.signature.kinds[net::index_of(graph.agents[net::index_of(wire.left.agent)])];
        bool const is_left_giver = left_kind.port_types[wire.left.port].polarity == net::Polarity::Give;
        slot_at(values.giver, slot_of(is_left_giver ? wire.right : wire.left)) = is_left_giver ? wire.left : wire.right;
    }
    core::FixedArray<T, Outputs> outputs{};
    std::size_t output = 0;
    for (std::uint32_t agent = 0; agent < Agents; ++agent) {
        if (graph.agents[agent] != result_kind) continue;
        slot_at(outputs, output) = value_at(graph, leaves, empty, combine, less, values, port_of(agent, 0));
        ++output;
    }
    return outputs;
}

// ── The order of a sort with ties ───────────────────────────────────────
//
// A ticket has a key and a tag, and the order reads the key only.  So two
// tickets with one key are a tie, and the tags show where the
// compare-exchanges put them.  The network is not stable, so a different
// network, or a cmp with its two inputs crossed, gives a different order of
// the tags.

struct Ticket {
    std::uint32_t key = 0;
    std::uint32_t tag = 0;

    [[nodiscard]] friend constexpr bool operator==(Ticket const&, Ticket const&) noexcept = default;
};

struct ByKey {
    [[nodiscard]] constexpr bool operator()(Ticket const& left, Ticket const& right) const noexcept {
        return left.key < right.key;
    }
};

// The combine of a net with no add.  No agent of a sort net calls it.
struct NoCombine {
    [[nodiscard]] constexpr Ticket operator()(Ticket const& left, Ticket const& right) const noexcept {
        return left.key < right.key ? left : right;
    }
};

// Tickets with keys from 0 to 2 and the tag of their position, so most keys
// tie with some other key.
template <std::size_t Count>
[[nodiscard]] constexpr core::FixedArray<Ticket, Count> draw_tickets(std::uint64_t seed) noexcept {
    ::crucible::test::PhiloxStream stream{seed};
    core::FixedArray<Ticket, Count> tickets{};
    std::uint32_t tag = 0;
    for (Ticket& ticket : tickets) {
        ticket = Ticket{static_cast<std::uint32_t>(stream.below(3)), tag};
        ++tag;
    }
    return tickets;
}

template <std::size_t Count, bool IsCrossed>
[[nodiscard]] constexpr core::FixedArray<Ticket, Count>
sort_by_net(core::FixedArray<Ticket, Count> const& tickets) noexcept {
    return evaluate_net<Count>(sort_net_of<Count, IsCrossed>, tickets, Ticket{}, NoCombine{}, ByKey{});
}

// sort_by_network gives the keys and the tags of the net of its plan.
template <std::size_t Count, bool IsCrossed>
[[nodiscard]] constexpr bool does_sort_follow(core::FixedArray<Ticket, Count> const& tickets) noexcept {
    auto const from_net = sort_by_net<Count, IsCrossed>(tickets);
    core::FixedArray<Ticket, Count> sorted = tickets;
    sorted = core::with_mut(std::move(sorted),
                            [](auto elements) noexcept { core::sort_by_network(std::move(elements), ByKey{}); });
    std::size_t position = 0;
    for (Ticket const& ticket : sorted) {
        if (!(ticket == slot_at(from_net, position))) return false;
        ++position;
    }
    return true;
}

template <std::size_t Count>
[[nodiscard]] constexpr bool is_sorted_by_key(core::FixedArray<Ticket, Count> const& tickets) noexcept {
    std::uint32_t last = 0;
    for (Ticket const& ticket : tickets) {
        if (ticket.key < last) return false;
        last = ticket.key;
    }
    return true;
}

static_assert(does_sort_follow<4, false>(draw_tickets<4>(0x5EED0001u)));
static_assert(does_sort_follow<7, false>(draw_tickets<7>(0x5EED0002u)));
static_assert(does_sort_follow<16, false>(draw_tickets<16>(0x5EED0003u)));
static_assert(does_sort_follow<16, false>(draw_tickets<16>(0x5EED0004u)));

// The tickets 0 and 1 tie at the first cmp, which reads the positions 0 and
// 1.  The net with that cmp crossed passes the structural checks and sorts
// the keys, but it puts the tags of the tie in the other order.
inline constexpr core::FixedArray<Ticket, 4> tie_at_first_cmp{Ticket{1, 0}, Ticket{1, 1}, Ticket{0, 2}, Ticket{2, 3}};
static_assert(net::check_dataflow(sort_net_of<4, true>).is_admitted());
static_assert(net::check_vicious(sort_net_of<4, true>).is_admitted());
static_assert(is_sorted_by_key(sort_by_net<4, true>(tie_at_first_cmp)));
static_assert(does_sort_follow<4, false>(tie_at_first_cmp));
static_assert(!does_sort_follow<4, true>(tie_at_first_cmp));

// ── The bits of the float forms ─────────────────────────────────────────
//
// A float with a random sign, a random mantissa and an exponent from -12 to
// 11, and a double with an exponent from -20 to 19.  A change of the
// association of a sum then changes its bits.  sum_by_tree, its lane forms
// and the sum scans run on SIMD lanes at run time, so main runs these tests.

struct AddValues {
    template <class T>
    [[nodiscard]] constexpr T operator()(T left, T right) const noexcept {
        return left + right;
    }
};

template <class T>
[[nodiscard]] T draw_value(::crucible::test::PhiloxStream& stream) noexcept {
    std::uint64_t const bits = stream.next_wide();
    if constexpr (sizeof(T) == 4) {
        auto const mantissa = static_cast<std::uint32_t>(bits & 0x7FFFFFu);
        auto const exponent = static_cast<std::uint32_t>(127 + (bits >> 23) % 24 - 12);
        auto const sign = static_cast<std::uint32_t>((bits >> 40) & 1u);
        return core::bit_cast<T>((sign << 31) | (exponent << 23) | mantissa);
    } else {
        std::uint64_t const mantissa = stream.next_wide() & ((std::uint64_t{1} << 52) - 1);
        std::uint64_t const exponent = 1023 + bits % 40 - 20;
        std::uint64_t const sign = (bits >> 40) & 1u;
        return core::bit_cast<T>((sign << 63) | (exponent << 52) | mantissa);
    }
}

template <class T, std::size_t Count>
[[nodiscard]] core::FixedArray<T, Count> draw_values(std::uint64_t seed) noexcept {
    ::crucible::test::PhiloxStream stream{seed};
    core::FixedArray<T, Count> values{};
    for (T& drawn : values) {
        drawn = draw_value<T>(stream);
    }
    return values;
}

[[nodiscard]] bool has_same_bits(float left, float right) noexcept {
    return core::bit_cast<std::uint32_t>(left) == core::bit_cast<std::uint32_t>(right);
}

[[nodiscard]] bool has_same_bits(double left, double right) noexcept {
    return core::bit_cast<std::uint64_t>(left) == core::bit_cast<std::uint64_t>(right);
}

// sum_by_tree and its forms of 4, 8 and 16 lanes give the bits of the net of
// the tree plan, on 16 draws.
template <class T, std::size_t Leaves>
[[nodiscard]] bool does_tree_sum_follow(std::uint64_t seed) noexcept {
    ::crucible::test::PhiloxStream seeds{seed};
    bool is_each = true;
    for (std::uint32_t round = 0; round < 16; ++round) {
        auto const leaves = draw_values<T, Leaves>(seeds.next_wide());
        T const from_net =
            slot_at(evaluate_net<1>(tree_net_of<Leaves, false>, leaves, T{}, AddValues{}, core::Ascending{}), 0);
        core::View<T const> const run{leaves.view()};
        is_each = is_each && has_same_bits(from_net, core::sum_by_tree(run))
               && has_same_bits(from_net, core::sum_by_tree_in_lanes<4>(run))
               && has_same_bits(from_net, core::sum_by_tree_in_lanes<8>(run))
               && has_same_bits(from_net, core::sum_by_tree_in_lanes<16>(run));
    }
    return is_each;
}

// The net of the left fold gives bits that sum_by_tree does not give, on
// some of 64 draws, so the bit test tells the two plans apart.  A few large
// elements can rule a sum, and then the two plans round to the same bits.
// So one draw is not sufficient.  On the float draws of the seed of main,
// the bits differ on 45 of the 64 draws.
template <class T, std::size_t Leaves>
[[nodiscard]] bool does_fold_differ_on_some(std::uint64_t seed) noexcept {
    ::crucible::test::PhiloxStream seeds{seed};
    bool is_apart = false;
    for (std::uint32_t round = 0; round < 64; ++round) {
        auto const leaves = draw_values<T, Leaves>(seeds.next_wide());
        T const from_fold =
            slot_at(evaluate_net<1>(fold_net_of<Leaves>, leaves, T{}, AddValues{}, core::Ascending{}), 0);
        is_apart = is_apart || !has_same_bits(from_fold, core::sum_by_tree(core::View<T const>{leaves.view()}));
    }
    return is_apart;
}

// The sum scan of Lanes lanes, or of the lanes of the target for Lanes 0.
template <int Lanes, core::ScanKind Kind, class T, class Out>
[[nodiscard]] bool scan_sums_in(core::View<T const> in, Out out) noexcept {
    if constexpr (Lanes == 0 && Kind == core::ScanKind::Inclusive) {
        return core::scan_inclusive_sums(in, std::move(out)).is_ok();
    } else if constexpr (Lanes == 0) {
        return core::scan_exclusive_sums(in, std::move(out)).is_ok();
    } else if constexpr (Kind == core::ScanKind::Inclusive) {
        return core::scan_inclusive_sums_in_lanes<Lanes>(in, std::move(out)).is_ok();
    } else {
        return core::scan_exclusive_sums_in_lanes<Lanes>(in, std::move(out)).is_ok();
    }
}

// The sum scan of Lanes lanes gives the bits of the net of the scan plan at
// each output.  The empty value of an exclusive sum scan is +0.
template <int Lanes, class T, std::size_t Count, core::ScanKind Kind>
[[nodiscard]] bool does_sum_scan_follow(std::uint64_t seed) noexcept {
    auto const leaves = draw_values<T, Count>(seed);
    auto const from_net = evaluate_net<Count>(scan_net_of<Count, Kind>, leaves, T{}, AddValues{}, core::Ascending{});
    core::FixedArray<T, Count> scanned{};
    bool is_written = false;
    scanned = core::with_mut(std::move(scanned), [&](auto out) noexcept {
        is_written = scan_sums_in<Lanes, Kind>(core::View<T const>{leaves.view()}, std::move(out));
    });
    std::size_t position = 0;
    for (T const sum : scanned) {
        is_written = is_written && has_same_bits(sum, slot_at(from_net, position));
        ++position;
    }
    return is_written;
}

// Each count of lanes follows the net, on 16 draws.
template <class T, std::size_t Count, core::ScanKind Kind>
[[nodiscard]] bool does_each_sum_scan_follow(std::uint64_t seed) noexcept {
    ::crucible::test::PhiloxStream seeds{seed};
    bool is_each = true;
    for (std::uint32_t round = 0; round < 16; ++round) {
        std::uint64_t const draw = seeds.next_wide();
        is_each = is_each && does_sum_scan_follow<0, T, Count, Kind>(draw)
               && does_sum_scan_follow<4, T, Count, Kind>(draw) && does_sum_scan_follow<16, T, Count, Kind>(draw);
    }
    return is_each;
}

// sort_by_network gives the tags of the net of its plan on tickets that
// Philox draws.
template <std::size_t Count>
[[nodiscard]] bool does_each_sort_follow(std::uint64_t seed) noexcept {
    ::crucible::test::PhiloxStream stream{seed};
    bool is_each = true;
    for (std::uint32_t round = 0; round < 64; ++round) {
        is_each = is_each && does_sort_follow<Count, false>(draw_tickets<Count>(stream.next_wide()));
    }
    return is_each;
}

}  // namespace

int main() {
    bool const is_tree_float =
        does_tree_sum_follow<float, 6>(0x7E570101u) && does_tree_sum_follow<float, 13>(0x7E570102u)
        && does_tree_sum_follow<float, 37>(0x7E570103u) && does_tree_sum_follow<float, 64>(0x7E570104u);
    bool const is_tree_double =
        does_tree_sum_follow<double, 13>(0x7E570105u) && does_tree_sum_follow<double, 37>(0x7E570106u);
    bool const is_fold_apart =
        does_fold_differ_on_some<float, 37>(0x7E570501u) && does_fold_differ_on_some<double, 37>(0x7E570502u);
    bool const is_inclusive = does_each_sum_scan_follow<float, 13, core::ScanKind::Inclusive>(0x7E570201u)
                           && does_each_sum_scan_follow<float, 37, core::ScanKind::Inclusive>(0x7E570202u)
                           && does_each_sum_scan_follow<double, 37, core::ScanKind::Inclusive>(0x7E570203u);
    bool const is_exclusive = does_each_sum_scan_follow<float, 13, core::ScanKind::Exclusive>(0x7E570301u)
                           && does_each_sum_scan_follow<float, 37, core::ScanKind::Exclusive>(0x7E570302u)
                           && does_each_sum_scan_follow<double, 37, core::ScanKind::Exclusive>(0x7E570303u);
    bool const is_sort = does_each_sort_follow<7>(0x7E570401u) && does_each_sort_follow<16>(0x7E570402u)
                      && does_each_sort_follow<37>(0x7E570403u);
    assert(is_tree_float);
    assert(is_tree_double);
    assert(is_fold_apart);
    assert(is_inclusive);
    assert(is_exclusive);
    assert(is_sort);
    return 0;
}
