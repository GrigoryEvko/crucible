// The checks of foundation/algebra/net at the scale of probe E4: nets of
// about 1,000 agents and a ring of 1,000 processes.  Each check runs in the
// compile of this unit, so the unit holds the compile cost of each check.
//
//   fork tree   a binary tree of forks (a par of two children) and joins,
//               with a body at each leaf: 3L + 2 agents for L leaves
//   chain       a pipeline of a starter, stages and a sink
//   sum tree    the tree of foundation/core/Reduce.h over num agents, with
//               one boundary result: a dataflow net
//   ring        the Ring global type over one rendezvous for each pair, with
//               N roles and two iterations: 4N actions
//
// main runs each find function of the checks at run time on the same nets
// and systems, and compares each fault with the fault of the compile.  A
// chain with two crossed stages and a chain with a miswired stage give a
// fault of each check, so a body that admits each net fails the test.

#include <foundation/algebra/net/Net.h>
#include <foundation/algebra/net/Priority.h>

#include <cstddef>
#include <cstdint>

namespace {

using foundation::algebra::net::Action;
using foundation::algebra::net::ActionId;
using foundation::algebra::net::AgentId;
using foundation::algebra::net::CanonicalNet;
using foundation::algebra::net::ChannelId;
using foundation::algebra::net::give;
using foundation::algebra::net::KindId;
using foundation::algebra::net::LivenessFault;
using foundation::algebra::net::LivenessFaultCode;
using foundation::algebra::net::max_fault_path;
using foundation::algebra::net::max_liveness_path;
using foundation::algebra::net::Move;
using foundation::algebra::net::Net;
using foundation::algebra::net::NetChannel;
using foundation::algebra::net::NetFault;
using foundation::algebra::net::NetFaultCode;
using foundation::algebra::net::NetName;
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
        protocol.process_names[role] = NetName{"role"};
        protocol.channels[role] = NetChannel{.name = NetName{"queue"}};
    }
    std::uint32_t next = 0;
    for (std::uint32_t role = 0; role < Roles; ++role) {
        std::uint32_t const left = (role + roles - 1) % roles;
        Action const send{.process = ProcessId{role}, .channel = ChannelId{role}, .move = Move::Send};
        Action const receive{.process = ProcessId{role}, .channel = ChannelId{left}, .move = Move::Receive};
        for (std::uint32_t iteration = 0; iteration < 2; ++iteration) {
            protocol.actions[next] = role == 0 ? send : receive;
            protocol.actions[next + 1] = role == 0 ? receive : send;
            next += 2;
        }
    }
    return protocol;
}

inline constexpr auto ring_of_1000 = build_ring<1000>();
static_assert(check_liveness(ring_of_1000).is_admitted());

// A ring whose first role also waits first: each role waits on its left
// neighbour, so the ring deadlocks.
template <std::size_t Roles>
[[nodiscard]] consteval ProcessSystem<Roles, Roles, 2 * Roles> build_waiting_ring() noexcept {
    constexpr auto roles = static_cast<std::uint32_t>(Roles);
    ProcessSystem<Roles, Roles, 2 * Roles> protocol{};
    for (std::uint32_t role = 0; role < Roles; ++role) {
        protocol.process_names[role] = NetName{"role"};
        protocol.channels[role] = NetChannel{.name = NetName{"queue"}};
        std::uint32_t const left = (role + roles - 1) % roles;
        protocol.actions[2 * role] =
            Action{.process = ProcessId{role}, .channel = ChannelId{left}, .move = Move::Receive};
        protocol.actions[2 * role + 1] =
            Action{.process = ProcessId{role}, .channel = ChannelId{role}, .move = Move::Send};
    }
    return protocol;
}

inline constexpr auto waiting_ring_of_1000 = build_waiting_ring<1000>();
inline constexpr NetVerdict waiting_ring = check_liveness(waiting_ring_of_1000);
static_assert(!waiting_ring.is_admitted());

// ── The run-time form ───────────────────────────────────────────────────

