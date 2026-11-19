#pragma once

// The interaction nets of the base: a plain constexpr graph of agents, ports
// and wires, and the checks over it.  Each check is one constexpr body, which
// a consteval door runs at compile time and a program runs at run time.
//
//   NetName                 the name of a type, an agent kind or a port: a
//                           string literal, or a static string of reflection
//   PortType                a type and its polarity: give(t), take(t), meet(t)
//   MergeDoor               the merge door of a kind: closed, or open through
//                           allow_merge(reason) (THE DETERMINISM)
//   AgentKind               the ports of an agent: a name and a type for each
//                           port, the principal port, a par group, the
//                           boundary flag, and the merge door
//   NetSignature<T, K>      the T type names and the K agent kinds
//   Net<T, K, A, W>         a signature, the kind of each of A agents, and W
//                           wires.  Each wire joins two ports
//   NetVerdict              the answer of a check: admitted, or a refusal with
//                           one line of text
//   NetFault                the fault that a check finds: a code, and the
//                           type, the kind, the agent, the port, the wire or
//                           the cycle that the code names
//   SealedNet<T, K, A, W>   a copy of a net that no caller can change, and
//                           the fault of its wiring (THE SEALED NET)
//
//   check_signature(signature)   the kinds are well formed
//   check_wiring(net)            each port has exactly one wire, and each wire
//                                joins a type with its dual
//   check_forest(net)            the agents and the wires form a forest
//   check_proof_net(net)         the criterion of Danos and Regnier, with par
//                                groups: each switching is a tree
//   check_vicious(net)           no vicious circle of principal ports
//   check_dataflow(net)          each agent can take its inputs before it
//                                gives its outputs: the wires, from a give
//                                port to a take port, form no cycle
//
//   find_signature_fault, find_wiring_fault, find_forest_fault,
//   find_proof_net_fault, find_vicious_fault, find_dataflow_fault
//                                the body of each check: the first fault, or
//                                the fault NoFault, at compile time or at run
//                                time
//   explain_fault(net, fault)    the text of the refusal of a fault
//   determinism_of(net)          the determinism class of the net:
//                                Deterministic, or MergesThroughDoor
//   find_canonical_form(net)     the hash of the canonical form of the net,
//                                or the fault that leaves it with no form
//   check_canonical_form(net)    the consteval door of find_canonical_form
//   is_same_net(net, net)        the two nets are the same net
//   seal_net(net)                the sealed net of a net: its wiring checked
//                                one time.  Each find function and
//                                is_same_net also take a sealed net
//
// THE MODEL
//   An agent has ports, and one of them is its principal port.  A rule fires
//   only where two principal ports meet, and it replaces the two agents with
//   a small net (Lafont 1990).  A port has a type.  give(t) and take(t) are
//   duals, and meet(t) is its own dual.  A wire joins a port with a port of
//   the dual type.  A par group holds two or more auxiliary ports of one
//   agent, and a switching of the net keeps one port of each group (a par of
//   linear logic, a fork of threads, or a contraction of a shared read).  A
//   boundary agent has one port, and it stands for a free port of the net: an
//   input, a result or a conclusion.  No rule reads a boundary agent.
//
//   A multiparty session is one agent with one port for each role: the
//   coherence node of multiparty classical processes (Carbone, Lindley,
//   Montesi, Schurmann and Wadler 2016).  A triangle of binary wires for
//   three roles is a cycle, and check_forest refuses it, although the global
//   type is live.
//
// WHICH CHECK FOR WHICH NET
//   Each structural check runs check_wiring first, because each of them reads
//   each port through its one wire.
//
//   check_forest and check_proof_net are for a net with the shape of a tree:
//   a fork tree of threads, a reduction tree, a composition of channels with
//   no cycle.  check_forest admits a net of several trees, and it refuses a
//   par group.  check_proof_net reads the par groups, so it admits the cycle
//   of a fork and its join, and it asks for one connected net.  Each check
//   refuses a cycle that is safe: two sessions in a fixed order, a ring of
//   queues, a loop between two stages (probe E4).  The liveness check of
//   foundation/algebra/net/Priority.h decides those.
//
//   A value with two uses needs an explicit copy agent.  So a scan net, where
//   one partial sum feeds an output and a later combine, and a sorting
//   network, whose comparators take two values and give two, have cycles
//   through their copies and their comparators.  Such a net passes
//   check_wiring, check_dataflow and check_vicious.  check_dataflow is the
//   liveness check of a net that runs one time: each agent waits on its
//   inputs, so an order exists exactly when the directed wires form no cycle.
//
//   No structural check sees the order of the operands of an agent: a net and
//   the same net with two operands swapped pass the same checks.  A test that
//   an implementation follows its net must also read the operand of each
//   port.
//
// THE DETERMINISM
//   A net whose rules give at most one rule for each pair gives one result
//   in each schedule, because each step commutes with each other step
//   (Lafont 1990).  Plain code that runs a net keeps this only when each
//   agent computes a function of its inputs.  An agent whose result depends
//   on the order in which its inputs come, such as the consumer of a channel
//   with two producers, is a merge.  The kind of such an agent opens its
//   merge door with allow_merge(reason), and the reason tells which order
//   the schedule picks.  allow_merge is the only way to an open door, so a
//   search for allow_merge finds each merge kind of the tree.
//
//   determinism_of(net) gives MergesThroughDoor for a net with an agent of a
//   merge kind, and Deterministic for each other net.  A user that needs one
//   result in each schedule writes
//   static_assert(determinism_of(net) == NetDeterminism::Deterministic).  The
//   effect row of a context already refuses the pop of a channel with two
//   producers with no Nondet tag, so the door is for the nets that no row
//   reads: the plans of the reductions and the nets of tasks.  A boundary
//   agent stands for a free port, so check_signature refuses a boundary kind
//   with an open door.
//
// THE CANONICAL FORM
//   Two nets are the same net when a map of the agents of one onto the
//   agents of the other keeps each kind, each wire and each port, and maps
//   each boundary agent onto the boundary agent at the same place among the
//   boundary agents.  The boundary agents stand for the free ports of the
//   net in their order, so their order belongs to the net.  The inner agents
//   have no order.
//
//   The canonical walk gives the boundary agents the first numbers, in their
//   order in the net.  Then it walks the net depth first from each boundary
//   agent in that order, with the ports of each agent in port order, and it
//   gives each inner agent the next number at its first visit.  The walk
//   reads only the kinds, the ports and the numbers, so two same nets get
//   the same numbers.  The canonical form lists, for each number, the form
//   of the kind of its agent, and the number and the port of the peer of
//   each port.  An inner agent with no path to a boundary agent gets no
//   number, so a net with such an agent, or with no boundary agent, has no
//   canonical form.
//
//   The form of a kind is its content, and not its index in the signature:
//   the name of the kind, the name, the type and the polarity of each port,
//   the principal port, the par group, the boundary flag, and the merge door
//   with its reason.  A type is its name, and not its index.  So two kinds of
//   one name with other merge doors are two kinds, and a net with a merge
//   kind is never the same net as a net with no merge.  A signature that
//   lists the same kinds and types in another order gives the same form, and
//   a kind or a type that no agent has is not part of the form.
//
//   find_canonical_form gives the hash of the form.  is_same_net compares
//   the two forms entry by entry, so a collision of two hashes does not make
//   two nets the same.  A net that takes its inputs as inner agents, such as
//   num agents, has no order of its inputs: two leaves of one kind have no
//   identity, so a net and the same net with two such operands swapped have
//   one form.  A user that tells operands apart gives each input as a
//   boundary agent.
//
// THE VERDICT
//   A check gives a NetVerdict.  An admitted verdict has no text.  A refusal
//   names the agent, the node and the port in one line, for example:
//     net: port 'kids' of agent 'join' (node 7) has no wire.  Each port has
//     exactly one wire.
//   data() and size() give the text, so static_assert(verdict.is_admitted(),
//   verdict) prints it (P2741).  A verdict is a constant at namespace scope in
//   a check file or a test: a verdict in a header runs again in each unit
//   that includes it, and 23 constexpr verdicts in one function cost 22.8 s of
//   debug information (probe E4).
//
// THE FAULT
//   The body of each check finds a NetFault and makes no text.  The code
//   names the rule that the net breaks, and the other fields name the place:
//   the type or the kind of a fault of the signature, the agent and the port,
//   the wire, and for a cycle its first agents in the order of the text.  A
//   check that admits the net gives the fault NoFault.  explain_fault makes
//   the text from the fault and the net, and each consteval door check_*
//   gives that text.  So a caller that needs no text reads the fault, which
//   holds 48 bytes and no text.
//
// THE RUN-TIME FORM
//   Each find function is constexpr: the door check_* runs it at compile
//   time, and a program calls it at run time on the same net, with the same
//   body and the same fault.  The counts of the agents and the wires are the
//   template arguments of the net, so a run-time net has the sizes of its
//   type.  Its signature comes from a constant, because a NetName is
//   consteval, and its agents and wires can come from the program.
//
//   At -O1 and above, the compiler tries to fold each call of a constexpr
//   function whose arguments are constants.  So a run-time call on a constant
//   net runs the whole check again in the compile: a test that called four
//   checks on nets of 1,000 agents that way took 905 G compile instructions.
//   A program calls a find function on a net that it holds as a parameter or
//   as a variable, never on a constant.
//
//   A run-time call keeps the arrays of its body on the stack: check_proof_net
//   takes about 190 bytes for each agent and 20 for each wire, and each other
//   check less.  The flag -Wstack-usage of the build refuses a frame of more
//   than 512 KB, so the compiler refuses a run-time call on a net of more
//   than some thousand agents.  The text of explain_fault costs about 0.8 s of
//   compile time in a Debug unit, so a program reads the fault and makes the
//   text in one source file.
//
// THE SEALED NET
//   Each find function on a Net runs find_wiring_fault first, because each
//   body reads each port through its one wire.  A program that runs some
//   checks on one net at run time checks the wiring one time, at the
//   boundary where the net comes in: seal_net copies the net into a
//   SealedNet and finds the fault of the wiring of the copy.  No caller can
//   change the copy, so the fault stays the fault of the net.  Each find
//   function on a SealedNet gives that fault, or runs its body alone, as the
//   plain form of its algorithm does.  The consteval doors check_* take a
//   Net, and run the wiring check in each call.
//
//   The door is seal_net and not a door mint_*.  A mint of the base takes a
//   context and a concept of the layer of the effects as its constraint, and
//   this header includes no such header (THE COSTS).  allow_merge is the
//   door of MergeDoor for the same reason.
//
// THE COSTS
//   P is the count of the ports, A of the agents and W of the wires.
//     check_signature   O(K) for K kinds
//     check_wiring      O(K + A + W), a table of the kinds and a mask of the
//                       used ports of each agent
//     seal_net          O(K + A + W), a copy of the net and check_wiring
//     check_forest      O(W a(A)), one union-find over the agents
//     check_proof_net   O(P log P), the contraction of Danos with a union-find
//                       and a ready queue of the pars, as probe E4 measured
//     check_vicious     O(A + W)
//     check_dataflow    O(A + W), the topological sort of Kahn
//     determinism_of    O(A)
//     find_canonical_form, is_same_net
//                       O(A + W), a walk with an explicit stack, and the names
//                       of the K kinds one time.  The hash folds in four
//                       chains side by side
//   A check on a sealed net costs the same, with no check_wiring.
//   Each check reads arrays, because a read of an array costs less in constant
//   evaluation than a read through a pointer.  The body of each check is a
//   template on the capacity of its arrays, and it reads the first agents and
//   the first wires up to the counts of the net:
//     a small net      at most 16 agents and 32 wires.  The check copies the
//                      agents and the wires into arrays of 16 and 32, so the
//                      small nets of one signature share one instance of each
//                      body
//     each other net   the arrays of the net, so the body is an instance of
//                      its sizes
//   Each structural check calls find_wiring_fault on the net itself, and not
//   on the copy, so the compiler keeps one result for each net that some
//   checks read.  explain_fault is a template on the capacity class too, and
//   it puts each name through helpers that are no templates.
//   This header includes two headers of the base: the mixer
//   foundation/reflect/Mix.h, which includes no other, and the seal
//   foundation/ByteSeal.h, which includes foundation/Platform.h.  It includes
//   no <meta>.  So a light header can include it.
//
// SOURCES
//   Lafont, Interaction nets, POPL 1990.  Lafont, Interaction combinators,
//   Information and Computation 137, 1997.  Danos and Regnier, The structure
//   of multiplicatives, 1989, and the contraction criterion of Danos 1990.
//   Jacobs, Hinrichsen and Krebbers, Deadlock-free separation logic, POPL
//   2024, for the forest condition.

