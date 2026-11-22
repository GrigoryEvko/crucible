#pragma once

// The reflection adapters of the net checker: the agents and the rules of a
// namespace, read from their annotations, as the plain tables of Net.h and
// Rules.h.
//
//   [[= agent{}]] on a class     the class is an agent kind.  Each data member
//                                is a port, in the order of the members: the
//                                name of the member is the name of the port,
//                                and its type Give<T>, Take<T> or Meet<T> is
//                                the type of the port.  The class T names the
//                                type
//   [[= boundary{}]] on a class  the agent stands for a free port of a net
//   [[= StepWeight{n}]] on a class
//                                the step weight of the agent kind (the
//                                weights in Rules.h).  With no such
//                                annotation, the step weight is 0
//   [[= principal{}]]            on the member of the principal port.  With no
//                                such member, the first member is principal
//   [[= par{}]]                  on each member of the par group
//   [[= rule<Left, Right>{}]]    on a consteval function of no parameter that
//                                gives the ReflectedSide of the rule
//   [[= grows{"reason"}]]        on a rule: the growth door of Rules.h
//
//   check_annotations<^^ns>()    the annotations of ns can make a table
//   signature_of<^^ns>()         the NetSignature of the agents of ns
//   kind_id_of<^^ns, ^^A>()      the kind of the agent class A in that
//                                signature, for a net of it
//   rule_table_of<^^ns>()        the RuleTable of the agents and rules of ns
//   check_rules_of<^^ns>()       check_annotations, then check_rules of the
//                                table
//
// Reads of reflection
//   GCC 16 gives each annotation the type T const, so a compare of type_of
//   with ^^T finds nothing.  The adapter compares remove_cv of the type of
//   each annotation.
//
//   The adapter reads each query of reflection through a constant array:
//   reflect_constant_array of the query, and extract of a pointer to its
//   first element.  A name is the static text of reflect_constant_string.  So
//   the adapter calls no member of the containers that the queries give.
//
//   One pass over the namespace reads the facts of each agent, each port and
//   each rule into fixed arrays of a class template on the namespace.  The
//   pass reads the annotations of each entity one time, and each later step
//   reads these facts.
//
// Costs
//   O(M + A * P + R) for M members of the namespace, A agents of P ports and R
//   rules, then check_rules.  The constant arrays and the static strings cost
//   the most.  reflect_constant_array and reflect_constant_string make one
//   instance for each new length of an array or a string, and each instance
//   costs about 25 M compile instructions.  A new content of a known length
//   costs about 6 M.  Thus the first table of a unit, with three agents and
//   two rules, costs about 450 M instructions, and each later table about
//   140 M.  A read through the vector of each query and through the data of
//   identifier_of costs about 110 M and 54 M, but each such read is a
//   finding of the quarantine rules of the base.

#include <foundation/algebra/net/Net.h>
#include <foundation/algebra/net/Rules.h>

#include <cstddef>
#include <cstdint>
#include <meta>

