// Microbench of the run-time form of the checks of foundation/algebra/net on
// one thread.
//
// Each row is one call of a find function on a net or a system that main
// holds in a variable, so each call runs at run time.  The nets are those of
// test_algebra_net_scale.cpp: the chain of 1,000 stages, the fork tree of
// 1,001 agents with a par group in each fork, and the ring of 1,000 processes
// with 4,000 actions.  The sink of the chain is a boundary agent here, so the
// chain has a canonical form.  Each net passes its checks, so each row runs
// the whole body of its check.
//
// The references are plain loops over the same input, with no check of the
// wiring and no fault record.  Each is the textbook form of its algorithm,
// linear or near linear in its input, so no known method is faster by more
// than a constant:
//   - the sort of Kahn over the wires of the chain, for the dataflow
//   - the union-find over the agents of the chain, for the forest, and for
//     the criterion of a net with no par group.  The graph of the agents and
//     the wires, with one edge for each port, is the graph of the agents with
//     each wire cut in two, so it is a tree exactly when the agents and the
//     wires form one tree
//   - the contraction of Danos by passes over the fork tree, for the
//     criterion with par groups
//   - the walk of the principal ports of the chain, for the vicious circles
//   - the sort of Kahn over a list of the edges with no token of the marked
//     graph of the ring, for the liveness, and the sort over the same edges
//     read from the arrays of the steps, as the body reads them, with no check
//     of the names, the sides and the ranges of the system.  The checked form
//     of that sort reads the names, the sides and the ranges in the same
//     loops, as the plain form of the body does, and it is the reference of
//     the plain form.  The bare form is the reference of the sealed system,
//     whose seal checked the form one time (the sealed system of Priority.h)
//   - the walk of the chain from its boundary agent with an explicit stack,
//     for the canonical form.  It folds the same count of words into four
//     chains, as the body does, and its word of a kind is the index of the
//     kind, where the body folds the hash of the form of the kind.
//   The sort of Kahn, the union-find and the canonical walk each have a
//   checked form too.  It runs the checks of the input that the plain form of
//   its check runs, in its own loops: the signature, the kind of each agent,
//   the two ends and the types of each wire, and the wire of each port.  The
//   bare form is the reference of a sealed net, and the checked form is the
//   reference of a plain net.
//
// Speed of light
//   A chain of dependent adds gives the cycles of one nanosecond of the core
//   in the run.  The floor row reads each wire and the kinds of its two
//   agents, with an add for each read and no check: the minimum of a check
//   that reads the plain chain.  Each line of cycles gives the p50 and the
//   p99 of one call, the floor, and the ratio to the floor.
//
// A find function on a Net runs the check of the wiring and then its body.
// A find function on a sealed net runs only its body, because seal_net
// checked the wiring one time (the sealed net of Net.h).  So the ratio of a
// row of a sealed net to its reference gives the cost of the body and of the
// fault record, and the row of seal_net gives the cost at the boundary.  The
// row of the canonical form against the check of the wiring stays, and the
// two rows of five checks give the total for a net that a program checks one
// time.
//
// Each line gives the core of its row, the percentiles of one call in
// nanoseconds, and the coefficient of variation in thousandths.  A ratio line
// gives the ratio of two medians in thousandths.

#include <fixy/Core.h>
#include <foundation/Quarantine.h>
#include <foundation/algebra/net/Net.h>
#include <foundation/algebra/net/Priority.h>

#include <cstddef>
#include <cstdint>

#include "bench_harness.h"

namespace {

namespace net = ::foundation::algebra::net;

using net::AgentId;
using net::KindId;
using net::PortRef;
using net::TypeId;
using net::Wire;

// The wall time of one row, in milliseconds.
constexpr std::size_t kRowWallMs = 2000;

// A time in nanoseconds, as the nearest integer.
[[nodiscard]] std::uint64_t nanoseconds(double value) noexcept { return static_cast<std::uint64_t>(value + 0.5); }

// Writes one line: the name of the row, the core of the row, and the
// percentiles of one call.
void print_row(fixy::TextView name, bench::Report const& measured) noexcept {
    bench::print("{}  core=cpu{}  p50={} ns  p99={} ns  max={} ns  cv={} permille\n", name, measured.pinned_cpu.raw(),
                 nanoseconds(measured.pct.p50), nanoseconds(measured.pct.p99), nanoseconds(measured.pct.max),
                 static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5));
}

// Writes the ratio of the median of a row to the median of its reference.
void print_ratio(fixy::TextView name, bench::Report const& measured, bench::Report const& reference) noexcept {
    double const base = reference.pct.p50 > 0.0 ? reference.pct.p50 : 1.0;
    bench::print("{}  ratio={} permille\n", name, static_cast<std::uint64_t>(measured.pct.p50 * 1000.0 / base + 0.5));
}

// Measures one row with the pin of CRUCIBLE_BENCH_CORE and the wall cap of
// kRowWallMs.
template <typename Body>
[[nodiscard]] bench::Report measure_row(bench::Run&& run, Body&& body) {
    if (int const core = bench::env_core(); core >= 0) static_cast<void>(run.core(core));
    static_cast<void>(run.max_wall_ms(kRowWallMs));
    return run.measure(static_cast<Body&&>(body));
}

// ── The nets ────────────────────────────────────────────────────────────

inline constexpr TypeId batch{0};

inline constexpr net::NetSignature<1, 3> chain_kinds{
    .type_names = {"Batch"},
    .kinds = {
        {.name = "starter", .port_names = {"out"}, .port_types = {net::give(batch)}},
        {.name = "stage", .port_names = {"in", "out"}, .port_types = {net::take(batch), net::give(batch)}},
        {.name = "sink", .port_names = {"in"}, .port_types = {net::take(batch)}, .is_boundary = true},
    }};

using LineNet = net::Net<1, 3, 1000, 999>;

// The chain of a starter, 998 stages and a sink.
[[nodiscard]] consteval LineNet build_chain() noexcept {
    LineNet chain{};
    chain.signature = chain_kinds;
    for (std::uint32_t agent = 0; agent < 1000; ++agent) {
        chain.agents[agent] = KindId{agent == 0 ? 0u : (agent == 999 ? 2u : 1u)};
    }
    for (std::uint32_t wire = 0; wire < 999; ++wire) {
        chain.wires[wire] = Wire{PortRef{AgentId{wire}, wire == 0 ? 0u : 1u}, PortRef{AgentId{wire + 1}, 0}};
    }
    return chain;
}

inline constexpr TypeId whole{0};
inline constexpr TypeId kids{1};

inline constexpr net::NetSignature<2, 7> fork_kinds{
    .type_names = {"Whole", "Kids"},
    .kinds = {
        {.name = "root", .port_names = {"whole"}, .port_types = {net::give(whole)}},
        {.name = "split", .port_names = {"whole", "kids"}, .port_types = {net::take(whole), net::give(kids)}},
        {.name = "fork",
         .port_names = {"kids", "left", "right"},
         .port_types = {net::take(kids), net::give(kids), net::give(kids)},
         .principal = 0,
         .par_ports = 0b110},
        {.name = "body", .port_names = {"in", "out"}, .port_types = {net::take(kids), net::give(kids)}},
        {.name = "join",
         .port_names = {"left", "right", "kids"},
         .port_types = {net::take(kids), net::take(kids), net::give(kids)},
         .principal = 2},
        {.name = "combine", .port_names = {"kids", "whole"}, .port_types = {net::take(kids), net::give(whole)}},
        {.name = "drop", .port_names = {"whole"}, .port_types = {net::take(whole)}},
    }};

