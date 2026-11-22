#pragma once

// The rule tables of interaction nets, and the consteval check of a table.
//
//   RightSide               the agents and the wires that replace a pair
//   Rule                    a name, the two kinds of the pair, and the right
//                           side
//   allow_growth(rule, reason)
//                           the growth door: the only way to a rule whose
//                           right side has more agents than its pair
//   StepWeight, KindWeight  the step weight of a kind (the weights)
//   RuleTable<T, K, R>      a signature, R rules, and the step weights of the
//                           kinds
//   check_rules(table)      the check of the table, with its class of growth
//   RuleVerdict             the answer: admitted or a refusal, the class of
//                           growth, and the forest property of the steps
//   weight_of(table, net)   the weight of a net: a bound of the steps and of
//                           the agents of each reduction of the net
//
// A rule
//   A rule fires where the principal ports of an agent of the left kind and
//   an agent of the right kind meet.  The right side replaces the two agents.
//   The interface of the pair is its free ports: the auxiliary ports of the
//   left kind, in port order, and then those of the right kind.  A wire of
//   the right side joins two ends.  An end is a port of a new agent
//   (point_at_agent), or a port of the interface (point_at_interface).
//
// Checks
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
//   - a wire of the right side that joins the principal ports of two new
//     agents.  Such an active pair fires again at once, so a rule of ping and
//     pong that makes its own pair never stops.  Lafont 1990 asks for a right
//     side with no active pair
//   - a rule for a pair of one kind whose right side changes when the two
//     agents change places.  The pair has no order, so such a rule gives two
//     results for one net (an inference from the commutation of the steps)
//   - a right side of more than 2 agents with no growth door, and a growth
//     door on a rule that does not grow
//   - a dataflow rule whose step can make a deadlock (the preservation)
//   - a rule whose step does not lower the weight of the net, and a step
//     weight that names no kind or a kind for the second time (the weights).
//
// Preservation
//   A check of a net holds for each net that a reduction makes, when each
//   step keeps it.  Lafont 1990 gives this form of proof: one test of each
//   rule, and no test of each net.
//
//   A dataflow rule is a rule whose pair has only give and take ports.  Its
//   step keeps a net that check_dataflow admits, so check_rules refuses:
//   - a wire of the right side between two ports of no direction, because
//     check_dataflow refuses a meet port
//   - a cycle of waits among the new agents of the right side
//   - a right side that lets an input of the pair reach an output that the
//     pair does not reach from that input.  The pair lets an input reach
//     each output of its own agent, and each output of the other agent when
//     its own agent gives on its principal port.  A path of the net outside
//     the pair can join that output to that input, so the new path closes a
//     cycle of waits.
//   Each path of a cycle through the right side then maps to a path through
//   the pair, so the net before the step had the cycle too.
//
//   The forest of check_forest is a property of the table, and no refusal,
//   because a correct dataflow rule can close a cycle with no direction: a
//   split that gives two values to one join.  A step keeps a forest when no
//   new agent has a par group and the right side has no cycle, with each port
//   of the interface as a vertex of its own.  In a forest, each port of the
//   interface of the pair leads to a tree of its own, so such a right side
//   closes no cycle.  A user that reduces forests writes
//   static_assert(verdict.keeps_forest()).
//
// Weights
//   Each kind has a step weight, 0 when the table names no weight for it.  An
//   agent weighs 1 plus the step weight of its kind, and the weight of a net
//   is the sum of the weights of its agents.  check_rules asks that each step
//   lower the weight of the net: the agents of the right side weigh less than
//   the pair.  The weight is never negative, so a reduction of a net of
//   weight W ends after at most W steps, and each net of the reduction holds
//   at most W agents (the potentials of Gimenez and Moser 2016).
//
//   The step weight of an agent is its credit for the agents that a later
//   step adds.  A rule that makes n agents from 2 passes only when the step
//   weights of its pair are more than n - 2 plus the step weights of the new
//   agents.  So a table in which each right side has at most 1 agent needs no
//   step weight, and the table of a growing rule states the credit that pays
//   for it.  A rule whose right side holds the kinds of its pair,
//   such as a token that goes past a node, never passes: on a ring of nodes
//   it never stops.  The commutation of the interaction combinators makes 4
//   agents from 2 of the same kinds, so no step weights admit it either.
//
// Class of growth
//   A rule that adds agents allocates: the commutation of a duplicator and a
//   constructor makes 4 agents from 2 (Lafont 1997), and HVM2 asks for 4 new
//   nodes in that rule.  The class of an admitted table:
//     Shrinks           each right side has at most 1 agent.  Each step
//                       removes an agent
//     AllocatesNothing  each right side has at most 2 agents.  The count of
//                       the agents of a net never increases, so a reduction
//                       reuses the slots of each pair and allocates nothing
//     GrowsWithinCredit a rule went through the growth door, and the step
//                       weights of its pair pay for the added agents.  The
//                       verdict counts the growing rules and the most agents
//                       that one of them adds
//   A user that must not allocate writes
//   static_assert(verdict.allocates_nothing(), verdict).
//
// Costs
//   R rules and K kinds.  check_rules is O(K^2 + R * I * E) for I the ports
//   of the interface of a pair and E the ends of a right side, with a table
//   of K * K pairs.  This header reads plain arrays.  The check of the value
//   table and the combinator table of its check file costs about 170 M
//   compile instructions and 40 ms of wall time together.  The checks of the
//   preservation are about 30 M and 10 ms of it.  A table that reflection
//   reads costs more, because of the constant arrays and strings of
//   foundation/algebra/net/Reflect.h (the costs there).

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

