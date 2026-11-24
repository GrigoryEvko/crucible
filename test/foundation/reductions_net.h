#pragma once

// The nets of the plans of the reductions library, for the match tests
// test_net_reductions_match_*.cpp.  Each plan of Reduce.h, Scan.h and Sort.h
// becomes an interaction net, a symbolic run reads the combines of an
// implementation against the adds of the net, and an evaluation of the net
// gives its value on real elements.
//
//   The net of a plan     a num agent for each leaf, an add for each step of a
//                         combine, a cmp for each compare-exchange, a chain of
//                         dup agents for each value with more than one use, a
//                         boundary result for each output, and a boundary drop
//                         for a leaf that no step and no output reads
//   The symbolic run      the implementation runs on symbols: the value of a
//                         node is the node, and a combine of a and b finds the
//                         add whose left port takes a and whose right port
//                         takes b.  Each combine of the implementation is one
//                         add of the net, in port order, one time each
//   The evaluation        the value of each result agent, from the wires of
//                         the net and not from the plan
//   The draws             floats and doubles whose exponents spread, so a
//                         change of the association of a sum changes its
//                         bits, and tickets whose keys tie

#include <foundation/algebra/net/Net.h>
#include <foundation/core/Array.h>
#include <foundation/core/Reduce.h>
#include <foundation/core/Scalar.h>
#include <foundation/core/Scan.h>
#include <foundation/core/Sort.h>

#include "philox_stream.h"

#include <cstddef>
#include <cstdint>

namespace crucible::test::reductions_net {

namespace net = ::foundation::algebra::net;
namespace core = ::foundation::core;

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
// empty_plan_node for the empty value of an exclusive scan or of an empty
// segment, which a num agent of its own gives.

template <std::size_t Leaves, std::size_t Steps, std::size_t Outputs>
struct CombinePlan {
    core::FixedArray<core::TreeStep, Steps> steps{};
    core::FixedArray<core::PlanNode, Outputs> outputs{};
};

// The index of a position of the array that the plan bounds.
template <class T, std::size_t N>
[[nodiscard]] constexpr core::Index<N> slot_index(core::FixedArray<T, N> const& /*array*/,
                                                  std::size_t position) noexcept {
    return core::Index<N>::check(position).expect("a position that the plan bounds");
}

// The element at a position that the plan bounds, for a read.
template <class T, std::size_t N>
[[nodiscard]] constexpr T const& slot_at(core::FixedArray<T, N> const& array, std::size_t position) noexcept {
    return array.at(slot_index(array, position));
}

// A temporary array ends with the full expression, so its slot comes back as
// a copy and not as a reference.
template <class T, std::size_t N>
[[nodiscard]] constexpr T slot_at(core::FixedArray<T, N> const&& array, std::size_t position) noexcept {
    return array.at(slot_index(array, position));
}

// The uses of each node: one for each step that reads it and one for each
// output.  The last node counts the outputs of the empty value.
template <std::size_t Leaves, std::size_t Steps, std::size_t Outputs>
[[nodiscard]] consteval core::FixedArray<std::uint32_t, Leaves + Steps + 1>
count_uses(CombinePlan<Leaves, Steps, Outputs> const& plan) noexcept {
    core::FixedArray<std::uint32_t, Leaves + Steps + 1> uses{};
    for (core::TreeStep const& step : plan.steps) {
        auto const left_slot = slot_index(uses, static_cast<std::size_t>(step.left));
        uses.set(left_slot, uses.at(left_slot) + 1);
        auto const right_slot = slot_index(uses, static_cast<std::size_t>(step.right));
        uses.set(right_slot, uses.at(right_slot) + 1);
    }
    for (core::PlanNode const output : plan.outputs) {
        std::size_t const node = output == core::empty_plan_node ? Leaves + Steps : static_cast<std::size_t>(output);
        auto const slot = slot_index(uses, node);
        uses.set(slot, uses.at(slot) + 1);
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
        return fired.with_mut([this, a, b](auto run) noexcept {
            std::uint32_t step = 0;
            for (std::uint32_t& count : run) {
                if (slot_at(left, step) == a.node && slot_at(right, step) == b.node && count == 0) {
                    count = 1;
                    return Symbol{leaves + step};
                }
                ++step;
            }
            is_stray = true;
            return Symbol{no_add};
        });
    }

    // The combine of an add that took a combine before, with the same
    // operands, gives the node of that add again: a split form computes a
    // value of the net two times.  A combine that no add takes marks the run
    // as stray.
    [[nodiscard]] constexpr Symbol combine_or_repeat(Symbol a, Symbol b) noexcept {
        bool const was_stray = is_stray;
        Symbol const first = combine(a, b);
        if (first.node != no_add) return first;
        is_stray = was_stray;
        return fired.with_mut([this, a, b](auto run) noexcept {
            std::uint32_t step = 0;
            for (std::uint32_t& count : run) {
                if (slot_at(left, step) == a.node && slot_at(right, step) == b.node) {
                    ++count;
                    return Symbol{leaves + step};
                }
                ++step;
            }
            is_stray = true;
            return Symbol{no_add};
        });
    }

    // Each add took one combine.
    [[nodiscard]] constexpr bool is_each_add_fired() const noexcept {
        for (std::uint32_t const count : fired) {
            if (count != 1) return false;
        }
        return !is_stray;
    }

    // Each add took one combine or more, and no combine was stray.
    [[nodiscard]] constexpr bool is_each_add_fired_at_least_once() const noexcept {
        for (std::uint32_t const count : fired) {
            if (count == 0) return false;
        }
        return !is_stray;
    }
};

template <std::size_t Leaves, std::size_t Steps, std::size_t Agents, std::size_t Wires>
[[nodiscard]] consteval Matcher<Steps> matcher_of(net::Net<1, 6, Agents, Wires> const& graph) noexcept {
    Matcher<Steps> matcher{};
    matcher.leaves = static_cast<std::uint32_t>(Leaves);
    for (std::uint32_t step = 0; step < Steps; ++step) {
        std::uint32_t const left_node = source_of(graph, port_of(static_cast<std::uint32_t>(Leaves) + step, 0));
        std::uint32_t const right_node = source_of(graph, port_of(static_cast<std::uint32_t>(Leaves) + step, 1));
        matcher.left.set(slot_index(matcher.left, step), left_node);
        matcher.right.set(slot_index(matcher.right, step), right_node);
    }
    return matcher;
}

template <std::size_t Leaves>
[[nodiscard]] consteval core::FixedArray<Symbol, Leaves> symbols_of_leaves() noexcept {
    core::FixedArray<Symbol, Leaves> symbols{};
    symbols.with_mut([](auto run) noexcept {
        std::uint32_t node = 0;
        for (Symbol& symbol : run) {
            symbol.node = node;
            ++node;
        }
    });
    return symbols;
}

// ── The plans and their nets ────────────────────────────────────────────

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
    plan.steps.with_mut([](auto run) noexcept {
        for (core::TreeStep& step : run) {
            core::TreeStep const swapped{step.right, step.left};
            step = swapped;
            break;
        }
    });
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
    plan.steps.with_mut([&previous, &next_node, &leaf](auto run) noexcept {
        for (core::TreeStep& step : run) {
            step = core::TreeStep{core::PlanNode{previous}, core::PlanNode{leaf}};
            previous = next_node;
            ++next_node;
            ++leaf;
        }
    });
    plan.outputs = core::FixedArray<core::PlanNode, 1>{core::PlanNode{previous}};
    return plan;
}