using ForkNet = net::Net<2, 7, 1001, 1332>;

// The writer of the fork tree: the next agent and the next wire.
struct ForkWriter {
    ForkNet tree{};
    std::uint32_t agents = 0;
    std::uint32_t wires = 0;

    consteval AgentId add_agent(KindId kind) noexcept {
        tree.agents[agents] = kind;
        return AgentId{agents++};
    }
    consteval void add_wire(Wire wire) noexcept {
        tree.wires[wires] = wire;
        ++wires;
    }
};

// The port that takes the kids of a fork tree, and the port that gives them
// back.
struct ForkEnds {
    PortRef in{};
    PortRef out{};
};

// The count of the leaves of a fork tree.
enum class LeafCount : std::uint32_t {
};

// A fork, two trees of half the leaves, and a join, or one body for a leaf.
consteval ForkEnds write_fork(ForkWriter& writer, LeafCount count) noexcept {
    auto const leaves = static_cast<std::uint32_t>(count);
    if (leaves == 1) {
        AgentId const body = writer.add_agent(KindId{3});
        return ForkEnds{PortRef{body, 0}, PortRef{body, 1}};
    }
    AgentId const fork = writer.add_agent(KindId{2});
    AgentId const join = writer.add_agent(KindId{4});
    ForkEnds const left = write_fork(writer, LeafCount{leaves / 2});
    ForkEnds const right = write_fork(writer, LeafCount{leaves - leaves / 2});
    writer.add_wire(Wire{PortRef{fork, 1}, left.in});
    writer.add_wire(Wire{PortRef{fork, 2}, right.in});
    writer.add_wire(Wire{left.out, PortRef{join, 0}});
    writer.add_wire(Wire{right.out, PortRef{join, 1}});
    return ForkEnds{PortRef{fork, 0}, PortRef{join, 2}};
}

// The fork tree of 333 leaves: 3 * 333 + 2 agents.
[[nodiscard]] consteval ForkNet build_fork_tree() noexcept {
    ForkWriter writer{};
    writer.tree.signature = fork_kinds;
    AgentId const root = writer.add_agent(KindId{0});
    AgentId const split = writer.add_agent(KindId{1});
    ForkEnds const top = write_fork(writer, LeafCount{333});
    AgentId const combine = writer.add_agent(KindId{5});
    AgentId const drop = writer.add_agent(KindId{6});
    writer.add_wire(Wire{PortRef{root, 0}, PortRef{split, 0}});
    writer.add_wire(Wire{PortRef{split, 1}, top.in});
    writer.add_wire(Wire{top.out, PortRef{combine, 0}});
    writer.add_wire(Wire{PortRef{combine, 1}, PortRef{drop, 0}});
    return writer.tree;
}

using RingSystem = net::ProcessSystem<1000, 1000, 4000>;

// The ring of 1,000 roles over one rendezvous for each pair, with two
// iterations.  Role 0 sends first, and each other role receives first.
[[nodiscard]] consteval RingSystem build_ring() noexcept {
    RingSystem ring{};
    for (std::uint32_t role = 0; role < 1000; ++role) {
        ring.process_names[role] = net::NetName{"role"};
        ring.channels[role] = net::NetChannel{.name = net::NetName{"queue"}};
    }
    std::uint32_t next = 0;
    for (std::uint32_t role = 0; role < 1000; ++role) {
        std::uint32_t const left = (role + 999) % 1000;
        net::Action const send{
            .process = net::ProcessId{role}, .channel = net::ChannelId{role}, .move = net::Move::Send};
        net::Action const receive{
            .process = net::ProcessId{role}, .channel = net::ChannelId{left}, .move = net::Move::Receive};
        for (std::uint32_t iteration = 0; iteration < 2; ++iteration) {
            ring.actions[next] = role == 0 ? send : receive;
            ring.actions[next + 1] = role == 0 ? receive : send;
            next += 2;
        }
    }
    return ring;
}

inline constexpr LineNet chain_of_1000 = build_chain();
inline constexpr ForkNet fork_of_1001 = build_fork_tree();
inline constexpr RingSystem ring_of_1000 = build_ring();

// ── The references ──────────────────────────────────────────────────────

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the plain loops over the nets and the ring are the references")

// The form of a reference.  The bare form reads its input with no check.
// The checked form also runs the checks of the input that the plain form of
// its check runs, in the loops of the reference, and it gives 0 for a fault.
// The bare form is the reference of a sealed net or system, and the checked
// form is the reference of a plain one.
enum class ReferenceForm : std::uint8_t {
    Bare = 0,
    Checked = 1,
};

// Whether the checks of the input of a reference refuse a kind with a par
// group, as the forest does.
enum class ParGroups : std::uint8_t {
    Admitted = 0,
    Refused = 1,
};

// The tables of the kinds of the chain, as find_wiring_fault reads them: the
// count of the ports of each kind, and the type of each port as one word,
// its type index and then two bits of its polarity.
struct ChainTables {
    std::uint32_t port_count[3]{};
    std::uint32_t type_word[3 * 8]{};
};

// The checks of the signature and of the kind of each agent, and the tables
// of the kinds.  Gives false for a fault.
[[nodiscard]] bool read_chain_kinds(LineNet const& chain, ChainTables& tables, ParGroups pars) noexcept {
    if (net::find_signature_fault(chain.signature).is_fault()) return false;
    bool is_par_kind[3]{};
    for (std::uint32_t kind = 0; kind < 3; ++kind) {
        net::AgentKind const& entry = chain.signature.kinds[kind];
        while (tables.port_count[kind] < 8 && !entry.port_names[tables.port_count[kind]].is_empty())
            ++tables.port_count[kind];
        for (std::uint32_t port = 0; port < tables.port_count[kind]; ++port) {
            net::PortType const type = entry.port_types[port];
            tables.type_word[kind * 8 + port] =
                (std::uint32_t{static_cast<std::uint16_t>(type.type)} << 2) | static_cast<std::uint32_t>(type.polarity);
        }
        is_par_kind[kind] = pars == ParGroups::Refused && entry.par_ports != 0u;
    }
    for (KindId const kind : chain.agents) {
        if (net::index_of(kind) >= 3 || is_par_kind[net::index_of(kind)]) return false;
    }
    return true;
}

// The agent and the type word of each end of one wire.
struct WireEnds {
    std::uint32_t agent[2]{};
    std::uint32_t word[2]{};
};

// The checks of one wire, as find_wiring_fault reads them: each end inside
// the net and inside the ports of its agent, each port with one wire, and two
// dual types.  Gives false for a fault.
[[gnu::always_inline]] inline bool read_wire_ends(LineNet const& chain, ChainTables const& tables,
                                                  std::uint32_t (&used)[1000], Wire const& wire,
                                                  WireEnds& ends) noexcept {
    for (std::uint32_t side = 0; side < 2; ++side) {
        PortRef const end = side == 0 ? wire.left : wire.right;
        std::uint32_t const agent = net::index_of(end.agent);
        if (agent >= 1000) return false;
        std::uint32_t const kind = net::index_of(chain.agents[agent]);
        if (end.port >= tables.port_count[kind]) return false;
        std::uint32_t const bit = 1u << end.port;
        if ((used[agent] & bit) != 0u) return false;
        used[agent] |= bit;
        ends.agent[side] = agent;
        ends.word[side] = tables.type_word[kind * 8 + end.port];
    }
    std::uint32_t const first = ends.word[0];
    return ends.word[1] == ((first & 2u) == 0u ? first ^ 1u : first);
}