// The step weight of a kind (the weights).  An agent weighs 1 plus the step
// weight of its kind.
enum class StepWeight : std::uint32_t {
};

// One kind of a table and its step weight.  An entry of step weight 0 names
// no kind, so the entries past the last one of a table keep their default.
struct KindWeight {
    KindId kind{};
    StepWeight weight{};
};

// A signature, its rules, and the step weights of the kinds that weigh more
// than 1, in any order.
template <std::size_t Types, std::size_t Kinds, std::size_t Rules>
    requires(Types >= 1 && Kinds >= 1 && Rules >= 1 && Kinds < 0x10000u)
struct RuleTable {
    NetSignature<Types, Kinds> signature{};
    Rule rules[Rules]{};
    KindWeight step_weights[Kinds]{};
};

// The class of growth of an admitted table.
enum class NetGrowth : std::uint8_t {
    Shrinks = 0,
    AllocatesNothing = 1,
    GrowsWithinCredit = 2,
};

// The forest of a net under the steps of a table (the preservation): each
// step keeps a forest, or some step can close a cycle.
enum class ForestKeeping : std::uint8_t {
    CanCloseCycle = 0,
    KeepsForest = 1,
};

// The count of the rules of a table that went through the growth door.
enum class GrowingRules : std::uint32_t {
};

// The most agents that one step of a table adds to a net.
enum class AddedAgents : std::uint32_t {
};

// The answer of check_rules.  data() and size() give the text of a refusal,
// which static_assert prints as its message.
class RuleVerdict {
    NetVerdict verdict_{};
    NetGrowth growth_ = NetGrowth::Shrinks;
    ForestKeeping forest_ = ForestKeeping::KeepsForest;
    std::uint8_t reserved_[2]{};
    GrowingRules growing_rules_{};
    AddedAgents most_added_agents_{};

public:
    constexpr RuleVerdict() noexcept = default;

    // A refusal.
    constexpr explicit RuleVerdict(NetVerdict const& refusal) noexcept : verdict_{refusal} {}

    // An admitted table of the class growth.
    constexpr RuleVerdict(NetGrowth growth, GrowingRules growing_rules, AddedAgents most_added_agents,
                          ForestKeeping forest) noexcept
        : growth_{growth}, forest_{forest}, growing_rules_{growing_rules}, most_added_agents_{most_added_agents} {}

