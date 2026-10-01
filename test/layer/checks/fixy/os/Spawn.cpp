// The compile-time checks of fixy/os/Spawn.h.

#include <fixy/os/Spawn.h>

namespace fixy::spawn::detail::spawn_self_test {

namespace atom_spawn = ::fixy::atom::spawn;
using ::fixy::atom::IsAtom;

// ── The rationale atoms ─────────────────────────────────────────────

static_assert(atom_spawn::rationale_nonempty_v<::fixy::atom::ctrl::rationale{"x"}>);
static_assert(atom_spawn::rationale_nonempty_v<::fixy::atom::ctrl::rationale{"reason"}>);
static_assert(!atom_spawn::rationale_nonempty_v<::fixy::atom::ctrl::rationale{""}>);

// Each one is an atom, on the axis design note 2 names.
static_assert(IsAtom<atom_spawn::detach_with<"r">>);
static_assert(IsAtom<atom_spawn::syscall_only<"r">>);
static_assert(IsAtom<atom_spawn::subprocess<"r">>);
static_assert(atom_spawn::detach_with<"r">::axis == Axis::Protocol);
static_assert(atom_spawn::syscall_only<"r">::axis == Axis::Protocol);
static_assert(atom_spawn::subprocess<"r">::axis == Axis::Protocol);

// Stating a justification performs no operation, so none of them lifts to
// an effect row.  That is what keeps a rationale out of a context gate.
static_assert(!::foundation::effects::LiftsToRow<atom_spawn::detach_with<"r">>,
              "a rationale atom must not lift to a row: saying why is not doing anything.");
static_assert(!::foundation::effects::LiftsToRow<atom_spawn::syscall_only<"r">>);
static_assert(!::foundation::effects::LiftsToRow<atom_spawn::subprocess<"r">>);

static_assert(::fixy::atom::detail::every_roster_member_is_atom_<::fixy::atom::detail::spawn_atom_samples>(),
              "fixy/os/Spawn.h: a member of spawn_atom_samples is not an atom.");
static_assert(
    ::fixy::atom::detail::every_roster_member_on_axis_<::fixy::atom::detail::spawn_atom_samples, Axis::Protocol>(),
    "fixy/os/Spawn.h: every spawn rationale atom engages Axis::Protocol.");

static_assert(std::is_empty_v<atom_spawn::detach_with<"x">>);
static_assert(sizeof(atom_spawn::detach_with<"x">) == 1);
static_assert(atom_spawn::detach_with<"audit">::reason.size() == 6);  // "audit" plus the terminator

// The rationale is part of the type, so two reasons are two types.
static_assert(!std::is_same_v<atom_spawn::detach_with<"reason_a">, atom_spawn::detach_with<"reason_b">>);
static_assert(!std::is_same_v<atom_spawn::detach_with<"x">, atom_spawn::syscall_only<"x">>);

// ── The throws gate ─────────────────────────────────────────────────

struct PlainCallable {
    void operator()() const noexcept {}
};

// A callable whose type names the atom, at the default family and at a
// named one.
template <typename Marker>
struct MarkedCallable {
    void operator()() const noexcept {}
};

struct SampleException {};

static_assert(detail::no_callable_throws_v<PlainCallable>);
static_assert(detail::no_callable_throws_v<PlainCallable, PlainCallable>);
static_assert(!detail::no_callable_throws_v<MarkedCallable<::fixy::atom::ctrl::throws<>>>);
static_assert(!detail::no_callable_throws_v<MarkedCallable<::fixy::atom::ctrl::throws<SampleException>>>,
              "a callable marked with a named exception family must be refused too. A search for the default "
              "family alone lets this case pass.");
static_assert(!detail::no_callable_throws_v<PlainCallable, MarkedCallable<::fixy::atom::ctrl::throws<>>>,
              "one marked callable anywhere in the pack refuses the whole spawn.");
static_assert(detail::no_callable_throws_v<>, "an empty pack spawns nothing and carries no throw.");

}  // namespace fixy::spawn::detail::spawn_self_test