// Each port of each agent has a wire.
[[nodiscard]] bool has_each_port_wired(LineNet const& chain, ChainTables const& tables,
                                       std::uint32_t const (&used)[1000]) noexcept {
    for (std::uint32_t agent = 0; agent < 1000; ++agent) {
        if (used[agent] != (1u << tables.port_count[net::index_of(chain.agents[agent])]) - 1u) return false;
    }
    return true;
}

// The floor of a check of the plain chain (the speed of light): one read of
// each wire and of the kind of each of its two agents, and an add for each
// read, with no check, no table and no sort.
[[gnu::noipa]] std::uint32_t read_each_wire(LineNet const& chain) noexcept {
    std::uint32_t sum = 0;
    for (Wire const& wire : chain.wires) {
        std::uint32_t const left = net::index_of(wire.left.agent);
        std::uint32_t const right = net::index_of(wire.right.agent);
        sum += left + wire.left.port + right + wire.right.port;
        sum += net::index_of(chain.agents[left]) + net::index_of(chain.agents[right]);
    }
    return sum;
}

// The sort of Kahn over the directed wires of the chain: the producer of each
// wire is the agent of its give port.  Gives the count of the sorted agents,
// which is 1,000 for a net with no cycle of waits.  The checked form also
// refuses a meet port, as the dataflow does.
template <ReferenceForm Form>
[[nodiscard]] std::uint32_t sort_by_kahn_(LineNet const& chain) noexcept {
    constexpr bool is_checked = Form == ReferenceForm::Checked;
    [[maybe_unused]] ChainTables tables{};
    [[maybe_unused]] std::uint32_t used[is_checked ? 1000 : 1]{};
    if constexpr (is_checked) {
        if (!read_chain_kinds(chain, tables, ParGroups::Admitted)) return 0;
    }
    std::uint32_t source[999]{};
    std::uint32_t target[999]{};
    std::uint32_t indegree[1000]{};
    std::uint32_t out_first[1001]{};
    for (std::uint32_t index = 0; index < 999; ++index) {
        Wire const& wire = chain.wires[index];
        bool is_left_source = false;
        if constexpr (is_checked) {
            WireEnds ends{};
            if (!read_wire_ends(chain, tables, used, wire, ends)) return 0;
            std::uint32_t const polarity = ends.word[0] & 3u;
            if (polarity == static_cast<std::uint32_t>(net::Polarity::Meet)) return 0;
            is_left_source = polarity == static_cast<std::uint32_t>(net::Polarity::Give);
        } else {
            net::AgentKind const& kind =
                chain.signature.kinds[net::index_of(chain.agents[net::index_of(wire.left.agent)])];
            is_left_source = kind.port_types[wire.left.port].polarity == net::Polarity::Give;
        }
        source[index] = net::index_of(is_left_source ? wire.left.agent : wire.right.agent);
        target[index] = net::index_of(is_left_source ? wire.right.agent : wire.left.agent);
        ++indegree[target[index]];
        ++out_first[source[index] + 1];
    }
    for (std::uint32_t agent = 0; agent < 1000; ++agent)
        out_first[agent + 1] += out_first[agent];
    std::uint32_t fill[1000]{};
    for (std::uint32_t agent = 0; agent < 1000; ++agent)
        fill[agent] = out_first[agent];
    std::uint32_t out_wire[999]{};
    for (std::uint32_t index = 0; index < 999; ++index) {
        out_wire[fill[source[index]]] = index;
        ++fill[source[index]];
    }
    std::uint32_t order[1000]{};
    std::uint32_t order_count = 0;
    for (std::uint32_t agent = 0; agent < 1000; ++agent) {
        if (indegree[agent] == 0u) order[order_count++] = agent;
    }
    for (std::uint32_t read = 0; read < order_count; ++read) {
        std::uint32_t const agent = order[read];
        for (std::uint32_t slot = out_first[agent]; slot < out_first[agent + 1]; ++slot) {
            std::uint32_t const next = target[out_wire[slot]];
            if (--indegree[next] == 0u) order[order_count++] = next;
        }
    }
    if constexpr (is_checked) {
        if (!has_each_port_wired(chain, tables, used)) return 0;
    }
    return order_count;
}

// noipa keeps the call on the net of the caller.
[[gnu::noipa]] std::uint32_t sort_by_kahn(LineNet const& chain) noexcept {
    return sort_by_kahn_<ReferenceForm::Bare>(chain);
}

[[gnu::noipa]] std::uint32_t sort_checked_by_kahn(LineNet const& chain) noexcept {
    return sort_by_kahn_<ReferenceForm::Checked>(chain);
}

// The union-find over the agents of the chain, with the union by size and
// the halving of paths.  Gives the count of the wires that join two trees,
// which is 999 for a forest.  The checked form also refuses a kind with a
// par group, as the forest does.
template <ReferenceForm Form>
[[nodiscard]] std::uint32_t join_by_union_find_(LineNet const& chain) noexcept {
    constexpr bool is_checked = Form == ReferenceForm::Checked;
    [[maybe_unused]] ChainTables tables{};
    [[maybe_unused]] std::uint32_t used[is_checked ? 1000 : 1]{};
    if constexpr (is_checked) {
        if (!read_chain_kinds(chain, tables, ParGroups::Refused)) return 0;
    }
    std::uint32_t parent[1000]{};
    std::uint32_t size[1000]{};
    for (std::uint32_t agent = 0; agent < 1000; ++agent) {
        parent[agent] = agent;
        size[agent] = 1;
    }
    auto const find = [&](std::uint32_t vertex) noexcept {
        while (parent[vertex] != vertex) {
            parent[vertex] = parent[parent[vertex]];
            vertex = parent[vertex];
        }
        return vertex;
    };
    std::uint32_t joined = 0;
    for (Wire const& wire : chain.wires) {
        std::uint32_t left_agent = net::index_of(wire.left.agent);
        std::uint32_t right_agent = net::index_of(wire.right.agent);
        if constexpr (is_checked) {
            WireEnds ends{};
            if (!read_wire_ends(chain, tables, used, wire, ends)) return 0;
            left_agent = ends.agent[0];
            right_agent = ends.agent[1];
        }
        std::uint32_t left = find(left_agent);
        std::uint32_t right = find(right_agent);
        if (left == right) continue;
        if (size[left] < size[right]) {
            std::uint32_t const smaller = left;
            left = right;
            right = smaller;
        }
        parent[right] = left;
        size[left] += size[right];
        ++joined;
    }
    if constexpr (is_checked) {
        if (!has_each_port_wired(chain, tables, used)) return 0;
    }
    return joined;
}

[[gnu::noipa]] std::uint32_t join_by_union_find(LineNet const& chain) noexcept {
    return join_by_union_find_<ReferenceForm::Bare>(chain);
}

[[gnu::noipa]] std::uint32_t join_checked_by_union_find(LineNet const& chain) noexcept {
    return join_by_union_find_<ReferenceForm::Checked>(chain);
}