    [[nodiscard]] constexpr bool is_admitted() const noexcept { return verdict_.is_admitted(); }
    [[nodiscard]] constexpr NetGrowth growth() const noexcept { return growth_; }
    [[nodiscard]] constexpr bool allocates_nothing() const noexcept {
        return verdict_.is_admitted() && growth_ != NetGrowth::GrowsWithinCredit;
    }
    // Each step of the table keeps a net that check_forest admits.
    [[nodiscard]] constexpr bool keeps_forest() const noexcept {
        return verdict_.is_admitted() && forest_ == ForestKeeping::KeepsForest;
    }
    // The count of the rules that went through the growth door.
    [[nodiscard]] constexpr std::uint32_t growing_rule_count() const noexcept {
        return static_cast<std::uint32_t>(growing_rules_);
    }
    // The most agents that one step adds to a net.
    [[nodiscard]] constexpr std::uint32_t most_added_agents() const noexcept {
        return static_cast<std::uint32_t>(most_added_agents_);
    }

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

// Each port of the kind gives or takes.
[[nodiscard]] constexpr bool is_directed(AgentKind const& kind) noexcept {
    std::uint32_t const count = port_count_of(kind);
    for (std::uint32_t port = 0; port < count; ++port) {
        if (kind.port_types[port].polarity == Polarity::Meet) return false;
    }
    return true;
}

// The checks of a dataflow rule (the preservation), on a right side whose
// ports passed their checks: no wire of no direction between new agents, no
// cycle of waits among the new agents, and no path from an input of the pair
// to an output that the pair does not reach from it.  Complexity: O(I * E)
// for I ports of the interface and E ends.
template <std::size_t Types, std::size_t Kinds>
[[nodiscard]] consteval NetVerdict check_dataflow_kept(NetSignature<Types, Kinds> const& signature, Rule const& rule,
                                                       RuleInterface const& interface, RuleEnds const& ends) noexcept {
    constexpr auto ports = static_cast<std::uint32_t>(max_agent_ports);
    AgentKind const& left = signature.kinds[index_of(rule.left())];
    AgentKind const& right = signature.kinds[index_of(rule.right())];
    RightSide const side = rule.side();
    auto const kind_at = [&](std::uint32_t agent) constexpr noexcept -> AgentKind const& {
        return signature.kinds[index_of(side.agents[agent])];
    };

    // The waits among the new agents: the giver and the taker of each wire
    // between two new agents.
    std::uint32_t giver_of[max_rule_wires]{};
    std::uint32_t taker_of[max_rule_wires]{};
    bool is_inner[max_rule_wires]{};
    std::uint32_t indegree[max_rule_agents]{};
    for (std::uint32_t wire = 0; wire < side.wire_count; ++wire) {
        RuleEnd const one = side.wires[wire].left;
        RuleEnd const other = side.wires[wire].right;
        if (one.side != RuleEndSide::Agent || other.side != RuleEndSide::Agent) continue;
        PortType const type = kind_at(one.agent).port_types[one.port];
        if (type.polarity == Polarity::Meet) {
            NetVerdict refusal = NetVerdict::refuse("rules: ");
            put_rule(refusal, rule);
            refusal.put(": wire ").put_number(wire).put(" of the right side joins ");
            put_new_port(refusal, signature, side, one.agent, one.port);
            refusal.put(" of the type ")
                .put_name(signature.type_names[index_of(type.type)])
                .put(", which has no direction.  The pair has only give and take ports, so the step keeps a "
                     "dataflow net, which has no such port.");
            return refusal;
        }
        bool const does_one_give = type.polarity == Polarity::Give;
        giver_of[wire] = does_one_give ? one.agent : other.agent;
        taker_of[wire] = does_one_give ? other.agent : one.agent;
        is_inner[wire] = true;
        ++indegree[taker_of[wire]];
    }

    // The sort of Kahn over the new agents.  An agent gives to the agent at
    // the other end of each of its give ports.
    std::uint32_t sorted[max_rule_agents]{};
    std::uint32_t sorted_count = 0;
    for (std::uint32_t agent = 0; agent < side.agent_count; ++agent) {
        if (indegree[agent] != 0u) continue;
        sorted[sorted_count] = agent;
        ++sorted_count;
    }
    for (std::uint32_t read = 0; read < sorted_count; ++read) {
        std::uint32_t const agent = sorted[read];
        std::uint32_t const count = port_count_of(kind_at(agent));
        for (std::uint32_t port = 0; port < count; ++port) {
            if (kind_at(agent).port_types[port].polarity != Polarity::Give) continue;
            std::uint32_t const end = ends.partner[interface.count + agent * ports + port];
            if (end < interface.count) continue;
            std::uint32_t const next = (end - interface.count) / ports;
            --indegree[next];
            if (indegree[next] != 0u) continue;
            sorted[sorted_count] = next;
            ++sorted_count;
        }
    }
    if (sorted_count != side.agent_count) {
        // Each agent that the sort left takes from another such agent, so a
        // walk back along these inputs comes to an agent of a cycle.
        std::uint32_t into[max_rule_agents]{};
        std::uint32_t walk = no_index;
        for (std::uint32_t wire = 0; wire < side.wire_count; ++wire) {
            if (!is_inner[wire] || indegree[giver_of[wire]] == 0u || indegree[taker_of[wire]] == 0u) continue;
            into[taker_of[wire]] = giver_of[wire];
            if (walk == no_index) walk = taker_of[wire];
        }
        bool is_seen[max_rule_agents]{};
        while (!is_seen[walk]) {
            is_seen[walk] = true;
            walk = into[walk];
        }
        NetVerdict refusal = NetVerdict::refuse("rules: ");
        put_rule(refusal, rule);
        refusal.put(": the new agent '")
            .put_name(kind_at(walk).name)
            .put("' (agent ")
            .put_number(walk)
            .put(" of the right side) waits on itself through the new agents.  A step makes no cycle of waits.");
        return refusal;
    }

    // The outputs that each input of the pair reaches through the right
    // side.  The end of a give port of a new agent at the interface is an
    // output of the pair, because the port keeps the type of that port.
    bool const does_left_give = left.port_types[left.principal].polarity == Polarity::Give;
    for (std::uint32_t input = 0; input < interface.count; ++input) {
        if (interface.types[input].polarity != Polarity::Take) continue;
        bool const does_own_agent_give = interface.is_right[input] ? !does_left_give : does_left_give;
        bool is_reached[max_rule_agents]{};
        std::uint32_t queue[max_rule_agents]{};
        std::uint32_t queue_count = 0;
        std::uint32_t stray = no_index;
        auto const follow = [&](std::uint32_t end) constexpr noexcept {
            if (end < interface.count) {
                bool const is_own_agent = interface.is_right[end] == interface.is_right[input];
                if (!is_own_agent && !does_own_agent_give && stray == no_index) stray = end;
                return;
            }
            std::uint32_t const agent = (end - interface.count) / ports;
            if (is_reached[agent]) return;
            is_reached[agent] = true;
            queue[queue_count] = agent;
            ++queue_count;
        };
        follow(ends.partner[input]);
        for (std::uint32_t read = 0; read < queue_count; ++read) {
            std::uint32_t const agent = queue[read];
            std::uint32_t const count = port_count_of(kind_at(agent));
            for (std::uint32_t port = 0; port < count; ++port) {
                if (kind_at(agent).port_types[port].polarity != Polarity::Give) continue;
                follow(ends.partner[interface.count + agent * ports + port]);
            }
        }
        if (stray == no_index) continue;
        NetVerdict refusal = NetVerdict::refuse("rules: ");
        put_rule(refusal, rule);
        refusal.put(": the right side lets ");
        put_interface_port(refusal, left, right, interface, input);
        refusal.put(" reach ");
        put_interface_port(refusal, left, right, interface, stray);
        refusal.put(".  The pair has no such path, so the step can close a cycle of waits.");
        return refusal;
    }
    return NetVerdict{};
}

// The step of the rule keeps a forest (the preservation): no new agent has a
// par group, and the right side has no cycle with each port of the interface
// as a vertex of its own.  Complexity: O(E a(E)).
template <std::size_t Types, std::size_t Kinds>
[[nodiscard]] consteval ForestKeeping forest_keeping_of(NetSignature<Types, Kinds> const& signature,
                                                        Rule const& rule) noexcept {
    RightSide const side = rule.side();
    std::uint32_t const interface_count = port_count_of(signature.kinds[index_of(rule.left())])
                                        + port_count_of(signature.kinds[index_of(rule.right())]) - 2u;
    for (std::uint32_t agent = 0; agent < side.agent_count; ++agent) {
        if (signature.kinds[index_of(side.agents[agent])].par_ports != 0u) return ForestKeeping::CanCloseCycle;
    }
    UnionFind<max_interface_ports + max_rule_agents> classes{};
    auto const vertex_of = [&](RuleEnd end) constexpr noexcept {
        return end.side == RuleEndSide::Interface ? end.port : interface_count + end.agent;
    };
    for (std::uint32_t wire = 0; wire < side.wire_count; ++wire) {
        std::uint32_t const one = classes.find(vertex_of(side.wires[wire].left));
        std::uint32_t const other = classes.find(vertex_of(side.wires[wire].right));
        if (one == other) return ForestKeeping::CanCloseCycle;
        static_cast<void>(classes.join_roots(one, other));
    }
    return ForestKeeping::KeepsForest;
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

    // No wire joins the principal ports of two new agents.
    for (std::uint32_t wire = 0; wire < side.wire_count; ++wire) {
        RuleEnd const one = side.wires[wire].left;
        RuleEnd const other = side.wires[wire].right;
        if (one.side != RuleEndSide::Agent || other.side != RuleEndSide::Agent) continue;
        AgentKind const& one_kind = signature.kinds[index_of(side.agents[one.agent])];
        AgentKind const& other_kind = signature.kinds[index_of(side.agents[other.agent])];
        if (one.port != one_kind.principal || other.port != other_kind.principal) continue;
        NetVerdict refusal = NetVerdict::refuse("rules: ");
        put_rule(refusal, rule);
        refusal.put(": wire ")
            .put_number(wire)
            .put(" of the right side joins the principal ports of the new agents '")
            .put_name(one_kind.name)
            .put("' (agent ")
            .put_number(one.agent)
            .put(") and '")
            .put_name(other_kind.name)
            .put("' (agent ")
            .put_number(other.agent)
            .put(").  A right side holds no active pair, because such a pair fires again at once.");
        return refusal;
    }

    if (is_directed(left) && is_directed(right)) {
        NetVerdict const kept = check_dataflow_kept(signature, rule, interface, ends);
        if (!kept.is_admitted()) return kept;
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
// The step weights run last, so a table with another fault gives that fault.
// Complexity: O(K^2 + R * I * E).
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
    ForestKeeping forest = ForestKeeping::KeepsForest;
    for (std::uint32_t index = 0; index < Rules; ++index) {
        Rule const& rule = table.rules[index];
        NetVerdict const one = detail::check_rule(table.signature, rule);
        if (!one.is_admitted()) return RuleVerdict{one};
        if (detail::forest_keeping_of(table.signature, rule) == ForestKeeping::CanCloseCycle) {
            forest = ForestKeeping::CanCloseCycle;
        }
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

    // The step weights (the weights).
    std::uint64_t weight[Kinds]{};
    bool is_weighed[Kinds]{};
    for (std::uint32_t entry = 0; entry < Kinds; ++entry) {
        KindWeight const named = table.step_weights[entry];
        if (named.weight == StepWeight{}) continue;
        std::uint32_t const kind = index_of(named.kind);
        if (kind >= Kinds) {
            return RuleVerdict{NetVerdict::refuse("rules: step weight ")
                                   .put_number(entry)
                                   .put(" names the kind ")
                                   .put_number(kind)
                                   .put(", and the signature has ")
                                   .put_number(Kinds)
                                   .put(" kinds.")};
        }
        if (is_weighed[kind]) {
            return RuleVerdict{NetVerdict::refuse("rules: step weight ")
                                   .put_number(entry)
                                   .put(" is a second step weight of the kind '")
                                   .put_name(table.signature.kinds[kind].name)
                                   .put("'.  A kind has one step weight.")};
        }
        is_weighed[kind] = true;
        weight[kind] = static_cast<std::uint64_t>(named.weight);
    }
    for (std::uint32_t index = 0; index < Rules; ++index) {
        Rule const& rule = table.rules[index];
        RightSide const side = rule.side();
        std::uint64_t const before = 2u + weight[index_of(rule.left())] + weight[index_of(rule.right())];
        std::uint64_t after = side.agent_count;
        for (std::uint32_t agent = 0; agent < side.agent_count; ++agent)
            after += weight[index_of(side.agents[agent])];
        if (after < before) continue;
        NetVerdict refusal = NetVerdict::refuse("rules: ");
        detail::put_rule(refusal, rule);
        refusal.put(" replaces a pair of weight ")
            .put_number(before)
            .put(" with agents of weight ")
            .put_number(after)
            .put(".  An agent weighs 1 plus the step weight of its kind, and each step lowers the weight of the net, "
                 "so each reduction ends.");
        return RuleVerdict{refusal};
    }

    NetGrowth growth = NetGrowth::GrowsWithinCredit;
    if (most_agents <= 1u) growth = NetGrowth::Shrinks;
    if (most_agents == 2u) growth = NetGrowth::AllocatesNothing;
    return RuleVerdict{growth, GrowingRules{growing_rules}, AddedAgents{most_agents > 2u ? most_agents - 2u : 0u},
                       forest};
}

// The weight of a net under the step weights of a table (the weights).
enum class NetWeight : std::uint64_t {
};

// The weight of a net of the signature of the table: the count of its agents
// plus the step weights of their kinds.  A reduction of the net by a table
// that check_rules admits ends after at most this count of steps, and each
// net of the reduction holds at most this count of agents.  The function is
// constexpr, so plain code that runs the rules can size its arrays at run
// time.  Complexity: O(K * A) for K entries of step weights and A agents.
template <std::size_t Types, std::size_t Kinds, std::size_t Rules, std::size_t Agents, std::size_t Wires>
[[nodiscard]] constexpr NetWeight weight_of(RuleTable<Types, Kinds, Rules> const& table,
                                            Net<Types, Kinds, Agents, Wires> const& graph) noexcept {
    std::uint64_t total = Agents;
    for (KindWeight const& named : table.step_weights) {
        if (named.weight == StepWeight{}) continue;
        for (KindId const kind : graph.agents) {
            if (kind == named.kind) total += static_cast<std::uint64_t>(named.weight);
        }
    }
    return NetWeight{total};
}

}  // namespace foundation::algebra::net
