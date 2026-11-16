#pragma once

// The rule tables of interaction nets, and the consteval check of a table.
//
//   RightSide               the agents and the wires that replace a pair
//   Rule                    a name, the two kinds of the pair, and the right
//                           side
//   allow_growth(rule, reason)
//                           the growth door: the only way to a rule whose
//                           right side has more agents than its pair
//   RuleTable<T, K, R>      a signature and R rules
//   check_rules(table)      the check of the table, with its class of growth
//   RuleVerdict             the answer: admitted or a refusal, and the class
//
// A RULE
//   A rule fires where the principal ports of an agent of the left kind and
//   an agent of the right kind meet.  The right side replaces the two agents.
//   The interface of the pair is its free ports: the auxiliary ports of the
//   left kind, in port order, and then those of the right kind.  A wire of
//   the right side joins two ends.  An end is a port of a new agent
//   (point_at_agent), or a port of the interface (point_at_interface).
//
// THE CHECKS
//   check_rules refuses, with one line that names the rule, the agent and the
//   port:
//   - a rule whose principal ports are not duals, because it never fires
//   - a rule that reads a boundary agent, or that makes one
//   - a second rule for one pair, in each order of the pair, because at most
//     one rule for each pair makes each step commute with each other step
//     (Lafont 1990, Lafont 1997)
//   - a pair of kinds with dual principal ports and no rule, because a net
//     with that pair stops
//   - a right side in which a port of a new agent or a port of the interface
//     has no wire or two wires, because the right side keeps each port
//   - a wire of the right side that changes a type.  A port of a new agent
//     that takes the place of an interface port has the type of that port.
//     Two ports of new agents, or two ports of the interface, have dual types
//   - a new agent with no path to the interface, which no other agent reaches
//   - a rule for a pair of one kind whose right side changes when the two
//     agents change places.  The pair has no order, so such a rule gives two
//     results for one net (an inference from the commutation of the steps)
//   - a right side of more than 2 agents with no growth door, and a growth
//     door on a rule that does not grow.
//
// THE CLASS OF GROWTH
//   A rule that adds agents allocates: the commutation of a duplicator and a
//   constructor makes 4 agents from 2 (Lafont 1997), and HVM2 asks for 4 new
//   nodes in that rule.  The class of an admitted table:
//     Shrinks           each right side has at most 1 agent.  Each step
//                       removes an agent, so a reduction of a net of A agents
//                       ends after at most A steps
//     AllocatesNothing  each right side has at most 2 agents.  The count of
//                       the agents of a net never increases, so a reduction
//                       reuses the slots of each pair and allocates nothing
//     Grows             a rule went through the growth door.  The verdict
//                       counts the growing rules and the most agents that one
//                       of them adds
//   A user that must not allocate writes
//   static_assert(verdict.allocates_nothing(), verdict).
//
// THE COSTS
//   R rules and K kinds.  check_rules is O(K^2 + R * E) for E the ends of a
//   right side, with a table of K * K pairs.  Probe E4 measured 114 M compile
//   instructions for a table that reflection read, most of it in the vector of
//   each lookup of an annotation.  This header reads plain arrays.

#include <foundation/algebra/net/Net.h>

#include <cstddef>
#include <cstdint>