#include <foundation/ByteSeal.h>
#include <foundation/reflect/Mix.h>

#include <cstddef>
#include <cstdint>

namespace foundation::algebra::net {

namespace detail {

// The consteval constructor of a NetName calls this function when a literal
// has no terminating zero.  It has no definition, so the call is no constant
// expression, and the error of the compiler names the rule.
void net_name_has_no_terminating_zero() noexcept;

// allow_merge calls this function for an empty reason.  It has no
// definition, so the call is no constant expression, and the error of the
// compiler names the rule.
void merge_door_has_no_reason() noexcept;

}  // namespace detail

// ── Names ───────────────────────────────────────────────────────────────

// The name of a type, an agent kind or a port.  It borrows a text of static
// storage: a string literal, or the static string that reflection gives
// (std::define_static_string).  The empty name is the default.
class NetName {
    char const* first_ = "";
    std::size_t size_ = 0;

public:
    constexpr NetName() noexcept = default;

    // The characters of a string literal, without its terminating zero.  A
    // literal with no terminating zero is no constant expression.
    template <std::size_t Length>
    consteval NetName(char const (&literal)[Length]) noexcept : first_{literal}, size_{Length - 1} {
        if (literal[Length - 1] != '\0') detail::net_name_has_no_terminating_zero();
    }

    // The characters of a text of static storage, up to its first zero.  A
    // null text is no constant expression.
    consteval explicit NetName(char const* text) noexcept : first_{text} {
        while (text[size_] != '\0')
            ++size_;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool is_empty() const noexcept { return size_ == 0; }

    // The character at index, or a zero past the last character.
    [[nodiscard]] constexpr char at(std::size_t index) const noexcept { return index < size_ ? first_[index] : '\0'; }

    // Two names are equal when they hold the same characters.
    [[nodiscard]] friend constexpr bool operator==(NetName const& left, NetName const& right) noexcept {
        if (left.size_ != right.size_) return false;
        for (std::size_t index = 0; index < left.size_; ++index) {
            if (left.first_[index] != right.first_[index]) return false;
        }
        return true;
    }
};

// ── Types of ports ──────────────────────────────────────────────────────

// The index of a type in the type names of a signature.
enum class TypeId : std::uint16_t {
};

// The side of a port.  Give and Take are duals.  Meet is its own dual, for a
// net whose ports have no direction, such as the interaction combinators.
enum class Polarity : std::uint8_t {
    Give = 0,
    Take = 1,
    Meet = 2,
};

// The type of a port: a type of the signature and a polarity.
struct PortType {
    TypeId type{};
    Polarity polarity = Polarity::Give;
    std::uint8_t reserved = 0;

    [[nodiscard]] friend constexpr bool operator==(PortType const& left, PortType const& right) noexcept {
        return left.type == right.type && left.polarity == right.polarity;
    }
};

// A port that gives a value of the type: the producer side of a channel.
[[nodiscard]] constexpr PortType give(TypeId type) noexcept { return PortType{type, Polarity::Give, 0}; }

// A port that takes a value of the type: the consumer side of a channel.
[[nodiscard]] constexpr PortType take(TypeId type) noexcept { return PortType{type, Polarity::Take, 0}; }

// A port with no direction, whose dual is the same type.
[[nodiscard]] constexpr PortType meet(TypeId type) noexcept { return PortType{type, Polarity::Meet, 0}; }

// The dual of a type: give and take change places, and meet stays.
[[nodiscard]] constexpr PortType dual_of(PortType type) noexcept {
    if (type.polarity == Polarity::Give) return take(type.type);
    if (type.polarity == Polarity::Take) return give(type.type);
    return type;
}

// ── The merge door ──────────────────────────────────────────────────────

// The merge door of an agent kind (THE DETERMINISM).  The default door is
// closed.  allow_merge is the only way to an open door.
class MergeDoor {
    NetName reason_{};

    friend consteval MergeDoor allow_merge(NetName reason) noexcept;

public:
    constexpr MergeDoor() noexcept = default;

    [[nodiscard]] constexpr bool is_open() const noexcept { return !reason_.is_empty(); }

    // The order that the schedule picks, or the empty name of a closed door.
    [[nodiscard]] constexpr NetName reason() const noexcept { return reason_; }
};

// The open merge door of a kind whose result depends on the order in which
// its inputs come.  The reason tells which order the schedule picks.  An
// empty reason is no constant expression.
[[nodiscard]] consteval MergeDoor allow_merge(NetName reason) noexcept {
    if (reason.is_empty()) detail::merge_door_has_no_reason();
    MergeDoor door{};
    door.reason_ = reason;
    return door;
}

// ── Agents and wires ────────────────────────────────────────────────────

// The most ports of one agent.  A par group is a mask of 8 bits.
inline constexpr std::size_t max_agent_ports = 8;

// The ports of an agent.  The ports are the names before the first empty
// name, so a kind of two ports names two.  par_ports holds one bit for each
// port of the par group: no port, or two or more auxiliary ports.  A
// boundary kind has one port, and no rule reads it.  A merge kind opens its
// merge door (THE DETERMINISM).
struct AgentKind {
    NetName name{};
    NetName port_names[max_agent_ports]{};
    PortType port_types[max_agent_ports]{};
    std::uint8_t principal = 0;
    std::uint8_t par_ports = 0;
    bool is_boundary = false;
    std::uint8_t reserved[5]{};
    MergeDoor merge{};
};

// The index of a kind in the kinds of a signature.
enum class KindId : std::uint32_t {
};

// The index of an agent in a net: the node of the agent.
enum class AgentId : std::uint32_t {
};

// A port of an agent of a net.
struct PortRef {
    AgentId agent{};
    std::uint32_t port = 0;
};

// A wire between two ports.
struct Wire {
    PortRef left{};
    PortRef right{};
};

// The integer of an index, for an array of a check.
[[nodiscard]] constexpr std::uint32_t index_of(TypeId id) noexcept { return static_cast<std::uint32_t>(id); }
[[nodiscard]] constexpr std::uint32_t index_of(KindId id) noexcept { return static_cast<std::uint32_t>(id); }
[[nodiscard]] constexpr std::uint32_t index_of(AgentId id) noexcept { return static_cast<std::uint32_t>(id); }

// The vocabulary of a net or of a rule table: the names of the types and the
// kinds of the agents.
template <std::size_t Types, std::size_t Kinds>
    requires(Types >= 1 && Kinds >= 1 && Types <= 0x10000u && Kinds < 0x10000000u)
struct NetSignature {
    NetName type_names[Types]{};
    AgentKind kinds[Kinds]{};
};

// A net: the signature, the kind of each agent, and the wires.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
    requires(Types >= 1 && Kinds >= 1 && Agents >= 1 && Wires >= 1 && Agents < 0x10000000u && Wires < 0x10000000u)
struct Net {
    NetSignature<Types, Kinds> signature{};
    KindId agents[Agents]{};
    Wire wires[Wires]{};
};

// ── The verdict ─────────────────────────────────────────────────────────

// The count of the characters of the text of a verdict.  A longer text stops
// at the capacity.
inline constexpr std::size_t net_verdict_capacity = 320;

// The answer of a check.  An admitted verdict has no text.  A refusal has one
// line that names the agent, the node and the port.  data() and size() give
// the text, which static_assert prints as its message.
class NetVerdict {
    char text_[net_verdict_capacity]{};
    std::size_t size_ = 0;
    bool is_refused_ = false;
    std::uint8_t reserved_[7]{};

public:
    // The admitted verdict.
    constexpr NetVerdict() noexcept = default;

    // A refusal whose text starts with opening, a text of static storage.  A
    // null opening gives a refusal with no text.
    [[nodiscard]] static constexpr NetVerdict refuse(char const* opening) noexcept {
        NetVerdict verdict{};
        verdict.is_refused_ = true;
        verdict.put(opening);
        return verdict;
    }

    [[nodiscard]] constexpr bool is_admitted() const noexcept { return !is_refused_; }
    [[nodiscard]] constexpr char const* data() const noexcept { return text_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }

    // The text is the literal, character for character.
    template <std::size_t Length>
    [[nodiscard]] consteval bool says(char const (&literal)[Length]) const noexcept {
        if (size_ != Length - 1) return false;
        for (std::size_t index = 0; index < size_; ++index) {
            if (text_[index] != literal[index]) return false;
        }
        return true;
    }

    // The text starts with the characters of prefix.  A tool that prints a
    // verdict at run time reads it.
    [[nodiscard]] constexpr bool starts_with(NetName prefix) const noexcept {
        if (prefix.size() > size_) return false;
        for (std::size_t index = 0; index < prefix.size(); ++index) {
            if (text_[index] != prefix.at(index)) return false;
        }
        return true;
    }

    // Puts the characters of a name after the text.
    constexpr NetVerdict& put_name(NetName text) noexcept {
        for (std::size_t index = 0; index < text.size() && size_ < net_verdict_capacity; ++index) {
            text_[size_] = text.at(index);
            ++size_;
        }
        return *this;
    }

    // Puts the characters of a text of static storage, up to its zero.  A
    // check passes each string literal of its text here, so a literal makes
    // no instance of the constructor template of NetName.  A null text puts
    // nothing.
    constexpr NetVerdict& put(char const* text) noexcept {
        if (text == nullptr) return *this;
        for (; *text != '\0' && size_ < net_verdict_capacity; ++text) {
            text_[size_] = *text;
            ++size_;
        }
        return *this;
    }

