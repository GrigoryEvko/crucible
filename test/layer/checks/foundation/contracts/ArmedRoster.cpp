// The compile-time checks of foundation/contracts/ArmedRoster.h.

#include <foundation/contracts/ArmedRoster.h>

namespace foundation::contracts {

// The roster walk, over a stand-in namespace.  Six predicates: one with
// a cell that holds, one with no cell, one with a cell whose accepting
// witness the predicate refuses, one whose parameter is a value and that
// has no cell, one over a value and a type with an instance cell that
// holds, and one with an instance cell whose witness instantiates a
// different predicate.  The walk must count two arms and four unarmed
// predicates.
//
// The walk skips a self_test namespace only below its root.  So a roster
// over the whole foundation tree skips this stand-in, and the walk
// rooted here reads it.
namespace detail::armed_roster_self_test_stand_in {

template <class T>
struct is_char : std::bool_constant<std::is_same_v<T, char>> {};

template <class T>
struct is_long : std::bool_constant<std::is_same_v<T, long>> {};

template <class T>
struct is_short : std::bool_constant<std::is_same_v<T, short>> {};

// Not a predicate by its name, so the walk does not read it.
template <class T>
struct width_of : std::integral_constant<std::size_t, sizeof(T)> {};

// A predicate by its name whose parameter is a value.  It cannot hold a
// cell, so the walk counts it unarmed.
template <int N>
struct is_even : std::bool_constant<N % 2 == 0> {};

// A predicate over a value and a type.  Its instance cell holds.
template <std::size_t Bytes, class T>
struct is_wider_than : std::bool_constant<(sizeof(T) > Bytes)> {};

// A predicate whose instance cell lists a specialization of is_even.
// That witness answers true, but it proves nothing about is_odd.
template <int N>
struct is_odd : std::bool_constant<N % 2 != 0> {};

}  // namespace detail::armed_roster_self_test_stand_in

template <>
struct armed_instances<^^detail::armed_roster_self_test_stand_in::is_wider_than> {
    using accepts = witnesses<detail::armed_roster_self_test_stand_in::is_wider_than<1, int>>;
    using refuses = witnesses<detail::armed_roster_self_test_stand_in::is_wider_than<8, int>,
                              detail::armed_roster_self_test_stand_in::is_wider_than<1, char>>;
};

template <>
struct armed_instances<^^detail::armed_roster_self_test_stand_in::is_odd> {
    using accepts = witnesses<detail::armed_roster_self_test_stand_in::is_even<2>>;
    using refuses = witnesses<detail::armed_roster_self_test_stand_in::is_odd<2>>;
};

template <>
struct armed_cell<detail::armed_roster_self_test_stand_in::is_char> {
    using accepts = witnesses<char>;
    using refuses = witnesses<int, signed char>;
};

template <>
struct armed_cell<detail::armed_roster_self_test_stand_in::is_short> {
    using accepts = witnesses<int>;
    using refuses = witnesses<short>;
};

namespace detail::armed_roster_self_test {

inline constexpr std::meta::info stand_in_scope[] = {
    ^^::foundation::contracts::detail::armed_roster_self_test_stand_in};
inline constexpr std::meta::info no_ledger[] = {
    ^^::foundation::contracts::detail::armed_roster_self_test_stand_in::is_long};

static_assert(names_a_predicate("is_char") && names_a_predicate("row_admits_bg_") && !names_a_predicate("width_of"));
static_assert(armed_cell_holds_v<::foundation::contracts::detail::armed_roster_self_test_stand_in::is_char>);
static_assert(!armed_cell_holds_v<::foundation::contracts::detail::armed_roster_self_test_stand_in::is_short>);

namespace stand_in = ::foundation::contracts::detail::armed_roster_self_test_stand_in;

static_assert(armed_instances_hold_v<^^stand_in::is_wider_than>);
static_assert(!armed_instances_hold_v<^^stand_in::is_odd>,
              "a witness that instantiates a different predicate must not arm this one.");
static_assert(!detail::is_instance_of_predicate<^^stand_in::is_odd, std::true_type>());
static_assert(detail::is_instance_of_predicate<^^stand_in::is_odd, stand_in::is_odd<3>>());

static_assert(armed_roster_verdict(stand_in_scope, {}).walked == 6);
static_assert(armed_roster_verdict(stand_in_scope, {}).armed == 2);
static_assert(armed_roster_verdict(stand_in_scope, {}).unarmed_outside_ledger == 4,
              "the walk must count the predicate with no cell, the predicate whose cell does not hold, the "
              "predicate over a value with no cell, and the predicate whose instance cell does not hold.");
static_assert(armed_roster_verdict(stand_in_scope, no_ledger).unarmed_outside_ledger == 3);
static_assert(armed_roster_verdict(stand_in_scope, no_ledger).ledgered == 1);
static_assert(armed_roster_verdict(stand_in_scope, no_ledger).stale_ledger_entries == 0);

// A ledger entry that is armed is stale, and so is one the walk never
// reaches.
inline constexpr std::meta::info armed_in_ledger[] = {
    ^^::foundation::contracts::detail::armed_roster_self_test_stand_in::is_char};
inline constexpr std::meta::info unwalked_in_ledger[] = {
    ^^::foundation::contracts::detail::armed_roster_self_test_stand_in::width_of};
static_assert(armed_roster_verdict(stand_in_scope, armed_in_ledger).stale_ledger_entries == 1);
static_assert(armed_roster_verdict(stand_in_scope, unwalked_in_ledger).stale_ledger_entries == 1);

static_assert(first_unarmed_outside_ledger(stand_in_scope, no_ledger)
                  == ^^::foundation::contracts::detail::armed_roster_self_test_stand_in::is_short
              || first_unarmed_outside_ledger(stand_in_scope, no_ledger)
                     == ^^::foundation::contracts::detail::armed_roster_self_test_stand_in::is_even);

}  // namespace detail::armed_roster_self_test

}  // namespace foundation::contracts