namespace foundation::algebra::net {

// ── The vocabulary of the annotations ───────────────────────────────────

// The types of the ports of an agent.  Payload is the class that names the
// type.  A port type has no object: an agent kind is a class that the
// adapter reads, and no code makes an object of it.
template <class Payload>
struct Give final {};
template <class Payload>
struct Take final {};
template <class Payload>
struct Meet final {};

struct agent final {};
struct boundary final {};
struct principal final {};
struct par final {};

template <class Left, class Right>
struct rule final {};

// The longest reason of a growth door, with its terminating zero.
inline constexpr std::size_t max_growth_reason = 200;

// The growth door of a rule, with its reason: [[= grows{"reason"}]].  An
// empty reason opens no door, and check_rules refuses the rule.
struct grows final {
    char reason[max_growth_reason]{};
};

// The right side of a rule, with the agents as their classes.
struct ReflectedSide {
    std::meta::info agents[max_rule_agents]{};
    RuleWire wires[max_rule_wires]{};
    std::uint32_t agent_count = 0;
    std::uint32_t wire_count = 0;
};

namespace detail {

// A constant array of reflections: its first element and its count.
struct InfoRun {
    std::meta::info const* first = nullptr;
    std::size_t count = 0;
};

// The run of the reflection of a constant array, which reflect_constant_array
// gives.  An empty query gives the empty run.
[[nodiscard]] consteval InfoRun run_of(std::meta::info array) {
    std::meta::info const type = std::meta::type_of(array);
    if (!std::meta::is_array_type(type) || std::meta::extent(type) == 0) return InfoRun{};
    return InfoRun{std::meta::extract<std::meta::info const*>(array), std::meta::extent(type)};
}

[[nodiscard]] consteval InfoRun members_of_scope(std::meta::info scope) {
    return run_of(
        std::meta::reflect_constant_array(std::meta::members_of(scope, std::meta::access_context::current())));
}

[[nodiscard]] consteval InfoRun fields_of(std::meta::info kind) {
    return run_of(std::meta::reflect_constant_array(
        std::meta::nonstatic_data_members_of(kind, std::meta::access_context::current())));
}

// The first template argument and the second one of a specialization.
[[nodiscard]] consteval std::meta::info argument_of(std::meta::info type, std::size_t position) {
    InfoRun const arguments = run_of(std::meta::reflect_constant_array(std::meta::template_arguments_of(type)));
    return position < arguments.count ? arguments.first[position] : std::meta::info{};
}

[[nodiscard]] consteval std::size_t parameter_count(std::meta::info function) {
    return run_of(std::meta::reflect_constant_array(std::meta::parameters_of(function))).count;
}

// The name of an entity, as a text of static storage.
[[nodiscard]] consteval NetName name_of(std::meta::info entity) {
    if (std::meta::has_identifier(entity)) {
        return NetName{
            std::meta::extract<char const*>(std::meta::reflect_constant_string(std::meta::identifier_of(entity)))};
    }
    return NetName{
        std::meta::extract<char const*>(std::meta::reflect_constant_string(std::meta::display_string_of(entity)))};
}

// The annotations of one entity that the adapter reads.
struct Notes {
    std::meta::info rule_type{};
    std::meta::info growth{};
    std::meta::info weight{};
    std::uint32_t rule_count = 0;
    std::uint32_t growth_count = 0;
    std::uint32_t weight_count = 0;
    bool is_agent = false;
    bool is_boundary = false;
    bool is_principal = false;
    bool is_par = false;
};

[[nodiscard]] consteval Notes read_notes(std::meta::info entity) {
    Notes notes{};
    InfoRun const run = run_of(std::meta::reflect_constant_array(std::meta::annotations_of(entity)));
    for (std::size_t index = 0; index < run.count; ++index) {
        std::meta::info const type = std::meta::remove_cv(std::meta::type_of(run.first[index]));
        if (type == ^^agent) {
            notes.is_agent = true;
        } else if (type == ^^boundary) {
            notes.is_boundary = true;
        } else if (type == ^^principal) {
            notes.is_principal = true;
        } else if (type == ^^par) {
            notes.is_par = true;
        } else if (type == ^^grows) {
            if (notes.growth_count == 0) notes.growth = run.first[index];
            ++notes.growth_count;
        } else if (type == ^^StepWeight) {
            if (notes.weight_count == 0) notes.weight = run.first[index];
            ++notes.weight_count;
        } else if (std::meta::has_template_arguments(type) && std::meta::template_of(type) == ^^rule) {
            if (notes.rule_count == 0) notes.rule_type = type;
            ++notes.rule_count;
        }
    }
    return notes;
}

// The facts of one port: its member, its payload class and its polarity.  A
// member of another type has no payload.
struct PortFacts {
    std::meta::info member{};
    std::meta::info payload{};
    Polarity polarity = Polarity::Give;
    bool is_principal = false;
    bool is_par = false;
};

[[nodiscard]] consteval PortFacts read_port(std::meta::info member) {
    PortFacts port{};
    port.member = member;
    std::meta::info const type = std::meta::remove_cv(std::meta::type_of(member));
    if (!std::meta::has_template_arguments(type)) return port;
    std::meta::info const shape = std::meta::template_of(type);
    if (shape != ^^Give&& shape != ^^Take&& shape != ^^Meet) return port;
    port.payload = argument_of(type, 0);
    if (shape == ^^Take) port.polarity = Polarity::Take;
    if (shape == ^^Meet) port.polarity = Polarity::Meet;
    Notes const notes = read_notes(member);
    port.is_principal = notes.is_principal;
    port.is_par = notes.is_par;
    return port;
}

// The facts of one agent kind.  stray is the first member that is no port.
struct AgentFacts {
    std::meta::info kind{};
    std::meta::info stray{};
    std::meta::info weight{};
    PortFacts ports[max_agent_ports]{};
    std::uint32_t member_count = 0;
    std::uint32_t principal_count = 0;
    std::uint32_t weight_count = 0;
    bool is_boundary = false;
};

// The facts of one rule: its function and its annotations.
struct RuleFacts {
    std::meta::info function{};
    Notes notes{};
};

// The facts of a namespace of Capacity members.  The arrays of structs take
// the default initialization of their elements, which sets each member of
// each element from its initializer.  GCC 16 cannot evaluate the value
// initialization {} of an array of structs that hold reflections: it holds
// the array as a range expression that the constant evaluator cannot read.
template <std::size_t Capacity>
struct ScopeFacts {
    AgentFacts agents[Capacity == 0 ? 1 : Capacity];
    RuleFacts rules[Capacity == 0 ? 1 : Capacity];
    std::meta::info types[Capacity == 0 ? 1 : Capacity * max_agent_ports]{};
    std::size_t agent_count = 0;
    std::size_t rule_count = 0;
    std::size_t type_count = 0;
};

// One pass over the members of the namespace.  Complexity: O(M + A * P * T).
template <std::size_t Capacity>
[[nodiscard]] consteval ScopeFacts<Capacity> read_scope(InfoRun members) {
    ScopeFacts<Capacity> facts{};
    for (std::size_t index = 0; index < members.count; ++index) {
        std::meta::info const member = members.first[index];
        bool const is_function = std::meta::is_function(member);
        bool const is_class = std::meta::is_type(member) && std::meta::is_class_type(member);
        if (!is_function && !is_class) continue;
        Notes const notes = read_notes(member);
        if (is_function) {
            if (notes.rule_count == 0) continue;
            facts.rules[facts.rule_count] = RuleFacts{member, notes};
            ++facts.rule_count;
            continue;
        }
        if (!notes.is_agent) continue;
        AgentFacts& kind = facts.agents[facts.agent_count];
        ++facts.agent_count;
        kind.kind = member;
        kind.is_boundary = notes.is_boundary;
        kind.weight = notes.weight;
        kind.weight_count = notes.weight_count;
        InfoRun const fields = fields_of(member);
        for (std::size_t field = 0; field < fields.count; ++field) {
            PortFacts const port = read_port(fields.first[field]);
            if (port.payload == std::meta::info{}) {
                if (kind.stray == std::meta::info{}) kind.stray = fields.first[field];
                continue;
            }
            if (kind.member_count < max_agent_ports) kind.ports[kind.member_count] = port;
            ++kind.member_count;
            if (port.is_principal) ++kind.principal_count;
            bool is_known = false;
            for (std::size_t type = 0; type < facts.type_count; ++type)
                is_known = is_known || facts.types[type] == port.payload;
            if (!is_known) {
                facts.types[facts.type_count] = port.payload;
                ++facts.type_count;
            }
        }
    }
    return facts;
}

// A fixed list of reflections, for an expansion statement.
template <std::size_t Count>
struct InfoList {
    std::meta::info items[Count == 0 ? 1 : Count]{};
};

// The facts of one namespace, read one time in a unit.
template <std::meta::info Scope>
struct NetNamespace {
    static constexpr InfoRun members = members_of_scope(Scope);
    static constexpr ScopeFacts<members.count> facts = read_scope<members.count>(members);
    static constexpr InfoList<facts.rule_count> rules = [] consteval {
        InfoList<facts.rule_count> list{};
        for (std::size_t index = 0; index < facts.rule_count; ++index)
            list.items[index] = facts.rules[index].function;
        return list;
    }();
};

// The index of an entity in a list of count entities, or no_index.
[[nodiscard]] consteval std::uint32_t index_in(std::meta::info const* first, std::size_t count,
                                               std::meta::info entity) {
    for (std::size_t index = 0; index < count; ++index) {
        if (first[index] == entity) return static_cast<std::uint32_t>(index);
    }
    return no_index;
}

template <std::size_t Capacity>
[[nodiscard]] consteval std::uint32_t kind_index(ScopeFacts<Capacity> const& facts, std::meta::info entity) {
    for (std::size_t index = 0; index < facts.agent_count; ++index) {
        if (facts.agents[index].kind == entity) return static_cast<std::uint32_t>(index);
    }
    return no_index;
}

// The rule is a function that the adapter can call: no parameter, and the
// result ReflectedSide.
[[nodiscard]] consteval bool is_callable_rule(std::meta::info function) {
    return function != std::meta::info{} && parameter_count(function) == 0
        && std::meta::remove_cv(std::meta::return_type_of(function)) == ^^ReflectedSide;
}

}  // namespace detail

// ── The checks of the annotations ───────────────────────────────────────

// Each annotation that the adapter reads makes one row of a table: the
// namespace holds an agent, each member of an agent is a port, an agent has
// 1 to max_agent_ports ports, at most one principal member and at most one
// step weight, and each rule has one rule annotation, a pair of agents of the
// namespace, no parameter,
// the result ReflectedSide, and at most one growth door.  Complexity:
// O(A + R) over the facts of the namespace.
template <std::meta::info Scope>
[[nodiscard]] consteval NetVerdict check_annotations() {
    constexpr auto const& facts = detail::NetNamespace<Scope>::facts;
    if (facts.agent_count == 0) {
        return NetVerdict::refuse("net: the namespace '")
            .put_name(detail::name_of(Scope))
            .put("' holds no class with the annotation agent.");
    }
    for (std::size_t index = 0; index < facts.agent_count; ++index) {
        detail::AgentFacts const& kind = facts.agents[index];
        if (kind.stray != std::meta::info{}) {
            return NetVerdict::refuse("net: member '")
                .put_name(detail::name_of(kind.stray))
                .put("' of agent '")
                .put_name(detail::name_of(kind.kind))
                .put("' is no port.  A port is a member of the type Give<T>, Take<T> or Meet<T>.");
        }
        if (kind.member_count == 0 || kind.member_count > max_agent_ports) {
            return NetVerdict::refuse("net: agent '")
                .put_name(detail::name_of(kind.kind))
                .put("' has ")
                .put_number(kind.member_count)
                .put(" ports.  An agent has 1 to 8 ports.");
        }
        if (kind.principal_count > 1) {
            return NetVerdict::refuse("net: agent '")
                .put_name(detail::name_of(kind.kind))
                .put("' has ")
                .put_number(kind.principal_count)
                .put(" members with the annotation principal.  An agent has one principal port.");
        }
        if (kind.weight_count > 1) {
            return NetVerdict::refuse("net: agent '")
                .put_name(detail::name_of(kind.kind))
                .put("' has ")
                .put_number(kind.weight_count)
                .put(" step weights.  An agent kind has at most one step weight.");
        }
    }
    for (std::size_t index = 0; index < facts.rule_count; ++index) {
        detail::RuleFacts const& rule_facts = facts.rules[index];
        if (rule_facts.notes.rule_count != 1) {
            return NetVerdict::refuse("rules: rule '")
                .put_name(detail::name_of(rule_facts.function))
                .put("' has more than one rule annotation.");
        }
        for (std::size_t side = 0; side < 2u; ++side) {
            std::meta::info const kind = detail::argument_of(rule_facts.notes.rule_type, side);
            if (detail::kind_index(facts, kind) == detail::no_index) {
                return NetVerdict::refuse("rules: rule '")
                    .put_name(detail::name_of(rule_facts.function))
                    .put("' names '")
                    .put_name(detail::name_of(kind))
                    .put("', which is no agent of the namespace.");
            }
        }
        if (!detail::is_callable_rule(rule_facts.function)) {
            return NetVerdict::refuse("rules: rule '")
                .put_name(detail::name_of(rule_facts.function))
                .put("' is no function of no parameter that gives a ReflectedSide.");
        }
        if (rule_facts.notes.growth_count > 1) {
            return NetVerdict::refuse("rules: rule '")
                .put_name(detail::name_of(rule_facts.function))
                .put("' has more than one growth door.");
        }
    }
    return NetVerdict{};
}

// ── The tables ──────────────────────────────────────────────────────────

// The signature of the agents of the namespace, in the order of the members.
// The types are the payload classes, in the order of their first port.
template <std::meta::info Scope>
[[nodiscard]] consteval auto signature_of() {
    constexpr auto const& facts = detail::NetNamespace<Scope>::facts;
    constexpr std::size_t types = facts.type_count == 0 ? 1 : facts.type_count;
    constexpr std::size_t kinds = facts.agent_count == 0 ? 1 : facts.agent_count;
    NetSignature<types, kinds> signature{};
    for (std::size_t type = 0; type < facts.type_count; ++type)
        signature.type_names[type] = detail::name_of(facts.types[type]);
    for (std::size_t index = 0; index < facts.agent_count; ++index) {
        detail::AgentFacts const& kind = facts.agents[index];
        AgentKind& out = signature.kinds[index];
        out.name = detail::name_of(kind.kind);
        out.is_boundary = kind.is_boundary;
        std::uint32_t const ports = kind.member_count < max_agent_ports ? kind.member_count : max_agent_ports;
        for (std::uint32_t port = 0; port < ports; ++port) {
            detail::PortFacts const& read = kind.ports[port];
            auto const type = detail::index_in(facts.types, facts.type_count, read.payload);
            out.port_names[port] = detail::name_of(read.member);
            out.port_types[port] = PortType{TypeId{static_cast<std::uint16_t>(type)}, read.polarity, 0};
            if (read.is_principal) out.principal = static_cast<std::uint8_t>(port);
            if (read.is_par) out.par_ports = static_cast<std::uint8_t>(out.par_ports | (1u << port));
        }
    }
    return signature;
}

// The kind of an agent class in signature_of<Scope>(), for the agents of a
// net of that signature.  A class that is no agent of the namespace stops the
// build here.
template <std::meta::info Scope, std::meta::info Agent>
[[nodiscard]] consteval KindId kind_id_of() {
    constexpr std::uint32_t index = detail::kind_index(detail::NetNamespace<Scope>::facts, Agent);
    static_assert(index != detail::no_index, "net: the class is no agent of the namespace");
    return KindId{index};
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// The table of the agents and the rules of the namespace.  A rule that
// check_annotations refuses gives a rule of no pair, which check_rules
// refuses too.
template <std::meta::info Scope>
[[nodiscard]] consteval auto rule_table_of() {
    using Namespace = detail::NetNamespace<Scope>;
    constexpr auto const& facts = Namespace::facts;
    constexpr std::size_t types = facts.type_count == 0 ? 1 : facts.type_count;
    constexpr std::size_t kinds = facts.agent_count == 0 ? 1 : facts.agent_count;
    constexpr std::size_t rules = facts.rule_count == 0 ? 1 : facts.rule_count;
    RuleTable<types, kinds, rules> table{};
    table.signature = signature_of<Scope>();
    std::size_t weighed = 0;
    for (std::size_t kind = 0; kind < facts.agent_count; ++kind) {
        if (facts.agents[kind].weight_count == 0) continue;
        table.step_weights[weighed] = KindWeight{KindId{static_cast<std::uint32_t>(kind)},
                                                 std::meta::extract<StepWeight>(facts.agents[kind].weight)};
        ++weighed;
    }
    std::size_t index = 0;
    template for (constexpr std::meta::info function : Namespace::rules.items) {
        if constexpr (detail::is_callable_rule(function)) {
            constexpr ReflectedSide reflected = [:function:]();
            detail::Notes const notes = facts.rules[index].notes;
            RightSide side{};
            side.agent_count = reflected.agent_count;
            side.wire_count = reflected.wire_count;
            for (std::size_t agent = 0; agent < reflected.agent_count && agent < max_rule_agents; ++agent)
                side.agents[agent] = KindId{detail::kind_index(facts, reflected.agents[agent])};
            for (std::size_t wire = 0; wire < max_rule_wires; ++wire)
                side.wires[wire] = reflected.wires[wire];
            Rule made{detail::name_of(function),
                      KindId{detail::kind_index(facts, detail::argument_of(notes.rule_type, 0))},
                      KindId{detail::kind_index(facts, detail::argument_of(notes.rule_type, 1))}, side};
            if (notes.growth_count != 0) {
                grows const door = std::meta::extract<grows>(notes.growth);
                made = allow_growth(
                    made, NetName{std::meta::extract<char const*>(std::meta::reflect_constant_string(door.reason))});
            }
            table.rules[index] = made;
        }
        ++index;
    }
    return table;
}

#pragma GCC diagnostic pop

// The check of the annotations, then the check of the table.
template <std::meta::info Scope>
[[nodiscard]] consteval RuleVerdict check_rules_of() {
    NetVerdict const annotations = check_annotations<Scope>();
    if (!annotations.is_admitted()) return RuleVerdict{annotations};
    return check_rules(rule_table_of<Scope>());
}

}  // namespace foundation::algebra::net