    // Puts the decimal digits of a number after the text.
    constexpr NetVerdict& put_number(std::uint64_t value) noexcept {
        char digits[20]{};
        std::size_t count = 0;
        do {
            digits[count] = static_cast<char>('0' + static_cast<int>(value % 10u));
            ++count;
            value /= 10u;
        } while (value != 0u);
        while (count != 0u && size_ < net_verdict_capacity) {
            --count;
            text_[size_] = digits[count];
            ++size_;
        }
        return *this;
    }
};

// ── The fault ───────────────────────────────────────────────────────────

// The rule that a net or a signature breaks.  Each code has one text, which
// explain_fault makes.
enum class NetFaultCode : std::uint8_t {
    NoFault = 0,
    // check_signature.  The fault names the type, or the kind and the port.
    TypeWithoutName = 1,
    KindWithoutName = 2,
    KindWithoutPort = 3,
    PortAfterEmptyName = 4,
    PortTypePastSignature = 5,
    PrincipalPastPorts = 6,
    ParGroupPastPorts = 7,
    ParGroupHoldsPrincipal = 8,
    ParGroupOfOnePort = 9,
    BoundaryOfManyPorts = 10,
    // check_wiring.  The fault names the agent, the port and the wire.
    KindPastSignature = 11,
    WireNodePastNet = 12,
    WirePortPastAgent = 13,
    PortWithSecondWire = 14,
    WireNotDual = 15,
    PortWithoutWire = 16,
    // check_forest
    ForestHasParGroup = 17,
    ForestCycle = 18,
    // check_proof_net
    ProofNetCycle = 19,
    ParReachesItself = 20,
    ParPortsNeverMeet = 21,
    NetNotConnected = 22,
    // check_vicious.  The fault names the cycle in path.
    ViciousCircle = 23,
    // check_dataflow.  A cycle of waits names its cycle in path.
    DataflowMeetPort = 24,
    DataflowCycle = 25,
    // check_signature.  The fault names the kind.
    BoundaryMerges = 26,
    // find_canonical_form.  The fault names the agent.
    AgentWithoutBoundaryPath = 27,
};

// The most agents of a cycle that a fault names.  A longer cycle sets
// is_path_cut.
inline constexpr std::size_t max_fault_path = 6;

// The fault that a check finds, with no text.  The code tells which fields
// it sets:
//   type            TypeWithoutName
//   kind            each other code of the signature, with port where the
//                   rule names a port
//   agent, port     the agent and the port of a fault of the net.  For
//                   WireNodePastNet, agent is the node past the net
//   wire            the wire of a fault of a wire
//   path            the agents of a cycle in the order of the text, path_count
//                   of them
struct NetFault {
    NetFaultCode code = NetFaultCode::NoFault;
    std::uint8_t path_count = 0;
    bool is_path_cut = false;
    std::uint8_t reserved = 0;
    TypeId type{};
    std::uint16_t reserved_type = 0;
    KindId kind{};
    AgentId agent{};
    std::uint32_t port = 0;
    std::uint32_t wire = 0;
    AgentId path[max_fault_path]{};

    [[nodiscard]] constexpr bool is_fault() const noexcept { return code != NetFaultCode::NoFault; }
};

namespace detail {

// The count of the agents of a net, and the count of its wires.  A body of a
// check reads the first agents and wires of its arrays up to these counts.
enum class AgentCount : std::uint32_t {
};
enum class WireCount : std::uint32_t {
};

[[nodiscard]] constexpr std::uint32_t count_of(AgentCount count) noexcept { return static_cast<std::uint32_t>(count); }
[[nodiscard]] constexpr std::uint32_t count_of(WireCount count) noexcept { return static_cast<std::uint32_t>(count); }

}  // namespace detail

namespace detail {

// An index that no array of a check holds.
inline constexpr std::uint32_t no_index = 0xFFFFFFFFu;

// The count of the ports of a kind: the names before the first empty name.
[[nodiscard]] constexpr std::uint32_t port_count_of(AgentKind const& kind) noexcept {
    std::uint32_t count = 0;
    while (count < max_agent_ports && !kind.port_names[count].is_empty())
        ++count;
    return count;
}

// The port is in the par group of the kind.
[[nodiscard]] constexpr bool is_par_port(AgentKind const& kind, std::uint32_t port) noexcept {
    return ((static_cast<std::uint32_t>(kind.par_ports) >> port) & 1u) != 0u;
}

// The count of the set bits of a mask of a par group.
[[nodiscard]] constexpr std::uint32_t count_bits(std::uint32_t mask) noexcept {
    std::uint32_t count = 0;
    for (; mask != 0u; mask &= mask - 1u)
        ++count;
    return count;
}

// The kind of an agent of a net that check_wiring admitted.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr AgentKind const& kind_of_(Net<Types, Kinds, Agents, Wires> const& graph,
                                                  AgentId agent) noexcept {
    return graph.signature.kinds[index_of(graph.agents[index_of(agent)])];
}

// ── The capacity class ──────────────────────────────────────────────────

// The capacity of the arrays of a small net (THE COSTS).
inline constexpr std::size_t small_net_agents = 16;
inline constexpr std::size_t small_net_wires = 32;

template <std::size_t Agents, std::size_t Wires>
inline constexpr bool is_small_net = Agents <= small_net_agents && Wires <= small_net_wires;

// A small net in the capacity of the small class.  The agents and the wires
// past the counts of the net keep their defaults, and no check reads them.
// Each public check copies a small net here and runs its body on the copy.
// A dispatch through a lambda costs about 3 M compile instructions more for
// each new size, so each check states the two arms.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
    requires is_small_net<Agents, Wires>
[[nodiscard]] constexpr Net<Types, Kinds, small_net_agents, small_net_wires>
copy_small_net_(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    Net<Types, Kinds, small_net_agents, small_net_wires> small{};
    small.signature = graph.signature;
    for (std::uint32_t agent = 0; agent < Agents; ++agent)
        small.agents[agent] = graph.agents[agent];
    for (std::uint32_t index = 0; index < Wires; ++index)
        small.wires[index] = graph.wires[index];
    return small;
}

// The texts of the refusals take names and numbers, so no helper depends on
// the sizes of a net.

// "Name!", "Name?" or "Name"
constexpr void put_type(NetVerdict& text, NetName type_name, Polarity polarity) noexcept {
    text.put_name(type_name);
    if (polarity == Polarity::Give) text.put("!");
    if (polarity == Polarity::Take) text.put("?");
}

// "agent 'NAME' (node N)"
constexpr void put_agent(NetVerdict& text, AgentKind const& kind, std::uint32_t node) noexcept {
    text.put("agent '").put_name(kind.name).put("' (node ").put_number(node).put(")");
}

// "port 'PORT' of agent 'NAME' (node N)".  A port past the ports of a kind
// has the empty name.
constexpr void put_port(NetVerdict& text, AgentKind const& kind, std::uint32_t node, std::uint32_t port) noexcept {
    text.put("port '").put_name(port < max_agent_ports ? kind.port_names[port] : NetName{}).put("' of ");
    put_agent(text, kind, node);
}

// A union-find over Count vertices, with the union by size and the halving
// of paths.  A find costs O(a(Count)) amortized.
template <std::size_t Count>
class UnionFind {
    std::uint32_t parent[Count]{};
    std::uint32_t size[Count]{};

public:
    constexpr UnionFind() noexcept {
        for (std::uint32_t vertex = 0; vertex < Count; ++vertex) {
            parent[vertex] = vertex;
            size[vertex] = 1;
        }
    }

    [[nodiscard]] constexpr std::uint32_t find(std::uint32_t vertex) noexcept {
        while (parent[vertex] != vertex) {
            parent[vertex] = parent[parent[vertex]];
            vertex = parent[vertex];
        }
        return vertex;
    }

