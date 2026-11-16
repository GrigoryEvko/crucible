// The compile-time checks of foundation/algebra/net/Rules.h.

#include <foundation/algebra/net/Rules.h>

#include <cstdint>

namespace foundation::algebra::net {

namespace detail::rules_self_test {

[[nodiscard]] consteval RuleWire join_ends(RuleEnd left, RuleEnd right) noexcept { return RuleWire{left, right}; }

// ── The value table of the reductions library ───────────────────────────
//
// num gives a value on its principal port.  add takes its left operand on
// its principal port, and add_left holds it and takes the right operand.
// dup gives one value two times.  cmp is a compare-exchange: cmp takes the
// low value, cmp_high holds it and takes the high value, and the step gives
// the minimum and the maximum.  result is a boundary agent.  negate, probe
// and relay are agents of one input and one output for the refusals.

inline constexpr TypeId value{0};

inline constexpr KindId num_kind{0};
inline constexpr KindId add_kind{1};
inline constexpr KindId add_left_kind{2};
inline constexpr KindId dup_kind{3};
inline constexpr KindId cmp_kind{4};
inline constexpr KindId cmp_high_kind{5};
inline constexpr KindId result_kind{6};
inline constexpr KindId negate_kind{7};
inline constexpr KindId probe_kind{8};
inline constexpr KindId relay_kind{9};

inline constexpr NetSignature<1, 10> value_kinds{
    .type_names = {"Value"},
    .kinds = {
        {.name = "num", .port_names = {"value"}, .port_types = {give(value)}},
        {.name = "add", .port_names = {"left", "right", "sum"}, .port_types = {take(value), take(value), give(value)}},
        {.name = "add_left", .port_names = {"right", "sum"}, .port_types = {take(value), give(value)}},
        {.name = "dup", .port_names = {"in", "first", "second"}, .port_types = {take(value), give(value), give(value)}},
        {.name = "cmp",
         .port_names = {"low", "high", "min", "max"},
         .port_types = {take(value), take(value), give(value), give(value)}},
        {.name = "cmp_high",
         .port_names = {"high", "min", "max"},
         .port_types = {take(value), give(value), give(value)}},
        {.name = "result", .port_names = {"in"}, .port_types = {take(value)}, .is_boundary = true},
        {.name = "negate", .port_names = {"in", "out"}, .port_types = {take(value), give(value)}},
        {.name = "probe", .port_names = {"in", "out"}, .port_types = {take(value), give(value)}},
        {.name = "relay", .port_names = {"in", "out"}, .port_types = {take(value), give(value)}},
    }};

// add >< num(a) => add_left(a).  The interface: add.right (0), add.sum (1).
inline constexpr Rule add_meets_num{"add_meets_num", add_kind, num_kind,
                                    RightSide{.agents = {add_left_kind},
                                              .wires = {join_ends(point_at_agent(0, 0), point_at_interface(0)),
                                                        join_ends(point_at_agent(0, 1), point_at_interface(1))},
                                              .agent_count = 1,
                                              .wire_count = 2}};

// add_left(a) >< num(b) => num(a + b).  The interface: add_left.sum (0).
inline constexpr Rule add_left_meets_num{"add_left_meets_num", add_left_kind, num_kind,
                                         RightSide{.agents = {num_kind},
                                                   .wires = {join_ends(point_at_agent(0, 0), point_at_interface(0))},
                                                   .agent_count = 1,
                                                   .wire_count = 1}};

// dup >< num(a) => num(a), num(a).  The interface: dup.first, dup.second.
inline constexpr Rule dup_meets_num{"dup_meets_num", dup_kind, num_kind,
                                    RightSide{.agents = {num_kind, num_kind},
                                              .wires = {join_ends(point_at_agent(0, 0), point_at_interface(0)),
                                                        join_ends(point_at_agent(1, 0), point_at_interface(1))},
                                              .agent_count = 2,
                                              .wire_count = 2}};

// cmp >< num(a) => cmp_high(a).  The interface: cmp.high, cmp.min, cmp.max.
inline constexpr Rule cmp_meets_num{"cmp_meets_num", cmp_kind, num_kind,
                                    RightSide{.agents = {cmp_high_kind},
                                              .wires = {join_ends(point_at_agent(0, 0), point_at_interface(0)),
                                                        join_ends(point_at_agent(0, 1), point_at_interface(1)),
                                                        join_ends(point_at_agent(0, 2), point_at_interface(2))},
                                              .agent_count = 1,
                                              .wire_count = 3}};

// cmp_high(a) >< num(b) => num(min), num(max).  The interface: min, max.
inline constexpr Rule cmp_high_meets_num{"cmp_high_meets_num", cmp_high_kind, num_kind,
                                         RightSide{.agents = {num_kind, num_kind},
                                                   .wires = {join_ends(point_at_agent(0, 0), point_at_interface(0)),
                                                             join_ends(point_at_agent(1, 0), point_at_interface(1))},
                                                   .agent_count = 2,
                                                   .wire_count = 2}};

// x >< num(a) => num(f(a)) for negate, probe and relay.  The interface:
// x.out (0).
inline constexpr RightSide one_value{.agents = {num_kind},
                                     .wires = {join_ends(point_at_agent(0, 0), point_at_interface(0))},
                                     .agent_count = 1,
                                     .wire_count = 1};
inline constexpr Rule negate_meets_num{"negate_meets_num", negate_kind, num_kind, one_value};
inline constexpr Rule probe_meets_num{"probe_meets_num", probe_kind, num_kind, one_value};
inline constexpr Rule relay_meets_num{"relay_meets_num", relay_kind, num_kind, one_value};

inline constexpr RuleTable<1, 10, 8> value_table{.signature = value_kinds,
                                                 .rules = {add_meets_num, add_left_meets_num, dup_meets_num,
                                                           cmp_meets_num, cmp_high_meets_num, negate_meets_num,
                                                           probe_meets_num, relay_meets_num}};
inline constexpr RuleVerdict value_verdict = check_rules(value_table);
static_assert(value_verdict.is_admitted(), value_verdict);
static_assert(value_verdict.growth() == NetGrowth::AllocatesNothing && value_verdict.allocates_nothing());
static_assert(value_verdict.growing_rule_count() == 0 && value_verdict.most_added_agents() == 0);

// The sum tree alone shrinks: each step removes an agent.
inline constexpr NetSignature<1, 4> sum_kinds{
    .type_names = {"Value"},
    .kinds = {value_kinds.kinds[0], value_kinds.kinds[1], value_kinds.kinds[2], value_kinds.kinds[6]}};
inline constexpr RuleTable<1, 4, 2> sum_table{.signature = sum_kinds, .rules = {add_meets_num, add_left_meets_num}};
static_assert(check_rules(sum_table).growth() == NetGrowth::Shrinks);

// ── The refusals of a value table ───────────────────────────────────────

// A second rule for one pair, in the other order.
inline constexpr Rule num_meets_add_again{"num_meets_add_again", num_kind, add_kind, add_meets_num.side()};
inline constexpr RuleTable<1, 10, 9> second_rule_table{
    .signature = value_kinds,
    .rules = {add_meets_num, add_left_meets_num, dup_meets_num, cmp_meets_num, cmp_high_meets_num, negate_meets_num,
              probe_meets_num, relay_meets_num, num_meets_add_again}};
static_assert(check_rules(second_rule_table)
                  .says("rules: rule 'num_meets_add_again' is a second rule for the pair 'num', 'add' after rule "
                        "'add_meets_num'.  A pair has at most one rule, so each step commutes."));

// A pair with dual principal ports and no rule.
inline constexpr RuleTable<1, 10, 7> stuck_table{.signature = value_kinds,
                                                 .rules = {add_meets_num, add_left_meets_num, dup_meets_num,
                                                           cmp_meets_num, cmp_high_meets_num, probe_meets_num,
                                                           relay_meets_num}};
static_assert(check_rules(stuck_table)
                  .says("rules: the agents 'num' and 'negate' have dual principal ports and no rule.  A net with this "
                        "pair stops."));

// A right side that leaves a free port with no wire.
inline constexpr Rule probe_drops_out{"probe_drops_out", probe_kind, num_kind, RightSide{}};
inline constexpr RuleTable<1, 10, 8> dropped_port_table{.signature = value_kinds,
                                                        .rules = {add_meets_num, add_left_meets_num, dup_meets_num,
                                                                  cmp_meets_num, cmp_high_meets_num, negate_meets_num,
                                                                  probe_drops_out, relay_meets_num}};
static_assert(check_rules(dropped_port_table)
                  .says("rules: rule 'probe_drops_out': the right side gives no wire to port 'out' of the left agent "
                        "'probe', which the pair leaves free.  The right side keeps each port of the pair."));

// A right side that takes a value where the free port gives one.
inline constexpr Rule probe_takes_a_value{"probe_takes_a_value", probe_kind, num_kind,
                                          RightSide{.agents = {dup_kind},
                                                    .wires = {join_ends(point_at_agent(0, 0), point_at_interface(0))},
                                                    .agent_count = 1,
                                                    .wire_count = 1}};
inline constexpr RuleTable<1, 10, 8> changed_type_table{.signature = value_kinds,
                                                        .rules = {add_meets_num, add_left_meets_num, dup_meets_num,
                                                                  cmp_meets_num, cmp_high_meets_num, negate_meets_num,
                                                                  probe_takes_a_value, relay_meets_num}};
static_assert(check_rules(changed_type_table)
                  .says("rules: rule 'probe_takes_a_value': wire 0 of the right side joins port 'in' of the new agent "
                        "'dup' (agent 0 of the right side) of type Value? with port 'out' of the left agent 'probe' "
                        "of type Value!.  A new port in the place of a free port keeps its type."));

// ── The growth door ─────────────────────────────────────────────────────
//
// In the value table each kind but num takes on its principal port, so a
// right side that grows holds an active pair or a cycle.  This table adds
// join, which gives a pair of two values on its principal port.  relay >< num
// gives the value as a pair of two copies, so it makes 3 agents from 2, and
// relay >< join gives the pair on.

inline constexpr KindId grown_num_kind{0};
inline constexpr KindId grown_relay_kind{1};
inline constexpr KindId grown_join_kind{2};

inline constexpr NetSignature<1, 3> growth_kinds{
    .type_names = {"Value"},
    .kinds = {
        {.name = "num", .port_names = {"value"}, .port_types = {give(value)}},
        {.name = "relay", .port_names = {"in", "out"}, .port_types = {take(value), give(value)}},
        {.name = "join", .port_names = {"out", "left", "right"}, .port_types = {give(value), take(value), take(value)}},
    }};

// relay >< num(a) => join(num(a), num(a)).  The interface: relay.out (0).
inline constexpr Rule relay_grows{"relay_grows", grown_relay_kind, grown_num_kind,
                                  RightSide{.agents = {grown_join_kind, grown_num_kind, grown_num_kind},
                                            .wires = {join_ends(point_at_agent(0, 0), point_at_interface(0)),
                                                      join_ends(point_at_agent(1, 0), point_at_agent(0, 1)),
                                                      join_ends(point_at_agent(2, 0), point_at_agent(0, 2))},
                                            .agent_count = 3,
                                            .wire_count = 3}};

// relay >< join(l, r) => join(l, r).  The interface: relay.out (0),
// join.left (1), join.right (2).
inline constexpr Rule relay_meets_join{"relay_meets_join", grown_relay_kind, grown_join_kind,
                                       RightSide{.agents = {grown_join_kind},
                                                 .wires = {join_ends(point_at_agent(0, 0), point_at_interface(0)),
                                                           join_ends(point_at_agent(0, 1), point_at_interface(1)),
                                                           join_ends(point_at_agent(0, 2), point_at_interface(2))},
                                                 .agent_count = 1,
                                                 .wire_count = 3}};

// A right side of three agents with no growth door.
inline constexpr RuleTable<1, 3, 2> growth_table{.signature = growth_kinds, .rules = {relay_grows, relay_meets_join}};
static_assert(check_rules(growth_table)
                  .says("rules: rule 'relay_grows' replaces 2 agents with 3, and only the door allow_growth admits a "
                        "rule that adds agents.  Such a step allocates."));

// The same rule through the door: the table grows by one agent.
inline constexpr RuleTable<1, 3, 2> door_table{
    .signature = growth_kinds,
    .rules = {allow_growth(relay_grows, "a relay gives each value as a pair of two copies"), relay_meets_join}};
inline constexpr RuleVerdict door_verdict = check_rules(door_table);
static_assert(door_verdict.is_admitted(), door_verdict);
static_assert(door_verdict.growth() == NetGrowth::Grows && !door_verdict.allocates_nothing());
static_assert(door_verdict.growing_rule_count() == 1 && door_verdict.most_added_agents() == 1);

// A door on a rule that does not grow, and a door with no reason.
inline constexpr RuleTable<1, 10, 8> idle_door_table{
    .signature = value_kinds,
    .rules = {allow_growth(add_meets_num, "no growth"), add_left_meets_num, dup_meets_num, cmp_meets_num,
              cmp_high_meets_num, negate_meets_num, probe_meets_num, relay_meets_num}};
static_assert(check_rules(idle_door_table)
                  .says("rules: rule 'add_meets_num' goes through the growth door, and its right side has 1 agents.  "
                        "Only a rule that adds agents takes the door."));
inline constexpr RuleTable<1, 3, 2> empty_reason_table{.signature = growth_kinds,
                                                       .rules = {allow_growth(relay_grows, ""), relay_meets_join}};
static_assert(!check_rules(empty_reason_table).is_admitted());

// ── An active pair in a right side ──────────────────────────────────────
//
// ping >< pong => ping, pong with their principal ports joined.  The step
// makes its own pair again, so the reduction never stops, and the count of
// the agents stays the same.

inline constexpr NetSignature<1, 2> ping_pong_kinds{
    .type_names = {"Value"},
    .kinds = {
        {.name = "ping", .port_names = {"out", "in"}, .port_types = {give(value), take(value)}},
        {.name = "pong", .port_names = {"in", "out"}, .port_types = {take(value), give(value)}},
    }};

// The interface: ping.in (0), pong.out (1).
inline constexpr Rule ping_meets_pong{"ping_meets_pong", KindId{0}, KindId{1},
                                      RightSide{.agents = {KindId{0}, KindId{1}},
                                                .wires = {join_ends(point_at_agent(0, 0), point_at_agent(1, 0)),
                                                          join_ends(point_at_agent(0, 1), point_at_interface(0)),
                                                          join_ends(point_at_agent(1, 1), point_at_interface(1))},
                                                .agent_count = 2,
                                                .wire_count = 3}};
inline constexpr RuleTable<1, 2, 1> ping_pong_table{.signature = ping_pong_kinds, .rules = {ping_meets_pong}};
static_assert(check_rules(ping_pong_table)
                  .says("rules: rule 'ping_meets_pong': wire 0 of the right side joins the principal ports of the new "
                        "agents 'ping' (agent 0) and 'pong' (agent 1).  A right side holds no active pair, because "
                        "such a pair fires again at once."));

// The same pair with a right side that joins the two free ports shrinks.
inline constexpr Rule ping_pong_annihilate{"ping_pong_annihilate", KindId{0}, KindId{1},
                                           RightSide{.agents = {},
                                                     .wires = {join_ends(point_at_interface(0), point_at_interface(1))},
                                                     .agent_count = 0,
                                                     .wire_count = 1}};
inline constexpr RuleTable<1, 2, 1> annihilate_table{.signature = ping_pong_kinds, .rules = {ping_pong_annihilate}};
static_assert(check_rules(annihilate_table).growth() == NetGrowth::Shrinks);

// A rule that reads a boundary agent, and a rule whose pair is not dual.
inline constexpr Rule result_meets_num{"result_meets_num", result_kind, num_kind, RightSide{}};
inline constexpr RuleTable<1, 10, 9> boundary_table{.signature = value_kinds,
                                                    .rules = {add_meets_num, add_left_meets_num, dup_meets_num,
                                                              cmp_meets_num, cmp_high_meets_num, negate_meets_num,
                                                              probe_meets_num, relay_meets_num, result_meets_num}};
static_assert(check_rules(boundary_table)
                  .says("rules: rule 'result_meets_num' reads the boundary agent 'result'.  A boundary agent stands "
                        "for a free port, so no rule reads it."));
inline constexpr Rule add_meets_dup{"add_meets_dup", add_kind, dup_kind, RightSide{}};
inline constexpr RuleTable<1, 10, 9> not_dual_table{.signature = value_kinds,
                                                    .rules = {add_meets_num, add_left_meets_num, dup_meets_num,
                                                              cmp_meets_num, cmp_high_meets_num, negate_meets_num,
                                                              probe_meets_num, relay_meets_num, add_meets_dup}};
static_assert(check_rules(not_dual_table)
                  .says("rules: rule 'add_meets_dup' joins the principal ports of 'add' and 'dup', which are not "
                        "duals.  The rule never fires."));

// A new agent with no path to the interface: a negate whose output feeds
// its own input.
inline constexpr Rule negate_leaks{"negate_leaks", negate_kind, num_kind,
                                   RightSide{.agents = {num_kind, negate_kind},
                                             .wires = {join_ends(point_at_agent(0, 0), point_at_interface(0)),
                                                       join_ends(point_at_agent(1, 0), point_at_agent(1, 1))},
                                             .agent_count = 2,
                                             .wire_count = 2}};
inline constexpr RuleTable<1, 10, 8> leak_table{.signature = value_kinds,
                                                .rules = {add_meets_num, add_left_meets_num, dup_meets_num,
                                                          cmp_meets_num, cmp_high_meets_num, negate_leaks,
                                                          probe_meets_num, relay_meets_num}};
static_assert(check_rules(leak_table)
                  .says("rules: rule 'negate_leaks': the new agent 'negate' (agent 1 of the right side) has no path to "
                        "the interface, so no other agent reaches it."));

// ── The interaction combinators (Lafont 1997) ───────────────────────────
//
// The constructor, the duplicator and the eraser, with one type that is its
// own dual.  The interface of a pair of two binary agents: the ports one and
// two of the left agent, then of the right agent.  The commutation of the
// duplicator and the constructor makes 4 agents from 2, so it takes the
// growth door.

inline constexpr TypeId term{0};

inline constexpr KindId constructor_kind{0};
inline constexpr KindId duplicator_kind{1};
inline constexpr KindId eraser_kind{2};

inline constexpr NetSignature<1, 3> combinator_kinds{
    .type_names = {"Term"},
    .kinds = {
        {.name = "constructor",
         .port_names = {"principal", "one", "two"},
         .port_types = {meet(term), meet(term), meet(term)}},
        {.name = "duplicator",
         .port_names = {"principal", "one", "two"},
         .port_types = {meet(term), meet(term), meet(term)}},
        {.name = "eraser", .port_names = {"principal"}, .port_types = {meet(term)}},
    }};

// The constructors annihilate crossed, and the duplicators straight.
inline constexpr Rule constructors_annihilate{
    "constructors_annihilate", constructor_kind, constructor_kind,
    RightSide{.agents = {},
              .wires = {join_ends(point_at_interface(0), point_at_interface(3)),
                        join_ends(point_at_interface(1), point_at_interface(2))},
              .agent_count = 0,
              .wire_count = 2}};
inline constexpr Rule duplicators_annihilate{
    "duplicators_annihilate", duplicator_kind, duplicator_kind,
    RightSide{.agents = {},
              .wires = {join_ends(point_at_interface(0), point_at_interface(2)),
                        join_ends(point_at_interface(1), point_at_interface(3))},
              .agent_count = 0,
              .wire_count = 2}};
inline constexpr Rule erasers_vanish{"erasers_vanish", eraser_kind, eraser_kind, RightSide{}};
inline constexpr RightSide two_erasers{.agents = {eraser_kind, eraser_kind},
                                       .wires = {join_ends(point_at_agent(0, 0), point_at_interface(0)),
                                                 join_ends(point_at_agent(1, 0), point_at_interface(1))},
                                       .agent_count = 2,
                                       .wire_count = 2};
inline constexpr Rule eraser_meets_constructor{"eraser_meets_constructor", eraser_kind, constructor_kind, two_erasers};
inline constexpr Rule eraser_meets_duplicator{"eraser_meets_duplicator", eraser_kind, duplicator_kind, two_erasers};
// constructor(x1, x2) >< duplicator(y1, y2): a duplicator on each of x1 and
// x2, a constructor on each of y1 and y2, and their auxiliary ports cross.
inline constexpr Rule constructor_meets_duplicator{
    "constructor_meets_duplicator", constructor_kind, duplicator_kind,
    RightSide{.agents = {duplicator_kind, duplicator_kind, constructor_kind, constructor_kind},
              .wires = {join_ends(point_at_agent(0, 0), point_at_interface(0)),
                        join_ends(point_at_agent(1, 0), point_at_interface(1)),
                        join_ends(point_at_agent(2, 0), point_at_interface(2)),
                        join_ends(point_at_agent(3, 0), point_at_interface(3)),
                        join_ends(point_at_agent(0, 1), point_at_agent(2, 1)),
                        join_ends(point_at_agent(0, 2), point_at_agent(3, 1)),
                        join_ends(point_at_agent(1, 1), point_at_agent(2, 2)),
                        join_ends(point_at_agent(1, 2), point_at_agent(3, 2))},
              .agent_count = 4,
              .wire_count = 8}};

inline constexpr RuleTable<1, 3, 6> combinator_table{
    .signature = combinator_kinds,
    .rules = {constructors_annihilate, duplicators_annihilate, erasers_vanish, eraser_meets_constructor,
              eraser_meets_duplicator,
              allow_growth(constructor_meets_duplicator,
                           "the duplicator copies the constructor, one copy for each auxiliary port")}};
inline constexpr RuleVerdict combinator_verdict = check_rules(combinator_table);
static_assert(combinator_verdict.is_admitted(), combinator_verdict);
static_assert(combinator_verdict.growth() == NetGrowth::Grows && combinator_verdict.growing_rule_count() == 1);
static_assert(combinator_verdict.most_added_agents() == 2);

// A rule for a pair of one kind that changes when the two agents change
// places: the free port of the left pivot goes into the principal port of a
// new pivot, and the free port of the right pivot into its other port.
inline constexpr NetSignature<1, 2> pivot_kinds{
    .type_names = {"Term"},
    .kinds = {
        {.name = "pivot", .port_names = {"principal", "aux"}, .port_types = {meet(term), meet(term)}},
        {.name = "constructor",
         .port_names = {"principal", "one", "two"},
         .port_types = {meet(term), meet(term), meet(term)}},
    }};
inline constexpr Rule pivots_twist{"pivots_twist", KindId{0}, KindId{0},
                                   RightSide{.agents = {KindId{0}},
                                             .wires = {join_ends(point_at_agent(0, 0), point_at_interface(0)),
                                                       join_ends(point_at_agent(0, 1), point_at_interface(1))},
                                             .agent_count = 1,
                                             .wire_count = 2}};
inline constexpr Rule pivot_meets_constructor{
    "pivot_meets_constructor", KindId{0}, KindId{1},
    RightSide{.agents = {KindId{1}},
              .wires = {join_ends(point_at_agent(0, 0), point_at_interface(0)),
                        join_ends(point_at_agent(0, 1), point_at_interface(1)),
                        join_ends(point_at_agent(0, 2), point_at_interface(2))},
              .agent_count = 1,
              .wire_count = 3}};
inline constexpr Rule pivot_constructors_annihilate{"pivot_constructors_annihilate", KindId{1}, KindId{1},
                                                    constructors_annihilate.side()};
inline constexpr RuleTable<1, 2, 3> pivot_table{
    .signature = pivot_kinds, .rules = {pivots_twist, pivot_meets_constructor, pivot_constructors_annihilate}};
static_assert(check_rules(pivot_table)
                  .says("rules: rule 'pivots_twist' for the pair 'pivot', 'pivot' changes when the two agents change "
                        "places.  The pair has no order, so the rule must be symmetric."));

// The same table with a symmetric rule for the pivots is admitted.
inline constexpr Rule pivots_join{"pivots_join", KindId{0}, KindId{0},
                                  RightSide{.agents = {},
                                            .wires = {join_ends(point_at_interface(0), point_at_interface(1))},
                                            .agent_count = 0,
                                            .wire_count = 1}};
inline constexpr RuleTable<1, 2, 3> symmetric_pivot_table{
    .signature = pivot_kinds, .rules = {pivots_join, pivot_meets_constructor, pivot_constructors_annihilate}};
static_assert(check_rules(symmetric_pivot_table).growth() == NetGrowth::Shrinks);

}  // namespace detail::rules_self_test

}  // namespace foundation::algebra::net