// The criterion of Danos and Regnier on the fork tree as a plain contraction
// by passes.  Vertex a is agent a, and vertex 1001 + w is wire w.  The edge of
// each port outside a par group joins its agent and its wire.  Then each pass
// joins each fork whose two par wires are in one part with that part.  A fork
// whose part holds its par wires closes a cycle.  The passes stop when a pass
// joins nothing.  Gives 1 for a proof net, and 0 for any other net.
[[gnu::noipa]] std::uint32_t contract_by_passes(ForkNet const& tree) noexcept {
    std::uint32_t port_count[7]{};
    for (std::uint32_t kind = 0; kind < 7; ++kind) {
        while (port_count[kind] < 8 && !tree.signature.kinds[kind].port_names[port_count[kind]].is_empty())
            ++port_count[kind];
    }
    std::uint32_t wire_at[1001 * 8]{};
    for (std::uint32_t index = 0; index < 1332; ++index) {
        Wire const& wire = tree.wires[index];
        wire_at[net::index_of(wire.left.agent) * 8 + wire.left.port] = index;
        wire_at[net::index_of(wire.right.agent) * 8 + wire.right.port] = index;
    }
    std::uint32_t parent[2333]{};
    std::uint32_t size[2333]{};
    for (std::uint32_t vertex = 0; vertex < 2333; ++vertex) {
        parent[vertex] = vertex;
        size[vertex] = 1;
    }
    auto const find = [&](std::uint32_t vertex) noexcept {
        while (parent[vertex] != vertex) {
            parent[vertex] = parent[parent[vertex]];
            vertex = parent[vertex];
        }
        return vertex;
    };
    std::uint32_t parts = 2333;
    auto const join = [&](std::uint32_t left, std::uint32_t right) noexcept {
        if (size[left] < size[right]) {
            std::uint32_t const smaller = left;
            left = right;
            right = smaller;
        }
        parent[right] = left;
        size[left] += size[right];
        --parts;
    };
    std::uint32_t par_agent[1001]{};
    std::uint32_t par_wire[1001][2]{};
    std::uint32_t par_count = 0;
    for (std::uint32_t agent = 0; agent < 1001; ++agent) {
        net::AgentKind const& kind = tree.signature.kinds[net::index_of(tree.agents[agent])];
        std::uint32_t sides = 0;
        for (std::uint32_t port = 0; port < port_count[net::index_of(tree.agents[agent])]; ++port) {
            std::uint32_t const wire_vertex = 1001 + wire_at[agent * 8 + port];
            if (((std::uint32_t{kind.par_ports} >> port) & 1u) != 0u) {
                par_wire[par_count][sides] = wire_vertex;
                ++sides;
                continue;
            }
            std::uint32_t const own = find(agent);
            std::uint32_t const other = find(wire_vertex);
            if (own == other) return 0;
            join(own, other);
        }
        if (sides != 0u) {
            par_agent[par_count] = agent;
            ++par_count;
        }
    }
    bool is_done[1001]{};
    std::uint32_t done = 0;
    for (bool is_joined = true; is_joined;) {
        is_joined = false;
        for (std::uint32_t par = 0; par < par_count; ++par) {
            if (is_done[par]) continue;
            std::uint32_t const premises = find(par_wire[par][0]);
            if (find(par_wire[par][1]) != premises) continue;
            std::uint32_t const own = find(par_agent[par]);
            if (own == premises) return 0;
            join(own, premises);
            is_done[par] = true;
            ++done;
            is_joined = true;
        }
    }
    return done == par_count && parts == 1 ? 1 : 0;
}

// The vicious circles as a plain walk: each agent points to the agent at the
// other end of its principal port when that end is an auxiliary port, and a
// walk with three colors finds each cycle of those pointers.  Gives 1 for a
// net with no vicious circle.
[[gnu::noipa]] std::uint32_t walk_principal_ports(LineNet const& chain) noexcept {
    constexpr std::uint32_t none = 0xFFFFFFFFu;
    std::uint32_t next[1000]{};
    for (std::uint32_t& slot : next)
        slot = none;
    for (Wire const& wire : chain.wires) {
        bool const is_left_principal =
            wire.left.port
            == chain.signature.kinds[net::index_of(chain.agents[net::index_of(wire.left.agent)])].principal;
        bool const is_right_principal =
            wire.right.port
            == chain.signature.kinds[net::index_of(chain.agents[net::index_of(wire.right.agent)])].principal;
        if (is_left_principal && !is_right_principal)
            next[net::index_of(wire.left.agent)] = net::index_of(wire.right.agent);
        if (is_right_principal && !is_left_principal)
            next[net::index_of(wire.right.agent)] = net::index_of(wire.left.agent);
    }
    std::uint8_t color[1000]{};
    for (std::uint32_t start = 0; start < 1000; ++start) {
        if (color[start] != 0u) continue;
        std::uint32_t walk = start;
        while (walk != none && color[walk] == 0u) {
            color[walk] = 1;
            walk = next[walk];
        }
        if (walk != none && color[walk] == 1u) return 0;
        for (std::uint32_t mark = start; mark != none && color[mark] == 1u; mark = next[mark])
            color[mark] = 2;
    }
    return 1;
}

// The liveness of the ring as a plain sort of Kahn over the edges with no
// token of its marked graph (the model of Priority.h): the order of each
// process, the edge from send s to receive s of a channel with a capacity k,
// the edge from receive s - k to send s, and one node for a send and its
// receive on a rendezvous.  Each channel of the ring is a rendezvous, and the
// loop reads each capacity, as the body does.  Gives the count of the nodes
// that the sort orders, or 0 when a cycle leaves a node out.
[[gnu::noipa]] std::uint32_t sort_marked_by_kahn(RingSystem const& ring) noexcept {
    constexpr std::uint32_t none = 0xFFFFFFFFu;
    std::uint32_t step_of[4000]{};
    std::uint32_t step_count[2000]{};
    for (std::uint32_t index = 0; index < 4000; ++index) {
        net::Action const& act = ring.actions[index];
        std::uint32_t const side =
            2 * static_cast<std::uint32_t>(act.channel) + (act.move == net::Move::Send ? 0u : 1u);
        step_of[index] = step_count[side];
        ++step_count[side];
    }
    std::uint32_t step_first[1000]{};
    std::uint32_t steps = 0;
    for (std::uint32_t channel = 0; channel < 1000; ++channel) {
        step_first[channel] = steps;
        steps += step_count[2 * channel];
    }
    std::uint32_t send_at[4000]{};
    std::uint32_t receive_at[4000]{};
    for (std::uint32_t index = 0; index < 4000; ++index) {
        net::Action const& act = ring.actions[index];
        std::uint32_t const step = step_first[static_cast<std::uint32_t>(act.channel)] + step_of[index];
        if (act.move == net::Move::Send) {
            send_at[step] = index;
        } else {
            receive_at[step] = index;
        }
    }
    std::uint32_t node_of[4000]{};
    std::uint32_t node_count = 0;
    for (std::uint32_t index = 0; index < 4000; ++index) {
        net::Action const& act = ring.actions[index];
        bool const is_joined =
            act.move == net::Move::Receive && ring.channels[static_cast<std::uint32_t>(act.channel)].capacity == 0u;
        node_of[index] =
            is_joined ? send_at[step_first[static_cast<std::uint32_t>(act.channel)] + step_of[index]] : index;
        if (!is_joined) ++node_count;
    }
    std::uint32_t edge_from[8000]{};
    std::uint32_t edge_to[8000]{};
    std::uint32_t edge_count = 0;
    std::uint32_t last_of[1000]{};
    for (std::uint32_t& slot : last_of)
        slot = none;
    for (std::uint32_t index = 0; index < 4000; ++index) {
        net::Action const& act = ring.actions[index];
        auto const process = static_cast<std::uint32_t>(act.process);
        auto const channel = static_cast<std::uint32_t>(act.channel);
        std::uint32_t const capacity = ring.channels[channel].capacity;
        std::uint32_t const step = step_of[index];
        if (last_of[process] != none) {
            edge_from[edge_count] = node_of[last_of[process]];
            edge_to[edge_count] = node_of[index];
            ++edge_count;
        }
        last_of[process] = index;
        if (act.move == net::Move::Receive && capacity != 0u) {
            edge_from[edge_count] = send_at[step_first[channel] + step];
            edge_to[edge_count] = index;
            ++edge_count;
        }
        if (act.move == net::Move::Send && capacity != 0u && step >= capacity) {
            edge_from[edge_count] = receive_at[step_first[channel] + step - capacity];
            edge_to[edge_count] = index;
            ++edge_count;
        }
    }
    std::uint32_t indegree[4000]{};
    std::uint32_t out_first[4001]{};
    for (std::uint32_t edge = 0; edge < edge_count; ++edge) {
        ++indegree[edge_to[edge]];
        ++out_first[edge_from[edge] + 1];
    }
    for (std::uint32_t node = 0; node < 4000; ++node)
        out_first[node + 1] += out_first[node];
    std::uint32_t fill[4000]{};
    for (std::uint32_t node = 0; node < 4000; ++node)
        fill[node] = out_first[node];
    std::uint32_t out_edge[8000]{};
    for (std::uint32_t edge = 0; edge < edge_count; ++edge) {
        out_edge[fill[edge_from[edge]]] = edge;
        ++fill[edge_from[edge]];
    }
    std::uint32_t order[4000]{};
    std::uint32_t order_count = 0;
    for (std::uint32_t index = 0; index < 4000; ++index) {
        if (node_of[index] == index && indegree[index] == 0u) order[order_count++] = index;
    }
    for (std::uint32_t read = 0; read < order_count; ++read) {
        std::uint32_t const node = order[read];
        for (std::uint32_t slot = out_first[node]; slot < out_first[node + 1]; ++slot) {
            std::uint32_t const target = edge_to[out_edge[slot]];
            if (--indegree[target] == 0u) order[order_count++] = target;
        }
    }
    return order_count == node_count ? order_count : 0;
}