    // Joins the classes of two roots, and gives the root of the joined class.
    constexpr std::uint32_t join_roots(std::uint32_t left, std::uint32_t right) noexcept {
        if (size[left] < size[right]) {
            parent[left] = right;
            size[right] += size[left];
            return right;
        }
        parent[right] = left;
        size[left] += size[right];
        return left;
    }
};

}  // namespace detail

// ── The signature ───────────────────────────────────────────────────────

// The first fault of the signature, or NoFault.  Each type has a name.  Each
// kind has a name and at least one port, its ports are the names before the
// first empty name, each port names a type of the signature, the principal
// port is one of its ports, the par group holds no port or two or more
// auxiliary ports, and a boundary kind has one port, no par group and a
// closed merge door.  Complexity: O(T + K).
template <std::size_t Types, std::size_t Kinds>
[[nodiscard]] constexpr NetFault find_signature_fault(NetSignature<Types, Kinds> const& signature) noexcept {
    for (std::uint32_t type = 0; type < Types; ++type) {
        if (signature.type_names[type].is_empty()) {
            return NetFault{.code = NetFaultCode::TypeWithoutName, .type = TypeId{static_cast<std::uint16_t>(type)}};
        }
    }
    for (std::uint32_t index = 0; index < Kinds; ++index) {
        AgentKind const& kind = signature.kinds[index];
        KindId const id{index};
        if (kind.name.is_empty()) return NetFault{.code = NetFaultCode::KindWithoutName, .kind = id};
        std::uint32_t const count = detail::port_count_of(kind);
        if (count == 0) return NetFault{.code = NetFaultCode::KindWithoutPort, .kind = id};
        for (std::uint32_t port = count; port < max_agent_ports; ++port) {
            if (!kind.port_names[port].is_empty()) {
                return NetFault{.code = NetFaultCode::PortAfterEmptyName, .kind = id, .port = port};
            }
        }
        for (std::uint32_t port = 0; port < count; ++port) {
            if (index_of(kind.port_types[port].type) >= Types) {
                return NetFault{.code = NetFaultCode::PortTypePastSignature, .kind = id, .port = port};
            }
        }
        if (kind.principal >= count) return NetFault{.code = NetFaultCode::PrincipalPastPorts, .kind = id};
        auto const group = static_cast<std::uint32_t>(kind.par_ports);
        if ((group >> count) != 0u) return NetFault{.code = NetFaultCode::ParGroupPastPorts, .kind = id};
        if (detail::is_par_port(kind, kind.principal)) {
            return NetFault{.code = NetFaultCode::ParGroupHoldsPrincipal, .kind = id};
        }
        if (detail::count_bits(group) == 1u) return NetFault{.code = NetFaultCode::ParGroupOfOnePort, .kind = id};
        if (kind.is_boundary && (count != 1u || group != 0u)) {
            return NetFault{.code = NetFaultCode::BoundaryOfManyPorts, .kind = id};
        }
        if (kind.is_boundary && kind.merge.is_open()) return NetFault{.code = NetFaultCode::BoundaryMerges, .kind = id};
    }
    return NetFault{};
}

namespace detail {

// The text of a fault of the signature.  Complexity: O(1) and the names.
template <std::size_t Types, std::size_t Kinds>
[[nodiscard, gnu::cold]] constexpr NetVerdict explain_signature_fault_(NetSignature<Types, Kinds> const& signature,
                                                                       NetFault const& fault) noexcept {
    std::uint32_t const index = index_of(fault.kind) < Kinds ? index_of(fault.kind) : 0u;
    AgentKind const& kind = signature.kinds[index];
    std::uint32_t const count = port_count_of(kind);
    NetName const port_name = fault.port < max_agent_ports ? kind.port_names[fault.port] : NetName{};
    switch (fault.code) {
        case NetFaultCode::NoFault:
            return NetVerdict{};
        case NetFaultCode::TypeWithoutName:
            return NetVerdict::refuse("net: type ")
                .put_number(index_of(fault.type))
                .put(" of the signature has no name.  Each type has a name.");
        case NetFaultCode::KindWithoutName:
            return NetVerdict::refuse("net: kind ").put_number(index).put(" of the signature has no name.");
        case NetFaultCode::KindWithoutPort:
            return NetVerdict::refuse("net: agent kind '")
                .put_name(kind.name)
                .put("' has no port.  An agent has one principal port.");
        case NetFaultCode::PortAfterEmptyName:
            return NetVerdict::refuse("net: port '")
                .put_name(port_name)
                .put("' of agent kind '")
                .put_name(kind.name)
                .put("' comes after a port with no name.  The ports of a kind are the names before the first "
                     "empty name.");
        case NetFaultCode::PortTypePastSignature:
            return NetVerdict::refuse("net: port '")
                .put_name(port_name)
                .put("' of agent kind '")
                .put_name(kind.name)
                .put("' has the type ")
                .put_number(fault.port < max_agent_ports ? index_of(kind.port_types[fault.port].type) : 0u)
                .put(", and the signature has ")
                .put_number(Types)
                .put(" types.");
        case NetFaultCode::PrincipalPastPorts:
            return NetVerdict::refuse("net: the principal port ")
                .put_number(kind.principal)
                .put(" of agent kind '")
                .put_name(kind.name)
                .put("' is past its ")
                .put_number(count)
                .put(" ports.");
        case NetFaultCode::ParGroupPastPorts:
            return NetVerdict::refuse("net: the par group of agent kind '")
                .put_name(kind.name)
                .put("' names a port past its ")
                .put_number(count)
                .put(" ports.");
        case NetFaultCode::ParGroupHoldsPrincipal:
            return NetVerdict::refuse("net: the par group of agent kind '")
                .put_name(kind.name)
                .put("' holds its principal port '")
                .put_name(kind.principal < max_agent_ports ? kind.port_names[kind.principal] : NetName{})
                .put("'.  A par group holds auxiliary ports.");
        case NetFaultCode::ParGroupOfOnePort:
            return NetVerdict::refuse("net: the par group of agent kind '")
                .put_name(kind.name)
                .put("' holds one port.  A par group holds two or more ports.");
        case NetFaultCode::BoundaryOfManyPorts:
            return NetVerdict::refuse("net: the boundary kind '")
                .put_name(kind.name)
                .put("' has ")
                .put_number(count)
                .put(" ports.  A boundary agent stands for one free port of the net, so it has one port.");
        case NetFaultCode::BoundaryMerges:
            return NetVerdict::refuse("net: the boundary kind '")
                .put_name(kind.name)
                .put("' opens the merge door.  A boundary agent stands for a free port of the net, so it merges "
                     "nothing.");
        case NetFaultCode::KindPastSignature:
        case NetFaultCode::WireNodePastNet:
        case NetFaultCode::WirePortPastAgent:
        case NetFaultCode::PortWithSecondWire:
        case NetFaultCode::WireNotDual:
        case NetFaultCode::PortWithoutWire:
        case NetFaultCode::ForestHasParGroup:
        case NetFaultCode::ForestCycle:
        case NetFaultCode::ProofNetCycle:
        case NetFaultCode::ParReachesItself:
        case NetFaultCode::ParPortsNeverMeet:
        case NetFaultCode::NetNotConnected:
        case NetFaultCode::ViciousCircle:
        case NetFaultCode::DataflowMeetPort:
        case NetFaultCode::DataflowCycle:
        case NetFaultCode::AgentWithoutBoundaryPath:
        default:
            break;
    }
    return NetVerdict::refuse("net: the fault names a net, and the signature alone cannot name its places.");
}

}  // namespace detail

// The check of the signature, as a verdict with the text of its fault.
template <std::size_t Types, std::size_t Kinds>
[[nodiscard]] consteval NetVerdict check_signature(NetSignature<Types, Kinds> const& signature) noexcept {
    NetFault const fault = find_signature_fault(signature);
    if (!fault.is_fault()) return NetVerdict{};
    return detail::explain_signature_fault_(signature, fault);
}

// ── The text of a fault ─────────────────────────────────────────────────

namespace detail {

// The kind that no agent has.  The text of a fault names it for an agent
// that the net does not have, so a fault of another net reads no array past
// its end.
inline constexpr AgentKind no_kind{};

// The kind of the agent, or no_kind for an agent past the net or a kind past
// the signature.
template <std::size_t Types, std::size_t Kinds, std::size_t AgentCapacity, std::size_t WireCapacity>
[[nodiscard]] constexpr AgentKind const& kind_at_(Net<Types, Kinds, AgentCapacity, WireCapacity> const& graph,
                                                  AgentId agent, AgentCount agents) noexcept {
    std::uint32_t const node = index_of(agent);
    if (node >= count_of(agents) || node >= AgentCapacity) return no_kind;
    std::uint32_t const kind = index_of(graph.agents[node]);
    return kind < Kinds ? graph.signature.kinds[kind] : no_kind;
}

// The type of a port of a kind, or the default type past its ports.
[[nodiscard]] constexpr PortType port_type_at(AgentKind const& kind, std::uint32_t port) noexcept {
    return port < max_agent_ports ? kind.port_types[port] : PortType{};
}

// The name of a type of the signature, or the empty name past its types.
template <std::size_t Types, std::size_t Kinds>
[[nodiscard]] constexpr NetName type_name_at_(NetSignature<Types, Kinds> const& signature, PortType type) noexcept {
    return index_of(type.type) < Types ? signature.type_names[index_of(type.type)] : NetName{};
}

// The arrow between two agents of the path of a fault: a vicious circle goes
// forward, and a cycle of waits goes back along the inputs.
enum class PathArrow : std::uint8_t {
    Forward = 0,
    Back = 1,
};

// "agent 'A' (node N) -> agent 'B' (node M) -> ..." for the path of a fault.
template <std::size_t Types, std::size_t Kinds, std::size_t AgentCapacity, std::size_t WireCapacity>
constexpr void put_path_(NetVerdict& text, Net<Types, Kinds, AgentCapacity, WireCapacity> const& graph,
                         NetFault const& fault, AgentCount agents, PathArrow arrow) noexcept {
    char const* const between = arrow == PathArrow::Forward ? " -> " : " <- ";
    for (std::uint32_t shown = 0; shown < fault.path_count && shown < max_fault_path; ++shown) {
        if (shown != 0u) text.put(between);
        put_agent(text, kind_at_(graph, fault.path[shown], agents), index_of(fault.path[shown]));
    }
    if (fault.is_path_cut) text.put(between).put("...");
}

// The text of a fault of the net, on the arrays of a capacity class.
// Complexity: O(1) and the names.
template <std::size_t Types, std::size_t Kinds, std::size_t AgentCapacity, std::size_t WireCapacity>
[[nodiscard, gnu::cold]] constexpr NetVerdict explain_in_(Net<Types, Kinds, AgentCapacity, WireCapacity> const& graph,
                                                          NetFault const& fault, AgentCount agents) noexcept {
    std::uint32_t const node = index_of(fault.agent);
    AgentKind const& kind = kind_at_(graph, fault.agent, agents);
    Wire const wire = fault.wire < WireCapacity ? graph.wires[fault.wire] : Wire{};
    AgentKind const& left_kind = kind_at_(graph, wire.left.agent, agents);
    AgentKind const& right_kind = kind_at_(graph, wire.right.agent, agents);
    PortType const left = port_type_at(left_kind, wire.left.port);
    PortType const right = port_type_at(right_kind, wire.right.port);
    switch (fault.code) {
        case NetFaultCode::NoFault:
            return NetVerdict{};
        case NetFaultCode::TypeWithoutName:
        case NetFaultCode::KindWithoutName:
        case NetFaultCode::KindWithoutPort:
        case NetFaultCode::PortAfterEmptyName:
        case NetFaultCode::PortTypePastSignature:
        case NetFaultCode::PrincipalPastPorts:
        case NetFaultCode::ParGroupPastPorts:
        case NetFaultCode::ParGroupHoldsPrincipal:
        case NetFaultCode::ParGroupOfOnePort:
        case NetFaultCode::BoundaryOfManyPorts:
        case NetFaultCode::BoundaryMerges:
            return explain_signature_fault_(graph.signature, fault);
        case NetFaultCode::KindPastSignature:
            return NetVerdict::refuse("net: node ")
                .put_number(node)
                .put(" has the kind ")
                .put_number(node < AgentCapacity ? index_of(graph.agents[node]) : 0u)
                .put(", and the signature has ")
                .put_number(Kinds)
                .put(" kinds.");
        case NetFaultCode::WireNodePastNet:
            return NetVerdict::refuse("net: wire ")
                .put_number(fault.wire)
                .put(" names node ")
                .put_number(node)
                .put(", and the net has ")
                .put_number(count_of(agents))
                .put(" agents.");
        case NetFaultCode::WirePortPastAgent: {
            NetVerdict refusal = NetVerdict::refuse("net: wire ");
            refusal.put_number(fault.wire).put(" names port ").put_number(fault.port).put(" of ");
            put_agent(refusal, kind, node);
            refusal.put(", which has ").put_number(port_count_of(kind)).put(" ports.");
            return refusal;
        }
        case NetFaultCode::PortWithSecondWire: {
            NetVerdict refusal = NetVerdict::refuse("net: ");
            put_port(refusal, kind, node, fault.port);
            refusal.put(" has a second wire, wire ").put_number(fault.wire).put(".  Each port has exactly one wire.");
            return refusal;
        }
        case NetFaultCode::WireNotDual: {
            NetVerdict refusal = NetVerdict::refuse("net: wire ");
            refusal.put_number(fault.wire).put(" joins ");
            put_port(refusal, left_kind, index_of(wire.left.agent), wire.left.port);
            refusal.put(" of type ");
            put_type(refusal, type_name_at_(graph.signature, left), left.polarity);
            refusal.put(" with ");
            put_port(refusal, right_kind, index_of(wire.right.agent), wire.right.port);
            refusal.put(" of type ");
            put_type(refusal, type_name_at_(graph.signature, right), right.polarity);
            refusal.put(", which is not its dual.");
            return refusal;
        }
        case NetFaultCode::PortWithoutWire: {
            NetVerdict refusal = NetVerdict::refuse("net: ");
            put_port(refusal, kind, node, fault.port);
            refusal.put(" has no wire.  Each port has exactly one wire.");
            return refusal;
        }
        case NetFaultCode::ForestHasParGroup: {
            NetVerdict refusal = NetVerdict::refuse("net: ");
            put_agent(refusal, kind, node);
            refusal.put(" has a par group, and check_forest reads no par group.  Use check_proof_net.");
            return refusal;
        }
        case NetFaultCode::ForestCycle: {
            NetVerdict refusal = NetVerdict::refuse("net: wire ");
            refusal.put_number(fault.wire).put(" from ");
            put_port(refusal, left_kind, index_of(wire.left.agent), wire.left.port);
            refusal.put(" to ");
            put_port(refusal, right_kind, index_of(wire.right.agent), wire.right.port);
            refusal.put(" closes a cycle.  The agents and the wires do not form a forest.");
            return refusal;
        }
        case NetFaultCode::ProofNetCycle: {
            NetVerdict refusal = NetVerdict::refuse("net: a cycle closes at ");
            put_port(refusal, kind, node, fault.port);
            refusal.put(".  Each switching of the net keeps this cycle, so the net is no proof net.");
            return refusal;
        }
        case NetFaultCode::ParReachesItself: {
            NetVerdict refusal = NetVerdict::refuse("net: ");
            put_agent(refusal, kind, node);
            refusal.put(" is a par whose ports reach the agent again.  A switching of it closes a cycle.");
            return refusal;
        }
        case NetFaultCode::ParPortsNeverMeet: {
            NetVerdict refusal = NetVerdict::refuse("net: ");
            put_agent(refusal, kind, node);
            refusal.put(" is a par whose ports never meet.  A switching of it leaves the net in two parts.");
            return refusal;
        }
        case NetFaultCode::NetNotConnected: {
            NetVerdict refusal = NetVerdict::refuse("net: the net is not connected.  ");
            put_agent(refusal, kind, node);
            refusal.put(" has no path to ");
            put_agent(refusal, kind_at_(graph, AgentId{0}, agents), 0);
            refusal.put(".");
            return refusal;
        }
        case NetFaultCode::ViciousCircle: {
            NetVerdict refusal = NetVerdict::refuse("net: vicious circle: ");
            put_path_(refusal, graph, fault, agents, PathArrow::Forward);
            refusal.put(".  Each principal port meets an auxiliary port, so no rule can fire.");
            return refusal;
        }
        case NetFaultCode::DataflowMeetPort: {
            NetVerdict refusal = NetVerdict::refuse("net: wire ");
            refusal.put_number(fault.wire).put(" joins ");
            put_port(refusal, left_kind, index_of(wire.left.agent), wire.left.port);
            refusal.put(" of the type ");
            put_type(refusal, type_name_at_(graph.signature, left), left.polarity);
            refusal.put(", which has no direction.  A dataflow takes give and take ports.");
            return refusal;
        }
        case NetFaultCode::DataflowCycle: {
            NetVerdict refusal = NetVerdict::refuse("net: a cycle of waits closes at ");
            put_port(refusal, kind, node, fault.port);
            refusal.put(": ");
            put_path_(refusal, graph, fault, agents, PathArrow::Back);
            refusal.put(".  Each agent waits on its inputs, so no agent of the cycle starts.");
            return refusal;
        }
        case NetFaultCode::AgentWithoutBoundaryPath: {
            NetVerdict refusal = NetVerdict::refuse("net: ");
            put_agent(refusal, kind, node);
            refusal.put(" has no path to a boundary agent.  The canonical form numbers each agent from the free ports "
                        "of the net.");
            return refusal;
        }
        default:
            break;
    }
    return NetVerdict::refuse("net: the fault has a code that no check gives.");
}

}  // namespace detail

// The text of the refusal of a fault that a find function gave for this net,
// or the admitted verdict for NoFault.  A fault of another net gives a text
// with empty names, and reads no array past its end.  The function is
// constexpr, so a program can call it at run time.  But the code of one
// run-time call costs about 0.8 s of compile time in a Debug unit, so a
// program calls it from one source file.  Complexity: O(1) and the names.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard, gnu::cold]] constexpr NetVerdict explain_fault(Net<Types, Kinds, Agents, Wires> const& graph,
                                                            NetFault const& fault) noexcept {
    if (!fault.is_fault()) return NetVerdict{};
    constexpr detail::AgentCount agents{static_cast<std::uint32_t>(Agents)};
    if constexpr (detail::is_small_net<Agents, Wires>) {
        auto const small = detail::copy_small_net_(graph);
        return detail::explain_in_(small, fault, agents);
    } else {
        return detail::explain_in_(graph, fault, agents);
    }
}

