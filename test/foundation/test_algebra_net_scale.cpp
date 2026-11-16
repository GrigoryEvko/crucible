// The checks of foundation/algebra/net at the scale of probe E4: nets of
// about 1,000 agents and a ring of 1,000 processes.  Each check runs in the
// compile of this unit, so the unit holds the compile cost of each check.
//
//   fork tree   a binary tree of forks (a par of two children) and joins,
//               with a body at each leaf: 3L + 2 agents for L leaves
//   chain       a pipeline of a starter, stages and a sink
//   sum tree    the tree of foundation/core/Reduce.h over num agents, with
//               one boundary result: a dataflow net
//   ring        the Ring global type over one queue for each pair, with N
//               roles and two iterations: 4N actions

#include <foundation/algebra/net/Net.h>
#include <foundation/algebra/net/Priority.h>

#include <cstddef>
#include <cstdint>

namespace {

using foundation::algebra::net::Action;
using foundation::algebra::net::AgentId;
using foundation::algebra::net::ChannelId;
using foundation::algebra::net::ChannelSide;
using foundation::algebra::net::give;
using foundation::algebra::net::KindId;
using foundation::algebra::net::Net;
using foundation::algebra::net::NetSignature;
using foundation::algebra::net::NetVerdict;
using foundation::algebra::net::PortRef;
using foundation::algebra::net::ProcessId;
using foundation::algebra::net::ProcessSystem;
using foundation::algebra::net::take;
using foundation::algebra::net::TypeId;
using foundation::algebra::net::Wire;

[[nodiscard]] consteval Wire join_ports(std::uint32_t left_agent, std::uint32_t left_port, std::uint32_t right_agent,
                                        std::uint32_t right_port) noexcept {
    return Wire{PortRef{AgentId{left_agent}, left_port}, PortRef{AgentId{right_agent}, right_port}};
}

// ── The fork tree ───────────────────────────────────────────────────────

inline constexpr TypeId whole{0};
inline constexpr TypeId kids{1};

inline constexpr NetSignature<2, 7> fork_kinds{
    .type_names = {"Whole", "Kids"},
    .kinds = {
        {.name = "root", .port_names = {"whole"}, .port_types = {give(whole)}},
        {.name = "split", .port_names = {"whole", "kids"}, .port_types = {take(whole), give(kids)}},
        {.name = "fork",
         .port_names = {"kids", "left", "right"},
         .port_types = {take(kids), give(kids), give(kids)},
         .principal = 0,
         .par_ports = 0b110},
        {.name = "body", .port_names = {"in", "out"}, .port_types = {take(kids), give(kids)}},
        {.name = "join",
         .port_names = {"left", "right", "kids"},
         .port_types = {take(kids), take(kids), give(kids)},
         .principal = 2},
        {.name = "combine", .port_names = {"kids", "whole"}, .port_types = {take(kids), give(whole)}},
        {.name = "drop", .port_names = {"whole"}, .port_types = {take(whole)}},
    }};

template <std::size_t Agents, std::size_t Wires>
struct NetWriter {
    Net<2, 7, Agents, Wires> net{};
    std::uint32_t agents = 0;
    std::uint32_t wires = 0;