// The liveness of the ring as a plain sort of Kahn over the same edges, read
// from the arrays of the steps: each action leaves an edge to the node of the
// next action of its process, and on a channel of capacity k, send s leaves
// an edge to receive s, and receive s to send s + k.  The send of a
// rendezvous names the receive of its node, whose process goes on after the
// node.  Gives the count of the nodes that the sort orders, or 0 when a cycle
// leaves a node out.  The checked form also reads the form of the ring, as
// the plain form of the body does: a name for each process and each channel,
// each action inside the ring, and one process for each side of a channel.
// It gives 0 for a fault of the form.
template <ReferenceForm Form>
[[nodiscard]] std::uint32_t sort_steps_by_kahn_(RingSystem const& ring) noexcept {
    constexpr std::uint32_t none = 0xFFFFFFFFu;
    if constexpr (Form == ReferenceForm::Checked) {
        for (net::NetName const& name : ring.process_names) {
            if (name.is_empty()) return 0;
        }
        for (net::NetChannel const& channel : ring.channels) {
            if (channel.name.is_empty()) return 0;
        }
    }
    std::uint32_t step_of[4000]{};
    // The checked form keeps the process of each side after the counts, at
    // 2000 + side, as the index of the process plus one, and 0 for a side
    // that no action holds yet.
    std::uint32_t step_count[Form == ReferenceForm::Checked ? 4000 : 2000]{};
    for (std::uint32_t index = 0; index < 4000; ++index) {
        net::Action const& act = ring.actions[index];
        std::uint32_t const side =
            2 * static_cast<std::uint32_t>(act.channel) + (act.move == net::Move::Send ? 0u : 1u);
        if constexpr (Form == ReferenceForm::Checked) {
            auto const process = static_cast<std::uint32_t>(act.process);
            if (process >= 1000 || static_cast<std::uint32_t>(act.channel) >= 1000
                || (act.move != net::Move::Send && act.move != net::Move::Receive))
                return 0;
            if (step_count[2000 + side] == 0u) step_count[2000 + side] = process + 1u;
            if (step_count[2000 + side] != process + 1u) return 0;
        }
        step_of[index] = step_count[side];
        ++step_count[side];
    }
    std::uint32_t step_first[1000]{};
    std::uint32_t steps = 0;
    for (std::uint32_t channel = 0; channel < 1000; ++channel) {
        step_first[channel] = steps;
        steps += step_count[2 * channel];
    }
    std::uint32_t send_at[4000]{};
    std::uint32_t receive_at[4000]{};
    for (std::uint32_t index = 0; index < 4000; ++index) {
        net::Action const& act = ring.actions[index];
        std::uint32_t const step = step_first[static_cast<std::uint32_t>(act.channel)] + step_of[index];
        if (act.move == net::Move::Send) {
            send_at[step] = index;
        } else {
            receive_at[step] = index;
        }
    }
    std::uint32_t node_of[4000]{};
    std::uint32_t indegree[4000]{};
    std::uint32_t process_target[4000]{};
    std::uint32_t channel_target[4000]{};
    std::uint32_t last_of[1000]{};
    for (std::uint32_t& slot : last_of)
        slot = none;
    std::uint32_t node_count = 0;
    for (std::uint32_t index = 0; index < 4000; ++index) {
        net::Action const& act = ring.actions[index];
        auto const channel = static_cast<std::uint32_t>(act.channel);
        auto const process = static_cast<std::uint32_t>(act.process);
        std::uint32_t const capacity = ring.channels[channel].capacity;
        std::uint32_t const step = step_of[index];
        std::uint32_t const slot = step_first[channel] + step;
        bool const is_send = act.move == net::Move::Send;
        bool const is_joined = !is_send && capacity == 0u;
        std::uint32_t const node = is_joined ? send_at[slot] : index;
        node_of[index] = node;
        if (!is_joined) ++node_count;
        process_target[index] = none;
        if (last_of[process] != none) {
            process_target[last_of[process]] = node;
            ++indegree[node];
        }
        last_of[process] = index;
        if (capacity == 0u) {
            channel_target[index] = is_send ? receive_at[slot] : none;
            continue;
        }
        if (!is_send || step >= capacity) ++indegree[index];
        if (is_send) {
            channel_target[index] = receive_at[slot];
        } else {
            channel_target[index] = capacity < step_count[2 * channel] - step ? send_at[slot + capacity] : none;
        }
    }
    std::uint32_t order[4000]{};
    std::uint32_t order_count = 0;
    for (std::uint32_t index = 0; index < 4000; ++index) {
        if (node_of[index] == index && indegree[index] == 0u) order[order_count++] = index;
    }
    auto const release = [&](std::uint32_t target) noexcept {
        if (target == none) return;
        if (--indegree[target] == 0u) order[order_count++] = target;
    };
    for (std::uint32_t read = 0; read < order_count; ++read) {
        std::uint32_t const node = order[read];
        release(process_target[node]);
        std::uint32_t const waiting = channel_target[node];
        if (waiting == none) continue;
        if (node_of[waiting] == node) {
            release(process_target[waiting]);
        } else {
            release(waiting);
        }
    }
    return order_count == node_count ? order_count : 0;
}

[[gnu::noipa]] std::uint32_t sort_steps_by_kahn(RingSystem const& ring) noexcept {
    return sort_steps_by_kahn_<ReferenceForm::Bare>(ring);
}

[[gnu::noipa]] std::uint32_t sort_checked_steps_by_kahn(RingSystem const& ring) noexcept {
    return sort_steps_by_kahn_<ReferenceForm::Checked>(ring);
}

