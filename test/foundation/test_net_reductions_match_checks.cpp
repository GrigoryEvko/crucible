// The match of the reductions library and its nets, part one: the checks of
// the nets, the symbolic runs of the forms with the combine of a caller, and
// the order of the sorts.  The nets come from reductions_net.h.
//
//   The checks            wiring, dataflow and the vicious circle for each
//                         net, the forest for each tree, and the value table
//                         of rules, which allocates nothing
//   The symbolic runs     reduce_by_tree and the two scans combine the adds of
//                         their net, in port order, one time each
//   The ties              sort_by_network sorts tickets by a key with ties,
//                         and it gives the tags of the evaluation of the net
//
// Two controls show that the tests can refuse a wrong net.  A tree plan with
// the operands of one step swapped passes each structural check, and the
// symbolic run refuses it.  A sort net with the inputs of its first cmp
// crossed sorts the keys, and the tie test refuses it.

#include <foundation/algebra/net/Net.h>
#include <foundation/algebra/net/Rules.h>
#include <foundation/core/Array.h>
#include <foundation/core/Reduce.h>
#include <foundation/core/Scan.h>
#include <foundation/core/Sort.h>

#include "../test_assert.h"
#include "philox_stream.h"
#include "reductions_net.h"

#include <cstddef>
#include <cstdint>

namespace {

using namespace ::crucible::test::reductions_net;

static_assert(net::check_forest(fold_net_of<37>).is_admitted());

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
    bool const is_sort = does_each_sort_follow<7>(0x7E570401u) && does_each_sort_follow<16>(0x7E570402u)
                      && does_each_sort_follow<37>(0x7E570403u);
    assert(is_sort);
    return 0;
}
