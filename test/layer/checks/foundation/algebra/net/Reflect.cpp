// The compile-time checks of foundation/algebra/net/Reflect.h.

#include <foundation/algebra/net/Reflect.h>

#include <meta>

namespace foundation::algebra::net {

namespace detail::reflect_self_test {

struct Value {};
struct Term {};

// ── The sum tree ────────────────────────────────────────────────────────

namespace sums {

struct[[= agent{}]] num {
    Give<Value> value;
};
struct[[= agent{}]] add {
    Take<Value> left;
    Take<Value> right;
    Give<Value> sum;
};
struct[[= agent{}]] add_left {
    Take<Value> right;
    Give<Value> sum;
};
struct[[ = agent{}, = boundary{} ]] result {
    Take<Value> in;
};

[[= rule<add, num>{}]] consteval ReflectedSide add_meets_num() {
    return ReflectedSide{.agents = {^^add_left},
                         .wires = {RuleWire{point_at_agent(0, 0), point_at_interface(0)},
                                   RuleWire{point_at_agent(0, 1), point_at_interface(1)}},
                         .agent_count = 1,
                         .wire_count = 2};
}

[[= rule<add_left, num>{}]] consteval ReflectedSide add_left_meets_num() {
    return ReflectedSide{.agents = {^^num},
                         .wires = {RuleWire{point_at_agent(0, 0), point_at_interface(0)}},
                         .agent_count = 1,
                         .wire_count = 1};
}

}  // namespace sums

// GCC 16 gives an annotation the type T const, so a compare with ^^T finds
// nothing.  The adapter reads the annotations through their type.
static_assert(std::meta::type_of(std::meta::annotations_of (^^sums::num)[0]) == ^^agent const);
static_assert(std::meta::type_of(std::meta::annotations_of (^^sums::num)[0]) != ^^agent);

inline constexpr RuleVerdict sums_verdict = check_rules_of<^^sums>();
static_assert(sums_verdict.is_admitted(), sums_verdict);
static_assert(sums_verdict.growth() == NetGrowth::Shrinks);

inline constexpr auto sums_signature = signature_of<^^sums>();
static_assert(sums_signature.type_names[0] == NetName{"Value"});
static_assert(sums_signature.kinds[1].name == NetName{"add"});
static_assert(sums_signature.kinds[1].port_names[2] == NetName{"sum"});
static_assert(sums_signature.kinds[1].port_types[0] == take(TypeId{0}));
static_assert(sums_signature.kinds[1].port_types[2] == give(TypeId{0}));
static_assert(sums_signature.kinds[3].is_boundary && !sums_signature.kinds[0].is_boundary);
static_assert(check_signature(sums_signature).is_admitted());

// A net of the reflected signature: the tree of three leaves.
inline constexpr KindId num_kind = kind_id_of<^^sums, ^^sums::num>();
inline constexpr KindId add_kind = kind_id_of<^^sums, ^^sums::add>();
inline constexpr KindId result_kind = kind_id_of<^^sums, ^^sums::result>();
static_assert(index_of(num_kind) == 0 && index_of(add_kind) == 1 && index_of(result_kind) == 3);
inline constexpr Net<1, 4, 6, 5> tree_of_three{
    .signature = sums_signature,
    .agents = {num_kind, num_kind, num_kind, add_kind, add_kind, result_kind},
    .wires = {
        Wire{PortRef{AgentId{0}, 0}, PortRef{AgentId{3}, 0}}, Wire{PortRef{AgentId{1}, 0}, PortRef{AgentId{3}, 1}},
        Wire{PortRef{AgentId{3}, 2}, PortRef{AgentId{4}, 0}}, Wire{PortRef{AgentId{2}, 0}, PortRef{AgentId{4}, 1}},
        Wire{PortRef{AgentId{4}, 2}, PortRef{AgentId{5}, 0}}}};
static_assert(check_forest(tree_of_three).is_admitted() && check_dataflow(tree_of_three).is_admitted());

// ── The interaction combinators ─────────────────────────────────────────

namespace combinators {

struct[[= agent{}]] constructor {
    Meet<Term> principal;
    Meet<Term> one;
    Meet<Term> two;
};
struct[[= agent{}]] duplicator {
    Meet<Term> principal;
    Meet<Term> one;
    Meet<Term> two;
};
struct[[= agent{}]] eraser {
    Meet<Term> principal;
};

[[= rule<constructor, constructor>{}]] consteval ReflectedSide constructors_annihilate() {
    return ReflectedSide{.agents = {},
                         .wires = {RuleWire{point_at_interface(0), point_at_interface(3)},
                                   RuleWire{point_at_interface(1), point_at_interface(2)}},
                         .agent_count = 0,
                         .wire_count = 2};
}
[[= rule<duplicator, duplicator>{}]] consteval ReflectedSide duplicators_annihilate() {
    return ReflectedSide{.agents = {},
                         .wires = {RuleWire{point_at_interface(0), point_at_interface(2)},
                                   RuleWire{point_at_interface(1), point_at_interface(3)}},
                         .agent_count = 0,
                         .wire_count = 2};
}
[[= rule<eraser, eraser>{}]] consteval ReflectedSide erasers_vanish() { return ReflectedSide{}; }
[[= rule<eraser, constructor>{}]] consteval ReflectedSide eraser_meets_constructor() {
    return ReflectedSide{.agents = {^^eraser, ^^eraser},
                         .wires = {RuleWire{point_at_agent(0, 0), point_at_interface(0)},
                                   RuleWire{point_at_agent(1, 0), point_at_interface(1)}},
                         .agent_count = 2,
                         .wire_count = 2};
}
[[= rule<eraser, duplicator>{}]] consteval ReflectedSide eraser_meets_duplicator() {
    return ReflectedSide{.agents = {^^eraser, ^^eraser},
                         .wires = {RuleWire{point_at_agent(0, 0), point_at_interface(0)},
                                   RuleWire{point_at_agent(1, 0), point_at_interface(1)}},
                         .agent_count = 2,
                         .wire_count = 2};
}
[[ = rule<constructor, duplicator>{},
   = grows{"the duplicator copies the constructor"} ]] consteval ReflectedSide constructor_meets_duplicator() {
    return ReflectedSide{.agents = {^^duplicator, ^^duplicator, ^^constructor, ^^constructor},
                         .wires = {RuleWire{point_at_agent(0, 0), point_at_interface(0)},
                                   RuleWire{point_at_agent(1, 0), point_at_interface(1)},
                                   RuleWire{point_at_agent(2, 0), point_at_interface(2)},
                                   RuleWire{point_at_agent(3, 0), point_at_interface(3)},
                                   RuleWire{point_at_agent(0, 1), point_at_agent(2, 1)},
                                   RuleWire{point_at_agent(0, 2), point_at_agent(3, 1)},
                                   RuleWire{point_at_agent(1, 1), point_at_agent(2, 2)},
                                   RuleWire{point_at_agent(1, 2), point_at_agent(3, 2)}},
                         .agent_count = 4,
                         .wire_count = 8};
}

}  // namespace combinators

inline constexpr RuleVerdict combinator_verdict = check_rules_of<^^combinators>();
static_assert(combinator_verdict.is_admitted(), combinator_verdict);
static_assert(combinator_verdict.growth() == NetGrowth::Grows && combinator_verdict.growing_rule_count() == 1);
static_assert(rule_table_of<^^combinators>().rules[5].growth_reason()
              == NetName{"the duplicator copies the constructor"});

// ── The refusals of the annotations ─────────────────────────────────────

namespace stray_member {
struct[[= agent{}]] num {
    Give<Value> value;
    int count;
};
}  // namespace stray_member
static_assert(check_annotations<^^stray_member>().says(
    "net: member 'count' of agent 'num' is no port.  A port is a member of the type Give<T>, Take<T> or Meet<T>."));

namespace two_principals {
struct[[= agent{}]] add {
    [[= principal{}]] Take<Value> left;
    [[= principal{}]] Take<Value> right;
};
}  // namespace two_principals
static_assert(check_annotations<^^two_principals>().says(
    "net: agent 'add' has 2 members with the annotation principal.  An agent has one principal port."));

namespace no_agent {
struct plain {};
}  // namespace no_agent
static_assert(
    check_annotations<^^no_agent>().says("net: the namespace 'no_agent' holds no class with the annotation agent."));

namespace foreign_pair {
struct[[= agent{}]] num {
    Give<Value> value;
};
struct outsider {};
[[= rule<num, outsider>{}]] consteval ReflectedSide num_meets_outsider() { return ReflectedSide{}; }
}  // namespace foreign_pair
static_assert(check_rules_of<^^foreign_pair>().says(
    "rules: rule 'num_meets_outsider' names 'outsider', which is no agent of the namespace."));

namespace rule_with_parameter {
struct[[= agent{}]] num {
    Give<Value> value;
};
struct[[= agent{}]] sink {
    Take<Value> in;
};
[[= rule<sink, num>{}]] consteval ReflectedSide sink_meets_num(int unused) {
    return unused == 0 ? ReflectedSide{} : ReflectedSide{};
}
}  // namespace rule_with_parameter
static_assert(check_rules_of<^^rule_with_parameter>().says(
    "rules: rule 'sink_meets_num' is no function of no parameter that gives a ReflectedSide."));

// The checks of Rules.h hold through the adapter: a second rule for a pair,
// and a rule that grows with no door.
namespace twice {
struct[[= agent{}]] num {
    Give<Value> value;
};
struct[[= agent{}]] sink {
    Take<Value> in;
};
[[= rule<sink, num>{}]] consteval ReflectedSide sink_meets_num() { return ReflectedSide{}; }
[[= rule<num, sink>{}]] consteval ReflectedSide num_meets_sink() { return ReflectedSide{}; }
}  // namespace twice
static_assert(check_rules_of<^^twice>().says("rules: rule 'num_meets_sink' is a second rule for the pair 'num', 'sink' "
                                             "after rule 'sink_meets_num'.  A pair has at most one rule, so each step "
                                             "commutes."));

namespace grows_without_door {
struct[[= agent{}]] num {
    Give<Value> value;
};
struct[[= agent{}]] relay {
    Take<Value> in;
    Give<Value> out;
};
[[= rule<relay, num>{}]] consteval ReflectedSide relay_grows() {
    return ReflectedSide{.agents = {^^num, ^^relay, ^^relay},
                         .wires = {RuleWire{point_at_agent(0, 0), point_at_agent(1, 0)},
                                   RuleWire{point_at_agent(1, 1), point_at_agent(2, 0)},
                                   RuleWire{point_at_agent(2, 1), point_at_interface(0)}},
                         .agent_count = 3,
                         .wire_count = 3};
}
}  // namespace grows_without_door
static_assert(check_rules_of<^^grows_without_door>().says(
    "rules: rule 'relay_grows' replaces 2 agents with 3, and only the door allow_growth admits a rule that adds "
    "agents.  Such a step allocates."));

}  // namespace detail::reflect_self_test

}  // namespace foundation::algebra::net