// The canonical form of the chain as plain loops: the peer of each port, the
// boundary agents first, a walk from each with an explicit stack, and the fold
// of the entry of each number into chain n % 4, as the body folds it.  Gives
// the hash, or 0 when an agent has no number.  The checked form gives 0 for a
// fault of the input too.
template <ReferenceForm Form>
[[nodiscard]] std::uint64_t number_by_walk_(LineNet const& chain) noexcept {
    constexpr std::uint32_t none = 0xFFFFFFFFu;
    constexpr bool is_checked = Form == ReferenceForm::Checked;
    [[maybe_unused]] ChainTables tables{};
    [[maybe_unused]] std::uint32_t used[is_checked ? 1000 : 1]{};
    if constexpr (is_checked) {
        if (!read_chain_kinds(chain, tables, ParGroups::Admitted)) return 0;
    }
    std::uint32_t port_count[3]{};
    for (std::uint32_t kind = 0; kind < 3; ++kind) {
        while (port_count[kind] < 8 && !chain.signature.kinds[kind].port_names[port_count[kind]].is_empty())
            ++port_count[kind];
    }
    std::uint32_t peer[1000 * 8]{};
    for (Wire const& wire : chain.wires) {
        std::uint32_t left = net::index_of(wire.left.agent) * 8 + wire.left.port;
        std::uint32_t right = net::index_of(wire.right.agent) * 8 + wire.right.port;
        if constexpr (is_checked) {
            WireEnds ends{};
            if (!read_wire_ends(chain, tables, used, wire, ends)) return 0;
            left = ends.agent[0] * 8 + wire.left.port;
            right = ends.agent[1] * 8 + wire.right.port;
        }
        peer[left] = right;
        peer[right] = left;
    }
    if constexpr (is_checked) {
        if (!has_each_port_wired(chain, tables, used)) return 0;
    }
    std::uint32_t number_of[1000]{};
    std::uint32_t agent_at[1000]{};
    std::uint32_t count = 0;
    for (std::uint32_t agent = 0; agent < 1000; ++agent) {
        number_of[agent] = none;
        if (!chain.signature.kinds[net::index_of(chain.agents[agent])].is_boundary) continue;
        number_of[agent] = count;
        agent_at[count++] = agent;
    }
    std::uint32_t const boundary_count = count;
    std::uint32_t stack_agent[1000]{};
    std::uint32_t stack_port[1000]{};
    for (std::uint32_t root = 0; root < boundary_count; ++root) {
        stack_agent[0] = agent_at[root];
        stack_port[0] = 0;
        std::uint32_t depth = 1;
        while (depth != 0) {
            std::uint32_t const agent = stack_agent[depth - 1];
            std::uint32_t const port = stack_port[depth - 1];
            if (port == port_count[net::index_of(chain.agents[agent])]) {
                --depth;
                continue;
            }
            ++stack_port[depth - 1];
            std::uint32_t const next = peer[agent * 8 + port] / 8;
            if (number_of[next] != none) continue;
            number_of[next] = count;
            agent_at[count++] = next;
            stack_agent[depth] = next;
            stack_port[depth] = 0;
            ++depth;
        }
    }
    if (count != 1000) return 0;
    auto const fold_entry = [&](std::uint64_t lane, std::uint32_t number) noexcept {
        std::uint32_t const agent = agent_at[number];
        std::uint32_t const kind = net::index_of(chain.agents[agent]);
        lane = ::foundation::reflect::combine_ids(lane, kind);
        for (std::uint32_t port = 0; port < port_count[kind]; ++port) {
            std::uint32_t const other = peer[agent * 8 + port];
            lane = ::foundation::reflect::combine_ids(lane, (std::uint64_t{number_of[other / 8]} << 3) | (other % 8));
        }
        return lane;
    };
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    std::uint64_t third = 0;
    std::uint64_t fourth = 0;
    for (std::uint32_t number = 0; number < 1000; number += 4) {
        first = fold_entry(first, number);
        second = fold_entry(second, number + 1);
        third = fold_entry(third, number + 2);
        fourth = fold_entry(fourth, number + 3);
    }
    std::uint64_t hash = ::foundation::reflect::combine_ids(0, 1000);
    hash = ::foundation::reflect::combine_ids(hash, first);
    hash = ::foundation::reflect::combine_ids(hash, second);
    hash = ::foundation::reflect::combine_ids(hash, third);
    return ::foundation::reflect::combine_ids(hash, fourth);
}

[[gnu::noipa]] std::uint64_t number_by_walk(LineNet const& chain) noexcept {
    return number_by_walk_<ReferenceForm::Bare>(chain);
}

[[gnu::noipa]] std::uint64_t number_checked_by_walk(LineNet const& chain) noexcept {
    return number_by_walk_<ReferenceForm::Checked>(chain);
}

// The count of the dependent adds of one call of the clock chain, and the
// calls of one sample of it.
constexpr std::uint32_t kClockAdds = 64;
constexpr std::size_t kClockBatch = 256;

