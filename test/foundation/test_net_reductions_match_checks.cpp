// The match of the reductions library and its nets, part one: the checks of
// the nets, the symbolic runs of the forms with the combine of a caller, and
// the order of the sorts.  The nets come from reductions_net.h.
//
//   The checks            wiring, dataflow and the vicious circle for each
//                         net that a unit of the match test reads, the forest
//                         for each tree, and the value table of rules, which
//                         allocates nothing
//   The symbolic runs     reduce_by_tree and the two scans combine the adds of
//                         their net, in port order, one time each
//   The ties              sort_by_network sorts tickets by a key with ties,
//                         and it gives the tags of the evaluation of the net
//
// Two controls show that the tests can refuse a wrong net.  A tree plan with
// the operands of one step swapped passes each structural check, and the
// symbolic run refuses it.  A sort net with the inputs of its first cmp
// crossed sorts the keys, and the tie test refuses it.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/algebra/net/Net.h>
#include <foundation/algebra/net/Rules.h>
#include <foundation/core/Array.h>
#include <foundation/core/Reduce.h>
#include <foundation/core/Scan.h>
#include <foundation/core/Sort.h>
#include <foundation/effects/Effect.h>

#include "philox_stream.h"
#include "reductions_net.h"

#include <cstddef>
#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace {

using namespace ::crucible::test::reductions_net;

// ── The checks of the nets ──────────────────────────────────────────────
//
// Each net that a unit of the match test reads passes the checks of its
// shape: each net passes the wiring, the dataflow and the vicious circle, and
// each tree is a forest.  The bits units read the trees of 5, 6, 7, 8, 13, 37
// and 64 leaves, the fold of 37, and the scans of 13 and 37 of each kind.
// This unit reads the trees of 6, 13 and 37, the scans of 8, 13 and 37, the
// net of the segments, and the sorts of 4, 7, 16 and 37 positions.

template <auto const& Graph>
inline constexpr bool is_dataflow_net =
    net::check_wiring(Graph).is_admitted() && net::check_dataflow(Graph).is_admitted()
    && net::check_vicious(Graph).is_admitted();

template <auto const& Graph>
inline constexpr bool is_tree_net = is_dataflow_net<Graph> && net::check_forest(Graph).is_admitted();

static_assert(is_tree_net<tree_net_of<5, false>> && is_tree_net<tree_net_of<7, false>>);
static_assert(is_tree_net<tree_net_of<8, false>>);
static_assert(is_tree_net<tree_net_of<6, false>> && is_tree_net<tree_net_of<13, false>>);
static_assert(is_tree_net<tree_net_of<37, false>> && is_tree_net<tree_net_of<64, false>>);
static_assert(is_tree_net<fold_net_of<37>>);
static_assert(is_dataflow_net<scan_net_of<8, core::ScanKind::Inclusive>>);
static_assert(is_dataflow_net<scan_net_of<8, core::ScanKind::Exclusive>>);
static_assert(is_dataflow_net<scan_net_of<13, core::ScanKind::Inclusive>>);
static_assert(is_dataflow_net<scan_net_of<13, core::ScanKind::Exclusive>>);
static_assert(is_dataflow_net<scan_net_of<37, core::ScanKind::Inclusive>>);
static_assert(is_dataflow_net<scan_net_of<37, core::ScanKind::Exclusive>>);

// A value with two uses needs its copies, and the copies meet again, so the
// scan net is no forest.
static_assert(!net::check_forest(scan_net_of<8, core::ScanKind::Inclusive>).is_admitted());

// ── The tree reduce ─────────────────────────────────────────────────────

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

static_assert(does_tree_reduce_follow<6, false>());
static_assert(does_tree_reduce_follow<13, false>());

// The swapped plan passes each structural check, and the run refuses it.
static_assert(net::check_forest(tree_net_of<6, true>).is_admitted());
static_assert(net::check_dataflow(tree_net_of<6, true>).is_admitted());
static_assert(!does_tree_reduce_follow<6, true>());

