#pragma once

// The interaction nets of the base: a plain constexpr graph of agents, ports
// and wires, and the consteval checks over it.
//
//   NetName                 the name of a type, an agent kind or a port: a
//                           string literal, or a static string of reflection
//   PortType                a type and its polarity: give(t), take(t), meet(t)
//   AgentKind               the ports of an agent: a name and a type for each
//                           port, the principal port, a par group, and the
//                           boundary flag
//   NetSignature<T, K>      the T type names and the K agent kinds
//   Net<T, K, A, W>         a signature, the kind of each of A agents, and W
//                           wires.  Each wire joins two ports
//   NetVerdict              the answer of a check: admitted, or a refusal with
//                           one line of text
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
//   queues, a loop between two stages (probe E4).  The priority check of
//   foundation/algebra/net/Priority.h decides those.
//
//   A value with two uses needs an explicit copy agent.  So a scan net, where
//   one partial sum feeds an output and a later combine, and a sorting
//   network, whose comparators take two values and give two, have cycles
//   through their copies and their comparators.  Such a net passes
//   check_wiring, check_dataflow and check_vicious.  check_dataflow is the
//   priority check with one priority for each agent: each agent waits on its
//   inputs, so an order exists exactly when the directed wires form no cycle.
//
//   No structural check sees the order of the operands of an agent: a net and
//   the same net with two operands swapped pass the same checks.  The match
//   of an implementation and its net runs the implementation on the agents of
//   the net (foundation/core/Reduce.h, Scan.h, Sort.h).
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
// THE COSTS
//   P is the count of the ports, A of the agents and W of the wires.
//     check_signature   O(K) for K kinds
//     check_wiring      O(A + W)
//     check_forest      O(W a(A)), one union-find over the agents
//     check_proof_net   O(P log P), the contraction of Danos with a union-find
//                       and a ready queue of the pars, as probe E4 measured
//     check_vicious     O(A + W)
//     check_dataflow    O(A + W), the topological sort of Kahn
//   Each check is a template on the sizes of the net, and it reads arrays of
//   those sizes: a read of an array costs less in constant evaluation than a
//   read through a pointer.  The texts of the refusals come from helpers that
//   are no templates, so a net of a new size makes a small instance.  This
//   header includes no other header of the base and no <meta>, so a light
//   header can include it.
//
// SOURCES
//   Lafont, Interaction nets, POPL 1990.  Lafont, Interaction combinators,
//   Information and Computation 137, 1997.  Danos and Regnier, The structure
//   of multiplicatives, 1989, and the contraction criterion of Danos 1990.
//   Jacobs, Hinrichsen and Krebbers, Deadlock-free separation logic, POPL
//   2024, for the forest condition.

#include <cstddef>
#include <cstdint>

namespace foundation::algebra::net {

namespace detail {

// The consteval constructor of a NetName calls this function when a literal
// has no terminating zero.  It has no definition, so the call is no constant
// expression, and the error of the compiler names the rule.
void net_name_has_no_terminating_zero() noexcept;

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

// ── Agents and wires ────────────────────────────────────────────────────

// The most ports of one agent.  A par group is a mask of 8 bits.
inline constexpr std::size_t max_agent_ports = 8;

// The ports of an agent.  The ports are the names before the first empty
// name, so a kind of two ports names two.  par_ports holds one bit for each
// port of the par group: no port, or two or more auxiliary ports.  A
// boundary kind has one port, and no rule reads it.
struct AgentKind {
    NetName name{};
    NetName port_names[max_agent_ports]{};
    PortType port_types[max_agent_ports]{};
    std::uint8_t principal = 0;
    std::uint8_t par_ports = 0;
    bool is_boundary = false;
    std::uint8_t reserved[5]{};
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

