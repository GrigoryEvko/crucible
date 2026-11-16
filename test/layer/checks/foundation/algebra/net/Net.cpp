// The compile-time checks of foundation/algebra/net/Net.h.

#include <foundation/algebra/net/Net.h>

#include <cstdint>

namespace foundation::algebra::net {

namespace detail::net_self_test {

[[nodiscard]] consteval PortRef point_at(std::uint32_t agent, std::uint32_t port) noexcept {
    return PortRef{AgentId{agent}, port};
}

[[nodiscard]] consteval Wire join_ports(std::uint32_t left_agent, std::uint32_t left_port, std::uint32_t right_agent,
                                        std::uint32_t right_port) noexcept {
    return Wire{point_at(left_agent, left_port), point_at(right_agent, right_port)};
}

// ── The types ───────────────────────────────────────────────────────────

static_assert(dual_of(give(TypeId{3})) == take(TypeId{3}));
static_assert(dual_of(take(TypeId{3})) == give(TypeId{3}));
static_assert(dual_of(meet(TypeId{3})) == meet(TypeId{3}));
static_assert(!(dual_of(give(TypeId{3})) == take(TypeId{4})));
static_assert(NetName{"kids"} == NetName{"kids"} && !(NetName{"kids"} == NetName{"kid"}));
static_assert(NetName{"kids"}.size() == 4 && NetName{}.is_empty() && NetName{"kids"}.at(9) == '\0');

// A name of a static string, as reflection gives it.
static_assert(NetName{static_cast<char const*>("drain")} == NetName{"drain"});

// A null text puts nothing, and the verdict stays a refusal.
static_assert(NetVerdict::refuse(nullptr).size() == 0 && !NetVerdict::refuse(nullptr).is_admitted());
static_assert(NetVerdict::refuse("net: ").put(nullptr).says("net: "));

// ── The signature ───────────────────────────────────────────────────────

inline constexpr TypeId value{0};

inline constexpr NetSignature<1, 1> no_port{.type_names = {"Value"}, .kinds = {{.name = "lonely"}}};
static_assert(check_signature(no_port).says("net: agent kind 'lonely' has no port.  An agent has one principal port."));

inline constexpr NetSignature<1, 1> gap{
    .type_names = {"Value"},
    .kinds = {{.name = "gap", .port_names = {"in", "", "out"}, .port_types = {take(value), take(value), give(value)}}}};
static_assert(check_signature(gap).says("net: port 'out' of agent kind 'gap' comes after a port with no name.  The "
                                        "ports of a kind are the names before the first empty name."));

inline constexpr NetSignature<1, 1> far_principal{
    .type_names = {"Value"},
    .kinds = {{.name = "far", .port_names = {"in"}, .port_types = {take(value)}, .principal = 3}}};
static_assert(
    check_signature(far_principal).says("net: the principal port 3 of agent kind 'far' is past its 1 ports."));

inline constexpr NetSignature<1, 1> lone_par{.type_names = {"Value"},
                                             .kinds = {{.name = "lone",
                                                        .port_names = {"in", "out"},
                                                        .port_types = {take(value), give(value)},
                                                        .principal = 0,
                                                        .par_ports = 0b10}}};
static_assert(check_signature(lone_par).says("net: the par group of agent kind 'lone' holds one port.  A par group "
                                             "holds two or more ports."));

inline constexpr NetSignature<1, 1> principal_par{.type_names = {"Value"},
                                                  .kinds = {{.name = "fork",
                                                             .port_names = {"in", "left", "right"},
                                                             .port_types = {take(value), give(value), give(value)},
                                                             .principal = 0,
                                                             .par_ports = 0b011}}};
static_assert(check_signature(principal_par)
                  .says("net: the par group of agent kind 'fork' holds its principal port "
                        "'in'.  A par group holds auxiliary ports."));

inline constexpr NetSignature<1, 1> wide_boundary{.type_names = {"Value"},
                                                  .kinds = {{.name = "result",
                                                             .port_names = {"in", "out"},
                                                             .port_types = {take(value), give(value)},
                                                             .is_boundary = true}}};
static_assert(check_signature(wide_boundary)
                  .says("net: the boundary kind 'result' has 2 ports.  A boundary agent "
                        "stands for one free port of the net, so it has one port."));

inline constexpr NetSignature<1, 1> unknown_type{
    .type_names = {"Value"}, .kinds = {{.name = "num", .port_names = {"value"}, .port_types = {give(TypeId{2})}}}};
static_assert(check_signature(unknown_type)
                  .says("net: port 'value' of agent kind 'num' has the type 2, and the "
                        "signature has 1 types."));

inline constexpr NetSignature<2, 1> unnamed_type{
    .type_names = {"Value", ""}, .kinds = {{.name = "num", .port_names = {"value"}, .port_types = {give(value)}}}};
static_assert(check_signature(unnamed_type).says("net: type 1 of the signature has no name.  Each type has a name."));

inline constexpr NetSignature<1, 2> unnamed_kind{
    .type_names = {"Value"}, .kinds = {{.name = "num", .port_names = {"value"}, .port_types = {give(value)}}, {}}};
static_assert(check_signature(unnamed_kind).says("net: kind 1 of the signature has no name."));

inline constexpr NetSignature<1, 1> far_par{.type_names = {"Value"},
                                            .kinds = {{.name = "far",
                                                       .port_names = {"in", "out"},
                                                       .port_types = {take(value), give(value)},
                                                       .principal = 0,
                                                       .par_ports = 0b1100}}};
static_assert(check_signature(far_par).says("net: the par group of agent kind 'far' names a port past its 2 ports."));

// ── The proof structures of multiplicative linear logic ─────────────────
//
//   identity        |- A par A~             provable      admit
//   axiom tensor    |- A tensor A~          not provable  a cycle
//   par and tensor  |- A~ par B~, A tensor B  provable    admit
//   two tensors     |- A~ tensor B~, A tensor B  not provable, a cycle
//   two pars        |- A~ par B~, A par B   not provable  not connected
//   nested pars     |- (A~ par B~) par C~, (A tensor B) tensor C  provable
//
// An axiom is a wire.  A conclusion is a boundary agent.

inline constexpr TypeId type_a{0};
inline constexpr TypeId type_b{1};
inline constexpr TypeId type_c{2};
inline constexpr TypeId type_ab{3};
inline constexpr TypeId type_abc{4};

inline constexpr KindId par_kind{0};
inline constexpr KindId tensor_kind{1};
inline constexpr KindId take_ab_kind{2};
inline constexpr KindId give_ab_kind{3};
inline constexpr KindId par_of_axiom_kind{4};
inline constexpr KindId tensor_of_axiom_kind{5};
inline constexpr KindId outer_par_kind{6};
inline constexpr KindId outer_tensor_kind{7};
inline constexpr KindId take_abc_kind{8};
inline constexpr KindId give_abc_kind{9};
inline constexpr KindId tensor_of_duals_kind{10};
inline constexpr KindId par_of_atoms_kind{11};

inline constexpr NetSignature<5, 12> proofs{
    .type_names = {"A", "B", "C", "AB", "ABC"},
    .kinds = {
        {.name = "par",
         .port_names = {"left", "right", "out"},
         .port_types = {take(type_a), take(type_b), give(type_ab)},
         .principal = 2,
         .par_ports = 0b011},
        {.name = "tensor",
         .port_names = {"left", "right", "out"},
         .port_types = {give(type_a), give(type_b), take(type_ab)},
         .principal = 2},
        {.name = "conclusion", .port_names = {"in"}, .port_types = {take(type_ab)}, .is_boundary = true},
        {.name = "conclusion", .port_names = {"in"}, .port_types = {give(type_ab)}, .is_boundary = true},
        {.name = "par",
         .port_names = {"left", "right", "out"},
         .port_types = {take(type_a), give(type_a), give(type_ab)},
         .principal = 2,
         .par_ports = 0b011},
        {.name = "tensor",
         .port_names = {"left", "right", "out"},
         .port_types = {take(type_a), give(type_a), give(type_ab)},
         .principal = 2},
        {.name = "outer par",
         .port_names = {"ab", "c", "out"},
         .port_types = {take(type_ab), take(type_c), give(type_abc)},
         .principal = 2,
         .par_ports = 0b011},
        {.name = "outer tensor",
         .port_names = {"ab", "c", "out"},
         .port_types = {give(type_ab), give(type_c), take(type_abc)},
         .principal = 2},
        {.name = "conclusion", .port_names = {"in"}, .port_types = {take(type_abc)}, .is_boundary = true},
        {.name = "conclusion", .port_names = {"in"}, .port_types = {give(type_abc)}, .is_boundary = true},
        {.name = "tensor",
         .port_names = {"left", "right", "out"},
         .port_types = {take(type_a), take(type_b), give(type_ab)},
         .principal = 2},
        {.name = "par",
         .port_names = {"left", "right", "out"},
         .port_types = {give(type_a), give(type_b), take(type_ab)},
         .principal = 2,
         .par_ports = 0b011},
    }};

inline constexpr Net<5, 12, 2, 2> identity{.signature = proofs,
                                           .agents = {par_of_axiom_kind, take_ab_kind},
                                           .wires = {join_ports(0, 0, 0, 1), join_ports(0, 2, 1, 0)}};
static_assert(check_proof_net(identity).is_admitted());

inline constexpr Net<5, 12, 2, 2> axiom_tensor{.signature = proofs,
                                               .agents = {tensor_of_axiom_kind, take_ab_kind},
                                               .wires = {join_ports(0, 0, 0, 1), join_ports(0, 2, 1, 0)}};
static_assert(check_wiring(axiom_tensor).is_admitted());
static_assert(check_proof_net(axiom_tensor)
                  .says("net: a cycle closes at port 'right' of agent 'tensor' (node 0).  "
                        "Each switching of the net keeps this cycle, so the net is no proof "
                        "net."));

inline constexpr Net<5, 12, 4, 4> par_and_tensor{
    .signature = proofs,
    .agents = {par_kind, tensor_kind, give_ab_kind, take_ab_kind},
    .wires = {join_ports(0, 0, 1, 0), join_ports(0, 1, 1, 1), join_ports(0, 2, 3, 0), join_ports(1, 2, 2, 0)}};
static_assert(check_proof_net(par_and_tensor).is_admitted());

inline constexpr Net<5, 12, 4, 4> two_tensors{
    .signature = proofs,
    .agents = {tensor_of_duals_kind, tensor_kind, take_ab_kind, give_ab_kind},
    .wires = {join_ports(0, 0, 1, 0), join_ports(0, 1, 1, 1), join_ports(0, 2, 2, 0), join_ports(1, 2, 3, 0)}};
static_assert(check_wiring(two_tensors).is_admitted());
static_assert(check_proof_net(two_tensors)
                  .says("net: a cycle closes at port 'right' of agent 'tensor' (node 1).  Each switching of the net "
                        "keeps this cycle, so the net is no proof net."));

inline constexpr Net<5, 12, 4, 4> two_pars{
    .signature = proofs,
    .agents = {par_kind, par_of_atoms_kind, take_ab_kind, give_ab_kind},
    .wires = {join_ports(0, 0, 1, 0), join_ports(0, 1, 1, 1), join_ports(0, 2, 2, 0), join_ports(1, 2, 3, 0)}};
static_assert(check_wiring(two_pars).is_admitted());
static_assert(check_proof_net(two_pars).says(
    "net: agent 'par' (node 0) is a par whose ports never meet.  A switching of it leaves the net "
    "in two parts."));

// A par and a tensor with each pair of ports joined: the ports of the par
// reach the par again through the tensor.
inline constexpr Net<5, 12, 2, 3> closed_par{
    .signature = proofs,
    .agents = {par_kind, tensor_kind},
    .wires = {join_ports(0, 0, 1, 0), join_ports(0, 1, 1, 1), join_ports(0, 2, 1, 2)}};
static_assert(check_proof_net(closed_par)
                  .says("net: agent 'par' (node 0) is a par whose ports reach the agent again.  A switching of it "
                        "closes a cycle."));

// The inner par contracts before the outer one.
inline constexpr Net<5, 12, 6, 7> nested_pars{
    .signature = proofs,
    .agents = {par_kind, outer_par_kind, tensor_kind, outer_tensor_kind, take_abc_kind, give_abc_kind},
    .wires = {join_ports(0, 0, 2, 0), join_ports(0, 1, 2, 1), join_ports(0, 2, 1, 0), join_ports(2, 2, 3, 0),
              join_ports(1, 1, 3, 1), join_ports(1, 2, 4, 0), join_ports(3, 2, 5, 0)}};
static_assert(check_proof_net(nested_pars).is_admitted());

// ── The background pipeline ─────────────────────────────────────────────
//
// starter -> drain -> detect -> build -> publish -> sink, one wire for each
// channel.  Then the loop that the code also has: the Vigil pushes into the
// trace ring that drain reads, and Vigil::flush() waits on the count that
// publish advances.

inline constexpr TypeId start_type{0};
inline constexpr TypeId trace_type{1};
inline constexpr TypeId work_type{2};
inline constexpr TypeId graph_type{3};
inline constexpr TypeId done_type{4};
inline constexpr TypeId ring_type{5};
inline constexpr TypeId processed_type{6};

inline constexpr NetSignature<7, 9> pipeline_kinds{
    .type_names = {"Start", "TraceBatch", "BuildWork", "GraphPublish", "Done", "RingEntry", "Processed"},
    .kinds = {
        {.name = "starter", .port_names = {"out"}, .port_types = {give(start_type)}},
        {.name = "drain", .port_names = {"in", "out"}, .port_types = {take(start_type), give(trace_type)}},
        {.name = "detect", .port_names = {"in", "out"}, .port_types = {take(trace_type), give(work_type)}},
        {.name = "build", .port_names = {"in", "out"}, .port_types = {take(work_type), give(graph_type)}},
        {.name = "publish", .port_names = {"in", "out"}, .port_types = {take(graph_type), give(done_type)}},
        {.name = "sink", .port_names = {"in"}, .port_types = {take(done_type)}},
        {.name = "drain",
         .port_names = {"in", "out", "ring"},
         .port_types = {take(start_type), give(trace_type), take(ring_type)}},
        {.name = "publish",
         .port_names = {"in", "out", "processed"},
         .port_types = {take(graph_type), give(done_type), give(processed_type)}},
        {.name = "vigil", .port_names = {"ring", "processed"}, .port_types = {give(ring_type), take(processed_type)}},
    }};

inline constexpr Net<7, 9, 6, 5> pipeline_as_built{
    .signature = pipeline_kinds,
    .agents = {KindId{0}, KindId{1}, KindId{2}, KindId{3}, KindId{4}, KindId{5}},
    .wires = {join_ports(0, 0, 1, 0), join_ports(1, 1, 2, 0), join_ports(2, 1, 3, 0), join_ports(3, 1, 4, 0),
              join_ports(4, 1, 5, 0)}};
static_assert(check_forest(pipeline_as_built).is_admitted());
static_assert(check_proof_net(pipeline_as_built).is_admitted());
static_assert(check_vicious(pipeline_as_built).is_admitted());
static_assert(check_dataflow(pipeline_as_built).is_admitted());

inline constexpr Net<7, 9, 7, 7> pipeline_with_foreground{
    .signature = pipeline_kinds,
    .agents = {KindId{0}, KindId{6}, KindId{2}, KindId{3}, KindId{7}, KindId{5}, KindId{8}},
    .wires = {join_ports(0, 0, 1, 0), join_ports(1, 1, 2, 0), join_ports(2, 1, 3, 0), join_ports(3, 1, 4, 0),
              join_ports(4, 1, 5, 0), join_ports(6, 0, 1, 2), join_ports(4, 2, 6, 1)}};
static_assert(check_wiring(pipeline_with_foreground).is_admitted());
static_assert(check_forest(pipeline_with_foreground)
                  .says("net: wire 6 from port 'processed' of agent 'publish' (node 4) to port 'processed' of agent "
                        "'vigil' (node 6) closes a cycle.  The agents and the wires do not form a forest."));
static_assert(!check_proof_net(pipeline_with_foreground).is_admitted());

// A wire between two types that are not duals.
inline constexpr Net<7, 9, 6, 5> pipeline_crossed{
    .signature = pipeline_kinds,
    .agents = {KindId{0}, KindId{1}, KindId{3}, KindId{2}, KindId{4}, KindId{5}},
    .wires = {join_ports(0, 0, 1, 0), join_ports(1, 1, 2, 0), join_ports(2, 1, 3, 0), join_ports(3, 1, 4, 0),
              join_ports(4, 1, 5, 0)}};
static_assert(check_wiring(pipeline_crossed)
                  .says("net: wire 1 joins port 'out' of agent 'drain' (node 1) of type TraceBatch! with port 'in' of "
                        "agent 'build' (node 2) of type BuildWork?, which is not its dual."));

// A port with two wires.
inline constexpr Net<7, 9, 6, 5> pipeline_doubled{
    .signature = pipeline_kinds,
    .agents = {KindId{0}, KindId{1}, KindId{2}, KindId{3}, KindId{4}, KindId{5}},
    .wires = {join_ports(0, 0, 1, 0), join_ports(1, 1, 2, 0), join_ports(2, 1, 3, 0), join_ports(3, 1, 4, 0),
              join_ports(3, 1, 5, 0)}};
static_assert(check_wiring(pipeline_doubled)
                  .says("net: port 'out' of agent 'build' (node 3) has a second wire, wire 4.  Each port has exactly "
                        "one wire."));

// A wire to a node past the agents of the net.  A check copies a net of at
// most 16 agents into arrays of 16 agents, so node 9 is in the arrays of the
// copy and not in the net.  Node 40 is also past the arrays.
inline constexpr Net<7, 9, 6, 5> pipeline_past_its_agents{
    .signature = pipeline_kinds,
    .agents = {KindId{0}, KindId{1}, KindId{2}, KindId{3}, KindId{4}, KindId{5}},
    .wires = {join_ports(0, 0, 1, 0), join_ports(1, 1, 2, 0), join_ports(2, 1, 3, 0), join_ports(3, 1, 4, 0),
              join_ports(4, 1, 9, 0)}};
static_assert(check_wiring(pipeline_past_its_agents).says("net: wire 4 names node 9, and the net has 6 agents."));
static_assert(check_dataflow(pipeline_past_its_agents).says("net: wire 4 names node 9, and the net has 6 agents."));

inline constexpr Net<7, 9, 6, 5> pipeline_past_the_arrays{
    .signature = pipeline_kinds,
    .agents = {KindId{0}, KindId{1}, KindId{2}, KindId{3}, KindId{4}, KindId{5}},
    .wires = {join_ports(0, 0, 1, 0), join_ports(1, 1, 2, 0), join_ports(2, 1, 3, 0), join_ports(3, 1, 4, 0),
              join_ports(4, 1, 40, 0)}};
static_assert(check_wiring(pipeline_past_the_arrays).says("net: wire 4 names node 40, and the net has 6 agents."));

// An agent of a kind past the signature, and a wire to a port past the ports
// of its agent.
inline constexpr Net<7, 9, 6, 5> pipeline_of_unknown_kind{
    .signature = pipeline_kinds,
    .agents = {KindId{0}, KindId{12}, KindId{2}, KindId{3}, KindId{4}, KindId{5}},
    .wires = {join_ports(0, 0, 1, 0), join_ports(1, 1, 2, 0), join_ports(2, 1, 3, 0), join_ports(3, 1, 4, 0),
              join_ports(4, 1, 5, 0)}};
static_assert(
    check_wiring(pipeline_of_unknown_kind).says("net: node 1 has the kind 12, and the signature has 9 kinds."));

inline constexpr Net<7, 9, 6, 5> pipeline_past_its_ports{
    .signature = pipeline_kinds,
    .agents = {KindId{0}, KindId{1}, KindId{2}, KindId{3}, KindId{4}, KindId{5}},
    .wires = {join_ports(0, 0, 1, 4), join_ports(1, 1, 2, 0), join_ports(2, 1, 3, 0), join_ports(3, 1, 4, 0),
              join_ports(4, 1, 5, 0)}};
static_assert(check_wiring(pipeline_past_its_ports)
                  .says("net: wire 0 names port 4 of agent 'drain' (node 1), which has 2 ports."));

// ── The fork tree ───────────────────────────────────────────────────────
//
// run_on_four_threads of test/fixy/test_core_atomic.cpp: Whole splits into
// four children, four bodies, a join and a combine.  The fork is a par, and
// the join is a tensor.  Then the same fork with the shared capture &body.

inline constexpr TypeId whole_type{0};
inline constexpr TypeId kids_type{1};
inline constexpr TypeId first_type{2};
inline constexpr TypeId second_type{3};
inline constexpr TypeId third_type{4};
inline constexpr TypeId fourth_type{5};
inline constexpr TypeId closure_type{6};

inline constexpr NetSignature<7, 17> fork_kinds{
    .type_names = {"Whole", "Kids", "First", "Second", "Third", "Fourth", "Closure"},
    .kinds = {
        {.name = "root", .port_names = {"whole"}, .port_types = {give(whole_type)}},
        {.name = "split", .port_names = {"whole", "kids"}, .port_types = {take(whole_type), give(kids_type)}},
        {.name = "fork",
         .port_names = {"kids", "first", "second", "third", "fourth"},
         .port_types = {take(kids_type), give(first_type), give(second_type), give(third_type), give(fourth_type)},
         .principal = 0,
         .par_ports = 0b11110},
        {.name = "body_first", .port_names = {"in", "out"}, .port_types = {take(first_type), give(first_type)}},
        {.name = "body_second", .port_names = {"in", "out"}, .port_types = {take(second_type), give(second_type)}},
        {.name = "body_third", .port_names = {"in", "out"}, .port_types = {take(third_type), give(third_type)}},
        {.name = "body_fourth", .port_names = {"in", "out"}, .port_types = {take(fourth_type), give(fourth_type)}},
        {.name = "join",
         .port_names = {"first", "second", "third", "fourth", "kids"},
         .port_types = {take(first_type), take(second_type), take(third_type), take(fourth_type), give(kids_type)},
         .principal = 4},
        {.name = "combine", .port_names = {"kids", "whole"}, .port_types = {take(kids_type), give(whole_type)}},
        {.name = "drop", .port_names = {"whole"}, .port_types = {take(whole_type)}},
        {.name = "body_first",
         .port_names = {"in", "out", "body"},
         .port_types = {take(first_type), give(first_type), take(closure_type)}},
        {.name = "body_second",
         .port_names = {"in", "out", "body"},
         .port_types = {take(second_type), give(second_type), take(closure_type)}},
        {.name = "body_third",
         .port_names = {"in", "out", "body"},
         .port_types = {take(third_type), give(third_type), take(closure_type)}},
        {.name = "body_fourth",
         .port_names = {"in", "out", "body"},
         .port_types = {take(fourth_type), give(fourth_type), take(closure_type)}},
        {.name = "closure",
         .port_names = {"use1", "use2", "use3", "use4"},
         .port_types = {give(closure_type), give(closure_type), give(closure_type), give(closure_type)}},
        {.name = "closure", .port_names = {"use"}, .port_types = {give(closure_type)}},
        {.name = "contract",
         .port_names = {"in", "left", "right"},
         .port_types = {take(closure_type), give(closure_type), give(closure_type)},
         .principal = 0,
         .par_ports = 0b110},
    }};

// Agents: 0 root, 1 split, 2 fork, 3 to 6 bodies, 7 join, 8 combine, 9 drop.
inline constexpr Net<7, 17, 10, 10> fork_tree_open{
    .signature = fork_kinds,
    .agents = {KindId{0}, KindId{1}, KindId{2}, KindId{3}, KindId{4}, KindId{5}, KindId{6}, KindId{7}, KindId{8},
               KindId{9}},
    .wires = {join_ports(0, 0, 1, 0), join_ports(1, 1, 2, 0), join_ports(2, 1, 3, 0), join_ports(2, 2, 4, 0),
              join_ports(2, 3, 5, 0), join_ports(2, 4, 6, 0), join_ports(3, 1, 7, 0), join_ports(4, 1, 7, 1),
              join_ports(5, 1, 7, 2), join_ports(6, 1, 7, 3)}};
static_assert(check_wiring(fork_tree_open)
                  .says("net: port 'kids' of agent 'join' (node 7) has no wire.  Each port has exactly one wire."));

inline constexpr Net<7, 17, 10, 12> fork_tree{
    .signature = fork_kinds,
    .agents = {KindId{0}, KindId{1}, KindId{2}, KindId{3}, KindId{4}, KindId{5}, KindId{6}, KindId{7}, KindId{8},
               KindId{9}},
    .wires = {join_ports(0, 0, 1, 0), join_ports(1, 1, 2, 0), join_ports(2, 1, 3, 0), join_ports(2, 2, 4, 0),
              join_ports(2, 3, 5, 0), join_ports(2, 4, 6, 0), join_ports(3, 1, 7, 0), join_ports(4, 1, 7, 1),
              join_ports(5, 1, 7, 2), join_ports(6, 1, 7, 3), join_ports(7, 4, 8, 0), join_ports(8, 1, 9, 0)}};
static_assert(check_proof_net(fork_tree).is_admitted());
static_assert(check_forest(fork_tree).says("net: agent 'fork' (node 2) has a par group, and check_forest reads no "
                                           "par group.  Use check_proof_net."));
static_assert(check_dataflow(fork_tree).is_admitted());

// &body as four plain wires from one closure: a cycle in each switching.
inline constexpr Net<7, 17, 11, 16> fork_shared_plain{
    .signature = fork_kinds,
    .agents = {KindId{0}, KindId{1}, KindId{2}, KindId{10}, KindId{11}, KindId{12}, KindId{13}, KindId{7}, KindId{8},
               KindId{9}, KindId{14}},
    .wires = {join_ports(0, 0, 1, 0), join_ports(1, 1, 2, 0), join_ports(2, 1, 3, 0), join_ports(2, 2, 4, 0),
              join_ports(2, 3, 5, 0), join_ports(2, 4, 6, 0), join_ports(3, 1, 7, 0), join_ports(4, 1, 7, 1),
              join_ports(5, 1, 7, 2), join_ports(6, 1, 7, 3), join_ports(7, 4, 8, 0), join_ports(8, 1, 9, 0),
              join_ports(10, 0, 3, 2), join_ports(10, 1, 4, 2), join_ports(10, 2, 5, 2), join_ports(10, 3, 6, 2)}};
static_assert(check_wiring(fork_shared_plain).is_admitted() && !check_proof_net(fork_shared_plain).is_admitted());

// &body through a tree of contractions, which a switching reads as pars.
inline constexpr Net<7, 17, 14, 19> fork_shared_contracted{
    .signature = fork_kinds,
    .agents = {KindId{0}, KindId{1}, KindId{2}, KindId{10}, KindId{11}, KindId{12}, KindId{13}, KindId{7}, KindId{8},
               KindId{9}, KindId{15}, KindId{16}, KindId{16}, KindId{16}},
    .wires = {join_ports(0, 0, 1, 0), join_ports(1, 1, 2, 0), join_ports(2, 1, 3, 0), join_ports(2, 2, 4, 0),
              join_ports(2, 3, 5, 0), join_ports(2, 4, 6, 0), join_ports(3, 1, 7, 0), join_ports(4, 1, 7, 1),
              join_ports(5, 1, 7, 2), join_ports(6, 1, 7, 3), join_ports(7, 4, 8, 0), join_ports(8, 1, 9, 0),
              join_ports(10, 0, 11, 0), join_ports(11, 1, 12, 0), join_ports(11, 2, 13, 0), join_ports(12, 1, 3, 2),
              join_ports(12, 2, 4, 2), join_ports(13, 1, 5, 2), join_ports(13, 2, 6, 2)}};
static_assert(check_proof_net(fork_shared_contracted).is_admitted());
static_assert(check_dataflow(fork_shared_contracted).is_admitted());

// ── Sessions ────────────────────────────────────────────────────────────
//
// A process is an agent with one port for each endpoint that it holds, and
// its principal port is the endpoint of its first action.

inline constexpr TypeId s1_type{0};
inline constexpr TypeId s2_type{1};
inline constexpr TypeId pq_type{2};
inline constexpr TypeId qr_type{3};
inline constexpr TypeId rp_type{4};
inline constexpr TypeId role_p_type{5};
inline constexpr TypeId role_q_type{6};
inline constexpr TypeId role_r_type{7};

inline constexpr NetSignature<8, 16> session_kinds{
    .type_names = {"S1", "S2", "PQ", "QR", "RP", "RoleP", "RoleQ", "RoleR"},
    .kinds = {
        // 0: P of CrossedSessions waits on s2 first.  1: Q waits on s1 first.
        {.name = "P", .port_names = {"s1", "s2"}, .port_types = {give(s1_type), take(s2_type)}, .principal = 1},
        {.name = "Q", .port_names = {"s1", "s2"}, .port_types = {take(s1_type), give(s2_type)}},
        // 2: P of OrderedSessions sends on s1 first.
        {.name = "P", .port_names = {"s1", "s2"}, .port_types = {give(s1_type), take(s2_type)}},
        // 3 to 5: CrossedRing, each process waits on its left neighbour first.
        {.name = "P", .port_names = {"pq", "rp"}, .port_types = {give(pq_type), take(rp_type)}, .principal = 1},
        {.name = "Q", .port_names = {"pq", "qr"}, .port_types = {take(pq_type), give(qr_type)}},
        {.name = "R", .port_names = {"qr", "rp"}, .port_types = {take(qr_type), give(rp_type)}},
        // 6 to 9: the roles of the Ring global type, and the session as one
        // coherence node.
        {.name = "P", .port_names = {"ring"}, .port_types = {give(role_p_type)}},
        {.name = "Q", .port_names = {"ring"}, .port_types = {give(role_q_type)}},
        {.name = "R", .port_names = {"ring"}, .port_types = {give(role_r_type)}},
        {.name = "ring_session",
         .port_names = {"p", "q", "r"},
         .port_types = {take(role_p_type), take(role_q_type), take(role_r_type)}},
        // 10 to 13: two_forked_sessions_in_a_cycle as the mints declare it.
        {.name = "left body of A", .port_names = {"a"}, .port_types = {take(s1_type)}},
        {.name = "right body of A", .port_names = {"a"}, .port_types = {give(s1_type)}},
        {.name = "left body of B", .port_names = {"b"}, .port_types = {take(s2_type)}},
        {.name = "right body of B", .port_names = {"b"}, .port_types = {give(s2_type)}},
        // 14 and 15: the same after the Slot moves the endpoints.
        {.name = "left body of A", .port_names = {"a", "b of B"}, .port_types = {take(s1_type), give(s2_type)}},
        {.name = "left body of B", .port_names = {"b", "a of A"}, .port_types = {take(s2_type), give(s1_type)}},
    }};

inline constexpr Net<8, 16, 2, 2> crossed_sessions{.signature = session_kinds,
                                                   .agents = {KindId{0}, KindId{1}},
                                                   .wires = {join_ports(0, 0, 1, 0), join_ports(0, 1, 1, 1)}};
inline constexpr Net<8, 16, 2, 2> ordered_sessions{.signature = session_kinds,
                                                   .agents = {KindId{2}, KindId{1}},
                                                   .wires = {join_ports(0, 0, 1, 0), join_ports(0, 1, 1, 1)}};
inline constexpr Net<8, 16, 3, 3> crossed_ring{
    .signature = session_kinds,
    .agents = {KindId{3}, KindId{4}, KindId{5}},
    .wires = {join_ports(0, 0, 1, 0), join_ports(1, 1, 2, 0), join_ports(2, 1, 0, 1)}};
inline constexpr Net<8, 16, 4, 3> ring_global{
    .signature = session_kinds,
    .agents = {KindId{6}, KindId{7}, KindId{8}, KindId{9}},
    .wires = {join_ports(0, 0, 3, 0), join_ports(1, 0, 3, 1), join_ports(2, 0, 3, 2)}};

// The tree checks refuse each cycle of sessions, also the safe order, and
// the priority check of Priority.h decides them.  The Ring global type as
// one coherence node is a tree.
static_assert(!check_forest(crossed_sessions).is_admitted() && !check_proof_net(crossed_sessions).is_admitted());
static_assert(!check_forest(ordered_sessions).is_admitted() && !check_proof_net(ordered_sessions).is_admitted());
static_assert(!check_forest(crossed_ring).is_admitted());
static_assert(check_proof_net(ring_global).is_admitted() && check_forest(ring_global).is_admitted());

// The vicious circle check sees the first wait only.
static_assert(check_vicious(crossed_sessions)
                  .says("net: vicious circle: agent 'P' (node 0) -> agent 'Q' (node 1).  Each principal port meets an "
                        "auxiliary port, so no rule can fire."));
static_assert(check_vicious(ordered_sessions).is_admitted());
static_assert(!check_vicious(crossed_ring).is_admitted());

// The cycle of two_forked_sessions_in_a_cycle: two trees as the mints declare
// them, and a cycle after the Slot moves the endpoints.  The proof net check
// refuses the two trees only because they are not connected.
inline constexpr Net<8, 16, 4, 2> ledger_declared{.signature = session_kinds,
                                                  .agents = {KindId{10}, KindId{11}, KindId{12}, KindId{13}},
                                                  .wires = {join_ports(0, 0, 1, 0), join_ports(2, 0, 3, 0)}};
inline constexpr Net<8, 16, 2, 2> ledger_escaped{.signature = session_kinds,
                                                 .agents = {KindId{14}, KindId{15}},
                                                 .wires = {join_ports(0, 0, 1, 1), join_ports(1, 0, 0, 1)}};
static_assert(check_forest(ledger_declared).is_admitted());
static_assert(check_proof_net(ledger_declared)
                  .says("net: the net is not connected.  agent 'left body of B' (node 2) has no path to agent 'left "
                        "body of A' (node 0)."));
static_assert(!check_forest(ledger_escaped).is_admitted());

// ── The shapes of the reductions library ────────────────────────────────
//
// A value net: a num agent gives a leaf, an add takes its left operand on
// its principal port, a dup gives one value two times, a cmp is a
// compare-exchange, and a boundary result takes each output.

inline constexpr KindId num_kind{0};
inline constexpr KindId add_kind{1};
inline constexpr KindId result_kind{2};
inline constexpr KindId dup_kind{3};
inline constexpr KindId cmp_kind{4};
inline constexpr KindId spin_kind{5};

inline constexpr NetSignature<1, 6> value_kinds{
    .type_names = {"Value"},
    .kinds = {
        {.name = "num", .port_names = {"value"}, .port_types = {give(value)}},
        {.name = "add", .port_names = {"left", "right", "sum"}, .port_types = {take(value), take(value), give(value)}},
        {.name = "result", .port_names = {"in"}, .port_types = {take(value)}, .is_boundary = true},
        {.name = "dup", .port_names = {"in", "first", "second"}, .port_types = {take(value), give(value), give(value)}},
        {.name = "cmp",
         .port_names = {"low", "high", "min", "max"},
         .port_types = {take(value), take(value), give(value), give(value)}},
        {.name = "spin", .port_names = {"in", "out"}, .port_types = {meet(value), meet(value)}},
    }};

// plan_tree<3>: (x0, x1), then (that, x2).  Agents: 0 to 2 num, 3 and 4
// add, 5 result.  The same plan with the operands of the first step swapped
// passes each check too: no structural check sees the order of operands.
inline constexpr Net<1, 6, 6, 5> tree_of_three{
    .signature = value_kinds,
    .agents = {num_kind, num_kind, num_kind, add_kind, add_kind, result_kind},
    .wires = {join_ports(0, 0, 3, 0), join_ports(1, 0, 3, 1), join_ports(3, 2, 4, 0), join_ports(2, 0, 4, 1),
              join_ports(4, 2, 5, 0)}};
inline constexpr Net<1, 6, 6, 5> tree_of_three_swapped{
    .signature = value_kinds,
    .agents = {num_kind, num_kind, num_kind, add_kind, add_kind, result_kind},
    .wires = {join_ports(1, 0, 3, 0), join_ports(0, 0, 3, 1), join_ports(3, 2, 4, 0), join_ports(2, 0, 4, 1),
              join_ports(4, 2, 5, 0)}};
static_assert(check_forest(tree_of_three).is_admitted() && check_proof_net(tree_of_three).is_admitted());
static_assert(check_vicious(tree_of_three).is_admitted() && check_dataflow(tree_of_three).is_admitted());
static_assert(check_forest(tree_of_three_swapped).is_admitted() && check_dataflow(tree_of_three_swapped).is_admitted());

// The part [0, 4) of the inclusive scan of 8: x2 and x3 each feed the scan
// and the tree sum of [2, 4), so the copies meet again.  Agents: 0 to 3 num,
// 4 to 8 add, 9 to 14 dup, 15 to 19 result.
inline constexpr Net<1, 6, 20, 21> scan_part{
    .signature = value_kinds,
    .agents = {num_kind, num_kind,    num_kind,    num_kind,    add_kind,    add_kind,   add_kind,
               add_kind, add_kind,    dup_kind,    dup_kind,    dup_kind,    dup_kind,   dup_kind,
               dup_kind, result_kind, result_kind, result_kind, result_kind, result_kind},
    .wires = {join_ports(0, 0, 9, 0),  join_ports(9, 1, 15, 0),  join_ports(9, 2, 4, 0),   join_ports(1, 0, 4, 1),
              join_ports(4, 2, 10, 0), join_ports(10, 1, 16, 0), join_ports(10, 2, 11, 0), join_ports(11, 1, 5, 0),
              join_ports(11, 2, 8, 0), join_ports(2, 0, 12, 0),  join_ports(12, 1, 5, 1),  join_ports(12, 2, 7, 0),
              join_ports(5, 2, 14, 0), join_ports(14, 1, 17, 0), join_ports(14, 2, 6, 0),  join_ports(3, 0, 13, 0),
              join_ports(13, 1, 6, 1), join_ports(13, 2, 7, 1),  join_ports(6, 2, 18, 0),  join_ports(7, 2, 8, 1),
              join_ports(8, 2, 19, 0)}};
static_assert(check_dataflow(scan_part).is_admitted() && check_vicious(scan_part).is_admitted());
static_assert(!check_forest(scan_part).is_admitted());

// plan_sort_network<4>: (0, 1), (2, 3), (0, 3), (1, 2), (0, 1), (2, 3).
// Agents: 0 to 3 num, 4 to 9 cmp, 10 to 13 result.
inline constexpr Net<1, 6, 14, 16> sort_of_four{
    .signature = value_kinds,
    .agents = {num_kind, num_kind, num_kind, num_kind, cmp_kind, cmp_kind, cmp_kind, cmp_kind, cmp_kind, cmp_kind,
               result_kind, result_kind, result_kind, result_kind},
    .wires = {join_ports(0, 0, 4, 0), join_ports(1, 0, 4, 1), join_ports(2, 0, 5, 0), join_ports(3, 0, 5, 1),
              join_ports(4, 2, 6, 0), join_ports(5, 3, 6, 1), join_ports(4, 3, 7, 0), join_ports(5, 2, 7, 1),
              join_ports(6, 2, 8, 0), join_ports(7, 2, 8, 1), join_ports(7, 3, 9, 0), join_ports(6, 3, 9, 1),
              join_ports(8, 2, 10, 0), join_ports(8, 3, 11, 0), join_ports(9, 2, 12, 0), join_ports(9, 3, 13, 0)}};
static_assert(check_dataflow(sort_of_four).is_admitted() && check_vicious(sort_of_four).is_admitted());
static_assert(check_forest(sort_of_four)
                  .says("net: wire 7 from port 'min' of agent 'cmp' (node 5) to port 'high' of agent 'cmp' (node 7) "
                        "closes a cycle.  The agents and the wires do not form a forest."));

// Two adds that take each other's sum: no vicious circle at the start, and a
// cycle of waits that the dataflow check names.
inline constexpr Net<1, 6, 4, 4> crossed_adds{
    .signature = value_kinds,
    .agents = {num_kind, num_kind, add_kind, add_kind},
    .wires = {join_ports(0, 0, 2, 0), join_ports(1, 0, 3, 0), join_ports(2, 2, 3, 1), join_ports(3, 2, 2, 1)}};
static_assert(check_vicious(crossed_adds).is_admitted());
static_assert(check_dataflow(crossed_adds)
                  .says("net: a cycle of waits closes at port 'right' of agent 'add' (node 2): agent 'add' (node 2) "
                        "<- agent 'add' (node 3).  Each agent waits on its inputs, so no agent of the cycle starts."));

// A meet port has no direction.
inline constexpr Net<1, 6, 2, 2> spins{.signature = value_kinds,
                                       .agents = {spin_kind, spin_kind},
                                       .wires = {join_ports(0, 0, 1, 0), join_ports(0, 1, 1, 1)}};
static_assert(check_wiring(spins).is_admitted());
static_assert(check_dataflow(spins).says("net: wire 0 joins port 'in' of agent 'spin' (node 0) of the type Value, "
                                         "which has no direction.  A dataflow takes give and take ports."));

// ── Long cycles ─────────────────────────────────────────────────────────
//
// A refusal names the first agents of a long cycle and then "...": six
// agents of a vicious circle, five agents of a cycle of waits.

inline constexpr NetSignature<1, 2> ring_kinds{
    .type_names = {"Value"},
    .kinds = {
        {.name = "relay", .port_names = {"in", "out"}, .port_types = {take(value), give(value)}},
        {.name = "link", .port_names = {"principal", "aux"}, .port_types = {meet(value), meet(value)}},
    }};

// Seven relays, each output into the next input.
inline constexpr Net<1, 2, 7, 7> relay_ring{
    .signature = ring_kinds,
    .agents = {KindId{0}, KindId{0}, KindId{0}, KindId{0}, KindId{0}, KindId{0}, KindId{0}},
    .wires = {join_ports(0, 1, 1, 0), join_ports(1, 1, 2, 0), join_ports(2, 1, 3, 0), join_ports(3, 1, 4, 0),
              join_ports(4, 1, 5, 0), join_ports(5, 1, 6, 0), join_ports(6, 1, 0, 0)}};
static_assert(check_dataflow(relay_ring)
                  .says("net: a cycle of waits closes at port 'in' of agent 'relay' (node 0): agent 'relay' (node 0) "
                        "<- agent 'relay' (node 6) <- agent 'relay' (node 5) <- agent 'relay' (node 4) <- agent "
                        "'relay' (node 3) <- ....  Each agent waits on its inputs, so no agent of the cycle starts."));

// Seven links, each principal port into the auxiliary port of the next.
inline constexpr Net<1, 2, 7, 7> link_ring{
    .signature = ring_kinds,
    .agents = {KindId{1}, KindId{1}, KindId{1}, KindId{1}, KindId{1}, KindId{1}, KindId{1}},
    .wires = {join_ports(0, 0, 1, 1), join_ports(1, 0, 2, 1), join_ports(2, 0, 3, 1), join_ports(3, 0, 4, 1),
              join_ports(4, 0, 5, 1), join_ports(5, 0, 6, 1), join_ports(6, 0, 0, 1)}};
static_assert(check_vicious(link_ring).says(
    "net: vicious circle: agent 'link' (node 0) -> agent 'link' (node 1) -> agent 'link' (node 2) "
    "-> agent 'link' (node 3) -> agent 'link' (node 4) -> agent 'link' (node 5) -> ....  Each "
    "principal port meets an auxiliary port, so no rule can fire."));

}  // namespace detail::net_self_test

}  // namespace foundation::algebra::net