// ── The scans ───────────────────────────────────────────────────────────

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
    bool const is_written = out.with_mut([&leaves, &combine, empty_node](auto written) noexcept {
        if constexpr (Kind == core::ScanKind::Inclusive) {
            return core::scan_inclusive(core::View<Symbol const>{leaves.view()}, std::move(written), combine).is_ok();
        } else {
            return core::scan_exclusive(core::View<Symbol const>{leaves.view()}, std::move(written), Symbol{empty_node},
                                        combine)
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

static_assert(does_scan_follow<8, core::ScanKind::Inclusive>());
static_assert(does_scan_follow<13, core::ScanKind::Inclusive>());
static_assert(does_scan_follow<8, core::ScanKind::Exclusive>());
static_assert(does_scan_follow<13, core::ScanKind::Exclusive>());

// ── The plan evaluators ─────────────────────────────────────────────────

// evaluate_tree_plan of the plan of the tree combines the adds of its net, in
// port order, one time each, and gives the node that the result agent takes.
template <std::size_t Leaves>
[[nodiscard]] consteval bool does_tree_plan_follow() noexcept {
    auto const& graph = tree_net_of<Leaves, false>;
    auto matcher = matcher_of<Leaves, Leaves - 1>(graph);
    auto const leaves = symbols_of_leaves<Leaves>();
    core::FixedArray<Symbol, Leaves - 1> scratch{};
    Symbol const root = scratch.with_mut([&leaves, &matcher](auto values) noexcept {
        return core::evaluate_tree_plan(
                   core::View<core::TreeStep const>{tree_plan_of<Leaves, false>.steps.view()},
                   core::View<Symbol const>{leaves.view()}, std::move(values),
                   [&](Symbol const& a, Symbol const& b) noexcept { return matcher.combine(a, b); })
            .expect("a plan of the count of its leaves");
    });
    std::uint32_t const result = source_of(graph, port_of(static_cast<std::uint32_t>(2 * Leaves), 0));
    return matcher.is_each_add_fired() && root.node == result;
}

static_assert(does_tree_plan_follow<6>());
static_assert(does_tree_plan_follow<13>());

// evaluate_scan_plan of the plan of the scan combines the adds of its net, in
// port order, one time each, and each output is the node that its result
// agent takes.
template <std::size_t Count, core::ScanKind Kind>
[[nodiscard]] consteval bool does_scan_plan_follow() noexcept {
    constexpr std::size_t steps = scan_steps_of<Count, Kind>;
    auto const& graph = scan_net_of<Count, Kind>;
    auto const& plan = scan_plan_of<Count, Kind>;
    auto matcher = matcher_of<Count, steps>(graph);
    auto const leaves = symbols_of_leaves<Count>();
    constexpr auto empty_node = static_cast<std::uint32_t>(Count + steps);
    core::FixedArray<Symbol, steps> scratch{};
    core::FixedArray<Symbol, Count> out{};
    bool const is_evaluated = scratch.with_mut([&out, &plan, &leaves, &matcher, empty_node](auto values) noexcept {
        return out.with_mut([&values, &plan, &leaves, &matcher, empty_node](auto written) noexcept {
            return core::evaluate_scan_plan(
                       core::View<core::TreeStep const>{plan.steps.view()},
                       core::View<core::PlanNode const>{plan.outputs.view()}, core::View<Symbol const>{leaves.view()},
                       std::move(values), std::move(written), Symbol{empty_node},
                       [&](Symbol const& a, Symbol const& b) noexcept { return matcher.combine(a, b); })
                .is_ok();
        });
    });
    bool is_each_output = is_evaluated;
    std::uint32_t position = 0;
    for (Symbol const& symbol : out) {
        is_each_output = is_each_output && symbol.node == source_of(graph, port_of(empty_node + 1 + position, 0));
        ++position;
    }
    return is_each_output && matcher.is_each_add_fired();
}

static_assert(does_scan_plan_follow<8, core::ScanKind::Inclusive>());
static_assert(does_scan_plan_follow<13, core::ScanKind::Inclusive>());
static_assert(does_scan_plan_follow<8, core::ScanKind::Exclusive>());
static_assert(does_scan_plan_follow<13, core::ScanKind::Exclusive>());

// ── The grains ──────────────────────────────────────────────────────────

// reduce_grains_by_tree of the first grain, and of the other grains in a
// second call, then reduce_by_tree of the partials, combines the adds of the
// tree net of the run, in port order, one time each.
template <std::size_t Leaves, std::size_t Grain>
[[nodiscard]] consteval bool does_grain_reduce_follow() noexcept {
    constexpr std::size_t grains = core::count_tree_grains<Grain>(Leaves);
    auto const& graph = tree_net_of<Leaves, false>;
    auto matcher = matcher_of<Leaves, Leaves - 1>(graph);
    auto const leaves = symbols_of_leaves<Leaves>();
    core::View<Symbol const> const run{leaves.view()};
    auto const combine = [&](Symbol const& a, Symbol const& b) noexcept { return matcher.combine(a, b); };
    core::FixedArray<Symbol, 1> head{};
    core::FixedArray<Symbol, grains - 1> tail{};
    head.with_mut(
        [run, &combine](auto out) noexcept { core::reduce_grains_by_tree<Grain>(run, 0, std::move(out), combine); });
    tail.with_mut(
        [run, &combine](auto out) noexcept { core::reduce_grains_by_tree<Grain>(run, 1, std::move(out), combine); });
    core::FixedArray<Symbol, grains> partials{};
    Symbol const first = slot_at(head, 0);
    partials.with_element(slot_index(partials, 0), [first](Symbol& held) noexcept { held = first; });
    for (std::size_t grain = 1; grain < grains; ++grain) {
        Symbol const element = slot_at(tail, grain - 1);
        partials.with_element(slot_index(partials, grain), [element](Symbol& held) noexcept { held = element; });
    }
    Symbol const root = core::reduce_by_tree(partials.view(), combine).expect("a run of grains");
    std::uint32_t const result = source_of(graph, port_of(static_cast<std::uint32_t>(2 * Leaves), 0));
    return matcher.is_each_add_fired() && root.node == result;
}

static_assert(does_grain_reduce_follow<13, 4>());

// The scans of the grains, with the carries of the exclusive scan of the tree
// sums of the grains, combine only adds of the scan net of the run, in port
// order, and each add at least one time.  The scan of a grain makes the sums
// of the left halves inside the grain again, which the tree sum of the grain
// made before, so such an add takes two combines of the same operands.  The
// carries read no sum of the last grain, so the run reduces each grain but
// the last one.
template <std::size_t Count, std::size_t Grain, core::ScanKind Kind>
[[nodiscard]] consteval bool does_grain_scan_follow() noexcept {
    constexpr std::size_t steps = scan_steps_of<Count, Kind>;
    constexpr std::size_t grains = core::count_tree_grains<Grain>(Count);
    auto const& graph = scan_net_of<Count, Kind>;
    auto matcher = matcher_of<Count, steps>(graph);
    auto const leaves = symbols_of_leaves<Count>();
    core::View<Symbol const> const run{leaves.view()};
    auto const combine = [&](Symbol const& a, Symbol const& b) noexcept { return matcher.combine_or_repeat(a, b); };
    constexpr auto empty_node = static_cast<std::uint32_t>(Count + steps);
    core::FixedArray<Symbol, grains - 1> heads{};
    heads.with_mut(
        [run, &combine](auto out) noexcept { core::reduce_grains_by_tree<Grain>(run, 0, std::move(out), combine); });
    core::FixedArray<Symbol, grains> sums{};
    for (std::size_t grain = 0; grain + 1 < grains; ++grain) {
        Symbol const element = slot_at(heads, grain);
        sums.with_element(slot_index(sums, grain), [element](Symbol& held) noexcept { held = element; });
    }
    sums.with_element(slot_index(sums, grains - 1), [](Symbol& held) noexcept { held = Symbol{no_add}; });
    core::FixedArray<Symbol, grains> carries{};
    bool const is_scanned = carries.with_mut([&sums, &combine, empty_node](auto out) noexcept {
        return core::scan_exclusive(core::View<Symbol const>{sums.view()}, std::move(out), Symbol{empty_node}, combine)
            .is_ok();
    });
    core::FixedArray<Symbol, Count> out{};
    out.with_mut([run, &carries, &combine, empty_node](auto written) noexcept {
        core::View<Symbol const> const carried{carries.view()};
        if constexpr (Kind == core::ScanKind::Inclusive) {
            core::scan_inclusive_grains<Grain>(run, 0, carried, std::move(written), combine);
        } else {
            core::scan_exclusive_grains<Grain>(run, 0, carried, std::move(written), Symbol{empty_node}, combine);
        }
    });
    bool is_each_output = is_scanned;
    std::uint32_t position = 0;
    for (Symbol const& symbol : out) {
        is_each_output = is_each_output && symbol.node == source_of(graph, port_of(empty_node + 1 + position, 0));
        ++position;
    }
    return is_each_output && matcher.is_each_add_fired_at_least_once();
}

static_assert(does_grain_scan_follow<13, 4, core::ScanKind::Inclusive>());
static_assert(does_grain_scan_follow<13, 4, core::ScanKind::Exclusive>());

// ── The segments ────────────────────────────────────────────────────────
//
// The segments of 13 leaves at the offsets 0, 3, 3, 9 and 13: a tree of 3
// leaves, an empty segment, a tree of 6 and a tree of 4.  The plan of each
// segment is plan_tree of its count, with its leaves and its steps moved to
// the place of the segment.  The empty segment takes the num of no leaf.

inline constexpr std::size_t segment_leaves = 13;
inline constexpr std::size_t segment_steps = 2 + 5 + 3;
inline constexpr core::FixedArray<std::size_t, 5> segment_offsets{0, 3, 3, 9, 13};

[[nodiscard]] consteval CombinePlan<segment_leaves, segment_steps, 4> segment_combine_plan() noexcept {
    CombinePlan<segment_leaves, segment_steps, 4> plan{};
    std::uint32_t base = 0;
    // Gives the root of the segment of Count leaves from first_leaf.
    auto const append = [&]<std::size_t Count>(core::PlanNode first_leaf) constexpr noexcept {
        auto const first = static_cast<std::uint32_t>(first_leaf);
        auto const moved = [&](core::PlanNode node) constexpr noexcept {
            auto const index = static_cast<std::uint32_t>(node);
            return index < Count ? core::PlanNode{first + index}
                                 : core::PlanNode{static_cast<std::uint32_t>(segment_leaves) + base + index
                                                  - static_cast<std::uint32_t>(Count)};
        };
        std::uint32_t step = 0;
        for (core::TreeStep const& one : core::plan_tree<Count>()) {
            core::TreeStep const moved_step{moved(one.left), moved(one.right)};
            plan.steps.with_element(slot_index(plan.steps, base + step),
                                    [moved_step](core::TreeStep& held) noexcept { held = moved_step; });
            ++step;
        }
        core::PlanNode const root{static_cast<std::uint32_t>(segment_leaves) + base + step - 1};
        base += step;
        return root;
    };
    plan.outputs = core::FixedArray<core::PlanNode, 4>{
        append.template operator()<3>(core::PlanNode{0}), core::empty_plan_node,
        append.template operator()<6>(core::PlanNode{3}), append.template operator()<4>(core::PlanNode{9})};
    return plan;
}

inline constexpr auto segment_plan = segment_combine_plan();
inline constexpr auto segment_net =
    build_combine_net<count_agents(segment_plan), count_wires(segment_plan)>(segment_plan);

static_assert(is_tree_net<segment_net>);

// reduce_segments_by_tree combines the adds of the net of the segments, in
// port order, one time each, and each result is the node that its result
// agent takes.
[[nodiscard]] consteval bool does_segment_reduce_follow() noexcept {
    auto matcher = matcher_of<segment_leaves, segment_steps>(segment_net);
    auto const leaves = symbols_of_leaves<segment_leaves>();
    constexpr auto empty_node = static_cast<std::uint32_t>(segment_leaves + segment_steps);
    core::FixedArray<Symbol, 4> results{};
    bool const is_reduced = results.with_mut([&leaves, &matcher, empty_node](auto out) noexcept {
        return core::reduce_segments_by_tree(
                   core::View<Symbol const>{leaves.view()}, core::View<std::size_t const>{segment_offsets.view()},
                   std::move(out), Symbol{empty_node},
                   [&](Symbol const& a, Symbol const& b) noexcept { return matcher.combine(a, b); })
            .is_ok();
    });
    bool is_each_result = is_reduced;
    std::uint32_t segment = 0;
    for (Symbol const& symbol : results) {
        is_each_result = is_each_result && symbol.node == source_of(segment_net, port_of(empty_node + 1 + segment, 0));
        ++segment;
    }
    return is_each_result && matcher.is_each_add_fired();
}

static_assert(does_segment_reduce_follow());

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
        net::PortRef const port = port_of(position, 0);
        giver.with_element(slot_index(giver, position), [port](net::PortRef& held) noexcept { held = port; });
    }
    std::uint32_t agent = count;
    std::uint32_t wire = 0;
    for (core::Comparator const& step : core::plan_sort_network<Count>()) {
        bool const is_crossed = IsCrossed && agent == count;
        graph.agents[agent] = cmp_kind;
        graph.wires[wire] = net::Wire{slot_at(giver, is_crossed ? step.high : step.low), port_of(agent, 0)};
        graph.wires[wire + 1] = net::Wire{slot_at(giver, is_crossed ? step.low : step.high), port_of(agent, 1)};
        net::PortRef const low_port = port_of(agent, 2);
        net::PortRef const high_port = port_of(agent, 3);
        giver.with_element(slot_index(giver, step.low), [low_port](net::PortRef& held) noexcept { held = low_port; });
        giver.with_element(slot_index(giver, step.high),
                           [high_port](net::PortRef& held) noexcept { held = high_port; });
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

static_assert(is_dataflow_net<sort_net_of<4>> && is_dataflow_net<sort_net_of<7>>);
static_assert(is_dataflow_net<sort_net_of<16>> && is_dataflow_net<sort_net_of<37>>);
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
              net::Rule{"cmp_high_meets_num", cmp_high_kind, num_kind, two_nums}},
    // dup >< num and cmp_high >< num make 2 agents from 2, so dup, cmp and
    // cmp_high weigh more than num.
    .step_weights = {net::KindWeight{dup_kind, net::StepWeight{1}}, net::KindWeight{cmp_kind, net::StepWeight{1}},
                     net::KindWeight{cmp_high_kind, net::StepWeight{1}}}};

inline constexpr net::RuleVerdict value_rules_verdict = net::check_rules(value_rules);
static_assert(value_rules_verdict.is_admitted(), value_rules_verdict);
static_assert(value_rules_verdict.allocates_nothing());

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
    tickets.with_mut([&stream](auto run) noexcept {
        std::uint32_t tag = 0;
        for (Ticket& ticket : run) {
            ticket = Ticket{static_cast<std::uint32_t>(stream.below(3)), tag};
            ++tag;
        }
    });
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
    sorted.with_mut([](auto elements) noexcept { core::sort_by_network(std::move(elements), ByKey{}); });
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

// ── The stable sort ─────────────────────────────────────────────────────
//
// The stable sort of a run has one result: the elements in the order of the
// less function, and two equivalent elements in their order in the input
// (THE ORDER of Sort.h).  The reference is an insertion sort of the test,
// which moves an element only past a larger one.  sort_stably has two arms:
// the comparator arm, driftsort, for a less function of the caller, and the
// radix arm for Ascending from stable_sort_radix_count elements.

template <class T, std::size_t Count, class Less>
[[nodiscard]] constexpr core::FixedArray<T, Count> sort_by_insertion(core::FixedArray<T, Count> elements,
                                                                     Less less) noexcept {
    for (std::size_t next = 1; next < Count; ++next) {
        T const moved = slot_at(elements, next);
        std::size_t place = next;
        while (place > 0 && less(moved, slot_at(elements, place - 1))) {
            T const shifted = slot_at(elements, place - 1);
            elements.with_element(slot_index(elements, place), [shifted](T& held) noexcept { held = shifted; });
            --place;
        }
        elements.with_element(slot_index(elements, place), [moved](T& held) noexcept { held = moved; });
    }
    return elements;
}

template <class T, std::size_t Count, class Less>
[[nodiscard]] constexpr core::FixedArray<T, Count> sort_by_either_arm(core::FixedArray<T, Count> elements,
                                                                      Less less) noexcept {
    core::FixedArray<T, Count> scratch{};
    bool const is_sorted = elements.with_mut([&scratch, &less](auto run) noexcept {
        return scratch.with_mut([&run, &less](auto spare) noexcept {
            return core::sort_stably(std::move(run), std::move(spare), less).is_ok();
        });
    });
    if (!is_sorted) core::fatal("the scratch of the sort has the count of the run");
    return elements;
}

// The comparator arm keeps the order of the tags of each key, as the
// insertion sort does.  37 tickets take the eager merge tree: the small sorts
// of 32 and of 5 tickets, and one merge from the two ends.  64 tickets take
// two small sorts of 32 and one merge.
template <std::size_t Count>
[[nodiscard]] constexpr bool does_comparator_arm_follow(core::FixedArray<Ticket, Count> const& tickets) noexcept {
    return sort_by_either_arm(tickets, ByKey{}) == sort_by_insertion(tickets, ByKey{});
}

static_assert(does_comparator_arm_follow<37>(draw_tickets<37>(0x5EED0101u)));
static_assert(does_comparator_arm_follow<64>(draw_tickets<64>(0x5EED0102u)));

// The order of Ascending for a float, as a type of its own, so sort_stably
// takes the comparator arm for each count.
struct TotalOrder {
    template <class T>
    [[nodiscard]] constexpr bool operator()(T const& left, T const& right) const noexcept {
        return core::Ascending{}(left, right);
    }
};

// The elements of the radix arm: Philox draws, and the values whose order the
// total order of IEEE 754 states: -0 and +0, the two infinities, NaNs of
// each sign with two payloads, and repeated values.
template <class T, std::size_t Count>
[[nodiscard]] core::FixedArray<T, Count> draw_ordered_values(std::uint64_t seed) noexcept {
    using Bits = decltype([] {
        if constexpr (sizeof(T) == 4) {
            return std::uint32_t{};
        } else {
            return std::uint64_t{};
        }
    }());
    constexpr std::size_t mantissa_bits = sizeof(T) == 4 ? 23 : 52;
    constexpr std::size_t exponent_bits = sizeof(T) == 4 ? 8 : 11;
    constexpr Bits sign = Bits{1} << (8 * sizeof(T) - 1);
    constexpr Bits infinity = ((Bits{1} << exponent_bits) - 1) << mantissa_bits;
    constexpr Bits quiet = Bits{1} << (mantissa_bits - 1);
    auto values = draw_values<T, Count>(seed);
    core::FixedArray<Bits, 10> const specials{Bits{0},
                                              sign,
                                              infinity,
                                              sign | infinity,
                                              infinity | quiet,
                                              sign | infinity | quiet,
                                              infinity | quiet | 1u,
                                              sign | infinity | quiet | 1u,
                                              infinity | quiet,
                                              Bits{0}};
    std::size_t position = 0;
    for (Bits const bits : specials) {
        T const special = core::bit_cast<T>(bits);
        values.with_element(slot_index(values, 3 * position + 1), [special](T& held) noexcept { held = special; });
        ++position;
    }
    // Repeated values: each element of the last part copies an element of
    // the first part.
    for (std::size_t copy = Count - 20; copy < Count; ++copy) {
        T const repeated = slot_at(values, copy - (Count - 20));
        values.with_element(slot_index(values, copy), [repeated](T& held) noexcept { held = repeated; });
    }
    return values;
}

// The radix arm and the comparator arm give the same bits, and the result is
// in the total order.
template <class T, std::size_t Count>
[[nodiscard]] bool does_radix_follow_comparator(std::uint64_t seed) noexcept {
    static_assert(Count >= core::stable_sort_radix_count, "the radix arm sorts from stable_sort_radix_count");
    auto const values = draw_ordered_values<T, Count>(seed);
    auto const by_radix = sort_by_either_arm(values, core::Ascending{});
    auto const by_comparator = sort_by_either_arm(values, TotalOrder{});
    bool is_same = true;
    for (std::size_t position = 0; position < Count; ++position) {
        is_same = is_same && has_same_bits(slot_at(by_radix, position), slot_at(by_comparator, position));
        if (position > 0) {
            is_same = is_same && !core::Ascending{}(slot_at(by_radix, position), slot_at(by_radix, position - 1));
        }
    }
    return is_same;
}

// The comparator arm keeps the order of the tags of each key on runs of the
// stable quicksort and its small sorts.
template <std::size_t Count>
[[nodiscard]] bool does_each_comparator_arm_follow(std::uint64_t seed) noexcept {
    ::crucible::test::PhiloxStream stream{seed};
    bool is_each = true;
    for (std::uint32_t round = 0; round < 8; ++round) {
        is_each = is_each && does_comparator_arm_follow<Count>(draw_tickets<Count>(stream.next_wide()));
    }
    return is_each;
}

}  // namespace

int main() {
    bool const is_sort = does_each_sort_follow<7>(0x7E570401u) && does_each_sort_follow<16>(0x7E570402u)
                      && does_each_sort_follow<37>(0x7E570403u);
    bool const is_stable = does_each_comparator_arm_follow<300>(0x7E570A01u)
                        && does_radix_follow_comparator<float, 300>(0x7E570A02u)
                        && does_radix_follow_comparator<double, 300>(0x7E570A03u);
    CRUCIBLE_TEST_ASSERT(kTest, is_sort);
    CRUCIBLE_TEST_ASSERT(kTest, is_stable);
    return 0;
}