// Two faults are the same when each field that a check sets is the same.
[[nodiscard]] constexpr bool is_same_fault(NetFault const& left, NetFault const& right) noexcept {
    if (left.code != right.code || left.type != right.type || left.kind != right.kind || left.agent != right.agent
        || left.port != right.port || left.wire != right.wire || left.path_count != right.path_count
        || left.is_path_cut != right.is_path_cut) {
        return false;
    }
    for (std::uint32_t index = 0; index < left.path_count && index < max_fault_path; ++index) {
        if (left.path[index] != right.path[index]) return false;
    }
    return true;
}

[[nodiscard]] constexpr bool is_same_fault(LivenessFault const& left, LivenessFault const& right) noexcept {
    if (left.code != right.code || left.process != right.process || left.channel != right.channel
        || left.action != right.action || left.path_count != right.path_count
        || left.is_path_cut != right.is_path_cut) {
        return false;
    }
    for (std::uint32_t index = 0; index < left.path_count && index < max_liveness_path; ++index) {
        if (left.path[index] != right.path[index]) return false;
    }
    return true;
}

// The chain with the inputs of two stages crossed from the stage at first:
// stage first feeds stage first + 101, and stage first + 100 feeds stage
// first + 1.  The wires of the stages between them close a cycle, their
// principal ports meet auxiliary ports in a cycle, and they wait on each
// other in a cycle.
template <std::size_t Agents>
[[nodiscard]] constexpr Net<1, 3, Agents, Agents - 1> cross_chain(Net<1, 3, Agents, Agents - 1> net,
                                                                  AgentId first) noexcept {
    auto const stage = static_cast<std::uint32_t>(first);
    net.wires[stage].right = PortRef{AgentId{stage + 101}, 0};
    net.wires[stage + 100].right = PortRef{AgentId{stage + 1}, 0};
    return net;
}

// The chain with the output of a stage wired to the output of the next
// stage: the wire joins two give ports, which are no duals.
template <std::size_t Agents>
[[nodiscard]] constexpr Net<1, 3, Agents, Agents - 1> miswire_chain(Net<1, 3, Agents, Agents - 1> net,
                                                                    AgentId stage) noexcept {
    auto const index = static_cast<std::uint32_t>(stage);
    net.wires[index].right = PortRef{AgentId{index + 1}, 1};
    return net;
}

inline constexpr AgentId crossed_stage{100};
inline constexpr AgentId miswired_stage{500};
inline constexpr auto crossed_chain = cross_chain(chain_of_1000, crossed_stage);
inline constexpr auto miswired_chain = miswire_chain(chain_of_1000, miswired_stage);

// The faults of the compile on the two faulty chains and the waiting ring.
inline constexpr NetFault miswired_wiring_fault = find_wiring_fault(miswired_chain);
static_assert(miswired_wiring_fault.code == NetFaultCode::WireNotDual && miswired_wiring_fault.wire == 500);
inline constexpr NetFault crossed_forest_fault = find_forest_fault(crossed_chain);
static_assert(crossed_forest_fault.code == NetFaultCode::ForestCycle && crossed_forest_fault.wire == 200);
inline constexpr NetFault crossed_proof_net_fault = find_proof_net_fault(crossed_chain);
static_assert(crossed_proof_net_fault.code == NetFaultCode::ProofNetCycle);
inline constexpr NetFault crossed_vicious_fault = find_vicious_fault(crossed_chain);
static_assert(crossed_vicious_fault.code == NetFaultCode::ViciousCircle && crossed_vicious_fault.is_path_cut);
inline constexpr NetFault crossed_dataflow_fault = find_dataflow_fault(crossed_chain);
static_assert(crossed_dataflow_fault.code == NetFaultCode::DataflowCycle && crossed_dataflow_fault.path_count == 5);
static_assert(crossed_dataflow_fault.is_path_cut);
inline constexpr LivenessFault waiting_ring_fault = find_liveness_fault(waiting_ring_of_1000);
static_assert(waiting_ring_fault.code == LivenessFaultCode::CycleWithoutToken);