namespace foundation::algebra::net {

// The most agents and wires of one right side.
inline constexpr std::size_t max_rule_agents = 8;
inline constexpr std::size_t max_rule_wires = 40;

// The most ports of the interface of a pair: the auxiliary ports of two
// agents.
inline constexpr std::size_t max_interface_ports = 2 * (max_agent_ports - 1);

// The two kinds of end of a wire of a right side.
enum class RuleEndSide : std::uint8_t {
    Agent = 0,
    Interface = 1,
};

// One end of a wire of a right side: a port of a new agent, or a port of the
// interface.
struct RuleEnd {
    RuleEndSide side = RuleEndSide::Agent;
    std::uint8_t reserved[3]{};
    std::uint32_t agent = 0;
    std::uint32_t port = 0;
};

// The port of agent number agent of the right side.
[[nodiscard]] constexpr RuleEnd point_at_agent(std::uint32_t agent, std::uint32_t port) noexcept {
    return RuleEnd{RuleEndSide::Agent, {}, agent, port};
}

// The port number port of the interface of the pair.
[[nodiscard]] constexpr RuleEnd point_at_interface(std::uint32_t port) noexcept {
    return RuleEnd{RuleEndSide::Interface, {}, 0, port};
}

struct RuleWire {
    RuleEnd left{};
    RuleEnd right{};
};

// The net that replaces a pair: agent_count agents and wire_count wires.
struct RightSide {
    KindId agents[max_rule_agents]{};
    RuleWire wires[max_rule_wires]{};
    std::uint32_t agent_count = 0;
    std::uint32_t wire_count = 0;
};

// A rule.  The growth reason is empty, unless the rule went through the door
// allow_growth.
class Rule {
    NetName name_{};
    KindId left_{};
    KindId right_{};
    RightSide side_{};
    NetName growth_reason_{};

    friend constexpr Rule allow_growth(Rule rule, NetName reason) noexcept;

public:
    // The rule of no pair, which the check refuses.  A table holds it only
    // when it states more rules than it gives.
    constexpr Rule() noexcept = default;

    constexpr Rule(NetName name, KindId left, KindId right, RightSide const& side) noexcept
        : name_{name}, left_{left}, right_{right}, side_{side} {}

    [[nodiscard]] constexpr NetName name() const noexcept { return name_; }
    [[nodiscard]] constexpr KindId left() const noexcept { return left_; }
    [[nodiscard]] constexpr KindId right() const noexcept { return right_; }
    [[nodiscard]] constexpr RightSide side() const noexcept { return side_; }
    [[nodiscard]] constexpr NetName growth_reason() const noexcept { return growth_reason_; }
    [[nodiscard]] constexpr bool has_growth_door() const noexcept { return !growth_reason_.is_empty(); }
};

// The growth door: the rule may have more agents in its right side than its
// pair.  The reason says why the rule allocates, and it must not be empty.
// A search for allow_growth finds each rule that allocates.
[[nodiscard]] constexpr Rule allow_growth(Rule rule, NetName reason) noexcept {
    rule.growth_reason_ = reason;
    return rule;
}

// A signature and its rules.
template <std::size_t Types, std::size_t Kinds, std::size_t Rules>
    requires(Types >= 1 && Kinds >= 1 && Rules >= 1 && Kinds < 0x10000u)
struct RuleTable {
    NetSignature<Types, Kinds> signature{};
    Rule rules[Rules]{};
};

// The class of growth of an admitted table.
enum class NetGrowth : std::uint8_t {
    Shrinks = 0,
    AllocatesNothing = 1,
    Grows = 2,
};

// The answer of check_rules.  data() and size() give the text of a refusal,
// which static_assert prints as its message.
class RuleVerdict {
    NetVerdict verdict_{};
    NetGrowth growth_ = NetGrowth::Shrinks;
    std::uint8_t reserved_[3]{};
    std::uint32_t growing_rules_ = 0;
    std::uint32_t most_added_agents_ = 0;

public:
    constexpr RuleVerdict() noexcept = default;

    // A refusal.
    constexpr explicit RuleVerdict(NetVerdict const& refusal) noexcept : verdict_{refusal} {}

    // An admitted table of the class growth.
    constexpr RuleVerdict(NetGrowth growth, std::uint32_t growing_rules, std::uint32_t most_added_agents) noexcept
        : growth_{growth}, growing_rules_{growing_rules}, most_added_agents_{most_added_agents} {}