template <std::size_t Leaves>
inline constexpr auto fold_net_of =
    build_combine_net<count_agents(fold_combine_plan<Leaves>()), count_wires(fold_combine_plan<Leaves>())>(
        fold_combine_plan<Leaves>());

template <std::size_t Leaves, bool IsSwapped>
inline constexpr auto tree_plan_of = IsSwapped ? swapped_tree_combine_plan<Leaves>() : tree_combine_plan<Leaves>();

template <std::size_t Leaves, bool IsSwapped>
inline constexpr auto tree_net_of =
    build_combine_net<count_agents(tree_plan_of<Leaves, IsSwapped>), count_wires(tree_plan_of<Leaves, IsSwapped>)>(
        tree_plan_of<Leaves, IsSwapped>);

template <std::size_t Count, core::ScanKind Kind>
inline constexpr std::size_t scan_steps_of = core::count_scan_plan_steps(Count, Kind).expect("a scan of a short run");

template <std::size_t Count, core::ScanKind Kind>
[[nodiscard]] consteval CombinePlan<Count, scan_steps_of<Count, Kind>, Count> scan_combine_plan() noexcept {
    CombinePlan<Count, scan_steps_of<Count, Kind>, Count> plan{};
    bool const is_written = plan.steps.with_mut([&plan](auto steps) noexcept {
        return plan.outputs.with_mut([&steps](auto outputs) noexcept {
            return core::write_scan_plan(Kind, std::move(steps), std::move(outputs)).is_ok();
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
    values.value.set(slot_index(values.value, slot), computed);
    values.is_known.set(slot_index(values.is_known, slot), true);
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
        net::PortRef const giver = is_left_giver ? wire.left : wire.right;
        values.giver.set(slot_index(values.giver, slot_of(is_left_giver ? wire.right : wire.left)), giver);
    }
    core::FixedArray<T, Outputs> outputs{};
    std::size_t output = 0;
    for (std::uint32_t agent = 0; agent < Agents; ++agent) {
        if (graph.agents[agent] != result_kind) continue;
        T const result = value_at(graph, leaves, empty, combine, less, values, port_of(agent, 0));
        outputs.set(slot_index(outputs, output), result);
        ++output;
    }
    return outputs;
}

// ── The draws of the bit tests ──────────────────────────────────────────
//
// A float with a random sign, a random mantissa and an exponent from -12 to
// 11, and a double with an exponent from -20 to 19.  A change of the
// association of a sum then changes its bits.

struct AddValues {
    template <class T>
    [[nodiscard]] constexpr T operator()(T left, T right) const noexcept {
        return left + right;
    }
};

template <class T>
[[nodiscard]] T draw_value(PhiloxStream& stream) noexcept {
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
    PhiloxStream stream{seed};
    core::FixedArray<T, Count> values{};
    values.with_mut([&stream](auto run) noexcept {
        for (T& drawn : run) {
            drawn = draw_value<T>(stream);
        }
    });
    return values;
}

[[nodiscard]] inline bool has_same_bits(float left, float right) noexcept {
    return core::bit_cast<std::uint32_t>(left) == core::bit_cast<std::uint32_t>(right);
}

[[nodiscard]] inline bool has_same_bits(double left, double right) noexcept {
    return core::bit_cast<std::uint64_t>(left) == core::bit_cast<std::uint64_t>(right);
}

}  // namespace crucible::test::reductions_net
