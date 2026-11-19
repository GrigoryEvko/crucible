#pragma once

// Pairs of grades that must not coexist on one binding.
//
// Each rule here is a theorem about two axes, with its citation.  The
// rules are knowledge, not boilerplate: the reason a pair is refused is
// the point, and the message a reader sees is the theorem.
//
// ---------------------------------------------------------------------
// The three headers
//
// fixy/collision/Codes.h holds the rule codes, the record of the premises,
// the rule table and the corpus, and this header includes it.  This header
// holds the grade lookup, the joined atom roster, the readers of the
// grades, and the passes that give the premises and the verdict of a
// binding.  fixy/collision/Report.h holds the checks
// that compare the corpus, the rosters and the rules, and only the check
// files include it.  The std headers of the three come from the lowest
// header that uses them, so a header above it does not name them again.
// The quarantine ledger counts each include of such a header in the
// base, and that count can only fall.
//
// ---------------------------------------------------------------------
// Live rules and pending rules
//
// A rule can only fire if an atom can move both of its axes off their
// strict poles.  The shipped atom catalog reaches every axis but Type,
// whose grade is the payload the binding names.  A rule that read an
// axis with no atom could never fire, no matter what a caller writes.
//
// A rule that cannot fire is a comment.  A rule that cannot fire and
// that nobody can tell cannot fire is worse than a comment: it looks
// like coverage and demands nothing.  That shape — something that looks
// like a gate and asks for nothing — recurs, so the pending rules here
// are not marked with a TODO and left to be believed.  They are
// registered against the axis they wait on, and
// `every_pending_axis_is_still_empty()` FIRES the day
// that axis gains its first atom, naming the rules that must then be
// wired.  Forgetting is not possible, because forgetting reddens.
//
// The two counts are assertions rather than something a reader tallies.
//
// ---------------------------------------------------------------------
// Why grades<Atoms...> and not fn
//
// A rule that reads a member of fn completes fn, whose body asserts
// ValidComposition in fixy/Reject.h, and the instantiation recurses.  GCC
// reports "satisfaction of atomic constraint depends on itself".  The pass
// of the premises reads the record of the pack instead, which it
// calculates from the pack alone, so a rule does not complete fn.  fn
// reads its own grades through the same record, so a binding and its
// rules cannot resolve one pack two ways.
//
// ---------------------------------------------------------------------
// Why binding_verdict_v<Payload, Atoms...> takes the payload, and still not fn
//
// Five rules pair a grade with the payload's replay claim, which is the
// DetSafe band the payload carries and nothing in the pack.  I002 and
// I003 read a failure that the payload returns as a Result<T, E>, an
// Option<T> or a std::expected<T, E>.  So the verdict takes the payload as
// a template parameter of its own.  That is the first template parameter
// of the fn, and never a member of the fn: a read of a member would
// complete fn, and the cycle above closes.  pack_verdict_v<Atoms...> is
// the same verdict with void for the payload, under which each payload
// rule stands down, so a pack-only cell keeps meaning what it says.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Bands.h>
#include <fixy/atoms/Barrier.h>
#include <fixy/atoms/Ctrl.h>
#include <fixy/atoms/Dispatch.h>
#include <fixy/atoms/Fp.h>
#include <fixy/atoms/Global.h>
#include <fixy/atoms/Hw.h>
#include <fixy/atoms/Observe.h>
#include <fixy/atoms/Os.h>
#include <fixy/atoms/Regime.h>
#include <fixy/atoms/Scope.h>
#include <fixy/atoms/Session.h>
#include <fixy/atoms/Simd.h>
#include <fixy/atoms/Stack.h>
#include <fixy/atoms/Stdio.h>
#include <fixy/atoms/Sync.h>
#include <fixy/atoms/Syscall.h>
#include <fixy/collision/Codes.h>
#include <fixy/Secret.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Modality.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Anchor.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <meta>
#include <optional>
#include <type_traits>
#include <utility>

namespace fixy {

// The three spawn atoms that the Protocol reading of L003 names, declared
// and not included.  fixy/concurrent/Spawn.h defines them beside the
// spawning mints, and an include here would pull the thread door and the
// permission layer into each header that sees this one.  The relation
// compares reflections of the templates, so it needs only their names.
// Each redeclaration must match the template head of Spawn.h exactly, so a
// change there that this file does not follow is a compile error.
namespace atom::spawn {
template <::fixy::atom::ctrl::rationale Rationale>
struct detach_with;
template <::fixy::atom::ctrl::rationale Rationale>
struct syscall_only;
template <::fixy::atom::ctrl::rationale Rationale>
struct subprocess;
}  // namespace atom::spawn

namespace collision {

// ---------------------------------------------------------------------
// The pack as a grade lookup.

namespace detail {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// The pack as reflections.  define_static_array gives each element a
// constant address.  `template for` binds the element as constexpr only
// with that address, and a splice names its type with it.  fixy/Reject.h
// also walks the pack through this helper.
template <class... Atoms>
[[nodiscard]] consteval auto pack_entries_() {
    using entry_list =
        ::foundation::reflect::anchored_t<std::meta::reflect_constant(sizeof...(Atoms)), std::vector<std::meta::info>>;
    return std::define_static_array(entry_list{^^Atoms...});
}

// The grades of one pack, read in one walk of the pack.
//
// The record has one slot for each axis.  The slot holds the grade that an
// atom of the pack states on the axis, or the strict pole of the axis when
// no atom of the pack names it.  Axis::Type has no pole, and its slot holds
// void.  Each grade is a type and never an alias, so two slots hold one
// grade exactly when their reflections are equal.
//
// The record also counts the atoms on each axis and names the first entry
// that is not an atom.  So tier 2, tier 4, the grade lookup and the
// question whether a pack names an axis read one walk.  An entry that is
// not an atom states no axis, and the walk does not read one from it.
// When two atoms name one axis, the slot holds the last of them.
struct pack_record {
    std::meta::info grade[::fixy::axis_count]{};
    // True when the grade of the axis is not its strict pole.
    bool names_axis[::fixy::axis_count]{};
    std::size_t atoms_on[::fixy::axis_count]{};
    std::meta::info first_malformed = ^^void;
};

// The walk.  It runs one time for each pack, because pack_record_v keeps
// its answer.  Complexity: the axis count plus the pack size.
template <class... Atoms>
[[nodiscard]] consteval pack_record read_pack_() noexcept {
    pack_record record{};
    std::meta::info pole[::fixy::axis_count]{};
    for (std::size_t place = 0; place < ::fixy::axis_count; ++place) {
        const std::meta::info strict = axis_record(static_cast<Axis>(place)).strict;
        pole[place] = strict == std::meta::info{} ? ^^void : std::meta::dealias(strict);
        record.grade[place] = pole[place];
    }
    static constexpr auto entries = pack_entries_<Atoms...>();
    template for (constexpr auto entry : entries) {
        using Candidate = [:entry:];
        if constexpr (::fixy::atom::IsAtom<Candidate>) {
            constexpr std::size_t place = std::to_underlying(Candidate::axis);
            record.grade[place] = std::meta::dealias(entry);
            ++record.atoms_on[place];
        } else {
            if (record.first_malformed == ^^void) record.first_malformed = entry;
        }
    }
    for (std::size_t place = 0; place < ::fixy::axis_count; ++place)
        record.names_axis[place] = record.grade[place] != pole[place];
    return record;
}

#pragma GCC diagnostic pop

template <class... Atoms>
inline constexpr pack_record pack_record_v = read_pack_<Atoms...>();

// The slot of an axis.  A value outside the enum has no row in the axis
// table, so it has no pole.  Only a cast produces such a value, and a cast
// into the resolver is a mistake and not a question.
template <Axis A>
[[nodiscard]] consteval std::size_t axis_slot_() noexcept {
    static_assert(std::to_underlying(A) < ::fixy::axis_count,
                  "fixy::collision::grades<Atoms...>::on<A>: A is not an Axis enumerator.  A value outside the "
                  "enum has no row in the axis table of fixy/Axis.h, so it has no pole.");
    return std::to_underlying(A);
}

// The grade of the pack on one axis, as a type.
template <Axis A, class... Atoms>
using grade_on_t = [:pack_record_v<Atoms...>.grade[axis_slot_<A>()]:];

}  // namespace detail

// The one resolver of a pack.  The collision rules, the corpus and
// fn::grade_on all read it, and fn answers only the Type axis itself,
// with the payload the binding names.  Here Axis::Type resolves to void,
// and no rule reads it.
//
// Every entry must be an atom of the closed catalog.  A rule reads a
// grade by its axis alone, so a type that states an axis without being
// an atom would be read as a shipped grade.
//
// An axis the pack does not mention resolves to the strict pole of its
// row in the axis table.  The switch of axis_record names each enumerator,
// so each axis of the enum has a row.  The check file of this header shows
// where the resolver depends on that table.
template <class... Atoms>
    requires(::fixy::atom::IsAtom<Atoms> && ...)
struct grades {
    template <Axis A>
    using on = detail::grade_on_t<A, Atoms...>;