    [[nodiscard]] constexpr bool is_admitted() const noexcept { return verdict_.is_admitted(); }
    [[nodiscard]] constexpr NetGrowth growth() const noexcept { return growth_; }
    [[nodiscard]] constexpr bool allocates_nothing() const noexcept {
        return verdict_.is_admitted() && growth_ != NetGrowth::Grows;
    }
    // The count of the rules that went through the growth door.
    [[nodiscard]] constexpr std::uint32_t growing_rule_count() const noexcept { return growing_rules_; }
    // The most agents that one step adds to a net.
    [[nodiscard]] constexpr std::uint32_t most_added_agents() const noexcept { return most_added_agents_; }

    [[nodiscard]] constexpr char const* data() const noexcept { return verdict_.data(); }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return verdict_.size(); }

    template <std::size_t Length>
    [[nodiscard]] consteval bool says(char const (&literal)[Length]) const noexcept {
        return verdict_.says(literal);
    }
};

namespace detail {

// The most ends of a right side: the interface and the ports of the agents.
inline constexpr std::size_t max_rule_ends = max_interface_ports + max_rule_agents * max_agent_ports;

// The interface of a pair: the auxiliary ports of the left kind, then those
// of the right kind.
struct RuleInterface {
    PortType types[max_interface_ports]{};
    std::uint32_t ports[max_interface_ports]{};
    bool is_right[max_interface_ports]{};
    std::uint32_t left_count = 0;
    std::uint32_t count = 0;
};

[[nodiscard]] constexpr RuleInterface interface_of(AgentKind const& left, AgentKind const& right) noexcept {
    RuleInterface interface{};
    for (std::uint32_t side = 0; side < 2u; ++side) {
        AgentKind const& kind = side == 0u ? left : right;
        std::uint32_t const count = port_count_of(kind);
        for (std::uint32_t port = 0; port < count; ++port) {
            if (port == kind.principal) continue;
            interface.types[interface.count] = kind.port_types[port];
            interface.ports[interface.count] = port;
            interface.is_right[interface.count] = side == 1u;
            ++interface.count;
        }
        if (side == 0u) interface.left_count = interface.count;
    }
    return interface;
}

// "rule 'NAME'"
constexpr void put_rule(NetVerdict& text, Rule const& rule) noexcept {
    text.put("rule '").put_name(rule.name()).put("'");
}

// "port 'PORT' of the left agent 'KIND'"
constexpr void put_interface_port(NetVerdict& text, AgentKind const& left, AgentKind const& right,
                                  RuleInterface const& interface, std::uint32_t port) noexcept {
    AgentKind const& kind = interface.is_right[port] ? right : left;
    text.put("port '").put_name(kind.port_names[interface.ports[port]]);
    if (interface.is_right[port]) {
        text.put("' of the right agent '");
    } else {
        text.put("' of the left agent '");
    }
    text.put_name(kind.name).put("'");
}

// "port 'PORT' of the new agent 'KIND' (agent I of the right side)"
template <std::size_t Types, std::size_t Kinds>
constexpr void put_new_port(NetVerdict& text, NetSignature<Types, Kinds> const& signature, RightSide const& side,
                            std::uint32_t agent, std::uint32_t port) noexcept {
    AgentKind const& kind = signature.kinds[index_of(side.agents[agent])];
    text.put("port '")
        .put_name(kind.port_names[port])
        .put("' of the new agent '")
        .put_name(kind.name)
        .put("' (agent ")
        .put_number(agent)
        .put(" of the right side)");
}

// The index of an end in the arrays of a right side: the interface ports
// first, then max_agent_ports slots for each agent.
[[nodiscard]] constexpr std::uint32_t end_index(RuleEnd end, std::uint32_t interface_count) noexcept {
    return end.side == RuleEndSide::Interface
             ? end.port
             : interface_count + end.agent * static_cast<std::uint32_t>(max_agent_ports) + end.port;
}

// The ends of a right side that passed the checks of its ports: the other
// end of the wire at each end.
struct RuleEnds {
    std::uint32_t partner[max_rule_ends]{};
};

// The symmetry of a rule for a pair of one kind.  The swap of the two agents
// maps interface port i of the left agent to port i of the right agent, and
// back.  The right side is symmetric when a map of its agents, with each new
// agent to an agent of the same kind, makes the swap an automorphism of its
// wires.  Each new agent has a path to the interface, so the walk from the
// interface fixes the map, and the check is O(E) for E ends.
template <std::size_t Types, std::size_t Kinds>
[[nodiscard]] constexpr bool is_symmetric(NetSignature<Types, Kinds> const& signature, RightSide const& side,
                                          RuleInterface const& interface, RuleEnds const& ends) noexcept {
    std::uint32_t const half = interface.left_count;
    std::uint32_t const count = interface.count;
    std::uint32_t image[max_rule_agents]{};
    for (std::uint32_t& slot : image)
        slot = no_index;
    std::uint32_t queue[max_rule_agents]{};
    std::uint32_t queue_count = 0;
    // The image of the end at index under the swap: false when the map of
    // the agents cannot give one.
    auto const maps_to = [&](std::uint32_t from, std::uint32_t to) constexpr noexcept {
        bool const is_from_interface = from < count;
        bool const is_to_interface = to < count;
        if (is_from_interface || is_to_interface) {
            if (!is_from_interface || !is_to_interface) return false;
            std::uint32_t const swapped = from < half ? from + half : from - half;
            return swapped == to;
        }
        std::uint32_t const from_agent = (from - count) / static_cast<std::uint32_t>(max_agent_ports);
        std::uint32_t const to_agent = (to - count) / static_cast<std::uint32_t>(max_agent_ports);
        if ((from - count) % max_agent_ports != (to - count) % max_agent_ports) return false;
        if (!(side.agents[from_agent] == side.agents[to_agent])) return false;
        if (image[from_agent] == no_index) {
            image[from_agent] = to_agent;
            queue[queue_count] = from_agent;
            ++queue_count;
            return true;
        }
        return image[from_agent] == to_agent;
    };
    for (std::uint32_t port = 0; port < count; ++port) {
        std::uint32_t const swapped = port < half ? port + half : port - half;
        if (!maps_to(ends.partner[port], ends.partner[swapped])) return false;
    }
    for (std::uint32_t read = 0; read < queue_count; ++read) {
        std::uint32_t const agent = queue[read];
        std::uint32_t const ports = port_count_of(signature.kinds[index_of(side.agents[agent])]);
        for (std::uint32_t port = 0; port < ports; ++port) {
            std::uint32_t const at = count + agent * static_cast<std::uint32_t>(max_agent_ports) + port;
            std::uint32_t const swapped_at = count + image[agent] * static_cast<std::uint32_t>(max_agent_ports) + port;
            if (!maps_to(ends.partner[at], ends.partner[swapped_at])) return false;
        }
    }
    // The map is a permutation of the agents.
    bool is_taken[max_rule_agents]{};
    for (std::uint32_t agent = 0; agent < side.agent_count; ++agent) {
        if (image[agent] == no_index || is_taken[image[agent]]) return false;
        is_taken[image[agent]] = true;
    }
    return true;
}

// The checks of one rule, apart from the second rule of a pair and the
// totality of the table.
template <std::size_t Types, std::size_t Kinds>
[[nodiscard]] consteval NetVerdict check_rule(NetSignature<Types, Kinds> const& signature, Rule const& rule) noexcept {
    if (rule.name().is_empty()) {
        return NetVerdict::refuse("rules: a rule has no name.  A table names each of its rules.");
    }
    if (index_of(rule.left()) >= Kinds || index_of(rule.right()) >= Kinds) {
        NetVerdict refusal = NetVerdict::refuse("rules: ");
        put_rule(refusal, rule);
        refusal.put(" names a kind past the ").put_number(Kinds).put(" kinds of the signature.");
        return refusal;
    }
    AgentKind const& left = signature.kinds[index_of(rule.left())];
    AgentKind const& right = signature.kinds[index_of(rule.right())];
    if (left.is_boundary || right.is_boundary) {
        NetVerdict refusal = NetVerdict::refuse("rules: ");
        put_rule(refusal, rule);
        refusal.put(" reads the boundary agent '")
            .put_name(left.is_boundary ? left.name : right.name)
            .put("'.  A boundary agent stands for a free port, so no rule reads it.");
        return refusal;
    }
    PortType const left_principal = left.port_types[left.principal];
    PortType const right_principal = right.port_types[right.principal];
    if (!(right_principal == dual_of(left_principal))) {
        NetVerdict refusal = NetVerdict::refuse("rules: ");
        put_rule(refusal, rule);
        refusal.put(" joins the principal ports of '")
            .put_name(left.name)
            .put("' and '")
            .put_name(right.name)
            .put("', which are not duals.  The rule never fires.");
        return refusal;
    }
    RightSide const side = rule.side();
    if (side.agent_count > max_rule_agents || side.wire_count > max_rule_wires) {
        NetVerdict refusal = NetVerdict::refuse("rules: ");
        put_rule(refusal, rule);
        refusal.put(" states more agents or wires than its right side holds.");
        return refusal;
    }
    for (std::uint32_t agent = 0; agent < side.agent_count; ++agent) {
        std::uint32_t const kind = index_of(side.agents[agent]);
        if (kind >= Kinds || signature.kinds[kind].is_boundary) {
            NetVerdict refusal = NetVerdict::refuse("rules: ");
            put_rule(refusal, rule);
            refusal.put(": agent ")
                .put_number(agent)
                .put(" of the right side is not an agent kind of the signature that a rule can make.");
            return refusal;
        }
    }

    // Each end names a port, and each port has exactly one wire.
    RuleInterface const interface = interface_of(left, right);
    std::uint8_t uses[max_rule_ends]{};
    RuleEnds ends{};
    for (std::uint32_t wire = 0; wire < side.wire_count; ++wire) {
        RuleWire const& joined = side.wires[wire];
        PortType types[2]{};
        bool is_interface[2]{};
        for (std::uint32_t at = 0; at < 2u; ++at) {
            RuleEnd const end = at == 0u ? joined.left : joined.right;
            is_interface[at] = end.side == RuleEndSide::Interface;
            if (is_interface[at]) {
                if (end.port >= interface.count) {
                    NetVerdict refusal = NetVerdict::refuse("rules: ");
                    put_rule(refusal, rule);
                    refusal.put(": wire ")
                        .put_number(wire)
                        .put(" of the right side names interface port ")
                        .put_number(end.port)
                        .put(", and the pair leaves ")
                        .put_number(interface.count)
                        .put(" ports free.");
                    return refusal;
                }
                types[at] = interface.types[end.port];
            } else {
                if (end.agent >= side.agent_count
                    || end.port >= port_count_of(signature.kinds[index_of(side.agents[end.agent])])) {
                    NetVerdict refusal = NetVerdict::refuse("rules: ");
                    put_rule(refusal, rule);
                    refusal.put(": wire ").put_number(wire).put(
                        " of the right side names a port that no new agent has.");
                    return refusal;
                }
                types[at] = signature.kinds[index_of(side.agents[end.agent])].port_types[end.port];
            }
            std::uint32_t const index = end_index(end, interface.count);
            ++uses[index];
            ends.partner[index] = end_index(at == 0u ? joined.right : joined.left, interface.count);
        }
        // A port of a new agent at an interface port takes the place of that
        // port, so it keeps its type.  Each other wire joins duals.
        bool const is_same_needed = is_interface[0] != is_interface[1];
        bool const does_fit = is_same_needed ? types[0] == types[1] : types[1] == dual_of(types[0]);
        if (!does_fit) {
            NetVerdict refusal = NetVerdict::refuse("rules: ");
            put_rule(refusal, rule);
            refusal.put(": wire ").put_number(wire).put(" of the right side joins ");
            for (std::uint32_t at = 0; at < 2u; ++at) {
                RuleEnd const end = at == 0u ? joined.left : joined.right;
                if (at == 1u) refusal.put(" with ");
                if (is_interface[at]) {
                    put_interface_port(refusal, left, right, interface, end.port);
                } else {
                    put_new_port(refusal, signature, side, end.agent, end.port);
                }
                refusal.put(" of type ");
                put_type(refusal, signature.type_names[index_of(types[at].type)], types[at].polarity);
            }
            if (is_same_needed) {
                refusal.put(".  A new port in the place of a free port keeps its type.");
            } else {
                refusal.put(".  The two ports of a wire have dual types.");
            }
            return refusal;
        }
    }
    for (std::uint32_t port = 0; port < interface.count; ++port) {
        if (uses[port] == 1u) continue;
        NetVerdict refusal = NetVerdict::refuse("rules: ");
        put_rule(refusal, rule);
        refusal.put(": the right side gives ");
        if (uses[port] == 0u) {
            refusal.put("no wire to ");
        } else {
            refusal.put("two wires to ");
        }
        put_interface_port(refusal, left, right, interface, port);
        refusal.put(", which the pair leaves free.  The right side keeps each port of the pair.");
        return refusal;
    }
    for (std::uint32_t agent = 0; agent < side.agent_count; ++agent) {
        std::uint32_t const ports = port_count_of(signature.kinds[index_of(side.agents[agent])]);
        for (std::uint32_t port = 0; port < ports; ++port) {
            std::uint8_t const count =
                uses[interface.count + agent * static_cast<std::uint32_t>(max_agent_ports) + port];
            if (count == 1u) continue;
            NetVerdict refusal = NetVerdict::refuse("rules: ");
            put_rule(refusal, rule);
            refusal.put(": ");
            put_new_port(refusal, signature, side, agent, port);
            if (count == 0u) {
                refusal.put(" has no wire.  Each port has exactly one wire.");
            } else {
                refusal.put(" has two wires.  Each port has exactly one wire.");
            }
            return refusal;
        }
    }

    // Each new agent has a path to the interface.
    bool is_reached[max_rule_agents]{};
    std::uint32_t reached[max_rule_agents]{};
    std::uint32_t reached_count = 0;
    auto const reach = [&](std::uint32_t end) constexpr noexcept {
        if (end < interface.count) return;
        std::uint32_t const agent = (end - interface.count) / static_cast<std::uint32_t>(max_agent_ports);
        if (is_reached[agent]) return;
        is_reached[agent] = true;
        reached[reached_count] = agent;
        ++reached_count;
    };
    for (std::uint32_t port = 0; port < interface.count; ++port)
        reach(ends.partner[port]);
    for (std::uint32_t read = 0; read < reached_count; ++read) {
        std::uint32_t const agent = reached[read];
        std::uint32_t const ports = port_count_of(signature.kinds[index_of(side.agents[agent])]);
        for (std::uint32_t port = 0; port < ports; ++port) {
            reach(ends.partner[interface.count + agent * static_cast<std::uint32_t>(max_agent_ports) + port]);
        }
    }
    for (std::uint32_t agent = 0; agent < side.agent_count; ++agent) {
        if (is_reached[agent]) continue;
        NetVerdict refusal = NetVerdict::refuse("rules: ");
        put_rule(refusal, rule);
        refusal.put(": the new agent '")
            .put_name(signature.kinds[index_of(side.agents[agent])].name)
            .put("' (agent ")
            .put_number(agent)
            .put(" of the right side) has no path to the interface, so no other agent reaches it.");
        return refusal;
    }

    // The growth door.
    if (side.agent_count > 2u && !rule.has_growth_door()) {
        NetVerdict refusal = NetVerdict::refuse("rules: ");
        put_rule(refusal, rule);
        refusal.put(" replaces 2 agents with ")
            .put_number(side.agent_count)
            .put(", and only the door allow_growth admits a rule that adds agents.  Such a step allocates.");
        return refusal;
    }
    if (side.agent_count <= 2u && rule.has_growth_door()) {
        NetVerdict refusal = NetVerdict::refuse("rules: ");
        put_rule(refusal, rule);
        refusal.put(" goes through the growth door, and its right side has ")
            .put_number(side.agent_count)
            .put(" agents.  Only a rule that adds agents takes the door.");
        return refusal;
    }

    if (rule.left() == rule.right() && !is_symmetric(signature, side, interface, ends)) {
        NetVerdict refusal = NetVerdict::refuse("rules: ");
        put_rule(refusal, rule);
        refusal.put(" for the pair '")
            .put_name(left.name)
            .put("', '")
            .put_name(left.name)
            .put("' changes when the two agents change places.  The pair has no order, so the rule must be "
                 "symmetric.");
        return refusal;
    }
    return NetVerdict{};
}

}  // namespace detail