// ── Linearity and types ─────────────────────────────────────────────────

namespace detail {

// The type of a port as one word: the index of the type, then two bits of
// the polarity.
[[nodiscard]] constexpr std::uint32_t type_word_of(PortType type) noexcept {
    return (index_of(type.type) << 2u) | static_cast<std::uint32_t>(type.polarity);
}

// The word of the dual of a type: Give and Take change places, and Meet
// stays (dual_of).
[[nodiscard]] constexpr std::uint32_t dual_word_of(std::uint32_t word) noexcept {
    return (word & 2u) == 0u ? word ^ 1u : word;
}

// The body of find_wiring_fault, on the first agents and the first wires of
// the arrays of a capacity class.  A table of the kinds holds the count of
// the ports of each kind and the word of the type of each port, and a mask
// of 8 bits holds the ports of each agent that a wire uses.  So a wire reads
// the kinds of its two agents and two words of the table.
template <std::size_t Types, std::size_t Kinds, std::size_t AgentCapacity, std::size_t WireCapacity>
[[nodiscard]] constexpr NetFault find_wiring_fault_in_(Net<Types, Kinds, AgentCapacity, WireCapacity> const& graph,
                                                       AgentCount agents, WireCount wires) noexcept {
    NetFault const kinds = find_signature_fault(graph.signature);
    if (kinds.is_fault()) return kinds;
    constexpr auto ports = static_cast<std::uint32_t>(max_agent_ports);
    std::uint32_t const agent_count = count_of(agents);
    std::uint32_t const wire_count = count_of(wires);
    std::uint32_t port_count[Kinds]{};
    std::uint32_t type_word[Kinds * max_agent_ports]{};
    for (std::uint32_t kind = 0; kind < Kinds; ++kind) {
        AgentKind const& entry = graph.signature.kinds[kind];
        port_count[kind] = port_count_of(entry);
        for (std::uint32_t port = 0; port < port_count[kind]; ++port)
            type_word[kind * ports + port] = type_word_of(entry.port_types[port]);
    }
    for (std::uint32_t agent = 0; agent < agent_count; ++agent) {
        if (index_of(graph.agents[agent]) >= Kinds) {
            return NetFault{.code = NetFaultCode::KindPastSignature, .agent = AgentId{agent}};
        }
    }
    std::uint32_t used[AgentCapacity]{};
    for (std::uint32_t index = 0; index < wire_count; ++index) {
        Wire const& wire = graph.wires[index];
        std::uint32_t slot[2]{};
        for (std::uint32_t side = 0; side < 2u; ++side) {
            PortRef const end = side == 0u ? wire.left : wire.right;
            std::uint32_t const node = index_of(end.agent);
            if (node >= agent_count) {
                return NetFault{.code = NetFaultCode::WireNodePastNet, .agent = end.agent, .wire = index};
            }
            std::uint32_t const kind = index_of(graph.agents[node]);
            if (end.port >= port_count[kind]) {
                return NetFault{
                    .code = NetFaultCode::WirePortPastAgent, .agent = end.agent, .port = end.port, .wire = index};
            }
            std::uint32_t const bit = 1u << end.port;
            if ((used[node] & bit) != 0u) {
                return NetFault{
                    .code = NetFaultCode::PortWithSecondWire, .agent = end.agent, .port = end.port, .wire = index};
            }
            used[node] |= bit;
            slot[side] = kind * ports + end.port;
        }
        if (type_word[slot[1]] != dual_word_of(type_word[slot[0]])) {
            return NetFault{.code = NetFaultCode::WireNotDual, .wire = index};
        }
    }
    for (std::uint32_t agent = 0; agent < agent_count; ++agent) {
        std::uint32_t const all_ports = (1u << port_count[index_of(graph.agents[agent])]) - 1u;
        std::uint32_t const missing = all_ports & ~used[agent];
        if (missing != 0u) {
            std::uint32_t port = 0;
            while (((missing >> port) & 1u) == 0u)
                ++port;
            return NetFault{.code = NetFaultCode::PortWithoutWire, .agent = AgentId{agent}, .port = port};
        }
    }
    return NetFault{};
}

}  // namespace detail

// The first fault of the wiring, or NoFault.  Each agent has a kind of the
// signature, each wire names a port that its agent has, each port has
// exactly one wire, and the two ports of a wire have dual types.
// Complexity: O(K + A + W).
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr NetFault find_wiring_fault(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    constexpr detail::AgentCount agents{static_cast<std::uint32_t>(Agents)};
    constexpr detail::WireCount wires{static_cast<std::uint32_t>(Wires)};
    if constexpr (detail::is_small_net<Agents, Wires>) {
        auto const small = detail::copy_small_net_(graph);
        return detail::find_wiring_fault_in_(small, agents, wires);
    } else {
        return detail::find_wiring_fault_in_(graph, agents, wires);
    }
}

// The check of the wiring, as a verdict with the text of its fault.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] consteval NetVerdict check_wiring(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    return explain_fault(graph, find_wiring_fault(graph));
}

// ── The sealed net ──────────────────────────────────────────────────────

template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
class SealedNet;

// Seals a net (THE SEALED NET): copies it, and finds the fault of the wiring
// of the copy one time.  Complexity: O(K + A + W), the copy and
// find_wiring_fault.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr auto seal_net(Net<Types, Kinds, Agents, Wires> const& graph) noexcept
    -> SealedNet<Types, Kinds, Agents, Wires>;

// A net checked one time at a boundary (THE SEALED NET): a copy of the net,
// which no caller can change, and the fault of its wiring.  seal_net is the
// only door.  A copy of a sealed net is a sealed net, because the two
// fields copy together.  The seal closes the two byte routes, std::bit_cast
// and a lifetime start over bytes, so no image of bytes makes a sealed net
// with a fault that is not the fault of its net.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
class SealedNet {
    Net<Types, Kinds, Agents, Wires> net_{};
    NetFault wiring_fault_{};
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr explicit SealedNet(Net<Types, Kinds, Agents, Wires> const& graph) noexcept
        : net_{graph}, wiring_fault_{find_wiring_fault(net_)} {}

    template <std::size_t SealedTypes, std::size_t SealedKinds, std::size_t SealedAgents, std::size_t SealedWires>
    friend constexpr auto seal_net(Net<SealedTypes, SealedKinds, SealedAgents, SealedWires> const& graph) noexcept
        -> SealedNet<SealedTypes, SealedKinds, SealedAgents, SealedWires>;

public:
    // The net that the seal holds.  The reference is const, so no caller
    // changes the net that the fault describes.
    [[nodiscard]] constexpr Net<Types, Kinds, Agents, Wires> const& value() const noexcept { return net_; }

    // The fault of the wiring of the net, or NoFault.
    [[nodiscard]] constexpr NetFault wiring_fault() const noexcept { return wiring_fault_; }
};

template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr auto seal_net(Net<Types, Kinds, Agents, Wires> const& graph) noexcept
    -> SealedNet<Types, Kinds, Agents, Wires> {
    return SealedNet<Types, Kinds, Agents, Wires>{graph};
}

// ── The forest ──────────────────────────────────────────────────────────

namespace detail {

// The first agent of a kind with a par group, or NoFault.  The agents are
// read only when a kind of the signature has a par group.
template <std::size_t Types, std::size_t Kinds, std::size_t AgentCapacity, std::size_t WireCapacity>
[[nodiscard]] constexpr NetFault find_par_agent_(Net<Types, Kinds, AgentCapacity, WireCapacity> const& graph,
                                                 AgentCount agents) noexcept {
    bool has_par_kind = false;
    for (AgentKind const& kind : graph.signature.kinds)
        has_par_kind = has_par_kind || kind.par_ports != 0u;
    if (!has_par_kind) return NetFault{};
    for (std::uint32_t agent = 0; agent < count_of(agents); ++agent) {
        if (kind_of_(graph, AgentId{agent}).par_ports != 0u) {
            return NetFault{.code = NetFaultCode::ForestHasParGroup, .agent = AgentId{agent}};
        }
    }
    return NetFault{};
}

// The body of find_forest_fault, on the arrays of a capacity class of a net
// with no fault of its wiring.
template <std::size_t Types, std::size_t Kinds, std::size_t AgentCapacity, std::size_t WireCapacity>
[[nodiscard]] constexpr NetFault find_forest_fault_in_(Net<Types, Kinds, AgentCapacity, WireCapacity> const& graph,
                                                       AgentCount agents, WireCount wires) noexcept {
    NetFault const par = find_par_agent_(graph, agents);
    if (par.is_fault()) return par;
    UnionFind<AgentCapacity> classes{};
    for (std::uint32_t index = 0; index < count_of(wires); ++index) {
        Wire const& wire = graph.wires[index];
        std::uint32_t const left = classes.find(index_of(wire.left.agent));
        std::uint32_t const right = classes.find(index_of(wire.right.agent));
        if (left == right) return NetFault{.code = NetFaultCode::ForestCycle, .wire = index};
        static_cast<void>(classes.join_roots(left, right));
    }
    return NetFault{};
}

// The body of find_forest_fault on a net with no fault of its wiring, on the
// arrays of its capacity class.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr NetFault find_forest_fault_of_(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    constexpr AgentCount agents{static_cast<std::uint32_t>(Agents)};
    constexpr WireCount wires{static_cast<std::uint32_t>(Wires)};
    if constexpr (is_small_net<Agents, Wires>) {
        auto const small = copy_small_net_(graph);
        return find_forest_fault_in_(small, agents, wires);
    } else {
        return find_forest_fault_in_(graph, agents, wires);
    }
}

}  // namespace detail

// The first fault of the forest, or NoFault.  The agents and the wires form
// a forest: each wire joins two trees, and no wire closes a cycle.  A net can
// have several trees, because two separate compositions do not wait on each
// other.  This is the condition of LinearActris for threads and channels.  A
// par group is refused: a net with a par group takes check_proof_net.
// Complexity: O(K + A + W a(A)).
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr NetFault find_forest_fault(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    NetFault const wiring = find_wiring_fault(graph);
    if (wiring.is_fault()) return wiring;
    return detail::find_forest_fault_of_(graph);
}

// The first fault of the forest of a sealed net: the fault of its wiring, or
// the fault of the body alone.  Complexity: O(K + A + W a(A)), with no second
// check of the wiring.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr NetFault find_forest_fault(SealedNet<Types, Kinds, Agents, Wires> const& sealed) noexcept {
    if (sealed.wiring_fault().is_fault()) return sealed.wiring_fault();
    return detail::find_forest_fault_of_(sealed.value());
}