    template <Axis A>
    static constexpr bool mentions = detail::pack_record_v<Atoms...>.names_axis[detail::axis_slot_<A>()];
};

// ---------------------------------------------------------------------
// Which axes an atom can actually reach.
//
// A namespace walk cannot answer this: a parametric atom such as
// atom::with<Es...> is a template, not a class, so members_of reports
// the template and the walk never sees an axis for it.  The family
// rosters name concrete instantiations, and each family already proves
// its roster covers its namespace (every_atom_in_is_rostered_), so the
// join below is the complete picture.

using all_atom_roster = ::fixy::atom::detail::roster_cat_t<
    ::fixy::atom::detail::core_atom_roster, ::fixy::atom::detail::barrier_atom_roster,
    ::fixy::atom::detail::ctrl_atom_roster, ::fixy::atom::detail::dispatch_atom_roster,
    ::fixy::atom::detail::fp_atom_roster, ::fixy::atom::detail::global_atom_roster,
    ::fixy::atom::detail::hw_atom_roster, ::fixy::atom::detail::observe_atom_roster,
    ::fixy::atom::detail::os_atom_roster, ::fixy::atom::detail::regime_atom_roster,
    ::fixy::atom::detail::scope_atom_roster, ::fixy::atom::detail::session_atom_roster,
    ::fixy::atom::detail::simd_atom_roster, ::fixy::atom::detail::stack_atom_roster,
    ::fixy::atom::detail::stdio_atom_roster, ::fixy::atom::detail::sync_atom_roster,
    ::fixy::atom::detail::syscall_atom_roster>;

namespace detail {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

// The axis of each member of a roster, in the order of the members.  The
// walk expands once for each member, and each question about an axis
// reads this table, so a question instantiates no template for each
// member.
template <class Roster>
[[nodiscard]] consteval auto roster_axes_() noexcept {
    static constexpr auto members = ::fixy::atom::detail::roster_members_v<Roster>;
    std::array<Axis, members.size()> axes{};
    Axis* const axis = axes.data();
    std::size_t place = 0;
    template for (constexpr auto entry : members) {
        using Candidate = [:entry:];
        axis[place++] = Candidate::axis;
    }
    return axes;
}

#pragma GCC diagnostic pop

template <class Roster>
inline constexpr auto roster_axes_v = roster_axes_<Roster>();

// The population is a template parameter, so only a call reads its table.
template <Axis A, class Population = all_atom_roster>
[[nodiscard]] consteval bool axis_has_an_atom_() noexcept {
    for (const Axis axis : roster_axes_v<Population>) {
        if (axis == A) return true;
    }
    return false;
}

}  // namespace detail

template <Axis A>
inline constexpr bool axis_has_an_atom = detail::axis_has_an_atom_<A>();

// The axes no shipped atom reaches.  A rule reading one of them is
// registered as pending in fixy/collision/Codes.h.  Axis::Type is not
// here: it is
// caller-supplied by design, being the payload itself, so it is the one
// axis that is atomless and complete rather than atomless and waiting.
//
// The list is empty: every axis but Type has an atom, so every axis but
// Type can be written.  The families that give each axis its atoms are
// the headers under fixy/atoms/, and each one's rules are named in the
// corpus of fixy/collision/Codes.h.
//
// It is a std::array rather than a C array so it can hold zero entries.
// A C array of length zero is ill-formed.
inline constexpr std::array<Axis, 0> pending_axes{};

inline constexpr std::size_t pending_axis_count = pending_axes.size();

[[nodiscard]] consteval bool axis_is_pending(Axis axis) noexcept {
    for (const Axis listed : pending_axes) {
        if (listed == axis) return true;
    }
    return false;
}

// ---------------------------------------------------------------------
// The gate that makes a pending rule impossible to forget.
//
// One assertion, both directions.  A pending axis that gains its first
// atom leaves the set and fires this, naming the rules to wire.  A
// family roster forgotten from all_atom_roster makes an axis look empty
// and fires it too.
//
// pending_axes is empty, so only the second direction can fire, and it
// is the one worth keeping: the assertion currently reads
// "every axis but Type has an atom", and a family header that stops being
// joined into the roster takes that claim down.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

template <class Anchor = void>
[[nodiscard]] consteval bool every_pending_axis_is_still_empty() noexcept {
    bool unchanged = true;
    template for (constexpr auto axis_member : std::define_static_array(
                      static_cast<::foundation::reflect::anchored_t<^^Anchor, std::vector<std::meta::info>>>(
                          std::meta::enumerators_of(^^::fixy::Axis)))) {
        constexpr Axis axis = [:axis_member:];
        if constexpr (axis != Axis::Type) {
            unchanged = unchanged && (axis_is_pending(axis) != axis_has_an_atom<axis>);
        }
    }
    return unchanged;
}

#pragma GCC diagnostic pop

// ---------------------------------------------------------------------
// Reading a grade.
//
// A parametric atom carries its argument in the template-id, so the
// question "does this grade name a fence at or above SeqCst" is answered
// by matching the atom rather than by calling anything on it.
//
// No reader here reads the Effect grade.  A rule that asks what the
// binding does reads the row of the binding, which fixy/Atom.h computes:
// the Effect grade joined with the row that the atoms of the pack lift
// to.  That row is a canonical Row, so a question about one effect in it
// is row_contains.

namespace detail {

// P010's set is the observable atoms without Bg.  Whether an atom is
// observable is decided in foundation/effects/Effect.h, where a count pin
// makes each new atom take a decision, so a new observable atom joins
// this set there and cannot be left out here.  Bg is left out because the
// rules that read a Bg row, L007 and R003 among them, carry its theorem.
//
// The reader takes an Effect grade and reads its row through the closed
// relation of fixy/Atom.h, so a stated with<IO> answers as the row Row<IO>
// does.  P010 gives it the row of the binding, which is a canonical Row.
// A type that is not an Effect grade has no row, and the constraint
// refuses it.  It does not read as a row with no observable effect.
template <::foundation::effects::Effect... Es>
[[nodiscard]] consteval bool names_observable_effect_without_bg_(::foundation::effects::Row<Es...>) noexcept {
    return ((::foundation::effects::is_observable<Es>() && Es != ::foundation::effects::Effect::Bg) || ...);
}

template <::fixy::atom::IsEffectGrade G>
struct row_admits_observable_
    : std::bool_constant<names_observable_effect_without_bg_(::fixy::atom::effect_row_of_t<G>{})> {};

// The payload's replay claim.
//
// A payload carrying a DetSafe band at PhiloxRng or Pure claims that its
// bytes are replay-deterministic: the same inputs give the same bits on
// any host.  That claim is what F101, F102, V101, V203 and S011 pair with
// a grade that falsifies it — an FP mode that reorders a sum, an ISA pin
// that makes the reduction order host-dependent, a timestamp read, a
// capability that replay cannot mint again.
//
// The premise is the band, and this is the read.  The primary is false:
// a payload with no band claims nothing about replay, and every replay
// rule stands down for it.  void, the pack-only view's payload, takes
// that primary.
//
// A band of another lattice grades the payload and does not change it, so
// the claim is read through it.  The canonical order puts DetSafe outside
// each other band, and a stack in another order, such as
// Vendor<V, DetSafe<Pure, T>>, claims replay as fully as DetSafe<Pure, T>
// does.  The outermost DetSafe band decides.
template <class Payload>
struct is_replay_deterministic_ : std::false_type {};
template <class Payload>
    requires ::fixy::is_band_of_v<::foundation::algebra::lattices::DetSafeLattice, Payload>
struct is_replay_deterministic_<Payload>
    : std::bool_constant<::foundation::algebra::lattices::DetSafeLattice::leq(
          ::foundation::algebra::lattices::DetSafeTier::PhiloxRng, Payload::lattice_type::tier)> {};
template <class Payload>
    requires ::fixy::IsBand<Payload>
          && (!::fixy::is_band_of_v<::foundation::algebra::lattices::DetSafeLattice, Payload>)
struct is_replay_deterministic_<Payload> : is_replay_deterministic_<::fixy::band_value_t<Payload>> {};

// The fine grades of the payload against the row.  The row is the one
// source of the coarse fact, and a band of AllocClass or Wait refines it.
// A Pool or a Stack grade claims that no allocator ran.  A UmwaitC01,
// BoundedSpin or SpinPause grade claims that the wait stayed out of the
// kernel.  Each claim is read through the bands of other lattices, as the
// replay claim is, and the outermost band of the lattice decides.
template <class Payload>
inline constexpr bool claims_no_allocation_v_ = false;
template <class Payload>
    requires ::fixy::is_band_of_v<::foundation::algebra::lattices::AllocClassLattice, Payload>
inline constexpr bool claims_no_allocation_v_<Payload> = ::foundation::algebra::lattices::AllocClassLattice::leq(
    ::foundation::algebra::lattices::AllocClassTag::Pool, ::fixy::band_tier_v<Payload>);
template <class Payload>
    requires ::fixy::IsBand<Payload>
              && (!::fixy::is_band_of_v<::foundation::algebra::lattices::AllocClassLattice, Payload>)
inline constexpr bool claims_no_allocation_v_<Payload> = claims_no_allocation_v_<::fixy::band_value_t<Payload>>;

template <class Payload>
inline constexpr bool claims_user_space_wait_v_ = false;
template <class Payload>
    requires ::fixy::is_band_of_v<::foundation::algebra::lattices::WaitLattice, Payload>
inline constexpr bool claims_user_space_wait_v_<Payload> = ::foundation::algebra::lattices::WaitLattice::leq(
    ::foundation::algebra::lattices::WaitStrategy::UmwaitC01, ::fixy::band_tier_v<Payload>);
template <class Payload>
    requires ::fixy::IsBand<Payload> && (!::fixy::is_band_of_v<::foundation::algebra::lattices::WaitLattice, Payload>)
inline constexpr bool claims_user_space_wait_v_<Payload> = claims_user_space_wait_v_<::fixy::band_value_t<Payload>>;

// The Observability row, extracted from its grade.
//
// The Effect row is NOT extracted here.  fixy/Atom.h carries the one
// closed relation — atom::effect_row_of_t, over the stated with<Es...>
// and the bare Row the strict pole leaves behind — and
// atom::binding_row_of_t joins it with the lifts of the pack.  The rules
// below read that join, so this file holds no second copy of the
// relation.  A grade of any other shape fails there by name rather than
// passing as the empty row.
//
// Observability keeps its own map, because its shape is different: its
// pole is derived from Effect's, so the pole is that same empty row, and
// the answer Row<> for each other grade is the WEAKEST answer on this side
// rather than the strongest.  A binding whose observability grade is
// unrecognised claims to observe nothing, and B002 admits it, which is
// the direction that stays safe.
//
// The map is a deduction.  It is a member of a class that is not a
// template, so no other file can add an overload of it.  An explicit
// specialization of the member keeps the return type of the template, so
// no file can give a surface another row.
struct observability_parts_ {
    template <::foundation::effects::Effect... Es>
    static ::foundation::effects::Row<Es...> row_of(const ::fixy::atom::observe::surface<Es...>*) noexcept;
    static ::foundation::effects::Row<> row_of(const void*) noexcept;
};

template <class G>
using observability_row_t = decltype(observability_parts_::row_of(static_cast<const G*>(nullptr)));

// The hardware-instruction tier, read against a floor.  A grade with no
// tier does not satisfy the concept, because the strict pole of
// HwInstruction is Unconstrained: a binding that names no tier makes no
// claim about instruction classes, so neither V201 nor V202 fires on it.
// The chain order comes from fixy/atoms/Hw.h's own at_or_above, so a rule
// and the atom header cannot disagree about which way the ladder runs.
// The read is a concept, and a concept has no specialization, so no unit
// can give a grade another tier.
template <class G, ::fixy::atom::hw::HwInstruction Floor>
concept HasHwTierAtOrAbove =
    requires { G::tier; } && std::is_same_v<std::remove_cvref_t<decltype(G::tier)>, ::fixy::atom::hw::HwInstruction>
    && ::fixy::atom::hw::at_or_above(G::tier, Floor);

// The provided fence strength, read against a floor.  The primary is
// false: a binding that names no strength provides no fence, so there
// is nothing for V301 to refuse on it.  The order comes from
// fixy/atoms/Barrier.h's own at_or_above, which is the lattice's leq in
// its admission direction, so a rule cannot invert it.  The order is not
// a chain: an acquire load is not at or above a release floor, nor the
// reverse, so a floor on either tag admits only its own tag and above.  This
// and HasHwTierAtOrAbove both read a member named `tier`.  The type check
// in each constraint keeps each from answering for the atoms of the other.
template <::foundation::algebra::lattices::BarrierStrength Floor, class G>
struct is_barrier_at_or_above_ : std::false_type {};
template <::foundation::algebra::lattices::BarrierStrength Floor, class G>
    requires requires { G::tier; }
          && std::is_same_v<std::remove_cvref_t<decltype(G::tier)>, ::foundation::algebra::lattices::BarrierStrength>
struct is_barrier_at_or_above_<Floor, G> : std::bool_constant<::fixy::atom::barrier::at_or_above(G::tier, Floor)> {};

// The reached scope, read against a floor.  The primary is false: a
// binding that names no scope publishes to nobody in particular, so
// V401 has nothing to refuse on it.  The order is the lattice's own leq
// through fixy/atoms/Scope.h, and that lattice is two trunks, so a scope
// on the host trunk is NOT at or above an accelerator floor — Inner and
// Cluster are incomparable — and a rule reading this stands down for it
// rather than reading "incomparable" as "wide".
template <::foundation::algebra::lattices::MemoryScope Floor, class G>
struct is_scope_at_or_above_ : std::false_type {};
template <::foundation::algebra::lattices::MemoryScope Floor, class G>
    requires requires { G::scope; }
          && std::is_same_v<std::remove_cvref_t<decltype(G::scope)>, ::foundation::algebra::lattices::MemoryScope>
struct is_scope_at_or_above_<Floor, G> : std::bool_constant<::fixy::atom::scope::at_or_above(G::scope, Floor)> {};

// The two trunk readings V402 composes, and the ISA pin V101 reads.
// Each primary is false: a binding that names no scope or no ISA pins
// no trunk, so the rules have nothing to refuse on it.  The trunk
// predicates come from fixy/atoms/Scope.h and fixy/atoms/Simd.h, so the
// division is read from one place per axis.
template <class G>
struct is_scope_trunk_pinned_ : std::false_type {};
template <class G>
    requires requires { G::scope; }
          && std::is_same_v<std::remove_cvref_t<decltype(G::scope)>, ::foundation::algebra::lattices::MemoryScope>
struct is_scope_trunk_pinned_<G> : std::bool_constant<::fixy::atom::scope::is_trunk_pinned(G::scope)> {};

template <class G>
struct is_scope_on_host_trunk_ : std::false_type {};
template <class G>
    requires requires { G::scope; }
          && std::is_same_v<std::remove_cvref_t<decltype(G::scope)>, ::foundation::algebra::lattices::MemoryScope>
struct is_scope_on_host_trunk_<G> : std::bool_constant<::fixy::atom::scope::on_host_trunk(G::scope)> {};

template <class G>
struct is_isa_trunk_pinned_ : std::false_type {};
template <class G>
    requires requires { G::isa; } && std::is_same_v<std::remove_cvref_t<decltype(G::isa)>, ::fixy::atom::simd::SimdIsa>
struct is_isa_trunk_pinned_<G> : std::bool_constant<::fixy::atom::simd::is_trunk_pinned(G::isa)> {};

template <class G>
struct is_isa_on_arm_trunk_ : std::false_type {};
template <class G>
    requires requires { G::isa; } && std::is_same_v<std::remove_cvref_t<decltype(G::isa)>, ::fixy::atom::simd::SimdIsa>
struct is_isa_on_arm_trunk_<G> : std::bool_constant<::fixy::atom::simd::on_arm_trunk(G::isa)> {};

// Whether the FP mode NAMES one setting value.  The atom on this axis is
// a product rather than a family, so a rule asks it about one setting at
// a time and the atom answers from the pack it was written with.  The
// primary is false, which is the strict pole read correctly: a binding
// that names no mode leaves every setting at its enum's first
// enumerator — reassociation forbidden, contraction off — and those are
// exactly the values no rule refuses.
template <auto Wanted, class G>
struct fp_mode_has_setting_ : std::false_type {};
template <auto Wanted, class G>
    requires requires { G::template names<Wanted>; }
struct fp_mode_has_setting_<Wanted, G> : std::bool_constant<G::template names<Wanted>> {};

// The busy-wait classification, lifted from a grade to a type-level
// answer.  The primary is false because the strict pole of
// Synchronization is not a wait at all: a binding that names no strategy
// makes no claim about waiting, so W002 does not fire on it.
//
// It delegates to the consteval predicate in fixy/atoms/Sync.h and does
// not list the grades again, so a grade that moves sides moves for the
// atom header and for the rule together.  W001 has no predicate of this
// kind.  It reads the lift of a kernel wait, and fixy/atoms/Sync.h
// asserts that a wait lifts to Row<Block> exactly when it enters the
// kernel.
template <class G>
struct is_busy_wait_ : std::false_type {};
template <class G>
    requires requires { G::strategy; }
struct is_busy_wait_<G> : std::bool_constant<::fixy::atom::sync::burns_the_core(G::strategy)> {};

// Whether a grade names a wait strategy.  The strict pole of
// Synchronization names none, and a binding that states no atom on the
// axis takes that pole.  Such a grade states no wait, and W003 does not
// read it as one.
template <class G>
struct has_wait_strategy_ : std::false_type {};
template <class G>
    requires requires {
        { G::strategy } -> std::same_as<const ::foundation::algebra::lattices::WaitStrategy&>;
    }
struct has_wait_strategy_<G> : std::true_type {};

template <class G>
struct repr_is_atomic_ : std::false_type {};
template <>
struct repr_is_atomic_<::fixy::atom::repr<::fixy::pole::ReprKind::Atomic>> : std::true_type {};

template <class G>
struct is_thread_local_ : std::false_type {};
template <class Tag>
struct is_thread_local_<::fixy::atom::global::thread_local_<Tag>> : std::true_type {};

template <class G>
struct is_longjmp_unsafe_ : std::false_type {};
template <auto Reason>
struct is_longjmp_unsafe_<::fixy::atom::ctrl::longjmp_unsafe<Reason>> : std::true_type {};

// The function type a call family names, read in one structural step.
// References and cv are stripped first.  A function type names itself,
// and a pointer to a function or a pointer to a member names its pointee.
// The pointee of a member pointer can carry cv and ref qualifiers, and it
// is still one function type.  A class names its signature through a
// member type `signature` that is not a class.  Every other family names
// nothing, and the reader gives void.
template <class Family>
struct named_signature_ {
    using type = void;
};
template <class Family>
    requires(!std::same_as<Family, std::remove_cvref_t<Family>>)
struct named_signature_<Family> : named_signature_<std::remove_cvref_t<Family>> {};
template <class Function>
    requires std::is_function_v<Function>
struct named_signature_<Function> {
    using type = Function;
};
template <class Function>
    requires std::is_function_v<Function>
struct named_signature_<Function*> {
    using type = Function;
};
template <class Function, class Owner>
    requires std::is_function_v<Function>
struct named_signature_<Function Owner::*> {
    using type = Function;
};
template <class Family>
    requires std::is_class_v<Family> && requires { typename Family::signature; }
          && (!std::is_class_v<std::remove_cvref_t<typename Family::signature>>)
struct named_signature_<Family> : named_signature_<typename Family::signature> {};

// Whether a family names a signature that is noexcept.  Reflection reads
// the noexcept of the function type, so each form of the type is read by
// one query: cv and ref qualifiers, a C variadic tail, and every return
// and parameter type.  A family that names no signature answers false.
// That is the default of the language: a function declared with no
// noexcept-specifier is potentially-throwing ([except.spec]).
template <class Family>
[[nodiscard]] consteval bool names_noexcept_signature_() noexcept {
    using Signature = typename named_signature_<Family>::type;
    if constexpr (std::is_void_v<Signature>) {
        return false;
    } else {
        return std::meta::is_noexcept(^^Signature);
    }
}

template <class Family>
struct is_signature_noexcept_ : std::bool_constant<names_noexcept_signature_<Family>()> {};

// What D001 reads in a CallShape grade, as one closed relation.
//
// D001 asks one question of the CallShape grade: does the binding call
// through a pointer whose callee can throw?  An indirect_call names its
// family, and D001 reads the signature of that family.  A recursion and
// the strict pole call through no pointer.
//
// The relation is a function over reflections and not a template, so no
// other file can add a grade to it.  It answers for a template as for each
// of its specializations.  A CallShape grade that it does not name gets
// Unread, and the reader of D001 stops the build on that grade.  So a new
// atom on the axis cannot stand down as a call that cannot throw.  The
// strict pole is named by its type, and the check file of this header
// makes sure that the relation reads the pole of fixy/Axis.h and each
// CallShape atom of the roster.
enum class CallShapeReading : std::uint8_t {
    Unread = 0,  // the relation does not name the grade
    NoIndirectCall = 1,  // the strict pole, recurses<N> and recurses_unbounded: no call through a pointer
    IndirectCall = 2,  // indirect_call<Family>: a call through a pointer that the family names
};

[[nodiscard]] consteval CallShapeReading call_shape_reading_(std::meta::info grade) noexcept {
    const std::meta::info named = std::meta::dealias(grade);
    if (named == ^^::fixy::pole::Unconstrained<Axis::CallShape>
        || named == ^^::fixy::atom::dispatch::recurses_unbounded) {
        return CallShapeReading::NoIndirectCall;
    }
    const std::meta::info declared = std::meta::has_template_arguments(named) ? std::meta::template_of(named) : named;
    if (declared == ^^::fixy::atom::dispatch::indirect_call) return CallShapeReading::IndirectCall;
    if (declared == ^^::fixy::atom::dispatch::recurses) return CallShapeReading::NoIndirectCall;
    return CallShapeReading::Unread;
}

// An indirect call can throw unless its family names a noexcept
// signature.  An unknown family reads as one that throws.  A grade with
// no reading stops the build with one message, and then gives the answer
// that refuses.
template <class G>
[[nodiscard]] consteval bool indirect_call_throws_() noexcept {
    constexpr CallShapeReading reading = call_shape_reading_(^^G);
    static_assert(reading != CallShapeReading::Unread,
                  "fixy/Collision.h: the CallShape grade of this binding has no reading in "
                  "fixy::collision::detail::call_shape_reading_, so D001 cannot tell whether the binding calls "
                  "through a pointer whose callee can throw. Name the atom in the closed relation beside "
                  "indirect_call, recurses and recurses_unbounded.");
    if constexpr (reading == CallShapeReading::IndirectCall) {
        using Family = [:std::meta::template_arguments_of(std::meta::dealias(^^G))[0]:];
        return !is_signature_noexcept_<Family>::value;
    } else {
        return reading == CallShapeReading::Unread;
    }
}

template <class G>
struct can_indirect_call_throw_ : std::bool_constant<indirect_call_throws_<G>()> {};

// What L003 reads in a Protocol grade, as one closed relation.
//
// L003 asks one question of the Protocol grade: does it state a spawn that
// can run after the frame of the caller unwinds?  detach_with is never
// joined, and syscall_only is a raw clone that the structured join does
// not reach.  Each child shares the address space of the caller.  A
// subprocess runs in its own copy of the address space, so a borrow there
// refers to a copy and cannot dangle into the frame of the parent.  A
// protocol, a live handle and the strict pole state no spawn.
//
// The relation is closed as the CallShape relation above is.  A Protocol
// grade that it does not name stops the reader of L003, so a new spawn
// atom cannot stand down as a spawn that a join ties to the frame.  The
// spawn atoms are a sample set outside the roster, so a walk in the check
// file of fixy/collision/Report.h reads the declarations of their
// namespace.
enum class ProtocolReading : std::uint8_t {
    Unread = 0,  // the relation does not name the grade
    NoSpawn = 1,  // the strict pole, protocol<P> and session::live_handle<P>
    SpawnOutlivesTheFrame = 2,  // spawn::detach_with and spawn::syscall_only
    SpawnInItsOwnSpace = 3,  // spawn::subprocess
};

[[nodiscard]] consteval ProtocolReading protocol_reading_(std::meta::info grade) noexcept {
    const std::meta::info named = std::meta::dealias(grade);
    if (named == ^^::fixy::pole::proto::None) return ProtocolReading::NoSpawn;
    const std::meta::info declared = std::meta::has_template_arguments(named) ? std::meta::template_of(named) : named;
    if (declared == ^^::fixy::atom::protocol || declared == ^^::fixy::atom::session::live_handle) {
        return ProtocolReading::NoSpawn;
    }
    if (declared == ^^::fixy::atom::spawn::detach_with || declared == ^^::fixy::atom::spawn::syscall_only) {
        return ProtocolReading::SpawnOutlivesTheFrame;
    }
    if (declared == ^^::fixy::atom::spawn::subprocess) return ProtocolReading::SpawnInItsOwnSpace;
    return ProtocolReading::Unread;
}

// A grade with no reading stops the build with one message, and then
// gives the answer that refuses.
template <class G>
[[nodiscard]] consteval bool spawn_outlives_frame_() noexcept {
    constexpr ProtocolReading reading = protocol_reading_(^^G);
    static_assert(reading != ProtocolReading::Unread,
                  "fixy/Collision.h: the Protocol grade of this binding has no reading in "
                  "fixy::collision::detail::protocol_reading_, so L003 cannot tell whether the binding spawns a "
                  "child that can outlive its frame. Name the atom in the closed relation beside the spawn atoms.");
    return reading == ProtocolReading::Unread || reading == ProtocolReading::SpawnOutlivesTheFrame;
}

template <class G>
struct can_spawn_outlive_the_frame_ : std::bool_constant<spawn_outlives_frame_<G>()> {};

// ── The failure path, in the spellings the tree has ─────────────────
//
// A failure is not a row atom.  foundation/effects/Effect.h says why: an
// enumerator is one bit, and the error type is part of the failure.  The
// tree spells a failure in the type, and each spelling is the only
// spelling of its mechanism:
//
//   the payload is Result<T, E>         — the body returns the failure and its error
//   the payload is Option<T>            — the body returns the absence of a value
//   the payload is std::expected<T, E>  — the std spelling of a Result
//   ControlFlow is ctrl::throws<E>      — the body throws it
//
// So there is no second declaration of a failure to drift from the
// return type: the return type IS the declaration.  A band grades a
// payload and does not change what it holds, so the payload is read
// through every band first.  Every other wrapper is left in place.  In
// particular Secret<Result<T, E>> is not read as a failure, because its
// discriminant and its error are both inside the secret.
//
// Each reader answers three things: whether the path exists, whether the
// path carries an error value, and whether that error type is
// fixy::Secret.  An Option carries no error value, so only its
// discriminant leaves the binding.  An error type the reader cannot see
// — ctrl::any_exception, the family a bare throws<> names — is not
// Secret, which is the answer that refuses rather than the one that
// admits.

// The payload after each of its bands, as a type and never an alias.  The
// function is not a template, so no other file can give a band another
// value.  Complexity: the depth of the stack of bands.
[[nodiscard]] consteval std::meta::info unbanded_of(std::meta::info payload) {
    std::meta::info named = std::meta::dealias(payload);
    while (std::meta::extract<bool>(std::meta::substitute(^^::fixy::IsBand, {named})))
        named = std::meta::dealias(std::meta::substitute(^^::fixy::band_value_t, {named}));
    return named;
}

// The failure that a payload returns.
struct ReturnedFailure {
    bool fails = false;
    bool carries_error = false;
    bool error_is_secret = false;
};

// A payload that returns its failure as a std::expected does: it names the
// type of its error and the type of its unexpected value.  A std::expected
// is such a payload, and no file can change the members of a class of the
// standard library.  A concept and an alias template cannot have a
// specialization.
template <class Payload>
concept ReturnsExpectedFailure = requires {
    typename Payload::error_type;
    typename Payload::unexpected_type;
};

template <class Payload>
using expected_error_t = typename Payload::error_type;

[[nodiscard]] consteval bool is_secret_type_(std::meta::info type) {
    return std::meta::extract<bool>(std::meta::substitute(^^::fixy::IsSecret, {type}));
}

// The failure that a payload returns.  The function is not a template, so
// no other file can give a payload another failure.  The arms of Result
// and Option read the template of the payload, as IsInstanceOf does.  An
// arm on the pattern Option<Value> makes the gate read the open template
// niche of Choice.h, through the constraint ChoicePayload of the class
// template, and a unit can specialize niche.  A std::expected answers the
// concept ReturnsExpectedFailure.
[[nodiscard]] consteval ReturnedFailure returned_failure_of(std::meta::info payload) {
    const std::meta::info named = std::meta::dealias(std::meta::remove_cvref(payload));
    if (!std::meta::has_template_arguments(named)) return ReturnedFailure{};
    const std::meta::info family = std::meta::template_of(named);
    if (family == ^^::foundation::core::Result) {
        // The error type is the second argument of the Result.
        const std::meta::info error = std::meta::template_arguments_of(named)[1];
        return ReturnedFailure{.fails = true, .carries_error = true, .error_is_secret = is_secret_type_(error)};
    }
    if (family == ^^::foundation::core::Option) return ReturnedFailure{.fails = true};
    if (!std::meta::extract<bool>(std::meta::substitute(^^ReturnsExpectedFailure, {named}))) return ReturnedFailure{};
    const std::meta::info error = std::meta::dealias(std::meta::substitute(^^expected_error_t, {named}));
    return ReturnedFailure{.fails = true, .carries_error = true, .error_is_secret = is_secret_type_(error)};
}

// The failure that a ControlFlow grade throws, in the same three answers.
// Only ctrl::throws<E> throws, and its error is the exception family E.
// The function is not a template, so no other file can give a grade
// another failure.
[[nodiscard]] consteval ReturnedFailure thrown_failure_of(std::meta::info grade) {
    const std::meta::info named = std::meta::dealias(grade);
    if (!std::meta::has_template_arguments(named) || std::meta::template_of(named) != ^^::fixy::atom::ctrl::throws) {
        return ReturnedFailure{};
    }
    // The exception family is the one argument of throws.
    const std::meta::info family = std::meta::template_arguments_of(named)[0];
    return ReturnedFailure{.fails = true, .carries_error = true, .error_is_secret = is_secret_type_(family)};
}

// The class of the Security grade, read so that a grade the closed
// relation in fixy/Atom.h does not name stops the build with one message.
// The relation gives no class for such a grade, so a direct read would
// also stop the build, but with one error at each use and more in fn's
// tier walk.  After the assertion the answer is Classified, which is the
// answer that refuses, so nothing downstream reads the unnamed grade as
// public.
template <class G>
[[nodiscard]] consteval ::fixy::atom::SecurityClass security_class_or_refuse_() noexcept {
    static_assert(::fixy::atom::IsSecurityGrade<G>,
                  "fixy/Collision.h: the Security grade of this binding has no entry in "
                  "fixy::atom::detail::security_class_answer_of_, so no rule can tell whether the binding is "
                  "classified. Give the atom a SecurityClass beside the others in fixy/Atom.h.");
    if constexpr (::fixy::atom::IsSecurityGrade<G>) {
        return ::fixy::atom::security_class_of(^^G);
    } else {
        return ::fixy::atom::SecurityClass::Classified;
    }
}

// Whether the Protocol grade names a session.  The same axis carries the
// spawn atoms L003 reads, and protocol<proto::None> writes the strict
// pole out, so neither is a session.  A live handle is a session.
template <class G>
struct is_session_protocol_ : std::false_type {};
template <class Proto>
struct is_session_protocol_<::fixy::atom::protocol<Proto>>
    : std::bool_constant<!std::is_same_v<Proto, ::fixy::pole::proto::None>> {};
template <class Proto>
struct is_session_protocol_<::fixy::atom::session::live_handle<Proto>> : std::true_type {};

// Whether the Protocol grade states that the frame holds a live handle.
template <class G>
struct is_live_handle_grade_ : std::false_type {};
template <class Proto>
struct is_live_handle_grade_<::fixy::atom::session::live_handle<Proto>> : std::true_type {};

// Whether a payload is a session handle that still owes its protocol.
// The test reads the Stepping contract: the modality that
// foundation/algebra/Modality.h reserves for session handles, and the
// handle's own answer to is_terminal().  A Stepping type that does not
// answer is_terminal() is read as live, which is the answer that
// refuses.  A handle at End owes nothing, so a frame may hold it across
// a suspension.
//
// The residual, stated: the test reads the payload after its bands, and
// no deeper.  A handle inside an aggregate or a container is not seen,
// and a binding that holds one states session::live_handle instead.
template <class Payload>
[[nodiscard]] consteval bool payload_owes_a_protocol_() noexcept {
    if constexpr (requires {
                      requires std::same_as<std::remove_cvref_t<decltype(Payload::modality)>,
                                            ::foundation::algebra::ModalityKind>;
                  }) {
        if constexpr (Payload::modality == ::foundation::algebra::ModalityKind::Stepping) {
            if constexpr (requires {
                              { Payload::is_terminal() } -> std::same_as<bool>;
                          }) {
                return !Payload::is_terminal();
            } else {
                return true;
            }
        } else {
            return false;
        }
    } else {
        return false;
    }
}

template <class Payload>
struct is_live_handle_payload_
    : std::bool_constant<payload_owes_a_protocol_<std::remove_cvref_t<typename[:unbanded_of(^^Payload):]>>()> {};

// Whether ControlFlow states a suspension.  ctrl::coroutine<Policy> is
// the same suspension that atom::coroutine states on Reentrancy, written
// on the axis of the ways a frame is left.
template <class G>
struct control_flow_has_suspension_ : std::false_type {};
template <class SuspensionPolicy>
struct control_flow_has_suspension_<::fixy::atom::ctrl::coroutine<SuspensionPolicy>> : std::true_type {};

}  // namespace detail

// ---------------------------------------------------------------------
// What a Trust grade says: its level, one tag of fixy/Tags.h.
//
// T001 asks one question of the Trust grade: did anything verify the
// provenance of the binding?  The strict pole of Trust is the level
// tags::trust::Unverified itself, and the atom trust<tags::trust::Unverified>
// names that level, so the two give the same answer.  A binding that says
// nothing about Trust is unverified, and T001 reads it so.
//
// The relation is a function over reflections and not a template, so no
// other file can add a grade to it.  A Trust grade that it does not name
// has no level, and T001 stops the build on that grade.  The walk in the
// check file of this header makes sure that every Trust atom of the
// roster has a level.
namespace detail {

// The level of the grade, or the null reflection when the relation names
// no level.  A template argument is a type and never an alias.
[[nodiscard]] consteval std::meta::info trust_level_of_(std::meta::info grade) {
    const std::meta::info named = std::meta::dealias(grade);
    if (named == std::meta::dealias(axis_record(Axis::Trust).strict)) return named;
    if (std::meta::has_template_arguments(named)) {
        const auto primary = std::meta::template_of(named);
        if (primary == ^^::fixy::atom::trust) return std::meta::template_arguments_of(named)[0];
        if (primary == ^^::fixy::atom::trust_assumed) return ^^::fixy::tags::trust::Assumed;
    }
    return {};
}

}  // namespace detail

template <class Grade>
concept IsTrustGrade = detail::trust_level_of_(^^Grade) != decltype(^^Grade){};

// The level of a Trust grade, as a reflection of its tag.  A grade that the
// relation does not name has no level, and a read of it stops the build.
template <IsTrustGrade Grade>
inline constexpr auto trust_level_of_v = detail::trust_level_of_(^^Grade);

// The Trust grade has the level Level of fixy/Tags.h.  A template argument
// is a type and never an alias, so ^^Level needs no dealias.
template <class Grade, class Level>
concept HasTrustLevel = IsTrustGrade<Grade> && ::fixy::tags::IsTrustLevel<Level> && trust_level_of_v<Grade> == ^^Level;

// ---------------------------------------------------------------------
// The premises of the rules, and the verdict of each rule.
//
// fixy/collision/Codes.h declares the record of the premises and the rule
// table.  The passes below give each premise of a binding one time, and the
// verdict applies each row of the table to the premises one time.

namespace detail {

[[nodiscard]] consteval bool names_axis_(const pack_record& pack, Axis axis) noexcept {
    return pack.names_axis[std::to_underlying(axis)];
}

// The premises that the pack states.  The pass runs one time for each
// pack, because pack_premises_v keeps its answer, and only a pack that
// tiers 1 to 4 admit reaches it.
template <class... Atoms>
[[nodiscard]] consteval rule_premises read_pack_premises_() noexcept {
    constexpr const pack_record& pack = pack_record_v<Atoms...>;
    using Usage = grade_on_t<Axis::Usage, Atoms...>;
    using ControlFlow = grade_on_t<Axis::ControlFlow, Atoms...>;
    using CallShape = grade_on_t<Axis::CallShape, Atoms...>;
    using MemoryScope = grade_on_t<Axis::MemoryScope, Atoms...>;
    using BarrierStrength = grade_on_t<Axis::BarrierStrength, Atoms...>;
    using HwInstruction = grade_on_t<Axis::HwInstruction, Atoms...>;
    using SimdIsa = grade_on_t<Axis::SimdIsa, Atoms...>;
    using FpMode = grade_on_t<Axis::FpMode, Atoms...>;
    using Synchronization = grade_on_t<Axis::Synchronization, Atoms...>;
    using Protocol = grade_on_t<Axis::Protocol, Atoms...>;
    using Security = grade_on_t<Axis::Security, Atoms...>;
    using Observability = grade_on_t<Axis::Observability, Atoms...>;
    constexpr ReturnedFailure thrown = thrown_failure_of(pack.grade[std::to_underlying(Axis::ControlFlow)]);
    namespace fe = ::foundation::effects;
    namespace lat = ::foundation::algebra::lattices;

    // The rows of the binding.  The row joins the Effect grade with the
    // lifts of the atoms of the pack, through the relation of fixy/Atom.h
    // that fixy/Fn.h also reads for context admission.  Each rule that
    // asks what the binding does reads this row.  A binding can state a
    // futex call or a park and leave its Effect grade empty, and a rule
    // that reads only the grade admits that binding.
    using binding_row = ::fixy::atom::binding_row_of_t<grade_on_t<Axis::Effect, Atoms...>, Atoms...>;
    constexpr std::meta::info row = ^^binding_row;

    rule_premises premise{};
    premise.borrow = std::is_same_v<Usage, ::fixy::atom::borrow>;
    premise.ghost = std::is_same_v<Usage, ::fixy::atom::ghost>;
    premise.capability = std::is_same_v<Usage, ::fixy::atom::capability_usage>;
    // A binding says nothing about Usage when it takes the strict pole,
    // which on this axis is the linear grade: consumed exactly once.
    premise.linear = !names_axis_(pack, Axis::Usage);
    premise.monotonic = std::is_same_v<grade_on_t<Axis::Mutation, Atoms...>, ::fixy::atom::mut_monotonic>;
    premise.unverified = trust_level_of_v<grade_on_t<Axis::Trust, Atoms...>> == ^^::fixy::tags::trust::Unverified;

    // "Unstated" is the strict pole on Complexity, so an unstated cost is
    // the absence of a grade rather than a grade of its own.  A cost and a
    // space are facts about the binding, and an unstated grade puts no
    // bound on it.
    premise.cost_unstated = !names_axis_(pack, Axis::Complexity);
    premise.unbounded_cost = std::is_same_v<grade_on_t<Axis::Complexity, Atoms...>, ::fixy::atom::cost_unbounded>;
    premise.may_run_unbounded = premise.cost_unstated || premise.unbounded_cost || !names_axis_(pack, Axis::Space)
                             || std::is_same_v<grade_on_t<Axis::Space, Atoms...>, ::fixy::atom::space_unbounded>;

    premise.row_bg = fe::row_contains(row, fe::Effect::Bg);
    premise.row_observable = row_admits_observable_<binding_row>::value;
    premise.allocates = fe::row_contains(row, fe::Effect::Alloc);
    premise.row_alloc_or_io = premise.allocates || fe::row_contains(row, fe::Effect::IO);
    premise.row_init = fe::row_contains(row, fe::Effect::Init);
    premise.blocks = fe::row_contains(row, fe::Effect::Block);

    // The concurrency premise that several rules share.  A coroutine
    // suspends and a Bg row runs elsewhere, and either makes the body
    // concurrent with respect to the frame of the caller.  A suspension
    // can be written on Reentrancy or on ControlFlow, and the premise reads
    // both, so a pack cannot avoid a rule by writing the suspension on the
    // other axis.
    premise.coroutine = std::is_same_v<grade_on_t<Axis::Reentrancy, Atoms...>, ::fixy::atom::coroutine>;
    premise.suspends_in_control_flow = control_flow_has_suspension_<ControlFlow>::value;
    premise.suspends = premise.coroutine || premise.suspends_in_control_flow;
    premise.concurrent = premise.suspends || premise.row_bg;

    // Observability names which part of the row of the binding is
    // observation.  observes_within_the_row holds when the surface names
    // only effects of that row.
    premise.observes_something = names_axis_(pack, Axis::Observability);
    premise.observes_within_the_row = fe::Subrow<observability_row_t<Observability>, binding_row>;

    premise.atomic_repr = repr_is_atomic_<grade_on_t<Axis::Representation, Atoms...>>::value;
    premise.thread_local_state = is_thread_local_<grade_on_t<Axis::GlobalState, Atoms...>>::value;
    premise.longjmp_unsafe = is_longjmp_unsafe_<ControlFlow>::value;
    // A recursion with a proven depth bound has a bounded stack, so D002
    // reads only the recursion that states no depth bound.
    premise.recurses_unbounded = std::is_same_v<CallShape, ::fixy::atom::dispatch::recurses_unbounded>;
    // A store that a thread-local slot publishes past its own thread has a
    // reader on another thread.  The scope of the binding states how far
    // the store reaches, and the scope thread, the shared bottom, reaches
    // no other thread.
    premise.publishes_past_the_thread =
        names_axis_(pack, Axis::MemoryScope) && !std::is_same_v<MemoryScope, ::fixy::atom::scope::thread>;
    premise.indirect_call_may_throw = can_indirect_call_throw_<CallShape>::value;
    premise.spawn_outlives_the_frame = can_spawn_outlive_the_frame_<Protocol>::value;
    // A call through the vDSO and a deliberate leak lift the empty row, but
    // each atom still states an emitting surface on one of these two axes.
    premise.emits_outside_the_row = names_axis_(pack, Axis::Stdio) || names_axis_(pack, Axis::SyscallSurface);

    premise.hot = std::is_same_v<grade_on_t<Axis::Regime, Atoms...>, ::fixy::atom::regime::hot>;
    // pred::True is the Refinement strict pole, again an absence.
    premise.no_witness_floor = !names_axis_(pack, Axis::Refinement);
    premise.states_stdio = names_axis_(pack, Axis::Stdio);
    premise.core_burning_spin = is_busy_wait_<Synchronization>::value;
    premise.states_a_wait = has_wait_strategy_<Synchronization>::value;

    premise.hw_nondeterministic =
        HasHwTierAtOrAbove<HwInstruction, ::fixy::atom::hw::HwInstruction::NonDeterministicTsc>;
    premise.hw_privileged = HasHwTierAtOrAbove<HwInstruction, ::fixy::atom::hw::HwInstruction::PrivilegedMsr>;
    premise.barrier_seq_cst_or_above = is_barrier_at_or_above_<lat::BarrierStrength::SeqCst, BarrierStrength>::value;
    premise.barrier_acq_rel_or_above = is_barrier_at_or_above_<lat::BarrierStrength::AcqRel, BarrierStrength>::value;
    premise.scope_cluster_or_above = is_scope_at_or_above_<lat::MemoryScope::Cluster, MemoryScope>::value;
    premise.scope_pinned = is_scope_trunk_pinned_<MemoryScope>::value;
    premise.isa_pinned = is_isa_trunk_pinned_<SimdIsa>::value;
    premise.trunks_cohere = is_scope_on_host_trunk_<MemoryScope>::value && is_isa_on_arm_trunk_<SimdIsa>::value;

    premise.fp_rewrites_without_bound =
        fp_mode_has_setting_<::fixy::atom::fp::FpReassociate::UnrestrictedRewrite, FpMode>::value;
    premise.fp_reassociates = premise.fp_rewrites_without_bound
                           || fp_mode_has_setting_<::fixy::atom::fp::FpReassociate::BoundedTreeDepth, FpMode>::value;
    premise.fp_contracts_across_statements = fp_mode_has_setting_<::fixy::atom::fp::FpContract::Fast, FpMode>::value;
    premise.fp_mode_stated = names_axis_(pack, Axis::FpMode);
    premise.fp_flushes_denormal_inputs =
        fp_mode_has_setting_<::foundation::core::FpDenormalInput::DenormalsAreZero, FpMode>::value;
    premise.fp_flushes_subnormal_results = fp_mode_has_setting_<::foundation::core::FpFtz::FlushToZero, FpMode>::value;

    // A declassification discharges a rule only on a channel that the mask
    // of its policy names, in fixy/Atom.h.  The send is an export through
    // IO.  No policy licenses the failure channel.
    premise.constant_time = security_class_or_refuse_<Security>() == ::fixy::atom::SecurityClass::ConstantTime;
    premise.classified_on_send = ::fixy::atom::IsClassifiedOn<Security, ::fixy::atom::DischargeAxis::IO>;
    premise.classified_on_failure = ::fixy::atom::IsClassifiedOn<Security, ::fixy::atom::DischargeAxis::Failure>;
    premise.states_staleness = names_axis_(pack, Axis::Staleness);
    premise.session_protocol = is_session_protocol_<Protocol>::value;
    premise.has_live_handle = is_live_handle_grade_<Protocol>::value;

    premise.fails = thrown.fails;
    premise.fails_with_plain_error = thrown.carries_error && !thrown.error_is_secret;
    return premise;
}

template <class... Atoms>
inline constexpr rule_premises pack_premises_v = read_pack_premises_<Atoms...>();

// The premises that the payload states, read through its bands.  The
// premises of void, the payload of the pack-only view, are all false.
struct payload_premises {
    bool replay_deterministic{};
    bool claims_no_allocation{};
    bool claims_user_space_wait{};
    bool owes_a_protocol{};
    ReturnedFailure returned_failure{};
};

template <class Payload>
[[nodiscard]] consteval payload_premises read_payload_premises_() {
    return payload_premises{.replay_deterministic = is_replay_deterministic_<Payload>::value,
                            .claims_no_allocation = claims_no_allocation_v_<Payload>,
                            .claims_user_space_wait = claims_user_space_wait_v_<Payload>,
                            .owes_a_protocol = is_live_handle_payload_<Payload>::value,
                            .returned_failure = returned_failure_of(unbanded_of(^^Payload))};
}

template <class Payload>
inline constexpr payload_premises payload_premises_v = read_payload_premises_<Payload>();

// The premises of one binding: the premises of its pack, with the facts of
// its payload added.  A failure comes from the payload or from a throw,
// and a live handle from the Protocol grade or from the payload.
[[nodiscard]] consteval rule_premises join_premises_(rule_premises pack, const payload_premises& payload) noexcept {
    const ReturnedFailure& returned = payload.returned_failure;
    pack.replay_deterministic = payload.replay_deterministic;
    pack.claims_no_allocation = payload.claims_no_allocation;
    pack.claims_user_space_wait = payload.claims_user_space_wait;
    pack.has_live_handle = pack.has_live_handle || payload.owes_a_protocol;
    pack.fails = pack.fails || returned.fails;
    pack.fails_with_plain_error = pack.fails_with_plain_error || (returned.carries_error && !returned.error_is_secret);
    return pack;
}

}  // namespace detail

// The premises of a binding with this payload and this pack.  The pass
// over the pack runs one time for each pack, and the pass over the payload
// one time for each payload.
template <class Payload, class... Atoms>
inline constexpr rule_premises binding_premises_v =
    detail::join_premises_(detail::pack_premises_v<Atoms...>, detail::payload_premises_v<Payload>);

// The verdict of each rule on one binding: the premises, and one bit for
// each code whose rule refuses the binding.  The bit of a code is the
// position of its enumerator in RuleCode, which is the position of its row
// in the rule table.
struct binding_verdict {
    rule_premises premises{};
    std::uint64_t tripped = 0;