    // A refusal whose text starts with opening, a text of static storage.
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
    // no instance of the constructor template of NetName.
    constexpr NetVerdict& put(char const* text) noexcept {
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
[[nodiscard]] constexpr AgentKind const& kind_of(Net<Types, Kinds, Agents, Wires> const& graph,
                                                 AgentId agent) noexcept {
    return graph.signature.kinds[index_of(graph.agents[index_of(agent)])];
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

// "port 'PORT' of agent 'NAME' (node N)"
constexpr void put_port(NetVerdict& text, AgentKind const& kind, std::uint32_t node, std::uint32_t port) noexcept {
    text.put("port '").put_name(kind.port_names[port]).put("' of ");
    put_agent(text, kind, node);
}

// A union-find over Count vertices, with the union by size and the halving
// of paths.  A find costs O(a(Count)) amortized.
template <std::size_t Count>
struct UnionFind {
    std::uint32_t parent[Count]{};
    std::uint32_t size[Count]{};

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

// Each type has a name.  Each kind has a name and at least one port, its
// ports are the names before the first empty name, each port names a type
// of the signature, the principal port is one of its ports, the par group
// holds no port or two or more auxiliary ports, and a boundary kind has one
// port and no par group.  Complexity: O(T + K).
template <std::size_t Types, std::size_t Kinds>
[[nodiscard]] consteval NetVerdict check_signature(NetSignature<Types, Kinds> const& signature) noexcept {
    for (std::uint32_t type = 0; type < Types; ++type) {
        if (signature.type_names[type].is_empty()) {
            return NetVerdict::refuse("net: type ")
                .put_number(type)
                .put(" of the signature has no name.  Each type has a name.");
        }
    }
    for (std::uint32_t index = 0; index < Kinds; ++index) {
        AgentKind const& kind = signature.kinds[index];
        if (kind.name.is_empty()) {
            return NetVerdict::refuse("net: kind ").put_number(index).put(" of the signature has no name.");
        }
        std::uint32_t const count = detail::port_count_of(kind);
        if (count == 0) {
            return NetVerdict::refuse("net: agent kind '")
                .put_name(kind.name)
                .put("' has no port.  An agent has one principal port.");
        }
        for (std::uint32_t port = count; port < max_agent_ports; ++port) {
            if (!kind.port_names[port].is_empty()) {
                return NetVerdict::refuse("net: port '")
                    .put_name(kind.port_names[port])
                    .put("' of agent kind '")
                    .put_name(kind.name)
                    .put("' comes after a port with no name.  The ports of a kind are the names before the first "
                         "empty name.");
            }
        }
        for (std::uint32_t port = 0; port < count; ++port) {
            if (index_of(kind.port_types[port].type) >= Types) {
                return NetVerdict::refuse("net: port '")
                    .put_name(kind.port_names[port])
                    .put("' of agent kind '")
                    .put_name(kind.name)
                    .put("' has the type ")
                    .put_number(index_of(kind.port_types[port].type))
                    .put(", and the signature has ")
                    .put_number(Types)
                    .put(" types.");
            }
        }
        if (kind.principal >= count) {
            return NetVerdict::refuse("net: the principal port ")
                .put_number(kind.principal)
                .put(" of agent kind '")
                .put_name(kind.name)
                .put("' is past its ")
                .put_number(count)
                .put(" ports.");
        }
        auto const group = static_cast<std::uint32_t>(kind.par_ports);
        if ((group >> count) != 0u) {
            return NetVerdict::refuse("net: the par group of agent kind '")
                .put_name(kind.name)
                .put("' names a port past its ")
                .put_number(count)
                .put(" ports.");
        }
        if (detail::is_par_port(kind, kind.principal)) {
            return NetVerdict::refuse("net: the par group of agent kind '")
                .put_name(kind.name)
                .put("' holds its principal port '")
                .put_name(kind.port_names[kind.principal])
                .put("'.  A par group holds auxiliary ports.");
        }
        if (detail::count_bits(group) == 1u) {
            return NetVerdict::refuse("net: the par group of agent kind '")
                .put_name(kind.name)
                .put("' holds one port.  A par group holds two or more ports.");
        }
        if (kind.is_boundary && (count != 1u || group != 0u)) {
            return NetVerdict::refuse("net: the boundary kind '")
                .put_name(kind.name)
                .put("' has ")
                .put_number(count)
                .put(" ports.  A boundary agent stands for one free port of the net, so it has one port.");
        }
    }
    return NetVerdict{};
}

// ── Linearity and types ─────────────────────────────────────────────────

// Each agent has a kind of the signature, each wire names a port that its
// agent has, each port has exactly one wire, and the two ports of a wire have
// dual types.  Complexity: O(K + A + W).
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] consteval NetVerdict check_wiring(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    NetVerdict const signature = check_signature(graph.signature);
    if (!signature.is_admitted()) return signature;
    std::uint32_t port_count[Kinds]{};
    for (std::uint32_t kind = 0; kind < Kinds; ++kind)
        port_count[kind] = detail::port_count_of(graph.signature.kinds[kind]);
    for (std::uint32_t agent = 0; agent < Agents; ++agent) {
        if (index_of(graph.agents[agent]) >= Kinds) {
            return NetVerdict::refuse("net: node ")
                .put_number(agent)
                .put(" has the kind ")
                .put_number(index_of(graph.agents[agent]))
                .put(", and the signature has ")
                .put_number(Kinds)
                .put(" kinds.");
        }
    }
    std::uint8_t uses[Agents * max_agent_ports]{};
    for (std::uint32_t index = 0; index < Wires; ++index) {
        Wire const& wire = graph.wires[index];
        for (std::uint32_t side = 0; side < 2u; ++side) {
            PortRef const end = side == 0u ? wire.left : wire.right;
            std::uint32_t const node = index_of(end.agent);
            if (node >= Agents) {
                return NetVerdict::refuse("net: wire ")
                    .put_number(index)
                    .put(" names node ")
                    .put_number(node)
                    .put(", and the net has ")
                    .put_number(Agents)
                    .put(" agents.");
            }
            std::uint32_t const ports = port_count[index_of(graph.agents[node])];
            if (end.port >= ports) {
                NetVerdict refusal = NetVerdict::refuse("net: wire ");
                refusal.put_number(index).put(" names port ").put_number(end.port).put(" of ");
                detail::put_agent(refusal, detail::kind_of(graph, end.agent), node);
                refusal.put(", which has ").put_number(ports).put(" ports.");
                return refusal;
            }
            std::uint8_t& count = uses[node * max_agent_ports + end.port];
            if (count != 0u) {
                NetVerdict refusal = NetVerdict::refuse("net: ");
                detail::put_port(refusal, detail::kind_of(graph, end.agent), node, end.port);
                refusal.put(" has a second wire, wire ").put_number(index).put(".  Each port has exactly one wire.");
                return refusal;
            }
            count = 1;
        }
        AgentKind const& left_kind = detail::kind_of(graph, wire.left.agent);
        AgentKind const& right_kind = detail::kind_of(graph, wire.right.agent);
        PortType const left = left_kind.port_types[wire.left.port];
        PortType const right = right_kind.port_types[wire.right.port];
        if (!(right == dual_of(left))) {
            NetVerdict refusal = NetVerdict::refuse("net: wire ");
            refusal.put_number(index).put(" joins ");
            detail::put_port(refusal, left_kind, index_of(wire.left.agent), wire.left.port);
            refusal.put(" of type ");
            detail::put_type(refusal, graph.signature.type_names[index_of(left.type)], left.polarity);
            refusal.put(" with ");
            detail::put_port(refusal, right_kind, index_of(wire.right.agent), wire.right.port);
            refusal.put(" of type ");
            detail::put_type(refusal, graph.signature.type_names[index_of(right.type)], right.polarity);
            refusal.put(", which is not its dual.");
            return refusal;
        }
    }
    for (std::uint32_t agent = 0; agent < Agents; ++agent) {
        std::uint32_t const count = port_count[index_of(graph.agents[agent])];
        for (std::uint32_t port = 0; port < count; ++port) {
            if (uses[agent * max_agent_ports + port] == 0u) {
                NetVerdict refusal = NetVerdict::refuse("net: ");
                detail::put_port(refusal, detail::kind_of(graph, AgentId{agent}), agent, port);
                refusal.put(" has no wire.  Each port has exactly one wire.");
                return refusal;
            }
        }
    }
    return NetVerdict{};
}

// ── The forest ──────────────────────────────────────────────────────────

// The agents and the wires form a forest: each wire joins two trees, and no
// wire closes a cycle.  A net can have several trees, because two separate
// compositions do not wait on each other.  This is the condition of
// LinearActris for threads and channels.  A par group is refused: a net with
// a par group takes check_proof_net.  Complexity: O(K + A + W a(A)).
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] consteval NetVerdict check_forest(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    NetVerdict const wiring = check_wiring(graph);
    if (!wiring.is_admitted()) return wiring;
    for (std::uint32_t agent = 0; agent < Agents; ++agent) {
        AgentKind const& kind = detail::kind_of(graph, AgentId{agent});
        if (kind.par_ports == 0u) continue;
        NetVerdict refusal = NetVerdict::refuse("net: ");
        detail::put_agent(refusal, kind, agent);
        refusal.put(" has a par group, and check_forest reads no par group.  Use check_proof_net.");
        return refusal;
    }
    detail::UnionFind<Agents> classes{};
    for (std::uint32_t index = 0; index < Wires; ++index) {
        Wire const& wire = graph.wires[index];
        std::uint32_t const left = classes.find(index_of(wire.left.agent));
        std::uint32_t const right = classes.find(index_of(wire.right.agent));
        if (left == right) {
            NetVerdict refusal = NetVerdict::refuse("net: wire ");
            refusal.put_number(index).put(" from ");
            detail::put_port(refusal, detail::kind_of(graph, wire.left.agent), index_of(wire.left.agent),
                             wire.left.port);
            refusal.put(" to ");
            detail::put_port(refusal, detail::kind_of(graph, wire.right.agent), index_of(wire.right.agent),
                             wire.right.port);
            refusal.put(" closes a cycle.  The agents and the wires do not form a forest.");
            return refusal;
        }
        static_cast<void>(classes.join_roots(left, right));
    }
    return NetVerdict{};
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
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] consteval NetVerdict check_proof_net(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    NetVerdict const wiring = check_wiring(graph);
    if (!wiring.is_admitted()) return wiring;
    constexpr std::size_t vertices = Agents + Wires;
    constexpr std::uint32_t none = detail::no_index;
    std::uint32_t wire_at[Agents * max_agent_ports]{};
    for (std::uint32_t index = 0; index < Wires; ++index) {
        Wire const& wire = graph.wires[index];
        wire_at[index_of(wire.left.agent) * max_agent_ports + wire.left.port] = index;
        wire_at[index_of(wire.right.agent) * max_agent_ports + wire.right.port] = index;
    }
    detail::UnionFind<vertices> classes{};
    std::uint32_t class_count = static_cast<std::uint32_t>(vertices);

    // The unpaired edges.
    for (std::uint32_t agent = 0; agent < Agents; ++agent) {
        AgentKind const& kind = detail::kind_of(graph, AgentId{agent});
        std::uint32_t const count = detail::port_count_of(kind);
        for (std::uint32_t port = 0; port < count; ++port) {
            if (detail::is_par_port(kind, port)) continue;
            std::uint32_t const wire_vertex =
                static_cast<std::uint32_t>(Agents) + wire_at[agent * max_agent_ports + port];
            std::uint32_t const left = classes.find(agent);
            std::uint32_t const right = classes.find(wire_vertex);
            if (left == right) {
                NetVerdict refusal = NetVerdict::refuse("net: a cycle closes at ");
                detail::put_port(refusal, kind, agent, port);
                refusal.put(".  Each switching of the net keeps this cycle, so the net is no proof net.");
                return refusal;
            }
            static_cast<void>(classes.join_roots(left, right));
            --class_count;
        }
    }

    // The pars.  Each par has one entry in the list of the class of each of
    // its premise wires.  A par is ready when all its premises are in one
    // class.
    std::uint32_t par_agent[Agents]{};
    std::uint32_t par_premise[Agents][max_agent_ports]{};
    std::uint32_t par_width[Agents]{};
    bool is_par_done[Agents]{};
    std::uint32_t par_count = 0;
    for (std::uint32_t agent = 0; agent < Agents; ++agent) {
        AgentKind const& kind = detail::kind_of(graph, AgentId{agent});
        if (kind.par_ports == 0u) continue;
        par_agent[par_count] = agent;
        std::uint32_t const count = detail::port_count_of(kind);
        for (std::uint32_t port = 0; port < count; ++port) {
            if (!detail::is_par_port(kind, port)) continue;
            par_premise[par_count][par_width[par_count]] =
                static_cast<std::uint32_t>(Agents) + wire_at[agent * max_agent_ports + port];
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

    std::uint32_t list_head[vertices]{};
    std::uint32_t list_tail[vertices]{};
    std::uint32_t list_length[vertices]{};
    for (std::uint32_t vertex = 0; vertex < vertices; ++vertex) {
        list_head[vertex] = none;
        list_tail[vertex] = none;
    }
    std::uint32_t entry_par[max_agent_ports * Agents]{};
    std::uint32_t entry_next[max_agent_ports * Agents]{};
    std::uint32_t entry_count = 0;
    std::uint32_t ready[Agents]{};
    std::uint32_t ready_count = 0;
    bool is_queued[Agents]{};

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
        if (own == premises) {
            NetVerdict refusal = NetVerdict::refuse("net: ");
            detail::put_agent(refusal, detail::kind_of(graph, AgentId{agent}), agent);
            refusal.put(" is a par whose ports reach the agent again.  A switching of it closes a cycle.");
            return refusal;
        }
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
        if (is_par_done[par]) continue;
        NetVerdict refusal = NetVerdict::refuse("net: ");
        detail::put_agent(refusal, detail::kind_of(graph, AgentId{par_agent[par]}), par_agent[par]);
        refusal.put(" is a par whose ports never meet.  A switching of it leaves the net in two parts.");
        return refusal;
    }
    if (class_count != 1u) {
        std::uint32_t const root = classes.find(0);
        std::uint32_t apart = 0;
        for (std::uint32_t agent = 1; agent < Agents; ++agent) {
            if (classes.find(agent) != root) {
                apart = agent;
                break;
            }
        }
        NetVerdict refusal = NetVerdict::refuse("net: the net is not connected.  ");
        detail::put_agent(refusal, detail::kind_of(graph, AgentId{apart}), apart);
        refusal.put(" has no path to ");
        detail::put_agent(refusal, detail::kind_of(graph, AgentId{0}), 0);
        refusal.put(".");
        return refusal;
    }
    return NetVerdict{};
}

// ── Vicious circles (Lafont 1990) ───────────────────────────────────────

// No cycle of agents in which the principal port of each agent meets an
// auxiliary port of the next agent.  No rule can fire in such a cycle.  Each
// agent points to the agent at the other end of its principal port when
// that end is an auxiliary port, so one walk with three colors finds each
// cycle.  The check reads the net as it is, and a reduction can make a
// vicious circle later.  The criterion of check_proof_net prevents that for
// a proof net (Lafont 1990).  Complexity: O(K + A + W).
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] consteval NetVerdict check_vicious(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    NetVerdict const wiring = check_wiring(graph);
    if (!wiring.is_admitted()) return wiring;
    constexpr std::uint32_t none = detail::no_index;
    std::uint32_t next[Agents]{};
    for (std::uint32_t& slot : next)
        slot = none;
    for (std::uint32_t index = 0; index < Wires; ++index) {
        PortRef const left = graph.wires[index].left;
        PortRef const right = graph.wires[index].right;
        bool const is_left_principal = left.port == detail::kind_of(graph, left.agent).principal;
        bool const is_right_principal = right.port == detail::kind_of(graph, right.agent).principal;
        if (is_left_principal && !is_right_principal) next[index_of(left.agent)] = index_of(right.agent);
        if (is_right_principal && !is_left_principal) next[index_of(right.agent)] = index_of(left.agent);
    }
    std::uint8_t color[Agents]{};
    for (std::uint32_t start = 0; start < Agents; ++start) {
        if (color[start] != 0u) continue;
        std::uint32_t walk = start;
        while (walk != none && color[walk] == 0u) {
            color[walk] = 1;
            walk = next[walk];
        }
        if (walk != none && color[walk] == 1u) {
            NetVerdict refusal = NetVerdict::refuse("net: vicious circle: ");
            std::uint32_t member = walk;
            std::uint32_t shown = 0;
            do {
                if (shown != 0u) refusal.put(" -> ");
                detail::put_agent(refusal, detail::kind_of(graph, AgentId{member}), member);
                member = next[member];
                ++shown;
            } while (member != walk && shown < 6u);
            if (member != walk) refusal.put(" -> ...");
            refusal.put(".  Each principal port meets an auxiliary port, so no rule can fire.");
            return refusal;
        }
        for (std::uint32_t mark = start; mark != none && color[mark] == 1u; mark = next[mark])
            color[mark] = 2;
    }
    return NetVerdict{};
}

// ── The dataflow ────────────────────────────────────────────────────────

// Each wire goes from a give port to a take port, and the directed graph of
// the agents has no cycle.  Each agent waits on its inputs before it gives
// its outputs, so this is the priority check with one priority for each
// agent: the order exists exactly when the graph has no cycle.  Then each
// agent fires one time in each schedule.  A meet port has no direction, and
// the check refuses it.  Complexity: O(K + A + W), the sort of Kahn.
template <std::size_t Types, std::size_t Kinds, std::size_t Agents, std::size_t Wires>
[[nodiscard]] consteval NetVerdict check_dataflow(Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    NetVerdict const wiring = check_wiring(graph);
    if (!wiring.is_admitted()) return wiring;
    constexpr std::uint32_t none = detail::no_index;
    // The producer and the consumer of each wire.
    std::uint32_t source[Wires]{};
    std::uint32_t target[Wires]{};
    std::uint32_t indegree[Agents]{};
    std::uint32_t out_first[Agents + 1]{};
    for (std::uint32_t index = 0; index < Wires; ++index) {
        Wire const& wire = graph.wires[index];
        AgentKind const& left_kind = detail::kind_of(graph, wire.left.agent);
        PortType const left = left_kind.port_types[wire.left.port];
        if (left.polarity == Polarity::Meet) {
            NetVerdict refusal = NetVerdict::refuse("net: wire ");
            refusal.put_number(index).put(" joins ");
            detail::put_port(refusal, left_kind, index_of(wire.left.agent), wire.left.port);
            refusal.put(" of the type ");
            detail::put_type(refusal, graph.signature.type_names[index_of(left.type)], left.polarity);
            refusal.put(", which has no direction.  A dataflow takes give and take ports.");
            return refusal;
        }
        bool const is_left_source = left.polarity == Polarity::Give;
        source[index] = index_of(is_left_source ? wire.left.agent : wire.right.agent);
        target[index] = index_of(is_left_source ? wire.right.agent : wire.left.agent);
        ++indegree[target[index]];
        ++out_first[source[index] + 1];
    }
    for (std::uint32_t agent = 0; agent < Agents; ++agent)
        out_first[agent + 1] += out_first[agent];
    std::uint32_t fill[Agents]{};
    for (std::uint32_t agent = 0; agent < Agents; ++agent)
        fill[agent] = out_first[agent];
    std::uint32_t out_wire[Wires]{};
    for (std::uint32_t index = 0; index < Wires; ++index) {
        out_wire[fill[source[index]]] = index;
        ++fill[source[index]];
    }
    std::uint32_t order[Agents]{};
    std::uint32_t order_count = 0;
    for (std::uint32_t agent = 0; agent < Agents; ++agent) {
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
    if (order_count == Agents) return NetVerdict{};

    // A cycle remains.  Each agent that the sort left has an input from
    // another such agent, so a walk back along these inputs repeats an agent.
    std::uint32_t into[Agents]{};
    for (std::uint32_t& slot : into)
        slot = none;
    for (std::uint32_t index = 0; index < Wires; ++index) {
        if (indegree[source[index]] != 0u && indegree[target[index]] != 0u) into[target[index]] = index;
    }
    std::uint32_t walk = none;
    for (std::uint32_t agent = 0; agent < Agents; ++agent) {
        if (indegree[agent] != 0u) {
            walk = agent;
            break;
        }
    }
    std::uint8_t is_seen[Agents]{};
    while (is_seen[walk] == 0u) {
        is_seen[walk] = 1;
        walk = source[into[walk]];
    }
    Wire const& closing = graph.wires[into[walk]];
    PortRef const input = target[into[walk]] == index_of(closing.left.agent) ? closing.left : closing.right;
    NetVerdict refusal = NetVerdict::refuse("net: a cycle of waits closes at ");
    detail::put_port(refusal, detail::kind_of(graph, input.agent), index_of(input.agent), input.port);
    refusal.put(": ");
    std::uint32_t member = walk;
    std::uint32_t shown = 0;
    do {
        if (shown != 0u) refusal.put(" <- ");
        detail::put_agent(refusal, detail::kind_of(graph, AgentId{member}), member);
        member = source[into[member]];
        ++shown;
    } while (member != walk && shown < 5u);
    if (member != walk) refusal.put(" <- ...");
    refusal.put(".  Each agent waits on its inputs, so no agent of the cycle starts.");
    return refusal;
}

}  // namespace foundation::algebra::net