// The check of the forest, as a verdict with the text of its fault.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] consteval NetVerdict check_forest(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    return explain_fault(graph, find_forest_fault(graph));
}

// ── The criterion of Danos and Regnier, by contraction ──────────────────

// Each switching of the net keeps one port of each par group, and the graph
// of the agents and the wires that it keeps is connected and has no cycle.
//
// The paired graph has one vertex for each agent and one for each wire, and
// one edge for each port.  The ports of a par group are paired edges.  The
// contraction of Danos merges the two ends of an unpaired edge, and merges a
// par into the class that all of its paired edges reach.  The net is correct
// when it contracts to one vertex.  A loop of an unpaired edge, or a par
// whose edges reach its own class, closes a cycle.  The order here: each
// unpaired edge first, with a union-find, and then the pars from a ready
// queue.  Each class keeps a list of the pars that wait on it, and a merge
// reads the shorter list, so each entry is read O(log P) times.
// Complexity: O(P log P).
namespace detail {

// The body of find_proof_net_fault, on the arrays of a capacity class of a
// net with no fault of its wiring.  Vertex v is agent v below the count of
// the agents, and wire v - agent_count above.
template <std::size_t Types, std::size_t Kinds, std::size_t AgentCapacity, std::size_t WireCapacity>
[[nodiscard]] constexpr NetFault find_proof_net_fault_in_(Net<Types, Kinds, AgentCapacity, WireCapacity> const& graph,
                                                          AgentCount agents, WireCount wires) noexcept {
    constexpr std::size_t vertex_capacity = AgentCapacity + WireCapacity;
    constexpr std::uint32_t none = no_index;
    std::uint32_t const agent_count = count_of(agents);
    std::uint32_t const wire_count = count_of(wires);
    std::uint32_t const vertices = agent_count + wire_count;
    std::uint32_t wire_at[AgentCapacity * max_agent_ports]{};
    for (std::uint32_t index = 0; index < wire_count; ++index) {
        Wire const& wire = graph.wires[index];
        wire_at[index_of(wire.left.agent) * max_agent_ports + wire.left.port] = index;
        wire_at[index_of(wire.right.agent) * max_agent_ports + wire.right.port] = index;
    }
    UnionFind<vertex_capacity> classes{};
    std::uint32_t class_count = vertices;

    // The unpaired edges.
    for (std::uint32_t agent = 0; agent < agent_count; ++agent) {
        AgentKind const& kind = kind_of_(graph, AgentId{agent});
        std::uint32_t const count = port_count_of(kind);
        for (std::uint32_t port = 0; port < count; ++port) {
            if (is_par_port(kind, port)) continue;
            std::uint32_t const wire_vertex = agent_count + wire_at[agent * max_agent_ports + port];
            std::uint32_t const left = classes.find(agent);
            std::uint32_t const right = classes.find(wire_vertex);
            if (left == right)
                return NetFault{.code = NetFaultCode::ProofNetCycle, .agent = AgentId{agent}, .port = port};
            static_cast<void>(classes.join_roots(left, right));
            --class_count;
        }
    }

    // The pars.  Each par has one entry in the list of the class of each of
    // its premise wires.  A par is ready when all its premises are in one
    // class.
    std::uint32_t par_agent[AgentCapacity]{};
    std::uint32_t par_premise[AgentCapacity][max_agent_ports]{};
    std::uint32_t par_width[AgentCapacity]{};
    bool is_par_done[AgentCapacity]{};
    std::uint32_t par_count = 0;
    for (std::uint32_t agent = 0; agent < agent_count; ++agent) {
        AgentKind const& kind = kind_of_(graph, AgentId{agent});
        if (kind.par_ports == 0u) continue;
        par_agent[par_count] = agent;
        std::uint32_t const count = port_count_of(kind);
        for (std::uint32_t port = 0; port < count; ++port) {
            if (!is_par_port(kind, port)) continue;
            par_premise[par_count][par_width[par_count]] = agent_count + wire_at[agent * max_agent_ports + port];
            ++par_width[par_count];
        }
        ++par_count;
    }
    auto const do_premises_meet = [&](std::uint32_t par) constexpr noexcept {
        std::uint32_t const first = classes.find(par_premise[par][0]);
        for (std::uint32_t side = 1; side < par_width[par]; ++side) {
            if (classes.find(par_premise[par][side]) != first) return false;
        }
        return true;
    };

    std::uint32_t list_head[vertex_capacity]{};
    std::uint32_t list_tail[vertex_capacity]{};
    std::uint32_t list_length[vertex_capacity]{};
    for (std::uint32_t vertex = 0; vertex < vertices; ++vertex) {
        list_head[vertex] = none;
        list_tail[vertex] = none;
    }
    std::uint32_t entry_par[max_agent_ports * AgentCapacity]{};
    std::uint32_t entry_next[max_agent_ports * AgentCapacity]{};
    std::uint32_t entry_count = 0;
    std::uint32_t ready[AgentCapacity]{};
    std::uint32_t ready_count = 0;
    bool is_queued[AgentCapacity]{};

    for (std::uint32_t par = 0; par < par_count; ++par) {
        for (std::uint32_t side = 0; side < par_width[par]; ++side) {
            std::uint32_t const owner = classes.find(par_premise[par][side]);
            entry_par[entry_count] = par;
            entry_next[entry_count] = none;
            if (list_tail[owner] == none) {
                list_head[owner] = entry_count;
            } else {
                entry_next[list_tail[owner]] = entry_count;
            }
            list_tail[owner] = entry_count;
            ++list_length[owner];
            ++entry_count;
        }
        if (do_premises_meet(par)) {
            ready[ready_count] = par;
            ++ready_count;
            is_queued[par] = true;
        }
    }

    std::uint32_t ready_read = 0;
    while (ready_read < ready_count) {
        std::uint32_t const par = ready[ready_read];
        ++ready_read;
        std::uint32_t const agent = par_agent[par];
        std::uint32_t const own = classes.find(agent);
        std::uint32_t const premises = classes.find(par_premise[par][0]);
        if (own == premises) return NetFault{.code = NetFaultCode::ParReachesItself, .agent = AgentId{agent}};
        is_par_done[par] = true;
        std::uint32_t long_list = own;
        std::uint32_t short_list = premises;
        if (list_length[long_list] < list_length[short_list]) {
            long_list = premises;
            short_list = own;
        }
        std::uint32_t const joined = classes.join_roots(own, premises);
        --class_count;
        for (std::uint32_t entry = list_head[short_list]; entry != none; entry = entry_next[entry]) {
            std::uint32_t const other = entry_par[entry];
            if (is_queued[other]) continue;
            if (do_premises_meet(other)) {
                ready[ready_count] = other;
                ++ready_count;
                is_queued[other] = true;
            }
        }
        // The class that survives takes the two lists.
        std::uint32_t joined_head = list_head[long_list];
        std::uint32_t joined_tail = list_tail[long_list];
        if (list_head[short_list] != none) {
            if (joined_tail == none) {
                joined_head = list_head[short_list];
            } else {
                entry_next[joined_tail] = list_head[short_list];
            }
            joined_tail = list_tail[short_list];
        }
        std::uint32_t const joined_length = list_length[long_list] + list_length[short_list];
        list_head[long_list] = none;
        list_tail[long_list] = none;
        list_length[long_list] = 0;
        list_head[short_list] = none;
        list_tail[short_list] = none;
        list_length[short_list] = 0;
        list_head[joined] = joined_head;
        list_tail[joined] = joined_tail;
        list_length[joined] = joined_length;
    }

    for (std::uint32_t par = 0; par < par_count; ++par) {
        if (!is_par_done[par])
            return NetFault{.code = NetFaultCode::ParPortsNeverMeet, .agent = AgentId{par_agent[par]}};
    }
    if (class_count != 1u) {
        std::uint32_t const root = classes.find(0);
        std::uint32_t apart = 0;
        for (std::uint32_t agent = 1; agent < agent_count; ++agent) {
            if (classes.find(agent) != root) {
                apart = agent;
                break;
            }
        }
        return NetFault{.code = NetFaultCode::NetNotConnected, .agent = AgentId{apart}};
    }
    return NetFault{};
}

// The body of find_proof_net_fault on a net with no fault of its wiring, on
// the arrays of its capacity class.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr NetFault find_proof_net_fault_of_(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    constexpr AgentCount agents{static_cast<std::uint32_t>(Agents)};
    constexpr WireCount wires{static_cast<std::uint32_t>(Wires)};
    if constexpr (is_small_net<Agents, Wires>) {
        auto const small = copy_small_net_(graph);
        return find_proof_net_fault_in_(small, agents, wires);
    } else {
        return find_proof_net_fault_in_(graph, agents, wires);
    }
}

}  // namespace detail

// The first fault of the criterion of Danos and Regnier above, or NoFault.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr NetFault find_proof_net_fault(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    NetFault const wiring = find_wiring_fault(graph);
    if (wiring.is_fault()) return wiring;
    return detail::find_proof_net_fault_of_(graph);
}

// The first fault of the criterion of a sealed net: the fault of its wiring,
// or the fault of the body alone, with no second check of the wiring.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr NetFault find_proof_net_fault(SealedNet<Types, Kinds, Agents, Wires> const& sealed) noexcept {
    if (sealed.wiring_fault().is_fault()) return sealed.wiring_fault();
    return detail::find_proof_net_fault_of_(sealed.value());
}

// The criterion of Danos and Regnier, as a verdict with the text of its
// fault.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] consteval NetVerdict check_proof_net(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    return explain_fault(graph, find_proof_net_fault(graph));
}

// ── Vicious circles (Lafont 1990) ───────────────────────────────────────

// No cycle of agents in which the principal port of each agent meets an
// auxiliary port of the next agent.  No rule can fire in such a cycle.  Each
// agent points to the agent at the other end of its principal port when
// that end is an auxiliary port, so one walk with three colors finds each
// cycle.  The check reads the net as it is, and a reduction can make a
// vicious circle later.  The criterion of check_proof_net prevents that for
// a proof net (Lafont 1990).  Complexity: O(K + A + W).
namespace detail {

// The body of find_vicious_fault, on the arrays of a capacity class of a net
// with no fault of its wiring.
template <std::size_t Types, std::size_t Kinds, std::size_t AgentCapacity, std::size_t WireCapacity>
[[nodiscard]] constexpr NetFault find_vicious_fault_in_(Net<Types, Kinds, AgentCapacity, WireCapacity> const& graph,
                                                        AgentCount agents, WireCount wires) noexcept {
    constexpr std::uint32_t none = no_index;
    std::uint32_t const agent_count = count_of(agents);
    std::uint32_t next[AgentCapacity]{};
    for (std::uint32_t& slot : next)
        slot = none;
    for (std::uint32_t index = 0; index < count_of(wires); ++index) {
        PortRef const left = graph.wires[index].left;
        PortRef const right = graph.wires[index].right;
        bool const is_left_principal = left.port == kind_of_(graph, left.agent).principal;
        bool const is_right_principal = right.port == kind_of_(graph, right.agent).principal;
        if (is_left_principal && !is_right_principal) next[index_of(left.agent)] = index_of(right.agent);
        if (is_right_principal && !is_left_principal) next[index_of(right.agent)] = index_of(left.agent);
    }
    std::uint8_t color[AgentCapacity]{};
    for (std::uint32_t start = 0; start < agent_count; ++start) {
        if (color[start] != 0u) continue;
        std::uint32_t walk = start;
        while (walk != none && color[walk] == 0u) {
            color[walk] = 1;
            walk = next[walk];
        }
        if (walk != none && color[walk] == 1u) {
            NetFault fault{.code = NetFaultCode::ViciousCircle, .agent = AgentId{walk}};
            std::uint32_t member = walk;
            do {
                fault.path[fault.path_count] = AgentId{member};
                ++fault.path_count;
                member = next[member];
            } while (member != walk && fault.path_count < max_fault_path);
            fault.is_path_cut = member != walk;
            return fault;
        }
        for (std::uint32_t mark = start; mark != none && color[mark] == 1u; mark = next[mark])
            color[mark] = 2;
    }
    return NetFault{};
}

// The body of find_vicious_fault on a net with no fault of its wiring, on the
// arrays of its capacity class.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr NetFault find_vicious_fault_of_(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    constexpr AgentCount agents{static_cast<std::uint32_t>(Agents)};
    constexpr WireCount wires{static_cast<std::uint32_t>(Wires)};
    if constexpr (is_small_net<Agents, Wires>) {
        auto const small = copy_small_net_(graph);
        return find_vicious_fault_in_(small, agents, wires);
    } else {
        return find_vicious_fault_in_(graph, agents, wires);
    }
}

}  // namespace detail