    // True when the rule of the code admits the binding.
    [[nodiscard]] consteval bool admits(RuleCode code) const noexcept {
        return (tripped >> std::to_underlying(code) & 1U) == 0;
    }

    // True when each rule admits the binding.
    [[nodiscard]] consteval bool is_valid() const noexcept { return tripped == 0; }

    // Each code that the binding trips, in the order of the table, or an
    // empty view when it trips none.  A pack can trip several: the premises
    // of R002 imply those of L002, so no pack names R002 alone, and the list
    // of each code lets the fixture of each rule floor on its own code.  The
    // text depends on Anchor, so only a call builds it.
    template <class Anchor = void>
    [[nodiscard]] consteval std::string_view failing_codes() const {
        if (tripped == 0) return {};
        ::foundation::reflect::anchored_t<^^Anchor, std::string> text;
        for (std::size_t place = 0; place < rule_count; ++place) {
            if ((tripped >> place & 1U) == 0) continue;
            if (!text.empty()) text += ", ";
            for (char const* letter = rule_row_of(static_cast<RuleCode>(place), premises).code; *letter != '\0';
                 ++letter)
                text += *letter;
        }
        return std::define_static_string(text);
    }
};

namespace detail {

// Each row of the table, applied one time to the premises of a binding.
// Complexity: the count of the rules.
[[nodiscard]] consteval binding_verdict judge_(const rule_premises& premises) noexcept {
    binding_verdict verdict{.premises = premises};
    for (std::size_t place = 0; place < rule_count; ++place) {
        if (rule_row_of(static_cast<RuleCode>(place), premises).trips) verdict.tripped |= std::uint64_t{1} << place;
    }
    return verdict;
}

}  // namespace detail

// The verdict of the rules on a binding with this payload and this pack,
// computed one time for each binding.
template <class Payload, class... Atoms>
inline constexpr binding_verdict binding_verdict_v = detail::judge_(binding_premises_v<Payload, Atoms...>);

// The pack-only view.  Each rule that reads the payload stands down under
// void, so a cell written against the pack alone means what it says: this
// pack, with any payload, is or is not a contradiction.
template <class... Atoms>
inline constexpr const binding_verdict& pack_verdict_v = binding_verdict_v<void, Atoms...>;

}  // namespace collision

}  // namespace fixy