// kClockAdds dependent adds to the value in one block of code, one cycle each
// on the core (the speed of light).  The compiler cannot fold them.
[[gnu::always_inline]] inline void add_chain(std::uint64_t& value) noexcept {
#if defined(__x86_64__)
    asm volatile(".rept %c1\n\tadd $1, %0\n\t.endr" : "+r"(value) : "i"(kClockAdds));
#else
    asm volatile(".rept %c1\n\tadd %0, %0, #1\n\t.endr" : "+r"(value) : "i"(kClockAdds));
#endif
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The clock chain of the core.  The chain runs on a local, so the value stays
// in a register.
struct ClockChain {
    std::uint64_t value = 0;

    [[gnu::always_inline]] void operator()() noexcept {
        std::uint64_t local = value;
        add_chain(local);
        value = local;
    }
};

// The cycles of one nanosecond of the core in this run, from the clock chain.
[[nodiscard]] double measure_cycles_per_ns() {
    ClockChain chain{};
    bench::Run run{"clock"};
    if (int const core = bench::env_core(); core >= 0) static_cast<void>(run.core(core));
    bench::Report const report = run.batch(kClockBatch).measure(chain);
    bench::do_not_optimize(chain.value);
    return report.pct.p50 > 0.0 ? kClockAdds / report.pct.p50 : 0.0;
}

// The scale of the lines of cycles: the cycles of one nanosecond of the core
// in the run, and the p50 of the floor row in nanoseconds.
struct LightScale {
    double cycles_per_ns = 0.0;
    double floor_ns = 0.0;
};

// Writes one line of the speed of light: the cycles of the p50 and the p99 of
// one call, the cycles of the floor row, and the ratio of the p50 to it.
void print_light(fixy::TextView name, bench::Report const& measured, LightScale const& scale) noexcept {
    auto const centi = [](double value) noexcept { return static_cast<std::uint64_t>(value * 100.0 + 0.5); };
    std::uint64_t const p50 = centi(measured.pct.p50 * scale.cycles_per_ns) / 100;
    std::uint64_t const p99 = centi(measured.pct.p99 * scale.cycles_per_ns) / 100;
    std::uint64_t const floor_cycles = centi(scale.floor_ns * scale.cycles_per_ns) / 100;
    std::uint64_t const ratio = scale.floor_ns > 0.0 ? centi(measured.pct.p50 / scale.floor_ns) : 0;
    bench::print("  {}  p50={} cycles  p99={} cycles  floor={} cycles  ratio={}.{}{}\n", name, p50, p99, floor_cycles,
                 ratio / 100, ratio % 100 / 10, ratio % 10);
}

// ── The rows ────────────────────────────────────────────────────────────

// Each measured check runs in a function of its own, as each reference does.
// A check inlined into the loop of a row moved by 17% when an unrelated body
// of Net.h changed, with the same instructions, because the code of the row
// took another place.  noipa keeps the call on the net of the caller.
template <class Graph>
[[gnu::noipa]] std::uint32_t run_wiring(Graph const& graph) noexcept {
    return static_cast<std::uint32_t>(net::find_wiring_fault(graph).code);
}
template <class Graph>
[[gnu::noipa]] std::uint32_t run_seal(Graph const& graph) noexcept {
    return static_cast<std::uint32_t>(net::seal_net(graph).wiring_fault().code);
}
template <class Graph>
[[gnu::noipa]] std::uint64_t run_canonical(Graph const& graph) noexcept {
    return static_cast<std::uint64_t>(net::find_canonical_form(graph).hash);
}
template <class Graph>
[[gnu::noipa]] std::uint32_t run_forest(Graph const& graph) noexcept {
    return static_cast<std::uint32_t>(net::find_forest_fault(graph).code);
}
template <class Graph>
[[gnu::noipa]] std::uint32_t run_proof_net(Graph const& graph) noexcept {
    return static_cast<std::uint32_t>(net::find_proof_net_fault(graph).code);
}
template <class Graph>
[[gnu::noipa]] std::uint32_t run_vicious(Graph const& graph) noexcept {
    return static_cast<std::uint32_t>(net::find_vicious_fault(graph).code);
}
template <class Graph>
[[gnu::noipa]] std::uint32_t run_dataflow(Graph const& graph) noexcept {
    return static_cast<std::uint32_t>(net::find_dataflow_fault(graph).code);
}
template <class System>
[[gnu::noipa]] std::uint32_t run_liveness(System const& protocol) noexcept {
    return static_cast<std::uint32_t>(net::find_liveness_fault(protocol).code);
}
[[gnu::noipa]] std::uint32_t run_seal_system(RingSystem const& ring) noexcept {
    return static_cast<std::uint32_t>(net::seal_system(ring).form_fault().code);
}

// The five checks of a net, each with its own check of the wiring.
[[nodiscard]] std::uint64_t check_five_times(LineNet const& chain) noexcept {
    return static_cast<std::uint64_t>(net::find_forest_fault(chain).code)
         + static_cast<std::uint64_t>(net::find_proof_net_fault(chain).code)
         + static_cast<std::uint64_t>(net::find_vicious_fault(chain).code)
         + static_cast<std::uint64_t>(net::find_dataflow_fault(chain).code)
         + static_cast<std::uint64_t>(net::find_canonical_form(chain).hash);
}

// The seal of a net and the five checks of the sealed net.
[[nodiscard]] std::uint64_t seal_and_check(LineNet const& chain) noexcept {
    auto const sealed = net::seal_net(chain);
    return static_cast<std::uint64_t>(net::find_forest_fault(sealed).code)
         + static_cast<std::uint64_t>(net::find_proof_net_fault(sealed).code)
         + static_cast<std::uint64_t>(net::find_vicious_fault(sealed).code)
         + static_cast<std::uint64_t>(net::find_dataflow_fault(sealed).code)
         + static_cast<std::uint64_t>(net::find_canonical_form(sealed).hash);
}

void bench_chain(LineNet const& chain) {
    if (sort_by_kahn(chain) != 1000 || join_by_union_find(chain) != 999) {
        fixy::fatal("the references read the chain as a forest with no cycle of waits");
    }
    if (sort_checked_by_kahn(chain) != 1000 || join_checked_by_union_find(chain) != 999) {
        fixy::fatal("the checked references read the chain as a forest with no cycle of waits");
    }
    if (walk_principal_ports(chain) != 1 || number_by_walk(chain) == 0) {
        fixy::fatal("the references read the chain as a tree with no vicious circle and with a canonical form");
    }
    if (number_checked_by_walk(chain) != number_by_walk(chain)) {
        fixy::fatal("the checked canonical walk gives the hash of the bare walk");
    }
    double const cycles_per_ns = measure_cycles_per_ns();
    bench::print("clock of the core: {} cycles in 1000 ns\n", static_cast<std::uint64_t>(cycles_per_ns * 1000.0 + 0.5));
    if (net::find_canonical_form(chain).fault.is_fault()) fixy::fatal("the chain has a canonical form");
    auto const sealed = net::seal_net(chain);
    if (sealed.wiring_fault().is_fault()) fixy::fatal("the wiring of the chain is admitted");
    bench::do_not_optimize(sealed);
    bench::Report const wiring =
        measure_row(bench::Run{"wiring chain"}, [&] { bench::do_not_optimize(run_wiring(chain)); });
    print_row("find_wiring_fault, chain of 1000", wiring);
    print_row("seal_net, chain of 1000",
              measure_row(bench::Run{"seal chain"}, [&] { bench::do_not_optimize(run_seal(chain)); }));
    bench::Report const form =
        measure_row(bench::Run{"canonical form chain"}, [&] { bench::do_not_optimize(run_canonical(chain)); });
    print_row("find_canonical_form, chain of 1000", form);
    bench::Report const sealed_form =
        measure_row(bench::Run{"canonical form sealed chain"}, [&] { bench::do_not_optimize(run_canonical(sealed)); });
    print_row("find_canonical_form, sealed chain of 1000", sealed_form);
    bench::Report const forest =
        measure_row(bench::Run{"forest chain"}, [&] { bench::do_not_optimize(run_forest(chain)); });
    print_row("find_forest_fault, chain of 1000", forest);
    bench::Report const sealed_forest =
        measure_row(bench::Run{"forest sealed chain"}, [&] { bench::do_not_optimize(run_forest(sealed)); });
    print_row("find_forest_fault, sealed chain of 1000", sealed_forest);
    print_row("find_proof_net_fault, chain of 1000",
              measure_row(bench::Run{"proof net chain"}, [&] { bench::do_not_optimize(run_proof_net(chain)); }));
    bench::Report const sealed_proof_net =
        measure_row(bench::Run{"proof net sealed chain"}, [&] { bench::do_not_optimize(run_proof_net(sealed)); });
    print_row("find_proof_net_fault, sealed chain of 1000", sealed_proof_net);
    print_row("find_vicious_fault, chain of 1000",
              measure_row(bench::Run{"vicious chain"}, [&] { bench::do_not_optimize(run_vicious(chain)); }));
    bench::Report const sealed_vicious =
        measure_row(bench::Run{"vicious sealed chain"}, [&] { bench::do_not_optimize(run_vicious(sealed)); });
    print_row("find_vicious_fault, sealed chain of 1000", sealed_vicious);
    bench::Report const dataflow =
        measure_row(bench::Run{"dataflow chain"}, [&] { bench::do_not_optimize(run_dataflow(chain)); });
    print_row("find_dataflow_fault, chain of 1000", dataflow);
    bench::Report const sealed_dataflow =
        measure_row(bench::Run{"dataflow sealed chain"}, [&] { bench::do_not_optimize(run_dataflow(sealed)); });
    print_row("find_dataflow_fault, sealed chain of 1000", sealed_dataflow);
    print_row("five checks of the chain of 1000",
              measure_row(bench::Run{"five checks chain"}, [&] { bench::do_not_optimize(check_five_times(chain)); }));
    print_row("seal_net and five checks of the sealed chain of 1000",
              measure_row(bench::Run{"seal and five checks"}, [&] { bench::do_not_optimize(seal_and_check(chain)); }));
    bench::Report const kahn =
        measure_row(bench::Run{"kahn chain"}, [&] { bench::do_not_optimize(sort_by_kahn(chain)); });
    print_row("reference sort of Kahn, chain of 1000", kahn);
    bench::Report const union_find =
        measure_row(bench::Run{"union-find chain"}, [&] { bench::do_not_optimize(join_by_union_find(chain)); });
    print_row("reference union-find, chain of 1000", union_find);
    bench::Report const principal_walk =
        measure_row(bench::Run{"principal walk chain"}, [&] { bench::do_not_optimize(walk_principal_ports(chain)); });
    print_row("reference walk of the principal ports, chain of 1000", principal_walk);
    bench::Report const canonical_walk =
        measure_row(bench::Run{"canonical walk chain"}, [&] { bench::do_not_optimize(number_by_walk(chain)); });
    print_row("reference canonical walk, chain of 1000", canonical_walk);
    bench::Report const checked_kahn =
        measure_row(bench::Run{"checked kahn chain"}, [&] { bench::do_not_optimize(sort_checked_by_kahn(chain)); });
    print_row("reference sort of Kahn with the checks of the input, chain of 1000", checked_kahn);
    bench::Report const checked_union_find = measure_row(
        bench::Run{"checked union-find chain"}, [&] { bench::do_not_optimize(join_checked_by_union_find(chain)); });
    print_row("reference union-find with the checks of the input, chain of 1000", checked_union_find);
    bench::Report const checked_walk = measure_row(bench::Run{"checked canonical walk chain"},
                                                   [&] { bench::do_not_optimize(number_checked_by_walk(chain)); });
    print_row("reference canonical walk with the checks of the input, chain of 1000", checked_walk);
    bench::Report const floor =
        measure_row(bench::Run{"floor chain"}, [&] { bench::do_not_optimize(read_each_wire(chain)); });
    print_row("floor: one read of each wire and of its two kinds, chain of 1000", floor);
    print_ratio("find_canonical_form / find_wiring_fault", form, wiring);
    print_ratio("find_dataflow_fault / reference sort of Kahn with the checks of the input", dataflow, checked_kahn);
    print_ratio("find_forest_fault / reference union-find with the checks of the input", forest, checked_union_find);
    print_ratio("find_canonical_form / reference canonical walk with the checks of the input", form, checked_walk);
    print_ratio("find_dataflow_fault sealed / reference sort of Kahn", sealed_dataflow, kahn);
    print_ratio("find_forest_fault sealed / reference union-find", sealed_forest, union_find);
    print_ratio("find_proof_net_fault sealed / reference union-find", sealed_proof_net, union_find);
    print_ratio("find_vicious_fault sealed / reference walk of the principal ports", sealed_vicious, principal_walk);
    print_ratio("find_canonical_form sealed / reference canonical walk", sealed_form, canonical_walk);
    LightScale const scale{.cycles_per_ns = cycles_per_ns, .floor_ns = floor.pct.p50};
    print_light("find_forest_fault          ", forest, scale);
    print_light("checked union-find         ", checked_union_find, scale);
    print_light("find_forest_fault sealed   ", sealed_forest, scale);
    print_light("bare union-find            ", union_find, scale);
    print_light("find_dataflow_fault        ", dataflow, scale);
    print_light("checked sort of Kahn       ", checked_kahn, scale);
    print_light("find_dataflow_fault sealed ", sealed_dataflow, scale);
    print_light("bare sort of Kahn          ", kahn, scale);
    print_light("find_canonical_form        ", form, scale);
    print_light("checked canonical walk     ", checked_walk, scale);
    print_light("find_canonical_form sealed ", sealed_form, scale);
    print_light("bare canonical walk        ", canonical_walk, scale);
    print_light("find_wiring_fault          ", wiring, scale);
    print_light("floor row                  ", floor, scale);
}

void bench_fork_tree(ForkNet const& tree) {
    if (contract_by_passes(tree) != 1) fixy::fatal("the reference reads the fork tree as a proof net");
    print_row("find_proof_net_fault, fork tree of 1001",
              measure_row(bench::Run{"proof net fork tree"}, [&] { bench::do_not_optimize(run_proof_net(tree)); }));
    auto const sealed = net::seal_net(tree);
    bench::do_not_optimize(sealed);
    bench::Report const sealed_proof_net =
        measure_row(bench::Run{"proof net sealed fork tree"}, [&] { bench::do_not_optimize(run_proof_net(sealed)); });
    print_row("find_proof_net_fault, sealed fork tree of 1001", sealed_proof_net);
    bench::Report const contraction =
        measure_row(bench::Run{"contraction fork tree"}, [&] { bench::do_not_optimize(contract_by_passes(tree)); });
    print_row("reference contraction by passes, fork tree of 1001", contraction);
    print_ratio("find_proof_net_fault sealed / reference contraction by passes", sealed_proof_net, contraction);
}

void bench_ring(RingSystem const& ring) {
    if (sort_marked_by_kahn(ring) == 0 || sort_steps_by_kahn(ring) == 0 || sort_checked_steps_by_kahn(ring) == 0) {
        fixy::fatal("the references read the ring as a live system");
    }
    auto const sealed = net::seal_system(ring);
    bench::do_not_optimize(sealed);
    bench::Report const liveness =
        measure_row(bench::Run{"liveness ring"}, [&] { bench::do_not_optimize(run_liveness(ring)); });
    print_row("find_liveness_fault, ring of 4000 actions", liveness);
    print_row("seal_system, ring of 4000 actions",
              measure_row(bench::Run{"seal ring"}, [&] { bench::do_not_optimize(run_seal_system(ring)); }));
    bench::Report const sealed_liveness =
        measure_row(bench::Run{"liveness sealed ring"}, [&] { bench::do_not_optimize(run_liveness(sealed)); });
    print_row("find_liveness_fault, sealed ring of 4000 actions", sealed_liveness);
    bench::Report const checked_kahn = measure_row(bench::Run{"checked steps kahn ring"},
                                                   [&] { bench::do_not_optimize(sort_checked_steps_by_kahn(ring)); });
    print_row("reference sort of Kahn over the arrays of the steps with the checks of the form, ring of 4000 actions",
              checked_kahn);
    bench::Report const marked_kahn =
        measure_row(bench::Run{"marked kahn ring"}, [&] { bench::do_not_optimize(sort_marked_by_kahn(ring)); });
    print_row("reference sort of Kahn of the marked graph, ring of 4000 actions", marked_kahn);
    bench::Report const steps_kahn =
        measure_row(bench::Run{"steps kahn ring"}, [&] { bench::do_not_optimize(sort_steps_by_kahn(ring)); });
    print_row("reference sort of Kahn over the arrays of the steps, ring of 4000 actions", steps_kahn);
    print_ratio("find_liveness_fault / reference sort of Kahn of the marked graph", liveness, marked_kahn);
    print_ratio("find_liveness_fault / reference sort of Kahn over the arrays of the steps", liveness, steps_kahn);
    print_ratio("find_liveness_fault / reference sort of Kahn over the arrays of the steps with the checks of the form",
                liveness, checked_kahn);
    print_ratio("find_liveness_fault sealed / reference sort of Kahn over the arrays of the steps", sealed_liveness,
                steps_kahn);
}

}  // namespace

// main copies each net into a variable and lets it escape, so the compiler
// neither folds a check into a constant nor reads the net as a constant.
int main() {
    bench::elevate_priority();
    LineNet chain = chain_of_1000;
    ForkNet tree = fork_of_1001;
    RingSystem ring = ring_of_1000;
    bench::do_not_optimize(chain);
    bench::do_not_optimize(tree);
    bench::do_not_optimize(ring);
    bench_chain(chain);
    bench_fork_tree(tree);
    bench_ring(ring);
    return 0;
}