// The place of each fault: the agent and the port where the cycle closes, and
// the first members of each path.  A body that finds the same verdict at
// another place fails here.
static_assert(crossed_proof_net_fault.agent == AgentId{200} && crossed_proof_net_fault.port == 1);
static_assert(crossed_vicious_fault.agent == AgentId{101} && crossed_vicious_fault.path_count == max_fault_path
              && crossed_vicious_fault.path[1] == AgentId{200} && crossed_vicious_fault.path[5] == AgentId{196});
static_assert(crossed_dataflow_fault.agent == AgentId{101} && crossed_dataflow_fault.path[1] == AgentId{200}
              && crossed_dataflow_fault.path[4] == AgentId{197});
static_assert(waiting_ring_fault.path_count == max_liveness_path && waiting_ring_fault.is_path_cut
              && waiting_ring_fault.path[0] == ActionId{1} && waiting_ring_fault.path[1] == ActionId{1999}
              && waiting_ring_fault.path[3] == ActionId{1995});

// The canonical form of the sum tree: its result is a boundary agent, so the
// walk from it numbers each agent.  The chain has no boundary agent, so it
// has no form.
inline constexpr CanonicalNet sum_form = find_canonical_form(sum_of_1000);
static_assert(!sum_form.fault.is_fault());

// The types of the nets and the systems above.  The chain and the sum tree
// have one type.
using ForkNet = Net<2, 7, 1001, 1332>;
using LineNet = Net<1, 3, 1000, 999>;
using RingSystem = ProcessSystem<1000, 1000, 4000>;
using WaitingRingSystem = ProcessSystem<1000, 1000, 2000>;

// The checks of Net.h.
enum class NetCheck : std::uint8_t {
    Wiring = 0,
    Forest = 1,
    ProofNet = 2,
    Vicious = 3,
    Dataflow = 4,
};

// The fault of one check of a net at run time.  The net is a parameter, so
// the compiler does not fold the call into a second constant evaluation
// (THE RUN-TIME FORM of Net.h).  noinline keeps the arrays of each check in
// a frame of its own.
template <NetCheck Check, class Graph>
[[gnu::noinline]] NetFault find_at_run_time(Graph const& graph) noexcept {
    if constexpr (Check == NetCheck::Wiring) {
        return find_wiring_fault(graph);
    } else if constexpr (Check == NetCheck::Forest) {
        return find_forest_fault(graph);
    } else if constexpr (Check == NetCheck::ProofNet) {
        return find_proof_net_fault(graph);
    } else if constexpr (Check == NetCheck::Vicious) {
        return find_vicious_fault(graph);
    } else {
        return find_dataflow_fault(graph);
    }
}

// The fault of the liveness check of a system at run time.
template <class System>
[[gnu::noinline]] LivenessFault find_liveness_at_run_time(System const& protocol) noexcept {
    return find_liveness_fault(protocol);
}

// The canonical form of a net at run time.
template <class Graph>
[[gnu::noinline]] CanonicalNet find_form_at_run_time(Graph const& graph) noexcept {
    return find_canonical_form(graph);
}

// An admitted net has the fault NoFault at compile time, as the
// static_asserts of each net above show.
[[nodiscard]] bool does_fork_match(ForkNet const& tree) noexcept {
    return is_same_fault(find_at_run_time<NetCheck::Wiring>(tree), NetFault{})
        && is_same_fault(find_at_run_time<NetCheck::ProofNet>(tree), NetFault{})
        && is_same_fault(find_at_run_time<NetCheck::Vicious>(tree), NetFault{})
        && is_same_fault(find_at_run_time<NetCheck::Dataflow>(tree), NetFault{});
}