// The first vicious circle above, or NoFault.  The fault names the first
// agents of the circle in path.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr NetFault find_vicious_fault(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    NetFault const wiring = find_wiring_fault(graph);
    if (wiring.is_fault()) return wiring;
    return detail::find_vicious_fault_of_(graph);
}

// The first vicious circle of a sealed net: the fault of its wiring, or the
// fault of the body alone, with no second check of the wiring.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr NetFault find_vicious_fault(SealedNet<Types, Kinds, Agents, Wires> const& sealed) noexcept {
    if (sealed.wiring_fault().is_fault()) return sealed.wiring_fault();
    return detail::find_vicious_fault_of_(sealed.value());
}

// The check of vicious circles, as a verdict with the text of its fault.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] consteval NetVerdict check_vicious(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    return explain_fault(graph, find_vicious_fault(graph));
}

// ── The dataflow ────────────────────────────────────────────────────────

// Each wire goes from a give port to a take port, and the directed graph of
// the agents has no cycle.  Each agent waits on its inputs before it gives
// its outputs, so this is the liveness check of a net that runs one time:
// the order exists exactly when the graph has no cycle.  Then each
// agent fires one time in each schedule.  A meet port has no direction, and
// the check refuses it.  Complexity: O(K + A + W), the sort of Kahn.
namespace detail {

// The most agents of a cycle of waits that a fault names.
inline constexpr std::uint8_t max_wait_path = 5;

// The body of find_dataflow_fault, on the arrays of a capacity class of a
// net with no fault of its wiring.
template <std::size_t Types, std::size_t Kinds, std::size_t AgentCapacity, std::size_t WireCapacity>
[[nodiscard]] constexpr NetFault find_dataflow_fault_in_(Net<Types, Kinds, AgentCapacity, WireCapacity> const& graph,
                                                         AgentCount agents, WireCount wires) noexcept {
    constexpr std::uint32_t none = no_index;
    std::uint32_t const agent_count = count_of(agents);
    std::uint32_t const wire_count = count_of(wires);
    // The producer and the consumer of each wire.
    std::uint32_t source[WireCapacity]{};
    std::uint32_t target[WireCapacity]{};
    std::uint32_t indegree[AgentCapacity]{};
    std::uint32_t out_first[AgentCapacity + 1]{};
    for (std::uint32_t index = 0; index < wire_count; ++index) {
        Wire const& wire = graph.wires[index];
        PortType const left = kind_of_(graph, wire.left.agent).port_types[wire.left.port];
        if (left.polarity == Polarity::Meet) return NetFault{.code = NetFaultCode::DataflowMeetPort, .wire = index};
        bool const is_left_source = left.polarity == Polarity::Give;
        source[index] = index_of(is_left_source ? wire.left.agent : wire.right.agent);
        target[index] = index_of(is_left_source ? wire.right.agent : wire.left.agent);
        ++indegree[target[index]];
        ++out_first[source[index] + 1];
    }
    for (std::uint32_t agent = 0; agent < agent_count; ++agent)
        out_first[agent + 1] += out_first[agent];
    std::uint32_t fill[AgentCapacity]{};
    for (std::uint32_t agent = 0; agent < agent_count; ++agent)
        fill[agent] = out_first[agent];
    std::uint32_t out_wire[WireCapacity]{};
    for (std::uint32_t index = 0; index < wire_count; ++index) {
        out_wire[fill[source[index]]] = index;
        ++fill[source[index]];
    }
    std::uint32_t order[AgentCapacity]{};
    std::uint32_t order_count = 0;
    for (std::uint32_t agent = 0; agent < agent_count; ++agent) {
        if (indegree[agent] == 0u) {
            order[order_count] = agent;
            ++order_count;
        }
    }
    for (std::uint32_t read = 0; read < order_count; ++read) {
        std::uint32_t const agent = order[read];
        for (std::uint32_t slot = out_first[agent]; slot < out_first[agent + 1]; ++slot) {
            std::uint32_t const next = target[out_wire[slot]];
            --indegree[next];
            if (indegree[next] == 0u) {
                order[order_count] = next;
                ++order_count;
            }
        }
    }
    if (order_count == agent_count) return NetFault{};

    // A cycle remains.  Each agent that the sort left has an input from
    // another such agent, so a walk back along these inputs repeats an agent.
    std::uint32_t into[AgentCapacity]{};
    for (std::uint32_t& slot : into)
        slot = none;
    for (std::uint32_t index = 0; index < wire_count; ++index) {
        if (indegree[source[index]] != 0u && indegree[target[index]] != 0u) into[target[index]] = index;
    }
    std::uint32_t walk = none;
    for (std::uint32_t agent = 0; agent < agent_count; ++agent) {
        if (indegree[agent] != 0u) {
            walk = agent;
            break;
        }
    }
    std::uint8_t is_seen[AgentCapacity]{};
    while (is_seen[walk] == 0u) {
        is_seen[walk] = 1;
        walk = source[into[walk]];
    }
    Wire const& closing = graph.wires[into[walk]];
    PortRef const input = target[into[walk]] == index_of(closing.left.agent) ? closing.left : closing.right;
    NetFault fault{.code = NetFaultCode::DataflowCycle, .agent = input.agent, .port = input.port};
    std::uint32_t member = walk;
    do {
        fault.path[fault.path_count] = AgentId{member};
        ++fault.path_count;
        member = source[into[member]];
    } while (member != walk && fault.path_count < max_wait_path);
    fault.is_path_cut = member != walk;
    return fault;
}

// The body of find_dataflow_fault on a net with no fault of its wiring, on
// the arrays of its capacity class.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr NetFault find_dataflow_fault_of_(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    constexpr AgentCount agents{static_cast<std::uint32_t>(Agents)};
    constexpr WireCount wires{static_cast<std::uint32_t>(Wires)};
    if constexpr (is_small_net<Agents, Wires>) {
        auto const small = copy_small_net_(graph);
        return find_dataflow_fault_in_(small, agents, wires);
    } else {
        return find_dataflow_fault_in_(graph, agents, wires);
    }
}

}  // namespace detail

// The first fault of the dataflow above, or NoFault.  A cycle of waits names
// the input where it closes in agent and port, and its first agents in path.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr NetFault find_dataflow_fault(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    NetFault const wiring = find_wiring_fault(graph);
    if (wiring.is_fault()) return wiring;
    return detail::find_dataflow_fault_of_(graph);
}

// The first fault of the dataflow of a sealed net: the fault of its wiring,
// or the fault of the body alone, with no second check of the wiring.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr NetFault find_dataflow_fault(SealedNet<Types, Kinds, Agents, Wires> const& sealed) noexcept {
    if (sealed.wiring_fault().is_fault()) return sealed.wiring_fault();
    return detail::find_dataflow_fault_of_(sealed.value());
}

// The dataflow check, as a verdict with the text of its fault.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] consteval NetVerdict check_dataflow(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    return explain_fault(graph, find_dataflow_fault(graph));
}

// ── The determinism ─────────────────────────────────────────────────────

// The determinism class of a net (THE DETERMINISM).
enum class NetDeterminism : std::uint8_t {
    // No agent of the net is a merge, so the net gives one result in each
    // schedule.
    Deterministic = 0,
    // An agent of the net has a kind that opens the merge door, so the result
    // can depend on the schedule.
    MergesThroughDoor = 1,
};

// The determinism class of a net.  An agent whose kind is past the signature
// is no merge, and check_wiring refuses it.  Complexity: O(A).
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr NetDeterminism determinism_of(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    for (KindId const kind : graph.agents) {
        if (index_of(kind) < Kinds && graph.signature.kinds[index_of(kind)].merge.is_open()) {
            return NetDeterminism::MergesThroughDoor;
        }
    }
    return NetDeterminism::Deterministic;
}

// ── The canonical form ──────────────────────────────────────────────────

// The hash of the canonical form of a net (THE CANONICAL FORM).
enum class NetHash : std::uint64_t {
};

// The canonical form of a net: the fault that leaves the net with no form,
// or the hash of the form.  The hash is 0 with a fault.
struct CanonicalNet {
    NetFault fault{};
    NetHash hash{};
};

namespace detail {

// The count of the ports of each kind of a signature.
template <std::size_t Kinds>
struct PortCounts {
    std::uint32_t of[Kinds]{};
};

// The count of the ports of each kind.  Complexity: O(K).
template <std::size_t Types, std::size_t Kinds>
[[nodiscard]] constexpr PortCounts<Kinds> port_counts_of_(NetSignature<Types, Kinds> const& signature) noexcept {
    PortCounts<Kinds> counts{};
    for (std::uint32_t kind = 0; kind < Kinds; ++kind)
        counts.of[kind] = port_count_of(signature.kinds[kind]);
    return counts;
}

// The canonical numbers of the agents of a net with no fault of its wiring
// (THE CANONICAL FORM).  number_of holds the number of each agent, or
// no_index, and agent_at the agent of each number.  peer holds the other end
// of the wire at each port as one slot: its agent times max_agent_ports plus
// its port.  A net has fewer than 2^28 agents, so a slot fits 32 bits.
// count is the count of the numbered agents.
template <std::size_t Agents>
struct CanonicalOrder {
    std::uint32_t number_of[Agents]{};
    std::uint32_t agent_at[Agents]{};
    std::uint32_t peer[Agents * max_agent_ports]{};
    std::uint32_t count = 0;
};

// Writes the canonical numbers of a net with no fault of its wiring: the
// boundary agents first in their order, then each inner agent at its first
// visit of the walk.  Each entry of the stack holds an agent times 16 plus
// its next port, so one word holds the two.  Complexity: O(K + A + W).
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
constexpr void write_canonical_order_(Net<Types, Kinds, Agents, Wires> const& graph,
                                      CanonicalOrder<Agents>& order) noexcept {
    constexpr auto ports = static_cast<std::uint32_t>(max_agent_ports);
    constexpr std::uint32_t port_bits = 4;
    constexpr std::uint32_t port_mask = (1u << port_bits) - 1u;
    PortCounts<Kinds> const counts = port_counts_of_(graph.signature);
    for (Wire const& wire : graph.wires) {
        std::uint32_t const left = index_of(wire.left.agent) * ports + wire.left.port;
        std::uint32_t const right = index_of(wire.right.agent) * ports + wire.right.port;
        order.peer[left] = right;
        order.peer[right] = left;
    }
    for (std::uint32_t& slot : order.number_of)
        slot = no_index;
    for (std::uint32_t agent = 0; agent < Agents; ++agent) {
        if (!kind_of_(graph, AgentId{agent}).is_boundary) continue;
        order.number_of[agent] = order.count;
        order.agent_at[order.count] = agent;
        ++order.count;
    }
    std::uint32_t const boundary_count = order.count;
    std::uint32_t stack[Agents]{};
    for (std::uint32_t root = 0; root < boundary_count; ++root) {
        stack[0] = order.agent_at[root] << port_bits;
        std::uint32_t depth = 1;
        while (depth != 0u) {
            std::uint32_t const top = stack[depth - 1];
            std::uint32_t const agent = top >> port_bits;
            std::uint32_t const port = top & port_mask;
            if (port == counts.of[index_of(graph.agents[agent])]) {
                --depth;
                continue;
            }
            stack[depth - 1] = top + 1u;
            std::uint32_t const next = order.peer[agent * ports + port] / ports;
            if (order.number_of[next] != no_index) continue;
            order.number_of[next] = order.count;
            order.agent_at[order.count] = next;
            ++order.count;
            stack[depth] = next << port_bits;
            ++depth;
        }
    }
}

// One word of the canonical form, for its hash.
enum class FormWord : std::uint64_t {
};

// The hash after one more word of the form: the step combine_ids of the one
// mixer of the tree (foundation/reflect/Mix.h).  Each bit of the result
// depends on each bit of the two, and the order of the words counts.
[[nodiscard]] constexpr NetHash fold_word(NetHash hash, FormWord word) noexcept {
    return NetHash{
        ::foundation::reflect::combine_ids(static_cast<std::uint64_t>(hash), static_cast<std::uint64_t>(word))};
}

// The hash after one more name of the form: the count of its characters,
// and then its characters, eight in each word.  Complexity: O(the size of
// the name).
[[nodiscard]] constexpr NetHash fold_name_(NetHash hash, NetName name) noexcept {
    hash = fold_word(hash, FormWord{name.size()});
    for (std::size_t first = 0; first < name.size(); first += 8u) {
        std::uint64_t word = 0;
        for (std::uint32_t offset = 0; offset < 8u; ++offset) {
            word |= std::uint64_t{static_cast<unsigned char>(name.at(first + offset))} << (8u * offset);
        }
        hash = fold_word(hash, FormWord{word});
    }
    return hash;
}

// The form of a kind (THE CANONICAL FORM): the kind, and the name of the
// type of each of its ports.  A form reads no index of its signature.
struct KindForm {
    AgentKind kind{};
    NetName port_type_names[max_agent_ports]{};

