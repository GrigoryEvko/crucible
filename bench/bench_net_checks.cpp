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
// Two rows are the references: the sort of Kahn and the union-find, as plain
// loops over the wires of the chain, with no check of the wiring and no
// fault record.  Each is the textbook form of its algorithm, linear or near
// linear in the wires, so no known method is faster by more than a constant.
//
// A find function on a Net runs the check of the wiring and then its body.
// A find function on a sealed net runs only its body, because seal_net
// checked the wiring one time (THE SEALED NET of Net.h).  So the ratio of a
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
    fixy::report(fixy::Sink::Out, "{}  core=cpu{}  p50={} ns  p99={} ns  max={} ns  cv={} permille\n", name,
                 measured.pinned_cpu.raw(), nanoseconds(measured.pct.p50), nanoseconds(measured.pct.p99),
                 nanoseconds(measured.pct.max), static_cast<std::uint64_t>(measured.pct.cv * 1000.0 + 0.5));
}

// Writes the ratio of the median of a row to the median of its reference.
void print_ratio(fixy::TextView name, bench::Report const& measured, bench::Report const& reference) noexcept {
    double const base = reference.pct.p50 > 0.0 ? reference.pct.p50 : 1.0;
    fixy::report(fixy::Sink::Out, "{}  ratio={} permille\n", name,
                 static_cast<std::uint64_t>(measured.pct.p50 * 1000.0 / base + 0.5));
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

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the plain sort of Kahn and the plain union-find are the references")

// The sort of Kahn over the directed wires of the chain: the producer of each
// wire is the agent of its give port.  Gives the count of the sorted agents,
// which is 1,000 for a net with no cycle of waits.  noipa keeps the call on
// the net of the caller.
[[gnu::noipa]] std::uint32_t sort_by_kahn(LineNet const& chain) noexcept {
    std::uint32_t source[999]{};
    std::uint32_t target[999]{};
    std::uint32_t indegree[1000]{};
    std::uint32_t out_first[1001]{};
    for (std::uint32_t index = 0; index < 999; ++index) {
        Wire const& wire = chain.wires[index];
        net::AgentKind const& kind = chain.signature.kinds[net::index_of(chain.agents[net::index_of(wire.left.agent)])];
        bool const is_left_source = kind.port_types[wire.left.port].polarity == net::Polarity::Give;
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
    return order_count;
}

// The union-find over the agents of the chain, with the union by size and
// the halving of paths.  Gives the count of the wires that join two trees,
// which is 999 for a forest.
[[gnu::noipa]] std::uint32_t join_by_union_find(LineNet const& chain) noexcept {
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
        std::uint32_t left = find(net::index_of(wire.left.agent));
        std::uint32_t right = find(net::index_of(wire.right.agent));
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
    return joined;
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// ── The rows ────────────────────────────────────────────────────────────

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
    if (net::find_canonical_form(chain).fault.is_fault()) fixy::fatal("the chain has a canonical form");
    auto const sealed = net::seal_net(chain);
    if (sealed.wiring_fault().is_fault()) fixy::fatal("the wiring of the chain is admitted");
    bench::do_not_optimize(sealed);
    bench::Report const wiring =
        measure_row(bench::Run{"wiring chain"}, [&] { bench::do_not_optimize(net::find_wiring_fault(chain).code); });
    print_row("find_wiring_fault, chain of 1000", wiring);
    print_row("seal_net, chain of 1000", measure_row(bench::Run{"seal chain"}, [&] {
                  bench::do_not_optimize(net::seal_net(chain).wiring_fault().code);
              }));
    bench::Report const form = measure_row(bench::Run{"canonical form chain"},
                                           [&] { bench::do_not_optimize(net::find_canonical_form(chain).hash); });
    print_row("find_canonical_form, chain of 1000", form);
    bench::Report const sealed_form = measure_row(bench::Run{"canonical form sealed chain"}, [&] {
        bench::do_not_optimize(net::find_canonical_form(sealed).hash);
    });
    print_row("find_canonical_form, sealed chain of 1000", sealed_form);
    bench::Report const forest =
        measure_row(bench::Run{"forest chain"}, [&] { bench::do_not_optimize(net::find_forest_fault(chain).code); });
    print_row("find_forest_fault, chain of 1000", forest);
    bench::Report const sealed_forest = measure_row(
        bench::Run{"forest sealed chain"}, [&] { bench::do_not_optimize(net::find_forest_fault(sealed).code); });
    print_row("find_forest_fault, sealed chain of 1000", sealed_forest);
    print_row("find_proof_net_fault, chain of 1000", measure_row(bench::Run{"proof net chain"}, [&] {
                  bench::do_not_optimize(net::find_proof_net_fault(chain).code);
              }));
    print_row("find_proof_net_fault, sealed chain of 1000", measure_row(bench::Run{"proof net sealed chain"}, [&] {
                  bench::do_not_optimize(net::find_proof_net_fault(sealed).code);
              }));
    print_row("find_vicious_fault, chain of 1000", measure_row(bench::Run{"vicious chain"}, [&] {
                  bench::do_not_optimize(net::find_vicious_fault(chain).code);
              }));
    print_row("find_vicious_fault, sealed chain of 1000", measure_row(bench::Run{"vicious sealed chain"}, [&] {
                  bench::do_not_optimize(net::find_vicious_fault(sealed).code);
              }));
    bench::Report const dataflow = measure_row(bench::Run{"dataflow chain"},
                                               [&] { bench::do_not_optimize(net::find_dataflow_fault(chain).code); });
    print_row("find_dataflow_fault, chain of 1000", dataflow);
    bench::Report const sealed_dataflow = measure_row(
        bench::Run{"dataflow sealed chain"}, [&] { bench::do_not_optimize(net::find_dataflow_fault(sealed).code); });
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
    print_ratio("find_dataflow_fault / reference sort of Kahn", dataflow, kahn);
    print_ratio("find_forest_fault / reference union-find", forest, union_find);
    print_ratio("find_canonical_form / find_wiring_fault", form, wiring);
    print_ratio("find_dataflow_fault sealed / reference sort of Kahn", sealed_dataflow, kahn);
    print_ratio("find_forest_fault sealed / reference union-find", sealed_forest, union_find);
}

void bench_fork_tree(ForkNet const& tree) {
    print_row("find_proof_net_fault, fork tree of 1001", measure_row(bench::Run{"proof net fork tree"}, [&] {
                  bench::do_not_optimize(net::find_proof_net_fault(tree).code);
              }));
    auto const sealed = net::seal_net(tree);
    bench::do_not_optimize(sealed);
    print_row("find_proof_net_fault, sealed fork tree of 1001",
              measure_row(bench::Run{"proof net sealed fork tree"},
                          [&] { bench::do_not_optimize(net::find_proof_net_fault(sealed).code); }));
}

void bench_ring(RingSystem const& ring) {
    print_row("find_liveness_fault, ring of 4000 actions", measure_row(bench::Run{"liveness ring"}, [&] {
                  bench::do_not_optimize(net::find_liveness_fault(ring).code);
              }));
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