// The chain passes, and the miswired chain and the crossed chain, made from
// it at run time, give the faults of the compile.
[[nodiscard]] bool does_chain_match(LineNet const& chain) noexcept {
    LineNet const crossed = cross_chain(chain, crossed_stage);
    LineNet const miswired = miswire_chain(chain, miswired_stage);
    return is_same_fault(find_at_run_time<NetCheck::Forest>(chain), NetFault{})
        && is_same_fault(find_at_run_time<NetCheck::ProofNet>(chain), NetFault{})
        && is_same_fault(find_at_run_time<NetCheck::Dataflow>(chain), NetFault{})
        && is_same_fault(find_at_run_time<NetCheck::Wiring>(miswired), miswired_wiring_fault)
        && is_same_fault(find_at_run_time<NetCheck::Forest>(crossed), crossed_forest_fault)
        && is_same_fault(find_at_run_time<NetCheck::ProofNet>(crossed), crossed_proof_net_fault)
        && is_same_fault(find_at_run_time<NetCheck::Vicious>(crossed), crossed_vicious_fault)
        && is_same_fault(find_at_run_time<NetCheck::Dataflow>(crossed), crossed_dataflow_fault);
}

[[nodiscard]] bool does_sum_match(LineNet const& sum) noexcept {
    CanonicalNet const form = find_form_at_run_time(sum);
    return is_same_fault(find_at_run_time<NetCheck::Forest>(sum), NetFault{})
        && is_same_fault(find_at_run_time<NetCheck::Vicious>(sum), NetFault{})
        && is_same_fault(find_at_run_time<NetCheck::Dataflow>(sum), NetFault{})
        && is_same_fault(form.fault, sum_form.fault) && form.hash == sum_form.hash;
}

// The sealed nets of the chain, of the miswired chain and of the crossed
// chain, and of the sum tree, give the faults and the form of the compile at
// run time.  Each net is sealed one time, and each check reads its wiring
// fault from the seal.
[[nodiscard]] bool do_sealed_nets_match(LineNet const& chain, LineNet const& sum) noexcept {
    auto const sealed = seal_net(chain);
    auto const crossed = seal_net(cross_chain(chain, crossed_stage));
    auto const miswired = seal_net(miswire_chain(chain, miswired_stage));
    CanonicalNet const form = find_form_at_run_time(seal_net(sum));
    return is_same_fault(sealed.wiring_fault(), NetFault{})
        && is_same_fault(find_at_run_time<NetCheck::Forest>(sealed), NetFault{})
        && is_same_fault(find_at_run_time<NetCheck::ProofNet>(sealed), NetFault{})
        && is_same_fault(find_at_run_time<NetCheck::Dataflow>(sealed), NetFault{})
        && is_same_fault(miswired.wiring_fault(), miswired_wiring_fault)
        && is_same_fault(find_at_run_time<NetCheck::Forest>(miswired), miswired_wiring_fault)
        && is_same_fault(find_at_run_time<NetCheck::Forest>(crossed), crossed_forest_fault)
        && is_same_fault(find_at_run_time<NetCheck::ProofNet>(crossed), crossed_proof_net_fault)
        && is_same_fault(find_at_run_time<NetCheck::Vicious>(crossed), crossed_vicious_fault)
        && is_same_fault(find_at_run_time<NetCheck::Dataflow>(crossed), crossed_dataflow_fault)
        && is_same_fault(form.fault, sum_form.fault) && form.hash == sum_form.hash;
}

[[nodiscard]] bool do_rings_match(RingSystem const& ring, WaitingRingSystem const& waiting) noexcept {
    return is_same_fault(find_liveness_at_run_time(ring), LivenessFault{})
        && is_same_fault(find_liveness_at_run_time(waiting), waiting_ring_fault);
}

// ── The places of the faults ────────────────────────────────────────────
//
// Each variant below comes from a net or a system above at run time, and
// each expected fault holds every field that a check sets.  So a body that
// gives the same verdict at another place fails the test.  noinline keeps
// the copies of each function in a frame of its own.