// The check of a rule table, with the class of growth of an admitted table.
// Complexity: O(K^2 + R * E).
template <std::size_t Types, std::size_t Kinds, std::size_t Rules>
[[nodiscard]] consteval RuleVerdict check_rules(RuleTable<Types, Kinds, Rules> const& table) noexcept {
    NetVerdict const signature = check_signature(table.signature);
    if (!signature.is_admitted()) return RuleVerdict{signature};
    constexpr std::uint32_t none = detail::no_index;
    std::uint32_t rule_of_pair[Kinds * Kinds]{};
    for (std::uint32_t& slot : rule_of_pair)
        slot = none;
    std::uint32_t most_agents = 0;
    std::uint32_t growing_rules = 0;
    for (std::uint32_t index = 0; index < Rules; ++index) {
        Rule const& rule = table.rules[index];
        NetVerdict const one = detail::check_rule(table.signature, rule);
        if (!one.is_admitted()) return RuleVerdict{one};
        std::uint32_t const left = index_of(rule.left());
        std::uint32_t const right = index_of(rule.right());
        std::uint32_t const earlier = rule_of_pair[left * Kinds + right];
        if (earlier != none) {
            NetVerdict refusal = NetVerdict::refuse("rules: ");
            detail::put_rule(refusal, rule);
            refusal.put(" is a second rule for the pair '")
                .put_name(table.signature.kinds[left].name)
                .put("', '")
                .put_name(table.signature.kinds[right].name)
                .put("' after ");
            detail::put_rule(refusal, table.rules[earlier]);
            refusal.put(".  A pair has at most one rule, so each step commutes.");
            return RuleVerdict{refusal};
        }
        rule_of_pair[left * Kinds + right] = index;
        rule_of_pair[right * Kinds + left] = index;
        std::uint32_t const agents = rule.side().agent_count;
        if (agents > most_agents) most_agents = agents;
        if (rule.has_growth_door()) ++growing_rules;
    }
    for (std::uint32_t first = 0; first < Kinds; ++first) {
        AgentKind const& one = table.signature.kinds[first];
        if (one.is_boundary) continue;
        for (std::uint32_t second = first; second < Kinds; ++second) {
            AgentKind const& other = table.signature.kinds[second];
            if (other.is_boundary || rule_of_pair[first * Kinds + second] != none) continue;
            if (!(other.port_types[other.principal] == dual_of(one.port_types[one.principal]))) continue;
            NetVerdict refusal = NetVerdict::refuse("rules: the agents '");
            refusal.put_name(one.name)
                .put("' and '")
                .put_name(other.name)
                .put("' have dual principal ports and no rule.  A net with this pair stops.");
            return RuleVerdict{refusal};
        }
    }
    NetGrowth growth = NetGrowth::Grows;
    if (most_agents <= 1u) growth = NetGrowth::Shrinks;
    if (most_agents == 2u) growth = NetGrowth::AllocatesNothing;
    return RuleVerdict{growth, growing_rules, most_agents > 2u ? most_agents - 2u : 0u};
}

}  // namespace foundation::algebra::net