    // Two forms hold the same name, the same ports with the same names, type
    // names and polarities, the same principal port, par group and boundary
    // flag, and the same merge door with the same reason.  A closed door has
    // the empty reason.  The ports past the count of the kind are not part
    // of the form.  Complexity: O(the names of the kind).
    [[nodiscard]] friend constexpr bool operator==(KindForm const& left, KindForm const& right) noexcept {
        std::uint32_t const count = port_count_of(left.kind);
        if (!(left.kind.name == right.kind.name) || count != port_count_of(right.kind)) return false;
        for (std::uint32_t port = 0; port < count; ++port) {
            if (!(left.kind.port_names[port] == right.kind.port_names[port])) return false;
            if (!(left.port_type_names[port] == right.port_type_names[port])) return false;
            if (left.kind.port_types[port].polarity != right.kind.port_types[port].polarity) return false;
        }
        return left.kind.principal == right.kind.principal && left.kind.par_ports == right.kind.par_ports
            && left.kind.is_boundary == right.kind.is_boundary && left.kind.merge.reason() == right.kind.merge.reason();
    }
};

// The form of the kind at an index of a signature that check_signature
// admitted.  Complexity: O(1).
template <std::size_t Types, std::size_t Kinds>
[[nodiscard]] constexpr KindForm form_of_kind_(NetSignature<Types, Kinds> const& signature, KindId kind) noexcept {
    KindForm form{.kind = signature.kinds[index_of(kind)]};
    std::uint32_t const count = port_count_of(form.kind);
    for (std::uint32_t port = 0; port < count; ++port)
        form.port_type_names[port] = type_name_at_(signature, form.kind.port_types[port]);
    return form;
}

// The hash of the form of a kind: each part that operator== reads, in its
// order.  Complexity: O(the names of the kind).
[[nodiscard]] constexpr NetHash hash_kind_form_(KindForm const& form) noexcept {
    NetHash hash = fold_name_(NetHash{}, form.kind.name);
    std::uint32_t const count = port_count_of(form.kind);
    hash = fold_word(hash, FormWord{count});
    for (std::uint32_t port = 0; port < count; ++port) {
        hash = fold_name_(hash, form.kind.port_names[port]);
        hash = fold_name_(hash, form.port_type_names[port]);
        hash = fold_word(hash, FormWord{static_cast<std::uint64_t>(form.kind.port_types[port].polarity)});
    }
    std::uint64_t const shape = std::uint64_t{form.kind.principal} | (std::uint64_t{form.kind.par_ports} << 8u)
                              | (std::uint64_t{form.kind.is_boundary} << 16u);
    return fold_name_(fold_word(hash, FormWord{shape}), form.kind.merge.reason());
}

// The canonical form of a net with no fault of its wiring: the fault
// AgentWithoutBoundaryPath, or the hash of its form.  The entry of each
// number folds the hash of the form of the kind of its agent, and one word
// for each port: the number of the peer times 8 plus the port of the peer.
// The entry of number n folds into chain n % 4, so four chains of folds run
// side by side.  The hash folds the count of the agents and the four chains
// in their order.  Complexity: O(A + W), and the names of the K kinds.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr CanonicalNet find_canonical_form_of_(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    constexpr auto ports = static_cast<std::uint32_t>(max_agent_ports);
    CanonicalOrder<Agents> order{};
    write_canonical_order_(graph, order);
    for (std::uint32_t agent = 0; agent < Agents; ++agent) {
        if (order.number_of[agent] != no_index) continue;
        return CanonicalNet{.fault = NetFault{.code = NetFaultCode::AgentWithoutBoundaryPath, .agent = AgentId{agent}}};
    }
    PortCounts<Kinds> const counts = port_counts_of_(graph.signature);
    NetHash kind_hash[Kinds]{};
    for (std::uint32_t kind = 0; kind < Kinds; ++kind)
        kind_hash[kind] = hash_kind_form_(form_of_kind_(graph.signature, KindId{kind}));
    auto const fold_entry = [&](NetHash chain, std::uint32_t number) constexpr noexcept {
        std::uint32_t const agent = order.agent_at[number];
        std::uint32_t const kind = index_of(graph.agents[agent]);
        chain = fold_word(chain, FormWord{static_cast<std::uint64_t>(kind_hash[kind])});
        for (std::uint32_t port = 0; port < counts.of[kind]; ++port) {
            std::uint32_t const peer = order.peer[agent * ports + port];
            std::uint64_t const word = (std::uint64_t{order.number_of[peer / ports]} << 3u) | (peer % ports);
            chain = fold_word(chain, FormWord{word});
        }
        return chain;
    };
    NetHash first{};
    NetHash second{};
    NetHash third{};
    NetHash fourth{};
    std::uint32_t number = 0;
    for (; number + 4u <= Agents; number += 4u) {
        first = fold_entry(first, number);
        second = fold_entry(second, number + 1u);
        third = fold_entry(third, number + 2u);
        fourth = fold_entry(fourth, number + 3u);
    }
    if (number < Agents) first = fold_entry(first, number);
    if (number + 1u < Agents) second = fold_entry(second, number + 1u);
    if (number + 2u < Agents) third = fold_entry(third, number + 2u);
    NetHash hash = fold_word(NetHash{}, FormWord{Agents});
    hash = fold_word(hash, FormWord{static_cast<std::uint64_t>(first)});
    hash = fold_word(hash, FormWord{static_cast<std::uint64_t>(second)});
    hash = fold_word(hash, FormWord{static_cast<std::uint64_t>(third)});
    return CanonicalNet{.hash = fold_word(hash, FormWord{static_cast<std::uint64_t>(fourth)})};
}

// Two nets of one type with no fault of their wiring are the same net.  The
// two arguments can change places with no change of meaning.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr bool is_same_wired_net_(Net<Types, Kinds, Agents, Wires> const& left,
                                                Net<Types, Kinds, Agents, Wires> const& right) noexcept {
    constexpr auto ports = static_cast<std::uint32_t>(max_agent_ports);
    CanonicalOrder<Agents> one{};
    CanonicalOrder<Agents> other{};
    write_canonical_order_(left, one);
    write_canonical_order_(right, other);
    if (one.count != Agents || other.count != Agents) return false;
    PortCounts<Kinds> const counts = port_counts_of_(left.signature);
    // A kind of the left net, and a kind of the right net with the same form.
    std::uint32_t same_kind[Kinds]{};
    for (std::uint32_t& match : same_kind)
        match = no_index;
    for (std::uint32_t number = 0; number < Agents; ++number) {
        std::uint32_t const first = one.agent_at[number];
        std::uint32_t const second = other.agent_at[number];
        std::uint32_t const first_kind = index_of(left.agents[first]);
        if (same_kind[first_kind] != index_of(right.agents[second])) {
            if (!(form_of_kind_(left.signature, left.agents[first])
                  == form_of_kind_(right.signature, right.agents[second]))) {
                return false;
            }
            if (same_kind[first_kind] == no_index) same_kind[first_kind] = index_of(right.agents[second]);
        }
        for (std::uint32_t port = 0; port < counts.of[first_kind]; ++port) {
            std::uint32_t const one_peer = one.peer[first * ports + port];
            std::uint32_t const other_peer = other.peer[second * ports + port];
            if (one.number_of[one_peer / ports] != other.number_of[other_peer / ports]) return false;
            if (one_peer % ports != other_peer % ports) return false;
        }
    }
    return true;
}

}  // namespace detail

// The canonical form of a net: the first fault of its wiring, the fault
// AgentWithoutBoundaryPath, or the hash of its form (THE CANONICAL FORM).
// Complexity: O(A + W), and the names of the K kinds.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr CanonicalNet find_canonical_form(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    NetFault const wiring = find_wiring_fault(graph);
    if (wiring.is_fault()) return CanonicalNet{.fault = wiring};
    return detail::find_canonical_form_of_(graph);
}

// The canonical form of a sealed net: the fault of its wiring, or the form
// of the body alone, with no second check of the wiring.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr CanonicalNet
find_canonical_form(SealedNet<Types, Kinds, Agents, Wires> const& sealed) noexcept {
    if (sealed.wiring_fault().is_fault()) return CanonicalNet{.fault = sealed.wiring_fault()};
    return detail::find_canonical_form_of_(sealed.value());
}

// The check of the canonical form, as a verdict with the text of its fault.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] consteval NetVerdict check_canonical_form(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    return explain_fault(graph, find_canonical_form(graph).fault);
}

// Two nets of one type are the same net (THE CANONICAL FORM): each has a
// canonical form, and the two forms are equal entry by entry.  A net with no
// form is the same as no net.  The kinds compare by their forms, so the two
// signatures can list the kinds and the types in other orders.  The two
// arguments can change places with no change of meaning.
// Complexity: O(K + A + W), and the names of each pair of kinds that two
// agents of one number have.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr bool is_same_net(Net<Types, Kinds, Agents, Wires> const& left,
                                         Net<Types, Kinds, Agents, Wires> const& right) noexcept {
    if (find_wiring_fault(left).is_fault() || find_wiring_fault(right).is_fault()) return false;
    return detail::is_same_wired_net_(left, right);
}

// Two sealed nets of one type are the same net, with no second check of the
// wiring of either.  The two arguments can change places with no change of
// meaning.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr bool is_same_net(SealedNet<Types, Kinds, Agents, Wires> const& left,
                                         SealedNet<Types, Kinds, Agents, Wires> const& right) noexcept {
    if (left.wiring_fault().is_fault() || right.wiring_fault().is_fault()) return false;
    return detail::is_same_wired_net_(left.value(), right.value());
}

}  // namespace foundation::algebra::net