// A stage wired to itself, with its two neighbours joined: a wire of one
// agent, and a net of two parts.  The signature of the chain has no par
// kind, so the proof net check of this net names the port that closes the
// cycle in the order of the agents and their ports.
[[gnu::noinline, nodiscard]] bool does_looped_chain_keep_its_fault(LineNet const& chain) noexcept {
    LineNet looped = chain;
    looped.wires[499] = Wire{PortRef{AgentId{499}, 1}, PortRef{AgentId{501}, 0}};
    looped.wires[500] = Wire{PortRef{AgentId{500}, 1}, PortRef{AgentId{500}, 0}};
    NetFault const expected{.code = NetFaultCode::ProofNetCycle, .agent = AgentId{500}, .port = 1};
    return is_same_fault(find_at_run_time<NetCheck::ProofNet>(looped), expected)
        && is_same_fault(find_at_run_time<NetCheck::ProofNet>(seal_net(looped)), expected);
}

// The index of the wire with an end at a port of the fork tree.
[[nodiscard]] std::uint32_t wire_at(ForkNet const& tree, PortRef end) noexcept {
    for (std::uint32_t index = 0; index < 1332; ++index) {
        PortRef const left = tree.wires[index].left;
        PortRef const right = tree.wires[index].right;
        if ((left.agent == end.agent && left.port == end.port)
            || (right.agent == end.agent && right.port == end.port)) {
            return index;
        }
    }
    return 0;
}

// The fork at agent 16 has two bodies as its leaves, and its join is agent
// 17.  In the looped tree, the kids of the join go back into the fork, and
// the outside skips the two, so the par of the fork reaches its own class.
// In the cycled tree, the kids of the join go into its right body, and the
// right par port of the fork goes to the parent join, so the join and the
// body close a cycle with no par.  In the swapped tree, two premise wires
// change their ends, so the par ports of the fork at agent 23 never meet.
[[gnu::noinline, nodiscard]] bool do_fork_variants_keep_their_faults(ForkNet const& tree) noexcept {
    ForkNet looped = tree;
    std::uint32_t const into_fork = wire_at(looped, PortRef{AgentId{16}, 0});
    std::uint32_t const out_of_join = wire_at(looped, PortRef{AgentId{17}, 2});
    PortRef const outer_give = looped.wires[into_fork].left;
    PortRef const outer_take = looped.wires[out_of_join].right;
    looped.wires[into_fork] = Wire{PortRef{AgentId{17}, 2}, PortRef{AgentId{16}, 0}};
    looped.wires[out_of_join] = Wire{outer_give, outer_take};
    ForkNet cycled = tree;
    std::uint32_t const right_premise = wire_at(cycled, PortRef{AgentId{16}, 2});
    PortRef const body_in = cycled.wires[right_premise].right;
    cycled.wires[right_premise] = Wire{PortRef{AgentId{16}, 2}, outer_take};
    cycled.wires[out_of_join] = Wire{PortRef{AgentId{17}, 2}, body_in};
    ForkNet swapped = tree;
    swapped.wires[5].right = tree.wires[9].right;
    swapped.wires[9].right = tree.wires[5].right;
    NetFault const reaches_itself{.code = NetFaultCode::ParReachesItself, .agent = AgentId{16}};
    NetFault const closes_cycle{.code = NetFaultCode::ProofNetCycle, .agent = AgentId{19}, .port = 1};
    NetFault const never_meets{.code = NetFaultCode::ParPortsNeverMeet, .agent = AgentId{23}};
    return is_same_fault(find_at_run_time<NetCheck::ProofNet>(looped), reaches_itself)
        && is_same_fault(find_at_run_time<NetCheck::ProofNet>(seal_net(looped)), reaches_itself)
        && is_same_fault(find_at_run_time<NetCheck::ProofNet>(cycled), closes_cycle)
        && is_same_fault(find_at_run_time<NetCheck::ProofNet>(seal_net(cycled)), closes_cycle)
        && is_same_fault(find_at_run_time<NetCheck::ProofNet>(swapped), never_meets)
        && is_same_fault(find_at_run_time<NetCheck::ProofNet>(seal_net(swapped)), never_meets);
}