    consteval std::uint32_t add_agent(std::uint32_t kind) noexcept {
        net.agents[agents] = KindId{kind};
        return agents++;
    }
    consteval void add_wire(PortRef left, PortRef right) noexcept {
        net.wires[wires] = Wire{left, right};
        ++wires;
    }
};

// The fork and the join of count leaves: the port that takes the kids, and
// the port that gives them back.
struct ForkEnds {
    PortRef in{};
    PortRef out{};
};

template <std::size_t Agents, std::size_t Wires>
consteval ForkEnds write_fork(NetWriter<Agents, Wires>& writer, std::uint32_t count) noexcept {
    if (count == 1) {
        std::uint32_t const body = writer.add_agent(3);
        return ForkEnds{PortRef{AgentId{body}, 0}, PortRef{AgentId{body}, 1}};
    }
    std::uint32_t const fork = writer.add_agent(2);
    std::uint32_t const join = writer.add_agent(4);
    ForkEnds const left = write_fork(writer, count / 2);
    ForkEnds const right = write_fork(writer, count - count / 2);
    writer.add_wire(PortRef{AgentId{fork}, 1}, left.in);
    writer.add_wire(PortRef{AgentId{fork}, 2}, right.in);
    writer.add_wire(left.out, PortRef{AgentId{join}, 0});
    writer.add_wire(right.out, PortRef{AgentId{join}, 1});
    return ForkEnds{PortRef{AgentId{fork}, 0}, PortRef{AgentId{join}, 2}};
}

template <std::size_t Leaves>
[[nodiscard]] consteval Net<2, 7, 3 * Leaves + 2, 4 * Leaves> build_fork_tree() noexcept {
    NetWriter<3 * Leaves + 2, 4 * Leaves> writer{};
    writer.net.signature = fork_kinds;
    std::uint32_t const root = writer.add_agent(0);
    std::uint32_t const split = writer.add_agent(1);
    ForkEnds const top = write_fork(writer, Leaves);
    std::uint32_t const combine = writer.add_agent(5);
    std::uint32_t const drop = writer.add_agent(6);
    writer.add_wire(PortRef{AgentId{root}, 0}, PortRef{AgentId{split}, 0});
    writer.add_wire(PortRef{AgentId{split}, 1}, top.in);
    writer.add_wire(top.out, PortRef{AgentId{combine}, 0});
    writer.add_wire(PortRef{AgentId{combine}, 1}, PortRef{AgentId{drop}, 0});
    return writer.net;
}

inline constexpr auto fork_of_1001 = build_fork_tree<333>();
static_assert(check_wiring(fork_of_1001).is_admitted());
static_assert(check_proof_net(fork_of_1001).is_admitted());
static_assert(check_vicious(fork_of_1001).is_admitted());
static_assert(check_dataflow(fork_of_1001).is_admitted());

// ── The chain ───────────────────────────────────────────────────────────

inline constexpr TypeId batch{0};

inline constexpr NetSignature<1, 3> chain_kinds{
    .type_names = {"Batch"},
    .kinds = {
        {.name = "starter", .port_names = {"out"}, .port_types = {give(batch)}},
        {.name = "stage", .port_names = {"in", "out"}, .port_types = {take(batch), give(batch)}},
        {.name = "sink", .port_names = {"in"}, .port_types = {take(batch)}},
    }};

template <std::size_t Agents>
[[nodiscard]] consteval Net<1, 3, Agents, Agents - 1> build_chain() noexcept {
    Net<1, 3, Agents, Agents - 1> net{};
    net.signature = chain_kinds;
    for (std::uint32_t agent = 0; agent < Agents; ++agent) {
        net.agents[agent] = KindId{agent == 0 ? 0u : (agent + 1 == Agents ? 2u : 1u)};
    }
    for (std::uint32_t wire = 0; wire + 1 < Agents; ++wire) {
        net.wires[wire] = join_ports(wire, wire == 0 ? 0u : 1u, wire + 1, 0);
    }
    return net;
}

inline constexpr auto chain_of_1000 = build_chain<1000>();
static_assert(check_forest(chain_of_1000).is_admitted());
static_assert(check_proof_net(chain_of_1000).is_admitted());
static_assert(check_dataflow(chain_of_1000).is_admitted());

// ── The sum tree ────────────────────────────────────────────────────────

inline constexpr TypeId value{0};

inline constexpr NetSignature<1, 3> sum_kinds{
    .type_names = {"Value"},
    .kinds = {
        {.name = "num", .port_names = {"value"}, .port_types = {give(value)}},
        {.name = "add", .port_names = {"left", "right", "sum"}, .port_types = {take(value), take(value), give(value)}},
        {.name = "result", .port_names = {"in"}, .port_types = {take(value)}, .is_boundary = true},
    }};

// The largest power of two below count, for count of at least 2.
[[nodiscard]] consteval std::uint32_t split_of(std::uint32_t count) noexcept {
    std::uint32_t split = 1;
    while (split * 2 < count)
        split *= 2;
    return split;
}

template <std::size_t Agents, std::size_t Wires>
struct SumWriter {
    Net<1, 3, Agents, Wires> net{};
    std::uint32_t adds = 0;
    std::uint32_t wires = 0;
};

// The tree of the leaves [first, first + count) of Reduce.h, with the adds
// after the leaves.  Gives the port that holds the value of the tree.
template <std::size_t Leaves, std::size_t Agents, std::size_t Wires>
consteval PortRef write_sum(SumWriter<Agents, Wires>& writer, std::uint32_t first, std::uint32_t count) noexcept {
    if (count == 1) return PortRef{AgentId{first}, 0};
    std::uint32_t const split = split_of(count);
    PortRef const left = write_sum<Leaves>(writer, first, split);
    PortRef const right = write_sum<Leaves>(writer, first + split, count - split);
    std::uint32_t const add = static_cast<std::uint32_t>(Leaves) + writer.adds;
    ++writer.adds;
    writer.net.wires[writer.wires] = Wire{left, PortRef{AgentId{add}, 0}};
    writer.net.wires[writer.wires + 1] = Wire{right, PortRef{AgentId{add}, 1}};
    writer.wires += 2;
    return PortRef{AgentId{add}, 2};
}

template <std::size_t Leaves>
[[nodiscard]] consteval Net<1, 3, 2 * Leaves, 2 * Leaves - 1> build_sum_tree() noexcept {
    SumWriter<2 * Leaves, 2 * Leaves - 1> writer{};
    writer.net.signature = sum_kinds;
    for (std::uint32_t leaf = 0; leaf < Leaves; ++leaf)
        writer.net.agents[leaf] = KindId{0};
    for (std::uint32_t add = 0; add + 1 < Leaves; ++add)
        writer.net.agents[Leaves + add] = KindId{1};
    writer.net.agents[2 * Leaves - 1] = KindId{2};
    PortRef const root = write_sum<Leaves>(writer, 0, Leaves);
    writer.net.wires[writer.wires] = Wire{root, PortRef{AgentId{2 * Leaves - 1}, 0}};
    return writer.net;
}

inline constexpr auto sum_of_1000 = build_sum_tree<500>();
static_assert(check_forest(sum_of_1000).is_admitted());
static_assert(check_vicious(sum_of_1000).is_admitted());
static_assert(check_dataflow(sum_of_1000).is_admitted());

// ── The ring of queues ──────────────────────────────────────────────────

template <std::size_t Roles>
[[nodiscard]] consteval ProcessSystem<Roles, Roles, 4 * Roles> build_ring() noexcept {
    constexpr auto roles = static_cast<std::uint32_t>(Roles);
    ProcessSystem<Roles, Roles, 4 * Roles> protocol{};
    for (std::uint32_t role = 0; role < Roles; ++role) {
        protocol.process_names[role] = foundation::algebra::net::NetName{"role"};
        protocol.channel_names[role] = foundation::algebra::net::NetName{"queue"};
    }
    std::uint32_t next = 0;
    for (std::uint32_t role = 0; role < Roles; ++role) {
        std::uint32_t const left = (role + roles - 1) % roles;
        Action const send{ProcessId{role}, ChannelId{role}, ChannelSide::First, true, true, 0};
        Action const receive{ProcessId{role}, ChannelId{left}, ChannelSide::Second, false, true, 0};
        for (std::uint32_t iteration = 0; iteration < 2; ++iteration) {
            protocol.actions[next] = role == 0 ? send : receive;
            protocol.actions[next + 1] = role == 0 ? receive : send;
            next += 2;
        }
    }
    return protocol;
}

inline constexpr auto ring_of_1000 = build_ring<1000>();
static_assert(check_priorities(ring_of_1000).is_admitted());

// A ring whose first role also waits first: each role waits on its left
// neighbour, so the ring deadlocks.
template <std::size_t Roles>
[[nodiscard]] consteval ProcessSystem<Roles, Roles, 2 * Roles> build_waiting_ring() noexcept {
    constexpr auto roles = static_cast<std::uint32_t>(Roles);
    ProcessSystem<Roles, Roles, 2 * Roles> protocol{};
    for (std::uint32_t role = 0; role < Roles; ++role) {
        protocol.process_names[role] = foundation::algebra::net::NetName{"role"};
        protocol.channel_names[role] = foundation::algebra::net::NetName{"queue"};
        std::uint32_t const left = (role + roles - 1) % roles;
        protocol.actions[2 * role] = Action{ProcessId{role}, ChannelId{left}, ChannelSide::Second, false, true, 0};
        protocol.actions[2 * role + 1] = Action{ProcessId{role}, ChannelId{role}, ChannelSide::First, true, true, 0};
    }
    return protocol;
}

inline constexpr NetVerdict waiting_ring = check_priorities(build_waiting_ring<1000>());
static_assert(!waiting_ring.is_admitted());

}  // namespace

// The text of a refusal is a constant, and a program reads it at run time.
int main() {
    bool const is_named = waiting_ring.starts_with(
        "priorities: a cycle of waits: role waits on 'queue' step 0 before 'queue' step 0, role waits on");
    return is_named ? 0 : 1;
}