// The sum tree with each fault of the wiring.  find_canonical_form checks the
// wiring in its walk, so it gives the fault that find_wiring_fault gives.
[[gnu::noinline, nodiscard]] bool do_sum_variants_keep_their_faults(LineNet const& sum) noexcept {
    auto const keeps = [](LineNet const& graph, NetFault const& expected) noexcept {
        return is_same_fault(find_form_at_run_time(graph).fault, expected)
            && is_same_fault(find_at_run_time<NetCheck::Wiring>(graph), expected);
    };
    LineNet past_net = sum;
    past_net.wires[10].left = PortRef{AgentId{1000}, 0};
    LineNet past_ports = sum;
    past_ports.wires[10].left.port = 3;
    LineNet second_wire = sum;
    second_wire.wires[10].right = sum.wires[11].right;
    LineNet self_wire = sum;
    self_wire.wires[10].right = sum.wires[10].left;
    LineNet not_dual = sum;
    not_dual.wires[10].right = sum.wires[20].left;
    LineNet foreign = sum;
    foreign.agents[700] = KindId{5};
    return keeps(past_net, NetFault{.code = NetFaultCode::WireNodePastNet, .agent = AgentId{1000}, .wire = 10})
        && keeps(past_ports,
                 NetFault{.code = NetFaultCode::WirePortPastAgent, .agent = AgentId{503}, .port = 3, .wire = 10})
        && keeps(second_wire,
                 NetFault{.code = NetFaultCode::PortWithSecondWire, .agent = AgentId{505}, .port = 1, .wire = 11})
        && keeps(self_wire,
                 NetFault{.code = NetFaultCode::PortWithSecondWire, .agent = AgentId{503}, .port = 2, .wire = 10})
        && keeps(not_dual, NetFault{.code = NetFaultCode::WireNotDual, .wire = 10})
        && keeps(foreign, NetFault{.code = NetFaultCode::KindPastSignature, .agent = AgentId{700}});
}

// The waiting ring on channels of capacity 1: each role still receives
// first, so its cycle of waits holds no token.
[[gnu::noinline, nodiscard]] bool does_buffered_ring_keep_its_fault(WaitingRingSystem const& waiting) noexcept {
    WaitingRingSystem buffered = waiting;
    for (NetChannel& channel : buffered.channels)
        channel.capacity = 1;
    LivenessFault const expected{.code = LivenessFaultCode::CycleWithoutToken,
                                 .path_count = max_liveness_path,
                                 .is_path_cut = true,
                                 .path = {ActionId{0}, ActionId{1999}, ActionId{1998}, ActionId{1997}}};
    return is_same_fault(find_liveness_at_run_time(buffered), expected);
}

}  // namespace

// The text of a refusal is a constant, and a program reads it at run time.
// A null text at run time puts nothing.
int main() {
    bool const is_named = waiting_ring.starts_with(
        "liveness: a cycle of waits holds no token: 'queue' step 0 from role to role <- 'queue' step 0 from role to "
        "role <- ");
    NetVerdict const empty = NetVerdict::refuse(nullptr);
    bool const is_empty_refusal = empty.size() == 0 && !empty.is_admitted();
    bool const is_run_time_same = does_fork_match(fork_of_1001) && does_chain_match(chain_of_1000)
                               && does_sum_match(sum_of_1000) && do_rings_match(ring_of_1000, waiting_ring_of_1000)
                               && do_sealed_nets_match(chain_of_1000, sum_of_1000);
    bool const is_each_place_kept =
        does_looped_chain_keep_its_fault(chain_of_1000) && do_fork_variants_keep_their_faults(fork_of_1001)
        && do_sum_variants_keep_their_faults(sum_of_1000) && does_buffered_ring_keep_its_fault(waiting_ring_of_1000);
    return is_named && is_empty_refusal && is_run_time_same && is_each_place_kept ? 0 : 1;
}
