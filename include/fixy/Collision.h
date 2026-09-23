#pragma once

// Pairs of grades that must not coexist on one binding.
//
// Each rule here is a theorem about two axes, carried over from
// include/crucible/safety/CollisionCatalog.h with its citation.  The
// rules are knowledge, not boilerplate: the reason a pair is refused is
// the point, and the message a reader sees is the theorem.
//
// ---------------------------------------------------------------------
// Live rules and pending rules
//
// A rule can only fire if an atom can move both of its axes off their
// strict poles.  The shipped atom catalog reaches 24 of the 33 axes;
// eight have no atom at all, so a rule reading one of them can never
// fire no matter what a caller writes.
//
// A rule that cannot fire is a comment.  A rule that cannot fire and
// that nobody can tell cannot fire is worse than a comment: it looks
// like coverage and demands nothing.  That shape — something that looks
// like a gate and asks for nothing — is the one this migration has
// found repeatedly, so the pending rules here are not marked with a
// TODO and left to be believed.  They are registered against the axis
// they wait on, and `every_pending_axis_is_still_empty()` FIRES the day
// that axis gains its first atom, naming the rules that must then be
// wired.  Forgetting is not possible, because forgetting reddens.
//
// The two counts are assertions rather than something a reader tallies.
//
// ---------------------------------------------------------------------
// Why grades<Atoms...> and not fn
//
// CollisionRules is partial-specialised on fn, and a rule that read
// `F::something` would complete fn, whose body asserts this file's
// ValidComposition, and the instantiation recurses — GCC reports
// "satisfaction of atomic constraint depends on itself".  Every rule
// below reads grades<Atoms...>::on<Axis::X> instead, which is computed
// from the pack alone and never needs fn complete.  fn is forward
// declared here for the partial specialization and nothing more.
//
// ---------------------------------------------------------------------
// Why rules_of<Payload, Atoms...> takes the payload, and still not fn
//
// Five rules pair a grade with the payload's replay claim, which is the
// DetSafe band the payload carries and nothing in the pack.  I002 and
// I003 read a failure that the payload returns as std::expected<T, E>.
// So the rules take the payload as a template parameter of their own.  That is
// the fn's FIRST template parameter, read directly from the partial
// specialisation, and never a member of the fn: reading fn::type_t would
// complete fn, and the cycle above closes.  live_rules<Atoms...> is the
// same struct with void for the payload, under which every payload rule
// stands down, so a pack-only cell keeps meaning what it says.
//
// Old spelling: include/crucible/safety/CollisionCatalog.h.

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
#include <fixy/atoms/Simd.h>
#include <fixy/atoms/Stack.h>
#include <fixy/atoms/Stdio.h>
#include <fixy/atoms/Sync.h>
#include <fixy/Secret.h>
#include <foundation/Platform.h>
#include <foundation/diag/Catalog.h>
#include <foundation/effects/Row.h>

#include <array>
#include <cstddef>
#include <expected>
#include <meta>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace fixy {

template <class Type, class... Atoms>
class fn;

// The two spawn atoms L003 reads, declared rather than included.
// fixy/os/Spawn.h defines them beside the spawning mints, and including
// it here would pull std::thread and the permission layer into every
// header that sees this one.  A partial specialisation needs only the
// template's name, and the redeclaration must match Spawn.h's template
// head exactly, so a change there that this file does not follow is a
// compile error rather than a rule that silently stops matching.
namespace atom::spawn {
template <::fixy::atom::ctrl::rationale Rationale>
struct detach_with;
template <::fixy::atom::ctrl::rationale Rationale>
struct syscall_only;
}  // namespace atom::spawn

namespace collision {

// ---------------------------------------------------------------------
// The pack as a grade lookup.

namespace detail {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

template <class... Atoms>
[[nodiscard]] consteval auto pack_entries_() {
    return std::define_static_array(std::vector<std::meta::info>{^^Atoms...});
}

template <Axis A, class... Atoms>
[[nodiscard]] consteval std::meta::info grade_() noexcept {
    std::meta::info found = ^^void;
    if constexpr (requires { typename axis_traits<A>::strict; }) {
        using Strict = typename axis_traits<A>::strict;
        found = ^^Strict;
    }
    static constexpr auto entries = pack_entries_<Atoms...>();
    template for (constexpr auto entry : entries) {
        using Candidate = [:entry:];
        if constexpr (Candidate::axis == A) {
            found = entry;
        }
    }
    return found;
}

#pragma GCC diagnostic pop

}  // namespace detail

// The same answer fn::grade_on gives, computed from the pack alone.
// Axis::Type is absent on purpose: it resolves to the payload, which is
// fn's business, and no collision rule reads it.
template <class... Atoms>
struct grades {
    template <Axis A>
    using on = [:detail::grade_<A, Atoms...>():];

    template <Axis A>
    static constexpr bool mentions = !std::is_same_v<on<A>, typename axis_traits<A>::strict>;
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

using all_atom_roster =
    ::fixy::atom::detail::roster_cat_t<::fixy::atom::detail::core_atom_roster,
                                       ::fixy::atom::detail::barrier_atom_roster, ::fixy::atom::detail::ctrl_atom_roster,
                                       ::fixy::atom::detail::dispatch_atom_roster,
                                       ::fixy::atom::detail::fp_atom_roster,
                                       ::fixy::atom::detail::global_atom_roster, ::fixy::atom::detail::hw_atom_roster,
                                       ::fixy::atom::detail::observe_atom_roster,
                                       ::fixy::atom::detail::os_atom_roster,
                                       ::fixy::atom::detail::regime_atom_roster,
                                       ::fixy::atom::detail::scope_atom_roster,
                                       ::fixy::atom::detail::simd_atom_roster,
                                       ::fixy::atom::detail::stack_atom_roster,
                                       ::fixy::atom::detail::stdio_atom_roster,
                                       ::fixy::atom::detail::sync_atom_roster>;

namespace detail {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

template <Axis A>
[[nodiscard]] consteval bool axis_has_an_atom_() noexcept {
    bool found = false;
    static constexpr auto members = ::fixy::atom::detail::roster_members_v<all_atom_roster>;
    template for (constexpr auto entry : members) {
        using Candidate = [:entry:];
        if constexpr (Candidate::axis == A) {
            found = true;
        }
    }
    return found;
}

#pragma GCC diagnostic pop

}  // namespace detail

template <Axis A>
inline constexpr bool axis_has_an_atom = detail::axis_has_an_atom_<A>();

// The axes no shipped atom reaches.  A rule reading one of them is
// registered as pending below.  Axis::Type is not here: it is
// caller-supplied by design, being the payload itself, so it is the one
// axis that is atomless and complete rather than atomless and waiting.
//
// The list is empty: every axis but Type has an atom, so every axis but
// Type can be written.  The families that give each axis its atoms are
// the headers under fixy/atoms/, and each one's rules are named in the
// corpus below.
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
// The rule codes.  Letter codes are stable API: the negative corpus
// greps them, so a code is never renamed or reused.

enum class RuleCode : std::uint8_t {
    L002,  // borrow x async
    M012,  // monotonic x concurrent without atomic representation
    P010,  // ghost x observable effect
    L007,  // borrow x Bg row
    T001,  // capability x unverified trust
    R002,  // coroutine x borrow
    R003,  // coroutine x Bg row
    L006,  // linear x longjmp
    G002,  // thread-local x atomic representation
    D002,  // unbounded recursion x unbounded cost
    P002,  // ghost x an emitting surface other than the effect row
    // Pending: the axis each waits on is named in pending_rules below.
    B001,
    // B002 is NEW, not a renaming of B001.  B001's theorem is the
    // back-pressure trap and it has its own remedy; the containment
    // invariant below is a different theorem about the same axis, and the
    // codes above are stable API that is never reused, so it gets its own.
    B002,
    H001,
    H002,
    H003,
    H010,
    R001,
    S001,
    W001,
    W002,
    F101,
    F102,
    // An Absent or Retired code has no enumerator, and the pin below is
    // why: this enum holds exactly the codes a rule in this file can be
    // named by, and any other code is named only by the corpus, as a
    // string.  Nothing is renamed and nothing is reused: the corpus
    // carries each string, with the reason it cannot fire or is not
    // carried.
    V101,
    V201,
    V202,
    V203,
    V301,
    V401,
    V402,
    // Written after the migration: each premise turned out to be
    // writable in the new tree, where the old tree read a marker trait
    // nothing specialised.
    S011,
    D001,
    L003,
    // The constant-time family and the failure family.  Each reads the
    // Security grade atom::constant_time, a failure path in the type, or
    // both.  foundation/effects/Effect.h says why neither is a row atom.
    F103,
    F104,
    F105,
    E044,
    S010,
    I004,
    I003,
    I002,
};

// ---------------------------------------------------------------------
// The pending roster.  Each entry keeps the theorem and the citation,
// and names the axis that would make it live.

struct pending_rule {
    RuleCode code{};
    Axis waits_on{};
    std::string_view theorem{};
};

// Empty, and empty for two different reasons.
//
// Every rule that reads an axis family is live, because every axis has
// its atoms: H001, H002, H003, H010, R001 and S001 read
// fixy/atoms/Regime.h, W001 and W002 read Sync.h, B001 reads Observe.h,
// V201, V202 and V203 read Hw.h, V301 reads Barrier.h, V401 reads
// Scope.h, V101 and V402 read Simd.h, and F101 and F102 read Fp.h.  Each
// is a live_rules member below and a Live row in rule_corpus.  Three of
// them read two families: W001 a tier AND a wait strategy, V401 a scope
// AND a strength, V402 a scope AND a pinned ISA.
//
// Four sit in rule_corpus instead, because the FpMode or SimdIsa atom is
// not all they read.  F103, F104 and F105, which read an FP mode against
// a constant-time claim, read that claim from the Security grade
// atom::constant_time.  V102, which reads a SIMD width, is Retired,
// because the one ISA grade a binding names already fixes its width.
// That is the distinction this list exists to draw: pending means
// waiting on an atom, and absent means waiting on something this layer
// does not have.
inline constexpr std::array<pending_rule, 0> pending_rules{};

inline constexpr std::size_t pending_rule_count = pending_rules.size();

// ---------------------------------------------------------------------
// The corpus: every rule code the old catalog defines, and what became
// of each one here.
//
// This list is the external specification, written out by name.  That is
// the whole point of it.  A rule that was never written is absent from
// RuleCode, so any quantity computed FROM RuleCode — including a count —
// agrees with itself for a roster of the wrong size and cannot see the
// absence.  The first shape of this file pinned `live == roster - pending`,
// which is arithmetic rather than a measurement, and it held at 32 of 54.
//
// The three pins below compare this list against the RuleCode enum and
// against the members live_rules actually defines, so no one of the three
// can drift without one of the others reporting it.
//
// Source: include/crucible/safety/CollisionCatalog.h, whose RuleCode enum
// has 54 enumerators.

enum class Disposition : std::uint8_t {
    Live,     // implemented in live_rules below, and able to fire today
    Pending,  // carried as a theorem, waiting on an axis that has no atom
    Absent,   // not implemented; the note names what is missing
    Retired,  // not carried further; the note says why
};

struct corpus_entry {
    std::string_view code{};
    Disposition disposition{};
    std::string_view note{};
};

inline constexpr corpus_entry rule_corpus[] = {
    // The eleven that fire today.
    {"L002", Disposition::Live, "borrow x async"},
    {"M012", Disposition::Live, "monotonic x concurrent without an atomic representation"},
    {"P010", Disposition::Live, "ghost x an observable effect row"},
    {"P002", Disposition::Live, "ghost x stdio or a syscall surface"},
    {"L007", Disposition::Live, "borrow x Row<Bg>"},
    {"T001", Disposition::Live, "capability x unverified trust"},
    {"R002", Disposition::Live, "coroutine x borrow"},
    {"R003", Disposition::Live, "coroutine x Row<Bg>"},
    {"L006", Disposition::Live, "linear x longjmp"},
    {"G002", Disposition::Live, "thread-local x atomic representation"},
    {"D002", Disposition::Live, "unbounded recursion x unbounded cost"},

    // The regime family, which reads the three HotPathTier atoms of
    // fixy/atoms/Regime.h.
    {"H001", Disposition::Live, "hot x unstated or unbounded cost"},
    {"H002", Disposition::Live, "hot x no refinement witness"},
    {"H003", Disposition::Live, "hot x an Alloc or IO row x unbounded cost"},
    {"H010", Disposition::Live, "hot x Row<Bg>"},
    {"R001", Disposition::Live, "coroutine x hot"},
    {"S001", Disposition::Live, "stdio x hot"},

    // The wait family, which reads the six WaitStrategy atoms of
    // fixy/atoms/Sync.h.  W001 reads a tier and a wait, so it reads the
    // Regime family too.
    {"W001", Disposition::Live, "hot x a kernel wait"},
    {"W002", Disposition::Live, "Row<Bg> x a spin that burns the core"},

    // The observability family, which reads the surface atom of
    // fixy/atoms/Observe.h.  Two theorems, not one: B002 is the
    // containment of the observability row in the effect row, and B001 is
    // the back-pressure trap this catalog already recorded.  B002 is a new
    // code rather than a rereading of B001, because the codes are stable
    // API and the negative corpus greps them.
    {"B001", Disposition::Live, "Row<Bg> x an observable surface x an unbounded resource"},
    {"B002", Disposition::Live, "an observability row outside the binding's effect row"},

    // The hardware-instruction family, which reads the five
    // HwInstruction atoms of fixy/atoms/Hw.h.  V203 reads the payload's
    // replay claim against them.
    {"V201", Disposition::Live, "hot x a tier at or above NonDeterministicTsc"},
    {"V202", Disposition::Live, "the PrivilegedMsr tier x an effect row with no Init"},
    {"V203", Disposition::Live, "a replay-deterministic payload x a tier at or above NonDeterministicTsc"},

    // The barrier-strength family, which reads the seven BarrierStrength
    // atoms of fixy/atoms/Barrier.h.
    {"V301", Disposition::Live, "hot x a fence at or above SeqCst"},

    // The memory-scope family, which reads the eight MemoryScope atoms of
    // fixy/atoms/Scope.h.  V401 reads a scope and a strength together.
    {"V401", Disposition::Live, "a scope at or above Cluster x a fence below AcqRel"},

    // The SIMD-ISA family, which reads the fifteen SimdIsa atoms of
    // fixy/atoms/Simd.h.  V101 reads the payload's replay claim against
    // them, and V402 reads a scope and an ISA together.
    {"V101", Disposition::Live, "a replay-deterministic payload x an ISA pinned to one trunk"},
    {"V402", Disposition::Live, "a trunk-pinned scope x a trunk-pinned ISA that do not cohere"},

    // The floating-point-mode family, which reads the product atom of
    // fixy/atoms/Fp.h.  Both read the payload's replay claim, as V203 and
    // V101 do.
    {"F101", Disposition::Live, "a replay-deterministic payload x FP reassociation permitted"},
    {"F102", Disposition::Live, "a replay-deterministic payload x FP contraction across statements"},

    // Three the old tree carried as marker traits nothing specialised,
    // and whose premises the new tree states as grades.  S011 reads the
    // payload's replay claim as the family above does.
    {"S011", Disposition::Live, "capability x a replay-deterministic payload"},
    {"D001", Disposition::Live, "an indirect call whose named signature is not noexcept"},
    {"L003", Disposition::Live, "borrow x a spawn that no structured join ties to the frame"},

    // The constant-time family and the failure family.  The specification
    // writes the constant-time claim and the failure path as effects in
    // the row, `with CT` and Fail(E), and neither is a row atom here:
    // foundation/effects/Effect.h says why at its top.  Constant time is
    // the Security grade atom::constant_time, and a failure is the payload
    // std::expected<T, E> or the ControlFlow grade ctrl::throws<E>.
    {"F103", Disposition::Live, "constant time x FP reassociation by an unrestricted rewrite"},
    {"F104", Disposition::Live, "constant time x an FP mode that honours denormal inputs"},
    {"F105", Disposition::Live, "constant time x an FP mode that preserves subnormal results"},
    {"E044", Disposition::Live, "constant time x a suspension or a Bg row"},
    {"S010", Disposition::Live, "constant time x a staleness window"},
    {"I004", Disposition::Live, "classified x a suspension or a Bg row x a session protocol, without constant time"},
    {"I003", Disposition::Live, "constant time x a failure path"},
    {"I002", Disposition::Live, "classified x a failure whose error type is not Secret"},

    // Nothing waits on an atom any more.  pending_rules above is empty and
    // says why in two parts; the rest of this list is the second part.

    // The rules this layer cannot state yet.  Each note names the thing
    // that is missing, so the entry is a claim someone can check rather
    // than a gap someone has to notice.  absent_rule_codes below names the
    // same set, and the set only shrinks.
    //
    // Three read a premise nothing in the tree can write.
    {"F002", Disposition::Absent,
     "reads a federation-peer role; Canopy membership is not a grade, and no atom or band names it"},
    {"N002", Disposition::Absent,
     "reads an exact-decimal payload against overflow_wrap; the tree has no decimal type and Axis::Precision "
     "carries f32, f64 and higham only"},
    {"L004", Disposition::Absent,
     "reads whether a linear in_region<Tag> binding holds Permission<Tag>; the proof is a call argument, fn "
     "carries one payload, and in_region names its tag as a value where Permission names it as a type"},

    // Three hold across several bindings.  rules_of is handed one binding,
    // so a relation over a set of bindings has no place here.
    {"F001", Disposition::Absent, "a frame-level agreement across several bindings"},
    {"L005", Disposition::Absent, "compares two linear bindings that share a region tag"},
    {"S004", Disposition::Absent,
     "walks the init-dependency graph across every registered singleton; global::singleton<Tag> names no edge"},

    // Retired: the rule is not carried further, and the note says why.
    // Each of these is discharged by the shape of the new tree — the
    // combination it refused cannot be written — or never had a theorem.
    // A retired rule is not a rule deferred, so it leaves the Absent set
    // rather than waiting in it.
    {"C001", Disposition::Retired,
     "discharged: ctrl::abort<Reason> IS the binding's ControlFlow grade, so the abort declaration and the "
     "tier the old rule compared it with are one claim and cannot disagree"},
    {"P003", Disposition::Retired,
     "discharged: a fork worker is a callable handed to mint_spawn, which refuses one whose type carries "
     "ctrl::throws (fixy/os/Spawn.h, deviation 1), after foundation's fork gate has required it be noexcept"},
    {"V001", Disposition::Retired,
     "discharged: Axis::SimdIsa takes one grade per binding and tier 4 refuses a second, so a pack cannot name "
     "two vendors' instruction sets"},
    {"V002", Disposition::Retired,
     "discharged: Axis::SimdIsa takes one grade per binding and tier 4 refuses a second, so a pack cannot name "
     "an x86 set and an ARM set together"},
    {"V102", Disposition::Retired,
     "discharged: the width a body emits is the width of the one SimdIsa grade it names; no atom or band states "
     "a width apart from it, so a width wider than the set cannot be written"},
    {"G001", Disposition::Retired,
     "discharged: atom::global::thread_local_<StaticTag> requires the tag, so the untagged form this rule refused "
     "cannot be named"},
    {"M001", Disposition::Retired,
     "the old catalog declares M001_DontNeedRequiresReleaseAware and ships no CRUCIBLE_COLLISION_DIAGNOSTIC for it, "
     "so the code has a name and no theorem to port"},
    {"M011", Disposition::Retired,
     "discharged: a failure here is a std::expected return or a throw, and each runs every destructor in scope, so "
     "a linear value is released on the failure path; the one exit that skips destructors is longjmp, and L006 "
     "refuses it against a linear binding.  Which values are live at the failure point is a property of the body, "
     "and a binding carries no body"},

};

inline constexpr std::size_t rule_corpus_size = sizeof(rule_corpus) / sizeof(rule_corpus[0]);

namespace detail {

[[nodiscard]] consteval bool corpus_lists_(std::string_view code) noexcept {
    for (const corpus_entry& entry : rule_corpus) {
        if (entry.code == code) return true;
    }
    return false;
}

[[nodiscard]] consteval bool corpus_ships_(std::string_view code) noexcept {
    for (const corpus_entry& entry : rule_corpus) {
        if (entry.code == code) return entry.disposition == Disposition::Live || entry.disposition == Disposition::Pending;
    }
    return false;
}

[[nodiscard]] consteval std::size_t corpus_count_(Disposition wanted) noexcept {
    std::size_t found = 0;
    for (const corpus_entry& entry : rule_corpus) {
        if (entry.disposition == wanted) ++found;
    }
    return found;
}

[[nodiscard]] consteval bool rule_code_names_(std::string_view code) noexcept {
    bool found = false;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto enumerator : std::define_static_array(std::meta::enumerators_of(^^RuleCode))) {
        if (std::meta::identifier_of(enumerator) == code) found = true;
    }
#pragma GCC diagnostic pop
    return found;
}

}  // namespace detail

inline constexpr std::size_t live_rule_count = detail::corpus_count_(Disposition::Live);

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

[[nodiscard]] consteval bool every_pending_axis_is_still_empty() noexcept {
    bool unchanged = true;
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        constexpr Axis axis = [:axis_member:];
        if constexpr (axis != Axis::Type) {
            unchanged = unchanged && (axis_is_pending(axis) != axis_has_an_atom<axis>);
        }
    }
    return unchanged;
}

#pragma GCC diagnostic pop

// ---------------------------------------------------------------------
// Every declared atom roster is joined into all_atom_roster.
//
// The join above is a hand-written list, and the guard beside it only
// notices a missing family when the family's AXIS ends up with no atoms
// at all.  A roster whose axis is populated by some other family is
// invisible to it: the atoms exist, nothing reads them, and the guard
// whose name promises completeness reports clean.  That is how
// fixy/os/Spawn.h's three atoms sat outside the population while the
// check said every axis was covered, and it is the same shape as an
// allowlist key naming a file that is gone — a guard that cannot fail
// the way its name implies.
//
// So the relation is stated over the DECLARATIONS rather than over the
// axes.  A `*_atom_roster` alias in fixy::atom::detail must have every
// one of its atoms reachable from all_atom_roster.  A roster composed of
// sub-rosters satisfies it through the parent, which is why the io, fs,
// mmap and leak sets pass without being named in the join.
//
// The walk sees what its translation unit has included, so this file's
// own assertion covers the families this file includes and no more.  The
// complete answer needs a sentinel over every header that can declare a
// roster; scripts/check-atom-roster-joined.sh builds one from the
// directory rather than from a list, and calls the same two functions.
// Both this file and Stage A's gate check consume this derivation rather
// than building a second set.
//
// The Site parameter is what makes that second vantage point possible.
// std::meta::members_of answers as of the point where the walk is
// INSTANTIATED, and a specialization is instantiated once: a later call
// to the same specialization returns the earlier point's answer.  A
// non-template walk is frozen at this header's own line and can never
// see a roster declared by a header included after it.  Each vantage
// point passes its own tag type instead, so its call is a fresh
// specialization that walks the namespace as its translation unit has it
// at that line.  Reusing another site's tag silently reuses that site's
// answer, which is the one way to get a wrong clean result here.

namespace detail {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

template <class Atom>
[[nodiscard]] consteval bool atom_is_joined_() noexcept {
    bool joined = false;
    template for (constexpr auto member : ::fixy::atom::detail::roster_members_v<all_atom_roster>) {
        using Candidate = [:member:];
        if constexpr (std::is_same_v<Atom, Candidate>) joined = true;
    }
    return joined;
}

template <class Roster>
[[nodiscard]] consteval bool every_atom_joined_() noexcept {
    bool all_joined = true;
    template for (constexpr auto member : ::fixy::atom::detail::roster_members_v<Roster>) {
        using Atom = [:member:];
        all_joined = all_joined && atom_is_joined_<Atom>();
    }
    return all_joined;
}

#pragma GCC diagnostic pop

}  // namespace detail

// The offending rosters' names, comma-separated, or empty when every
// declared roster is joined.  Answering with the NAMES rather than a bool
// is what lets the diagnostic say which roster and leaves the reader one
// edit from the join site.
//
// Every offender, not the first.  Reporting only the first would make one
// known offender mask every other, which is how a second orphan would
// arrive unnoticed while the guard was already red for the first — and it
// would also make the guard's own self-test unable to tell its injected
// probe from the offender already there.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

template <class Site>
[[nodiscard]] consteval std::string_view roster_declared_but_not_joined() {
    std::string offenders;
    template for (constexpr auto member : std::define_static_array(
                      std::meta::members_of(^^::fixy::atom::detail, std::meta::access_context::current()))) {
        if constexpr (std::meta::is_type_alias(member)) {
            constexpr std::string_view name = std::meta::identifier_of(member);
            if constexpr (name.ends_with("_atom_roster")) {
                using Roster = [:std::meta::dealias(member):];
                if constexpr (!detail::every_atom_joined_<Roster>()) {
                    if (!offenders.empty()) offenders += ", ";
                    offenders += name;
                }
            }
        }
    }
    if (offenders.empty()) return {};
    return std::string_view{std::define_static_string(offenders)};
}

// The other direction, and it is what keeps the first from being dodged
// by a rename.  A set named `*_atom_samples` is representative
// instantiations for a local self-test and is deliberately OUTSIDE the
// population; if one is joined, the two categories have blurred and the
// name no longer says which it is.
template <class Site>
[[nodiscard]] consteval std::string_view sample_set_wrongly_joined() {
    std::string offenders;
    template for (constexpr auto member : std::define_static_array(
                      std::meta::members_of(^^::fixy::atom::detail, std::meta::access_context::current()))) {
        if constexpr (std::meta::is_type_alias(member)) {
            constexpr std::string_view name = std::meta::identifier_of(member);
            if constexpr (name.ends_with("_atom_samples")) {
                using Samples = [:std::meta::dealias(member):];
                if constexpr (detail::every_atom_joined_<Samples>()) {
                    if (!offenders.empty()) offenders += ", ";
                    offenders += name;
                }
            }
        }
    }
    if (offenders.empty()) return {};
    return std::string_view{std::define_static_string(offenders)};
}

#pragma GCC diagnostic pop

// The two diagnostics.
//
// P2741R3 lets a static_assert message be computed, and that is the only
// reason this relation can NAME the offending roster instead of saying
// that one of them is wrong.  A reader gets the roster and the single
// line to edit, which is the difference between a guard that reports a
// fault and a guard that reports a fault somebody can fix.
template <class Site>
[[nodiscard]] consteval std::string_view roster_join_diagnostic() {
    const std::string_view offenders = roster_declared_but_not_joined<Site>();
    if (offenders.empty()) return {};
    std::string message =
        "fixy/Collision.h: the atom-population relation: declared in fixy::atom::detail and NOT joined into "
        "fixy::collision::all_atom_roster, so their atoms sit outside the population that every axis-coverage "
        "and collision-rule check reads: ";
    message += offenders;
    message +=
        ".  Join each at the all_atom_roster alias in include/fixy/Collision.h.  If one is representative "
        "instantiations for a local self-test rather than a family population, rename it to end in "
        "_atom_samples instead — that spelling says so, and sample_set_wrongly_joined() holds it to it.";
    return std::string_view{std::define_static_string(message)};
}

template <class Site>
[[nodiscard]] consteval std::string_view sample_set_diagnostic() {
    const std::string_view offenders = sample_set_wrongly_joined<Site>();
    if (offenders.empty()) return {};
    std::string message =
        "fixy/Collision.h: the atom-population relation: these sample sets ARE joined into "
        "fixy::collision::all_atom_roster: ";
    message += offenders;
    message +=
        ".  A sample set is a set of INSTANTIATIONS of a parametric family, kept outside the population "
        "because the family has no finite membership and no list can enumerate it; the instantiations are "
        "placeholders chosen to instantiate a local self-test, not grades anybody writes.  A family whose "
        "members are distinct grades is finite and belongs in the joined population instead, under a name "
        "ending in _atom_roster.  Joining a sample set blurs the two categories, and a blurred boundary is "
        "what would let a real family be renamed out of roster_declared_but_not_joined() rather than joined "
        "into the population.  So: drop each from the all_atom_roster alias in include/fixy/Collision.h, or, "
        "if its members really are distinct grades, rename it to end in _atom_roster and leave it joined.";
    return std::string_view{std::define_static_string(message)};
}

// This header's own vantage point.  It covers the families included at
// the top of this file and no others; scripts/check-atom-roster-joined.sh
// instantiates the same two templates from a sentinel that includes every
// header under fixy/ and so covers the rest.  The tag is never defined —
// it is an identity for the instantiation point, not a type anyone uses.
struct collision_header_site;

static_assert(roster_join_diagnostic<collision_header_site>().empty(),
              roster_join_diagnostic<collision_header_site>());
static_assert(sample_set_diagnostic<collision_header_site>().empty(),
              sample_set_diagnostic<collision_header_site>());

static_assert(every_pending_axis_is_still_empty(),
              "fixy/Collision.h: the pending-rule roster is out of date.  Either an axis listed in "
              "collision::pending_axes has gained its first atom — in which case the rules registered against it in "
              "collision::pending_rules can now fire and must be written as live rules, and the axis removed from "
              "pending_axes — or, since both lists are empty, a family roster under fixy/atoms/ is no longer "
              "joined into collision::all_atom_roster, which makes its axes look empty.");

// ---------------------------------------------------------------------
// The corpus pins.
//
// Each compares two things that were written separately.  None computes
// one side from the other.

// Fifty-four inherited codes plus one written here.
//
// The 54 come from the RuleCode enum of
// include/crucible/safety/CollisionCatalog.h and the count is stated
// against that external list, so a code dropped from this one stops being
// reported as absent — the failure this list exists to prevent.
//
// B002 is the one addition, and it is an addition rather than a rereading
// of an inherited code.  Axis::Observability carries two theorems where
// the old catalog recorded one: B001's back-pressure trap, which keeps
// its code, and the containment of the observability row in the effect
// row, which had no code.  Reusing B001
// for the second would have discarded a theorem that has its own remedy,
// and the codes are stable API precisely so that cannot happen quietly.
static_assert(rule_corpus_size == 55,
              "fixy/Collision.h: the rule corpus must account for the 54 codes inherited from the RuleCode "
              "enum of include/crucible/safety/CollisionCatalog.h, plus B002, which is written here.  A code "
              "dropped from this list stops being reported as absent.");

// Every code the corpus says ships has an enumerator, and every code it
// says is absent or retired has none.  A rule written without a corpus entry, or
// recorded as absent after being written, reddens here.
//
// Both checks answer with the offending code rather than with a bool, so
// the diagnostic names the rule instead of asking the reader to diff two
// lists of fifty-four.

namespace detail {

[[nodiscard]] consteval std::string_view code_the_enum_disagrees_about_() noexcept {
    for (const corpus_entry& entry : rule_corpus) {
        const bool shipped = entry.disposition == Disposition::Live || entry.disposition == Disposition::Pending;
        if (rule_code_names_(entry.code) != shipped) return entry.code;
    }
    return {};
}

[[nodiscard]] consteval std::string_view code_the_corpus_never_listed_() noexcept {
    std::string_view missing{};
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto enumerator : std::define_static_array(std::meta::enumerators_of(^^RuleCode))) {
        constexpr std::string_view name = std::meta::identifier_of(enumerator);
        if (missing.empty() && !corpus_lists_(name)) missing = name;
    }
#pragma GCC diagnostic pop
    return missing;
}

[[nodiscard]] consteval std::string_view named_(std::string_view lead, std::string_view code) noexcept {
    return std::define_static_string(std::string{lead} + std::string{code});
}

}  // namespace detail

static_assert(detail::code_the_enum_disagrees_about_().empty(),
              detail::named_("fixy/Collision.h: the rule corpus and the RuleCode enum disagree about rule ",
                             detail::code_the_enum_disagrees_about_()));

static_assert(detail::code_the_corpus_never_listed_().empty(),
              detail::named_("fixy/Collision.h: this RuleCode enumerator is in no rule_corpus entry: ",
                             detail::code_the_corpus_never_listed_()));

// ---------------------------------------------------------------------
// The Absent set, by name.
//
// The pins above bind the Live rows to the rules that run, and they say
// nothing about the Absent rows.  An Absent row asks for nothing, so a
// rule could be moved to Absent, or a new code added as Absent, and every
// pin would stay green.  A count would not close that: one name can leave
// the set while another joins it, and the count holds.
//
// So the set is written out a second time, here, and the two lists must
// name the same codes.  The list only shrinks.  A code leaves it when its
// rule is written and its row turns Live, or when its row records why the
// rule is not carried further.  A code never joins it: adding a name here
// is the one edit that grows the set, and it is a visible line in a diff
// rather than a disposition changed in passing.
inline constexpr std::string_view absent_rule_codes[] = {
    "F002", "N002", "L004", "F001", "L005", "S004",
};

namespace detail {

[[nodiscard]] consteval bool absent_list_names_(std::string_view code) noexcept {
    for (const std::string_view listed : absent_rule_codes) {
        if (listed == code) return true;
    }
    return false;
}

[[nodiscard]] consteval bool corpus_marks_absent_(std::string_view code) noexcept {
    for (const corpus_entry& entry : rule_corpus) {
        if (entry.code == code) return entry.disposition == Disposition::Absent;
    }
    return false;
}

// Every offender, comma-separated, so one known offender cannot mask a
// second.  O(n^2) over the corpus, at compile time only.
//
// The two collectors return a std::string rather than a static view, and
// the diagnostic is assembled in one call.  Handing a static view built
// by define_static_string to std::string's constructor is not a constant
// expression under GCC 16: the constructor compares the pointer against
// null, and GCC refuses that comparison for such a pointer.
[[nodiscard]] consteval std::string absent_rows_not_listed_() {
    std::string offenders;
    for (const corpus_entry& entry : rule_corpus) {
        if (entry.disposition != Disposition::Absent || absent_list_names_(entry.code)) continue;
        if (!offenders.empty()) offenders += ", ";
        for (const char letter : entry.code) offenders += letter;
    }
    return offenders;
}

[[nodiscard]] consteval std::string listed_codes_not_absent_() {
    std::string offenders;
    for (const std::string_view listed : absent_rule_codes) {
        if (corpus_marks_absent_(listed)) continue;
        if (!offenders.empty()) offenders += ", ";
        for (const char letter : listed) offenders += letter;
    }
    return offenders;
}

[[nodiscard]] consteval std::string_view absent_code_listed_twice_() noexcept {
    const std::size_t listed = sizeof(absent_rule_codes) / sizeof(absent_rule_codes[0]);
    for (std::size_t first = 0; first < listed; ++first) {
        for (std::size_t second = first + 1; second < listed; ++second) {
            if (absent_rule_codes[first] == absent_rule_codes[second]) return absent_rule_codes[first];
        }
    }
    return {};
}

[[nodiscard]] consteval std::string_view absent_pin_message_(std::string_view lead, const std::string& offenders) {
    std::string message;
    for (const char letter : lead) message += letter;
    message += offenders;
    return std::define_static_string(message);
}

}  // namespace detail

static_assert(detail::absent_rows_not_listed_().empty(),
              detail::absent_pin_message_(
                  "fixy/Collision.h: the corpus marks these rules Absent and absent_rule_codes does not name them.  "
                  "The Absent set only shrinks: a rule leaves it by being written or by a recorded reason, and "
                  "never joins it.  Write the rule, or keep the row it had: ",
                  detail::absent_rows_not_listed_()));

static_assert(detail::listed_codes_not_absent_().empty(),
              detail::absent_pin_message_(
                  "fixy/Collision.h: absent_rule_codes names these codes and the corpus does not mark them "
                  "Absent.  A code whose row left the Absent set leaves this list in the same edit: ",
                  detail::listed_codes_not_absent_()));

static_assert(detail::absent_code_listed_twice_().empty(),
              detail::named_("fixy/Collision.h: absent_rule_codes names this code twice: ",
                             detail::absent_code_listed_twice_()));

// pending_rules and the corpus are two hand-written lists of the same
// set.  Comparing them catches an edit to one and not the other.
static_assert(pending_rule_count == detail::corpus_count_(Disposition::Pending),
              "fixy/Collision.h: pending_rules and the corpus disagree about how many rules are waiting on an "
              "atomless axis.");

// There is deliberately no pin on pending_axis_count here.  It would be
// derived from the array it counts, and the biconditional above already
// fixes the SET by name against the atom catalog: an axis dropped from
// pending_axes while still atomless fails it, and an axis added while it
// has an atom fails it too.  The count could therefore never fail on its
// own, and a pin that cannot fail reads as a second check.

// ---------------------------------------------------------------------
// Reading a grade.
//
// A parametric atom carries its argument in the template-id, so the
// question "does this effect row admit Bg" is answered by matching the
// atom rather than by calling anything on it.

namespace detail {

template <class G>
struct row_admits_bg_ : std::false_type {};
template <::foundation::effects::Effect... Es>
struct row_admits_bg_<::fixy::atom::with<Es...>>
    : std::bool_constant<((Es == ::foundation::effects::Effect::Bg) || ...)> {};

// P010's set is the observable atoms without Bg.  Whether an atom is
// observable is decided in foundation/effects/Effect.h, where a count pin
// makes each new atom take a decision, so a new observable atom joins
// this set there and cannot be left out here.  Bg is left out because the
// rules that read a Bg row, L007 and R003 among them, carry its theorem.
template <class G>
struct row_admits_observable_ : std::false_type {};
template <::foundation::effects::Effect... Es>
struct row_admits_observable_<::fixy::atom::with<Es...>>
    : std::bool_constant<((::foundation::effects::is_observable<Es>() && Es != ::foundation::effects::Effect::Bg)
                          || ...)> {};

// H003's theorem names Alloc and IO specifically, so it gets its own
// predicate rather than reusing row_admits_observable_ above, which also
// admits Block.  A blocking hot path is just as wrong, but it is W001's
// theorem and W001 cites the futex cost, not the allocator's.
// The payload's replay claim.
//
// A payload carrying a DetSafe band at PhiloxRng or Pure claims that its
// bytes are replay-deterministic: the same inputs give the same bits on
// any host.  That claim is what F101, F102, V101, V203 and S011 pair with
// a grade that falsifies it — an FP mode that reorders a sum, an ISA pin
// that makes the reduction order host-dependent, a timestamp read, a
// capability that replay cannot mint again.
//
// The old catalog carried this premise as marks_replay_required, a
// marker trait with a false_type primary that nothing ever specialised,
// so none of those five rules fired structurally in the old tree either.
// fixy/Collision.h's corpus already re-read the premise as the band; this
// is the read.  The primary is false: a payload with no band claims
// nothing about replay, and every replay rule stands down for it.  void,
// the pack-only view's payload, takes that primary.
template <class Payload>
struct replay_deterministic_ : std::false_type {};
template <class Payload>
    requires ::fixy::is_band_of_v<::foundation::algebra::lattices::DetSafeLattice, Payload>
struct replay_deterministic_<Payload>
    : std::bool_constant<::foundation::algebra::lattices::DetSafeLattice::leq(
          ::foundation::algebra::lattices::DetSafeTier::PhiloxRng, Payload::lattice_type::tier)> {};

// The Observability row, extracted from its grade.
//
// The Effect row is NOT extracted here any more.  This file used to
// carry its own two-arm map with a primary answering Row<> for a grade
// it did not recognise, which was safe for B002's comparison direction
// and was still a second copy of a relation.  fixy/Atom.h now carries
// the one closed relation — atom::effect_row_of_t, over the stated
// with<Es...> and the bare Row the strict pole leaves behind — and the
// rules below read it.  A grade of any other shape fails there by name
// rather than passing as the empty row.
//
// Observability keeps its own map, because its shape is different: its
// pole is derived from Effect's, so the pole is that same empty row, and
// the primary answering Row<> is the WEAKEST answer on this side rather
// than the strongest.  A binding whose observability grade is
// unrecognised claims to observe nothing, and B002 admits it, which is
// the direction that stays safe.

template <class G>
struct observability_row_of_ {
    using type = ::foundation::effects::Row<>;
};
template <::foundation::effects::Effect... Es>
struct observability_row_of_<::fixy::atom::observe::surface<Es...>> {
    using type = ::foundation::effects::Row<Es...>;
};

// The hardware-instruction tier, read against a floor.  The primary is
// false because the strict pole of HwInstruction is Unconstrained: a
// binding that names no tier makes no claim about instruction classes,
// so neither V201 nor V202 fires on it.  The chain order comes from
// fixy/atoms/Hw.h's own at_or_above, so a rule and the atom header
// cannot disagree about which way the ladder runs.
template <::fixy::atom::hw::HwInstruction Floor, class G>
struct hw_at_or_above_ : std::false_type {};
template <::fixy::atom::hw::HwInstruction Floor, class G>
    requires requires { G::tier; } && std::is_same_v<std::remove_cvref_t<decltype(G::tier)>,
                                                     ::fixy::atom::hw::HwInstruction>
struct hw_at_or_above_<Floor, G> : std::bool_constant<::fixy::atom::hw::at_or_above(G::tier, Floor)> {};

// The provided fence strength, read against a floor.  The primary is
// false: a binding that names no strength provides no fence, so there
// is nothing for V301 to refuse on it.  The chain order comes from
// fixy/atoms/Barrier.h's own at_or_above, which is the lattice's leq in
// its admission direction, so a rule cannot invert the ladder.  Both
// this and hw_at_or_above_ read a member named `tier`; the type check
// in the constraint is what keeps each from answering for the other's
// atoms.
template <::foundation::algebra::lattices::BarrierStrength Floor, class G>
struct barrier_at_or_above_ : std::false_type {};
template <::foundation::algebra::lattices::BarrierStrength Floor, class G>
    requires requires { G::tier; } && std::is_same_v<std::remove_cvref_t<decltype(G::tier)>,
                                                     ::foundation::algebra::lattices::BarrierStrength>
struct barrier_at_or_above_<Floor, G> : std::bool_constant<::fixy::atom::barrier::at_or_above(G::tier, Floor)> {};

// The reached scope, read against a floor.  The primary is false: a
// binding that names no scope publishes to nobody in particular, so
// V401 has nothing to refuse on it.  The order is the lattice's own leq
// through fixy/atoms/Scope.h, and that lattice is two trunks, so a scope
// on the host trunk is NOT at or above an accelerator floor — Inner and
// Cluster are incomparable — and a rule reading this stands down for it
// rather than reading "incomparable" as "wide".
template <::foundation::algebra::lattices::MemoryScope Floor, class G>
struct scope_at_or_above_ : std::false_type {};
template <::foundation::algebra::lattices::MemoryScope Floor, class G>
    requires requires { G::scope; } && std::is_same_v<std::remove_cvref_t<decltype(G::scope)>,
                                                      ::foundation::algebra::lattices::MemoryScope>
struct scope_at_or_above_<Floor, G> : std::bool_constant<::fixy::atom::scope::at_or_above(G::scope, Floor)> {};

// The two trunk readings V402 composes, and the ISA pin V101 reads.
// Each primary is false: a binding that names no scope or no ISA pins
// no trunk, so the rules have nothing to refuse on it.  The trunk
// predicates come from fixy/atoms/Scope.h and fixy/atoms/Simd.h, so the
// division is read from one place per axis.
template <class G>
struct scope_trunk_pinned_ : std::false_type {};
template <class G>
    requires requires { G::scope; } && std::is_same_v<std::remove_cvref_t<decltype(G::scope)>,
                                                      ::foundation::algebra::lattices::MemoryScope>
struct scope_trunk_pinned_<G> : std::bool_constant<::fixy::atom::scope::is_trunk_pinned(G::scope)> {};

template <class G>
struct scope_on_host_trunk_ : std::false_type {};
template <class G>
    requires requires { G::scope; } && std::is_same_v<std::remove_cvref_t<decltype(G::scope)>,
                                                      ::foundation::algebra::lattices::MemoryScope>
struct scope_on_host_trunk_<G> : std::bool_constant<::fixy::atom::scope::on_host_trunk(G::scope)> {};

template <class G>
struct isa_trunk_pinned_ : std::false_type {};
template <class G>
    requires requires { G::isa; } && std::is_same_v<std::remove_cvref_t<decltype(G::isa)>, ::fixy::atom::simd::SimdIsa>
struct isa_trunk_pinned_<G> : std::bool_constant<::fixy::atom::simd::is_trunk_pinned(G::isa)> {};

template <class G>
struct isa_on_arm_trunk_ : std::false_type {};
template <class G>
    requires requires { G::isa; } && std::is_same_v<std::remove_cvref_t<decltype(G::isa)>, ::fixy::atom::simd::SimdIsa>
struct isa_on_arm_trunk_<G> : std::bool_constant<::fixy::atom::simd::on_arm_trunk(G::isa)> {};

// Whether the FP mode NAMES one setting value.  The atom on this axis is
// a product rather than a family, so a rule asks it about one setting at
// a time and the atom answers from the pack it was written with.  The
// primary is false, which is the strict pole read correctly: a binding
// that names no mode leaves every setting at its enum's first
// enumerator — reassociation forbidden, contraction off — and those are
// exactly the values no rule refuses.
template <auto Wanted, class G>
struct fp_names_ : std::false_type {};
template <auto Wanted, class G>
    requires requires { G::template names<Wanted>; }
struct fp_names_<Wanted, G> : std::bool_constant<G::template names<Wanted>> {};

// Whether the Effect row carries Init, which is the context V202 asks a
// privileged tier to be reached from.  Same shape as row_admits_bg_.
template <class G>
struct row_admits_init_ : std::false_type {};
template <::foundation::effects::Effect... Es>
struct row_admits_init_<::fixy::atom::with<Es...>>
    : std::bool_constant<((Es == ::foundation::effects::Effect::Init) || ...)> {};

// The two wait classifications, lifted from a grade to a type-level
// answer.  The primaries are false because the strict pole of
// Synchronization is not a wait at all: a binding that names no strategy
// makes no claim about waiting, so neither rule fires on it.
//
// Both delegate to the consteval predicates in fixy/atoms/Sync.h rather
// than re-listing the grades, because the atom lift reads those same two
// functions.  A grade that moved sides would otherwise move for the lift
// and not for the rules.
template <class G>
struct wait_enters_the_kernel_ : std::false_type {};
template <class G>
    requires requires { G::strategy; }
struct wait_enters_the_kernel_<G> : std::bool_constant<::fixy::atom::sync::enters_the_kernel(G::strategy)> {};

template <class G>
struct wait_burns_the_core_ : std::false_type {};
template <class G>
    requires requires { G::strategy; }
struct wait_burns_the_core_<G> : std::bool_constant<::fixy::atom::sync::burns_the_core(G::strategy)> {};

template <class G>
struct row_admits_alloc_or_io_ : std::false_type {};
template <::foundation::effects::Effect... Es>
struct row_admits_alloc_or_io_<::fixy::atom::with<Es...>>
    : std::bool_constant<((Es == ::foundation::effects::Effect::Alloc || Es == ::foundation::effects::Effect::IO)
                          || ...)> {};

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

template <class G>
struct is_recursing_ : std::false_type {};
template <std::size_t MaxDepth>
struct is_recursing_<::fixy::atom::dispatch::recurses<MaxDepth>> : std::true_type {};

// Whether a signature is stated, and whether it is noexcept.  A function
// type, a pointer or reference to one, and a pointer to a member function
// state a signature; anything else — the opaque tag class a family is
// often named by — states none, and D001 has nothing to read on it.
template <class Signature>
struct signature_noexcept_ {
    static constexpr bool stated = false;
    static constexpr bool value = true;
};
template <class R, class... Args, bool IsNoexcept>
struct signature_noexcept_<R(Args...) noexcept(IsNoexcept)> {
    static constexpr bool stated = true;
    static constexpr bool value = IsNoexcept;
};
template <class R, class... Args, bool IsNoexcept>
struct signature_noexcept_<R (*)(Args...) noexcept(IsNoexcept)> : signature_noexcept_<R(Args...) noexcept(IsNoexcept)> {};
template <class R, class... Args, bool IsNoexcept>
struct signature_noexcept_<R (&)(Args...) noexcept(IsNoexcept)> : signature_noexcept_<R(Args...) noexcept(IsNoexcept)> {};
template <class R, class Owner, class... Args, bool IsNoexcept>
struct signature_noexcept_<R (Owner::*)(Args...) noexcept(IsNoexcept)>
    : signature_noexcept_<R(Args...) noexcept(IsNoexcept)> {};
template <class R, class Owner, class... Args, bool IsNoexcept>
struct signature_noexcept_<R (Owner::*)(Args...) const noexcept(IsNoexcept)>
    : signature_noexcept_<R(Args...) noexcept(IsNoexcept)> {};
template <class Signature>
struct signature_noexcept_<Signature const> : signature_noexcept_<Signature> {};

template <class G>
struct indirect_call_may_throw_ : std::false_type {};
template <class Family>
struct indirect_call_may_throw_<::fixy::atom::dispatch::indirect_call<Family>>
    : std::bool_constant<signature_noexcept_<Family>::stated && !signature_noexcept_<Family>::value> {};

// A spawn no structured join ties to the caller's frame, in a child that
// shares the caller's address space.  detach_with is never joined, and
// syscall_only is a raw clone that the structured join does not reach.
// subprocess is left out on purpose: a forked or posix_spawn'd child
// runs in its own copy of the address space, so a borrow there refers to
// a copy and cannot dangle into the parent's frame.
template <class G>
struct spawn_outlives_the_frame_ : std::false_type {};
template <::fixy::atom::ctrl::rationale Rationale>
struct spawn_outlives_the_frame_<::fixy::atom::spawn::detach_with<Rationale>> : std::true_type {};
template <::fixy::atom::ctrl::rationale Rationale>
struct spawn_outlives_the_frame_<::fixy::atom::spawn::syscall_only<Rationale>> : std::true_type {};

// ── The failure path, in the two spellings the tree has ──────────────
//
// A failure is not a row atom.  foundation/effects/Effect.h says why: an
// enumerator is one bit, and the error type is part of the failure.  The
// tree spells a failure in the type, one of two ways, and each way is
// the only spelling of its mechanism:
//
//   the payload is std::expected<T, E>  — the body returns the failure
//   ControlFlow is ctrl::throws<E>      — the body throws it
//
// So there is no second declaration of a failure to drift from the
// return type: the return type IS the declaration.  A band grades a
// payload and does not change what it holds, so the payload is read
// through every band first.  Every other wrapper is left in place.  In
// particular Secret<std::expected<T, E>> is not read as a failure,
// because its discriminant and its error are both inside the secret.
//
// Each reader answers two things: whether the path exists, and whether
// its error type is fixy::Secret.  An error type the reader cannot see
// — ctrl::any_exception, the family a bare throws<> names — is not
// Secret, which is the answer that refuses rather than the one that
// admits.
template <class Payload>
struct unbanded_ {
    using type = Payload;
};
template <class Payload>
    requires ::fixy::IsBand<Payload>
struct unbanded_<Payload> {
    using type = typename unbanded_<::fixy::band_value_t<Payload>>::type;
};

template <class Payload>
struct returned_failure_ {
    static constexpr bool fails = false;
    static constexpr bool error_is_secret = false;
};
template <class Value, class Error>
struct returned_failure_<std::expected<Value, Error>> {
    static constexpr bool fails = true;
    static constexpr bool error_is_secret = ::fixy::IsSecret<Error>;
};

template <class G>
struct thrown_failure_ {
    static constexpr bool fails = false;
    static constexpr bool error_is_secret = false;
};
template <class ExceptionFamily>
struct thrown_failure_<::fixy::atom::ctrl::throws<ExceptionFamily>> {
    static constexpr bool fails = true;
    static constexpr bool error_is_secret = ::fixy::IsSecret<ExceptionFamily>;
};

// The class of the Security grade, read so that a grade the closed
// relation in fixy/Atom.h does not name stops the build with one message.
// The relation's primary has no definition, so a direct read would also
// stop the build, but with one error at each use and more in fn's tier
// walk.  After the assertion the answer is Classified, which is the
// answer that refuses, so nothing downstream reads the unnamed grade as
// public.
template <class G>
[[nodiscard]] consteval ::fixy::atom::SecurityClass security_class_or_refuse_() noexcept {
    static_assert(::fixy::atom::IsSecurityGrade<G>,
                  "fixy/Collision.h: the Security grade of this binding has no entry in "
                  "fixy::atom::detail::security_class_of_, so no rule can tell whether the binding is classified. "
                  "Give the atom a SecurityClass beside the others in fixy/Atom.h.");
    if constexpr (::fixy::atom::IsSecurityGrade<G>) {
        return ::fixy::atom::security_class_of_v<G>;
    } else {
        return ::fixy::atom::SecurityClass::Classified;
    }
}

// Whether the Protocol grade names a session.  The same axis carries the
// spawn atoms L003 reads, and protocol<proto::None> writes the strict
// pole out, so neither is a session.
template <class G>
struct is_session_protocol_ : std::false_type {};
template <class Proto>
struct is_session_protocol_<::fixy::atom::protocol<Proto>>
    : std::bool_constant<!std::is_same_v<Proto, ::fixy::pole::proto::None>> {};

// Whether ControlFlow states a suspension.  ctrl::coroutine<Policy> is
// the same suspension that atom::coroutine states on Reentrancy, written
// on the axis of the ways a frame is left.
template <class G>
struct suspends_in_control_flow_ : std::false_type {};
template <class SuspensionPolicy>
struct suspends_in_control_flow_<::fixy::atom::ctrl::coroutine<SuspensionPolicy>> : std::true_type {};

}  // namespace detail

// ---------------------------------------------------------------------
// The live rules.
//
// Each reads grades<Atoms...> and nothing else.  The message is the
// theorem, carried from the old catalog with its citation, because the
// message is what a reader gets.

// The rules over one binding: its payload and its pack.
//
// Most rules read the pack alone, through grades<Atoms...>.  Five read
// the PAYLOAD as well — F101, F102, V101, V203 and S011 each pair a grade with
// a replay claim, and the replay claim is the DetSafe band the payload
// carries, not anything in the pack.  I002 and I003 also read the
// payload, for a failure it returns.  So the struct takes the payload as
// its first parameter, and `live_rules<Atoms...>` below is the pack-only
// view with the payload set to void, under which the payload half of
// every such rule stands down.  The two names are one struct: there is one verdicts()
// list, and a rule cannot be in the pack view and out of the bound one.
//
// Payload is the fn's first template parameter, never the fn.  Reading a
// member of the fn from here would complete it, and fn's body asserts
// this file's ValidComposition, which reads this struct — GCC reports
// the cycle as "satisfaction of atomic constraint depends on itself".
// Reading the payload type completes only the payload.
template <class Payload, class... Atoms>
struct rules_of {
    using G = grades<Atoms...>;

    static constexpr bool borrow = std::is_same_v<typename G::template on<Axis::Usage>, ::fixy::atom::borrow>;
    static constexpr bool ghost = std::is_same_v<typename G::template on<Axis::Usage>, ::fixy::atom::ghost>;
    static constexpr bool capability =
        std::is_same_v<typename G::template on<Axis::Usage>, ::fixy::atom::capability_usage>;
    static constexpr bool coroutine =
        std::is_same_v<typename G::template on<Axis::Reentrancy>, ::fixy::atom::coroutine>;
    static constexpr bool monotonic =
        std::is_same_v<typename G::template on<Axis::Mutation>, ::fixy::atom::mut_monotonic>;
    static constexpr bool unverified =
        std::is_same_v<typename G::template on<Axis::Trust>, ::fixy::atom::trust_unverified>;
    static constexpr bool unbounded_cost =
        std::is_same_v<typename G::template on<Axis::Complexity>, ::fixy::atom::cost_unbounded>;

    static constexpr bool row_bg = detail::row_admits_bg_<typename G::template on<Axis::Effect>>::value;
    static constexpr bool row_observable = detail::row_admits_observable_<typename G::template on<Axis::Effect>>::value;
    static constexpr bool atomic_repr = detail::repr_is_atomic_<typename G::template on<Axis::Representation>>::value;
    static constexpr bool thread_local_state =
        detail::is_thread_local_<typename G::template on<Axis::GlobalState>>::value;
    static constexpr bool longjmp_unsafe =
        detail::is_longjmp_unsafe_<typename G::template on<Axis::ControlFlow>>::value;
    static constexpr bool recurses = detail::is_recursing_<typename G::template on<Axis::CallShape>>::value;

    // A binding says nothing about Usage when it takes the strict pole,
    // which on this axis is the linear grade: consumed exactly once.
    static constexpr bool linear = !G::template mentions<Axis::Usage>;

    // The concurrency premise several rules share.  A coroutine suspends
    // and a Bg row runs elsewhere, and either makes the body concurrent
    // with respect to the caller's frame.  A suspension can be written on
    // Reentrancy or on ControlFlow, and the premise reads both.  A read of
    // one axis only would let a pack avoid every rule below by a write of
    // the suspension on the other axis.
    static constexpr bool suspends_in_control_flow =
        detail::suspends_in_control_flow_<typename G::template on<Axis::ControlFlow>>::value;
    static constexpr bool concurrent = coroutine || suspends_in_control_flow || row_bg;

    static constexpr bool L002_ok = !(borrow && concurrent);
    static constexpr bool M012_ok = !(monotonic && concurrent && !atomic_repr);
    static constexpr bool P010_ok = !(ghost && row_observable);
    static constexpr bool L007_ok = !(borrow && row_bg);
    static constexpr bool T001_ok = !(capability && unverified);
    static constexpr bool R002_ok = !(coroutine && borrow);
    static constexpr bool R003_ok = !(coroutine && row_bg);
    static constexpr bool L006_ok = !(linear && longjmp_unsafe);
    static constexpr bool G002_ok = !(thread_local_state && atomic_repr);
    static constexpr bool D002_ok = !(recurses && unbounded_cost);

    // ── The regime family, live since fixy/atoms/Regime.h ────────────
    //
    // Regime's discharge is Measurement: which tier a body ACHIEVES is
    // the bench's answer.  These six rules are the part that needs no
    // bench, because each is a contradiction between the declared tier
    // and something else the same binding declares.  A body cannot be
    // budgeted in nanoseconds and also admit an unbounded cost, a
    // background row, buffered stdio, or a coroutine suspension.
    static constexpr bool hot = std::is_same_v<typename G::template on<Axis::Regime>, ::fixy::atom::regime::hot>;

    // The one premise read from the payload rather than the pack: the
    // DetSafe band's claim that the bytes are replay-deterministic.  See
    // detail::replay_deterministic_ for what the claim is and where the
    // old catalog left it.  False under the pack-only view.
    static constexpr bool replay_deterministic = detail::replay_deterministic_<Payload>::value;
    static constexpr bool row_alloc_or_io =
        detail::row_admits_alloc_or_io_<typename G::template on<Axis::Effect>>::value;

    // "Unstated" is the strict pole on Complexity, so an unstated cost
    // is the absence of a grade rather than a grade of its own.  H001
    // refuses both it and the explicit unbounded grade: the hot path has
    // to say what its compute envelope is.
    static constexpr bool cost_unstated = !G::template mentions<Axis::Complexity>;
    static constexpr bool H001_ok = !(hot && (cost_unstated || unbounded_cost));

    // pred::True is the Refinement strict pole, again an absence.  A hot
    // body assumes an invariant it does not check — that is what buys
    // the nanoseconds — so it must carry a witness that something else
    // proved it.
    static constexpr bool no_witness_floor = !G::template mentions<Axis::Refinement>;
    static constexpr bool H002_ok = !(hot && no_witness_floor);

    static constexpr bool H003_ok = !(hot && row_alloc_or_io && unbounded_cost);

    // H010 is not covered by H001 or H003: a HotPath x Bg binding with
    // cost::Constant and no Alloc or IO row passes both of those and is
    // still a context contradiction, because the two name different
    // threads.
    static constexpr bool H010_ok = !(hot && row_bg);

    static constexpr bool R001_ok = !(coroutine && hot);
    static constexpr bool S001_ok = !(G::template mentions<Axis::Stdio> && hot);

    // ── The wait family, live since fixy/atoms/Sync.h ─────────────────
    //
    // Both read the same axis from opposite ends of its ladder, and the
    // ladder's own header draws the line: the three lowest grades enter
    // the kernel or the scheduler, the three highest stay in user space.
    // A kernel wait is too slow for the hot path, and a core-burning spin
    // is too expensive for a background one.
    //
    // The predicates come from fixy/atoms/Sync.h rather than being
    // rewritten here, so the rules and the atom lift cannot disagree
    // about where the line falls.
    static constexpr bool kernel_wait =
        detail::wait_enters_the_kernel_<typename G::template on<Axis::Synchronization>>::value;
    static constexpr bool core_burning_spin =
        detail::wait_burns_the_core_<typename G::template on<Axis::Synchronization>>::value;

    static constexpr bool W001_ok = !(hot && kernel_wait);

    // W002 reads burns_the_core rather than "not a kernel wait", which is
    // narrower by one grade: UMWAIT halts the core in C0.1 instead of
    // spinning it, so a background body may use it.  That grade is the
    // whole reason the atom header carries two predicates instead of one.
    static constexpr bool W002_ok = !(row_bg && core_burning_spin);

    // ── The observability family, live since fixy/atoms/Observe.h ─────
    //
    // Two theorems about one axis, and they are not the same theorem.
    //
    // B002 is the containment: the effects a binding declares OBSERVABLE
    // must be effects it declared at all.  The Effect grade stays the
    // single authority for what the binding may do, and Observability
    // names which part of that row is observation rather than
    // computation.  A surface naming an effect outside the Effect row
    // would widen what the operation is permitted to do, and a passive
    // surface that can do that is not passive — CLAUDE.md L15 says
    // Observe records facts and does not enforce policy.
    //
    // B001 is the back-pressure trap, and it is the theorem the catalog
    // recorded for this axis before any atom existed.  It is kept rather
    // than overwritten: a rule code is stable API here, so B002 above is
    // a new code rather than a reinterpretation of this one.
    using observability_row = typename detail::observability_row_of_<
        typename G::template on<Axis::Observability>>::type;
    using effect_row = ::fixy::atom::effect_row_of_t<typename G::template on<Axis::Effect>>;

    static constexpr bool observes_something = G::template mentions<Axis::Observability>;
    static constexpr bool B002_ok = ::foundation::effects::Subrow<observability_row, effect_row>;

    // "May run unbounded" reads three ways on this pack, and B001 refuses
    // all three: an explicit unbounded cost, an unstated cost, or an
    // explicit unbounded space grade.  The remedy the theorem names is
    // the pair space::Bounded plus cost::Linear, so a binding that states
    // neither is exactly the trap.
    static constexpr bool space_unbounded =
        std::is_same_v<typename G::template on<Axis::Space>, ::fixy::atom::space_unbounded>;
    static constexpr bool may_run_unbounded = cost_unstated || unbounded_cost || space_unbounded;
    static constexpr bool B001_ok = !(row_bg && observes_something && may_run_unbounded);

    // ── The hardware-instruction family, live since fixy/atoms/Hw.h ──
    //
    // The ladder is a chain where each tier admits every class below it,
    // so "at or above NonDeterministicTsc" covers PrivilegedMsr too, and
    // V201's old name — HotPathNondetTscOrPrivileged — needs no second
    // clause.  V203 is the first rule to consume the payload's replay
    // claim: the same tier that is too slow for the hot path is also
    // non-deterministic by construction, and a payload claiming
    // replay-determinism cannot survive it.
    static constexpr bool hw_nondeterministic = detail::hw_at_or_above_<
        ::fixy::atom::hw::HwInstruction::NonDeterministicTsc, typename G::template on<Axis::HwInstruction>>::value;
    static constexpr bool hw_privileged = detail::hw_at_or_above_<
        ::fixy::atom::hw::HwInstruction::PrivilegedMsr, typename G::template on<Axis::HwInstruction>>::value;
    static constexpr bool row_init = detail::row_admits_init_<typename G::template on<Axis::Effect>>::value;

    static constexpr bool V201_ok = !(hot && hw_nondeterministic);
    static constexpr bool V202_ok = !(hw_privileged && !row_init);
    static constexpr bool V203_ok = !(replay_deterministic && hw_nondeterministic);

    // ── The barrier-strength family, live since fixy/atoms/Barrier.h ──
    //
    // One rule, read at one boundary of the chain.  A seq_cst fence or
    // the standalone full fence drains the store buffer, and the hot
    // path's budget is bounded by the cache-coherence fabric, not by a
    // drain.  A release store and an acquire load are one MOV each on
    // x86 and are what the SPSC ring is made of, so the floor sits above
    // them and above acq_rel.
    static constexpr bool barrier_seq_cst_or_above = detail::barrier_at_or_above_<
        ::foundation::algebra::lattices::BarrierStrength::SeqCst, typename G::template on<Axis::BarrierStrength>>::value;

    static constexpr bool V301_ok = !(hot && barrier_seq_cst_or_above);

    // ── The memory-scope family, live since fixy/atoms/Scope.h ────────
    //
    // A scope is how far a publication REACHES and a strength is what the
    // fence ORDERS, and V401 is the rule that reads the two together: a
    // publication that reaches a cluster or further needs a fence at or
    // above acq_rel, or a reader it reaches can observe the write ahead
    // of the data the writer ordered before it.  A binding that names a
    // wide scope and no strength provides no fence at all, which is the
    // same trap with nothing named, so the rule fires on it too.  The
    // floor is read through the lattice's leq, so the host trunk is
    // incomparable with Cluster and the rule stands down for it.
    static constexpr bool scope_cluster_or_above = detail::scope_at_or_above_<
        ::foundation::algebra::lattices::MemoryScope::Cluster, typename G::template on<Axis::MemoryScope>>::value;
    static constexpr bool barrier_acq_rel_or_above = detail::barrier_at_or_above_<
        ::foundation::algebra::lattices::BarrierStrength::AcqRel, typename G::template on<Axis::BarrierStrength>>::value;

    static constexpr bool V401_ok = !(scope_cluster_or_above && !barrier_acq_rel_or_above);

    // ── The SIMD-ISA family, live since fixy/atoms/Simd.h ─────────────
    //
    // Two trunks again, and two rules.  V101 is the second rule to
    // consume the payload's replay claim: a body emitted for one vendor's
    // vector width reduces in that width's order, and a payload claiming
    // the same bits on every host cannot come out of it.  Scalar pins
    // nothing and Portable is by definition one kernel for every set, so
    // both stand down.
    //
    // V402 is the coherence of a pinned scope with a pinned ISA, and it
    // is structural where the old rule was a marker no atom ever set.
    // The host shareability trunk — Inner and Outer, the DMB ISH/OSH
    // family — is the ARM trunk's, so it coheres with an ARM ISA and with
    // nothing else; the accelerator trunk is GPU scope and coheres with
    // no host ISA at all.  The shared points on either axis cohere with
    // anything.
    static constexpr bool isa_pinned = detail::isa_trunk_pinned_<typename G::template on<Axis::SimdIsa>>::value;
    static constexpr bool isa_arm = detail::isa_on_arm_trunk_<typename G::template on<Axis::SimdIsa>>::value;
    static constexpr bool scope_pinned =
        detail::scope_trunk_pinned_<typename G::template on<Axis::MemoryScope>>::value;
    static constexpr bool scope_host = detail::scope_on_host_trunk_<typename G::template on<Axis::MemoryScope>>::value;
    static constexpr bool trunks_cohere = scope_host && isa_arm;

    static constexpr bool V101_ok = !(replay_deterministic && isa_pinned);
    static constexpr bool V402_ok = !(scope_pinned && isa_pinned && !trunks_cohere);

    // ── The floating-point-mode family, live since fixy/atoms/Fp.h ────
    //
    // Two rules, both reading the payload's replay claim against one
    // setting of the mode.  F101 is reassociation: a sum rewritten into a
    // different tree rounds differently, and the bounded-depth rung is
    // refused with the unrestricted one because a tree of ANY shape but
    // the one the source wrote is a different sum.  F102 is contraction
    // across statements: whether a multiply and an add fuse into one FMA
    // changes where the single rounding falls, and "fast" leaves that to
    // whatever the compiler decides per build.
    //
    // Contraction WITHIN one expression is not refused, and that is
    // deliberate rather than an omission: it is what -ffp-contract=on
    // permits, which is the flag CLAUDE.md's DetSafe discipline names as
    // safe, because the fusion is visible in the source expression and so
    // is the same on every build.
    using fp_mode_grade = typename G::template on<Axis::FpMode>;

    static constexpr bool fp_reassociates =
        detail::fp_names_<::fixy::atom::fp::FpReassociate::UnrestrictedRewrite, fp_mode_grade>::value
        || detail::fp_names_<::fixy::atom::fp::FpReassociate::BoundedTreeDepth, fp_mode_grade>::value;
    static constexpr bool fp_contracts_across_statements =
        detail::fp_names_<::fixy::atom::fp::FpContract::Fast, fp_mode_grade>::value;

    static constexpr bool F101_ok = !(replay_deterministic && fp_reassociates);
    static constexpr bool F102_ok = !(replay_deterministic && fp_contracts_across_statements);

    // ── Three rules the old tree carried as markers ───────────────────
    //
    // S011 is the capability against the replay claim.  A capability is
    // an authorization token minted for one run, so a replay cannot mint
    // the same one again, and a payload that claims the same bits on
    // every replay cannot hold one.  T001 also reads capability_usage,
    // and a capability with no trust grade is unverified, so a fixture
    // that isolates S011 states a trust.
    static constexpr bool S011_ok = !(capability && replay_deterministic);

    // D001 reads a signature only where the family states one.  A family
    // named by an opaque tag class states no signature, so the rule
    // stands down for it; a caller who wants the check names the pointer
    // type itself, indirect_call<void (*)(void*) noexcept>, the shape
    // BackgroundThread's region-ready callback already declares.
    static constexpr bool indirect_call_may_throw =
        detail::indirect_call_may_throw_<typename G::template on<Axis::CallShape>>::value;
    static constexpr bool D001_ok = !indirect_call_may_throw;

    // L003 is L002's lifetime hazard through a different door.  L002
    // reads a suspension or a Bg row; a detached or raw-cloned child is
    // neither, and it can still run after the frame the borrow points
    // into has unwound.
    static constexpr bool spawn_outlives_the_frame =
        detail::spawn_outlives_the_frame_<typename G::template on<Axis::Protocol>>::value;
    static constexpr bool L003_ok = !(borrow && spawn_outlives_the_frame);

    // P010 reads the effect row.  Two other axes also force emitted
    // code, and a ghost binding that engages either is the same
    // contradiction through a different door.
    static constexpr bool emits_outside_the_row = G::template mentions<Axis::Stdio>
                                               || G::template mentions<Axis::SyscallSurface>;
    static constexpr bool P002_ok = !(ghost && emits_outside_the_row);

    // ── The constant-time family and the failure family ─────────────
    //
    // Constant time is the Security grade atom::constant_time, read here
    // through the closed relation fixy/Atom.h declares beside the
    // Security atoms, so a new point on that axis cannot read as public.
    // A failure is the payload std::expected<T, E> or ctrl::throws<E>,
    // read by the two detail readers above.  Neither is in the effect
    // row, and foundation/effects/Effect.h says why at its top.
    static constexpr ::fixy::atom::SecurityClass security_class =
        detail::security_class_or_refuse_<typename G::template on<Axis::Security>>();
    static constexpr bool constant_time = security_class == ::fixy::atom::SecurityClass::ConstantTime;
    static constexpr bool classified = constant_time || security_class == ::fixy::atom::SecurityClass::Classified;

    // The payload rule's side of the failure reads the payload, as the
    // replay claim does, so it stands down under the pack-only view.
    using returned_failure = detail::returned_failure_<typename detail::unbanded_<Payload>::type>;
    using thrown_failure = detail::thrown_failure_<typename G::template on<Axis::ControlFlow>>;
    static constexpr bool fails = returned_failure::fails || thrown_failure::fails;
    static constexpr bool fails_with_plain_error = (returned_failure::fails && !returned_failure::error_is_secret)
                                                || (thrown_failure::fails && !thrown_failure::error_is_secret);

    // F103, F104 and F105 read the FP mode against the timing claim, as
    // F101 and F102 read it against the replay claim.  F103 refuses only
    // the unrestricted rewrite: a tree of bounded depth has a topology
    // that the data cannot choose, so it keeps the timing independent
    // even though F101 refuses it for replay.
    //
    // F104 and F105 refuse a mode that does not NAME the flush.  A
    // setting the mode does not name is at its strict value, and on these
    // two enums the strict value is the slow one: denormals honoured,
    // subnormals preserved.  So a constant-time body that names an FP mode
    // at all must name the flush.  A body that names no FP mode claims
    // nothing about floating point, and the two rules stand down for it.
    static constexpr bool fp_mode_stated = G::template mentions<Axis::FpMode>;
    static constexpr bool fp_flushes_denormal_inputs =
        detail::fp_names_<::fixy::atom::fp::FpDenormalInput::DenormalsAreZero, fp_mode_grade>::value;
    static constexpr bool fp_flushes_subnormal_results =
        detail::fp_names_<::fixy::atom::fp::FpFtz::FlushToZero, fp_mode_grade>::value;
    static constexpr bool fp_rewrites_without_bound =
        detail::fp_names_<::fixy::atom::fp::FpReassociate::UnrestrictedRewrite, fp_mode_grade>::value;

    static constexpr bool F103_ok = !(constant_time && fp_rewrites_without_bound);
    static constexpr bool F104_ok = !(constant_time && fp_mode_stated && !fp_flushes_denormal_inputs);
    static constexpr bool F105_ok = !(constant_time && fp_mode_stated && !fp_flushes_subnormal_results);

    // E044 and I004 read the one concurrency premise above, so a
    // classified async session is refused both ways: E044 when it claims
    // constant time, I004 when it does not.  That is the theorem.  The
    // one remedy is a synchronous constant-time region, or a
    // declassification before the send.
    static constexpr bool E044_ok = !(constant_time && concurrent);

    // A constant-time binding is classified, so every S010 trip is also a
    // trip of the corpus entry staleness_secret_without_declassify.  The
    // rule is kept for its own theorem and its own remedy, as R002 is
    // kept beside L002.
    static constexpr bool S010_ok = !(constant_time && G::template mentions<Axis::Staleness>);

    static constexpr bool session_protocol =
        detail::is_session_protocol_<typename G::template on<Axis::Protocol>>::value;
    static constexpr bool I004_ok = !(classified && concurrent && session_protocol && !constant_time);

    // I003 needs no condition beyond the failure path.  A constant-time
    // body works on classified data by definition, and the caller of a
    // body that can fail branches on the discriminant, so the failure is
    // a branch on the secret that leaves the constant-time region.
    // fixy::ct::eq traps on a length mismatch for this reason.
    static constexpr bool I003_ok = !(constant_time && fails);

    static constexpr bool I002_ok = !(classified && fails_with_plain_error);

    // ── Naming the rules a pack trips ────────────────────────────────
    //
    // fn's tier-5 message carries these codes, so a reader and a
    // negative fixture both learn WHICH rule refused rather than only
    // that one did.  The codes are stable API for exactly that reason.
    //
    // The pairing is written out rather than walked by reflection.  A
    // walk over members_of(^^live_rules<Atoms...>) reads the verdicts
    // directly, but completing that specialization instantiates
    // validate() below, whose static_asserts then fire once per failing
    // rule — eleven extra errors under a fixture that wants one.  The
    // two pins after this class compare the rows here against the
    // <code>_ok members reflection finds in live_rules<>, whose empty
    // pack trips nothing, so a rule written without a row is named.
    struct rule_verdict {
        bool ok{};
        std::string_view code{};
    };

    [[nodiscard]] static consteval auto verdicts() noexcept {
        return std::array{
            rule_verdict{L002_ok, "L002"}, rule_verdict{M012_ok, "M012"}, rule_verdict{P010_ok, "P010"},
            rule_verdict{L007_ok, "L007"}, rule_verdict{T001_ok, "T001"}, rule_verdict{R002_ok, "R002"},
            rule_verdict{R003_ok, "R003"}, rule_verdict{L006_ok, "L006"}, rule_verdict{G002_ok, "G002"},
            rule_verdict{D002_ok, "D002"}, rule_verdict{P002_ok, "P002"}, rule_verdict{H001_ok, "H001"},
            rule_verdict{H002_ok, "H002"}, rule_verdict{H003_ok, "H003"}, rule_verdict{H010_ok, "H010"},
            rule_verdict{R001_ok, "R001"}, rule_verdict{S001_ok, "S001"}, rule_verdict{W001_ok, "W001"},
            rule_verdict{W002_ok, "W002"}, rule_verdict{B001_ok, "B001"}, rule_verdict{B002_ok, "B002"},
            rule_verdict{V201_ok, "V201"}, rule_verdict{V202_ok, "V202"}, rule_verdict{V203_ok, "V203"},
            rule_verdict{V301_ok, "V301"}, rule_verdict{V401_ok, "V401"}, rule_verdict{V101_ok, "V101"},
            rule_verdict{V402_ok, "V402"}, rule_verdict{F101_ok, "F101"}, rule_verdict{F102_ok, "F102"},
            rule_verdict{S011_ok, "S011"}, rule_verdict{D001_ok, "D001"}, rule_verdict{L003_ok, "L003"},
            rule_verdict{F103_ok, "F103"}, rule_verdict{F104_ok, "F104"}, rule_verdict{F105_ok, "F105"},
            rule_verdict{E044_ok, "E044"}, rule_verdict{S010_ok, "S010"}, rule_verdict{I004_ok, "I004"},
            rule_verdict{I003_ok, "I003"}, rule_verdict{I002_ok, "I002"},
        };
    }

    // Every code the pack trips, in the order above, or an empty view
    // when it trips none.  A pack can trip several: R002's premises
    // imply L002's, so no pack names R002 alone, and listing every one
    // is what lets each rule's fixture floor on its own code.
    [[nodiscard]] static consteval std::string_view failing_codes() noexcept {
        std::string text;
        for (const rule_verdict& verdict : verdicts()) {
            if (verdict.ok) continue;
            if (!text.empty()) text += ", ";
            text += std::string{verdict.code};
        }
        return std::define_static_string(text);
    }

    // Every code, whatever the pack.  The pin below reads it against
    // the member walk; nothing else calls it.
    [[nodiscard]] static consteval std::string_view every_code() noexcept {
        std::string text;
        for (const rule_verdict& verdict : verdicts()) {
            text += std::string{verdict.code};
            text += ' ';
        }
        return std::define_static_string(text);
    }

    [[nodiscard]] static consteval bool validate() noexcept {
        static_assert(L002_ok, "L002: borrow x async. A borrowed reference's lifetime is tied to the caller's frame "
                               "and cannot bridge a suspension or a hand-off to another thread. Scope the borrow "
                               "before the await, or capture by value.");
        static_assert(M012_ok, "M012: monotonic x concurrent without an atomic representation. Two threads stepping "
                               "a monotonic counter through a non-atomic carrier lose updates. Use "
                               "repr<ReprKind::Atomic>.");
        static_assert(P010_ok, "P010: ghost x Row<Alloc|IO|Block>. A ghost binding is erased at codegen and emits no "
                               "instructions, but each of those three effects requires emitted code. Drop the ghost "
                               "grade, or drop the observable effect.");
        static_assert(L007_ok, "L007: borrow x Row<Bg>. When the background thread runs the body, the caller's frame "
                               "may have unwound and the borrow dangles. Move ownership into the closure, or drop "
                               "the Bg atom.");
        static_assert(T001_ok, "T001: capability x trust::unverified. A capability mints a non-revocable "
                               "authorization token that consumers treat as proof of authority, which a binding of "
                               "unverified provenance cannot establish.");
        static_assert(R002_ok, "R002: coroutine x borrow. A coroutine resumes after the caller's frame may have "
                               "unwound, dangling the borrow.");
        static_assert(R003_ok, "R003: coroutine x Row<Bg>. The suspension and the background hand-off are two "
                               "independent reasons the body outlives its caller; naming both says neither owns the "
                               "lifetime.");
        static_assert(L006_ok, "L006: linear x longjmp. A longjmp past a linear value's scope skips its consumption, "
                               "so the resource leaks with no diagnostic.");
        static_assert(G002_ok, "G002: thread-local x atomic representation. Thread-local storage is private to one "
                               "thread, so an atomic carrier inside it pays for synchronization nobody can observe.");
        static_assert(D002_ok, "D002: unbounded recursion x unbounded cost. Recursion with neither a depth bound nor "
                               "a cost bound is a stack overflow the type system could have refused.");
        static_assert(P002_ok, "P002: ghost x an emitting surface. A ghost binding is erased at codegen, and a "
                               "stdio write or a syscall is emitted code by definition. P010 catches this through "
                               "the effect row; these two axes are the other doors to the same contradiction.");
        static_assert(H001_ok, "H001: hot x an unstated or unbounded cost. The hot path must justify its compute "
                               "envelope, so declare cost::Constant or cost::Linear. An unstated cost is the "
                               "Complexity strict pole, which on a hot binding is a claim nobody made.");
        static_assert(H002_ok, "H002: hot x no refinement witness. A hot body buys its nanoseconds by assuming an "
                               "invariant instead of checking it, so something upstream must have proved it. Attach "
                               "a Refined input that carries the proof.");
        static_assert(H003_ok, "H003: hot x an Alloc or IO row x unbounded cost. Move the allocation or the I/O "
                               "outside the hot path, or give it an Init or Bg context that owns the unbounded "
                               "surface.");
        static_assert(H010_ok, "H010: hot x Row<Bg>. A function cannot be both on the foreground hot path and in "
                               "background context: the two name different threads. H001 and H003 both admit a hot "
                               "Bg binding with a constant cost and no Alloc or IO, which is still this "
                               "contradiction.");
        static_assert(R001_ok, "R001: coroutine x hot. A coroutine frame costs an indirect call plus a state spill "
                               "at every suspension point, and one resume breaches the budget.");
        static_assert(S001_ok, "S001: stdio x hot. Buffered stdio takes a lock and may block. Neither belongs on a "
                               "path budgeted in nanoseconds.");
        static_assert(W001_ok, "W001: hot x a kernel wait. A park or a futex wait costs 1-5 us because it is bounded "
                               "by the scheduler, against a budget bounded by the cache-coherence fabric at 10-40 "
                               "ns. Wait with a spin, or leave the hot path.");
        static_assert(W002_ok, "W002: Row<Bg> x a spin that burns the core. A background body that spins holds a core "
                               "the scheduler could have given to foreground work. Park, or use UMWAIT, which halts "
                               "the core instead of spinning it.");
        static_assert(B001_ok, "B001: a Bg observable surface that may run unbounded is a back-pressure trap. The "
                               "producer cannot see the consumer fall behind, because the surface exists to report "
                               "facts and not to apply back pressure. Declare space::Bounded and cost::Linear.");
        static_assert(B002_ok, "B002: an observability surface names an effect outside the binding's effect row. "
                               "Observability names which PART of the declared row is observation; it is not a "
                               "second row and cannot widen the first. Add the effect to the Effect grade if the "
                               "operation really performs it, or drop it from the surface.");
        static_assert(V201_ok, "V201: hot x an instruction tier at or above NonDeterministicTsc. A serialising "
                               "timestamp read drains the pipeline, 20-40 cycles against a budget of tens of "
                               "nanoseconds, and PrivilegedMsr sits above it on the ladder. Read the counter off "
                               "the hot path, or drop the tier.");
        static_assert(V202_ok, "V202: the PrivilegedMsr tier x an effect row with no Init. rdmsr, wrmsr, IN and OUT "
                               "belong to startup, where they can be ordered and audited and where the ring-0 "
                               "capability is held. Add Effect::Init to the row, or drop the tier.");
        static_assert(V203_ok, "V203: a replay-deterministic payload x an instruction tier at or above "
                               "NonDeterministicTsc. The payload's DetSafe band claims the same bits on every "
                               "replay; a timestamp counter differs per run by construction. Lower the band, or "
                               "drop the tier.");
        static_assert(V301_ok, "V301: hot x a fence at or above SeqCst. A seq_cst fence drains the store buffer, "
                               "about 30 ns on x86, against a budget bounded by the cache-coherence fabric at 10-40 "
                               "ns. Publish with a release store and read with an acquire load, or leave the hot "
                               "path.");
        static_assert(V401_ok, "V401: a scope at or above Cluster x a fence below AcqRel. The publication reaches "
                               "further than the fence orders, so a reader on another block can observe the write "
                               "before the data the writer published ahead of it. Fence with acq_rel or stronger, "
                               "or narrow the scope.");
        static_assert(V101_ok, "V101: a replay-deterministic payload x an ISA pinned to one trunk. A body emitted "
                               "for one vendor's vector width reduces in that width's order, so its bits differ "
                               "per host; the payload's DetSafe band claims the same bits on every replay. Emit "
                               "Scalar or Portable, or lower the band.");
        static_assert(V402_ok, "V402: a trunk-pinned scope x a trunk-pinned ISA that do not cohere. The host "
                               "shareability scopes are the ARM DMB ISH/OSH family and the accelerator scopes are "
                               "GPU scope; an x86 ISA has neither and an ARM ISA has only the first. Pin the scope "
                               "and the ISA on one trunk, or leave one at its shared point.");
        static_assert(F101_ok, "F101: a replay-deterministic payload x FP reassociation permitted. Reassociation "
                               "rewrites the sum into a different tree, and a different tree rounds differently, so "
                               "the bits differ between builds; the bounded-depth rung is refused with the "
                               "unrestricted one for the same reason. Forbid reassociation, or lower the band.");
        static_assert(F102_ok, "F102: a replay-deterministic payload x FP contraction across statements. Whether a "
                               "multiply and an add fuse into one FMA decides where the single rounding falls, and "
                               "'fast' leaves that to the build. Contract within an expression instead, which is "
                               "visible in the source and stable, or lower the band.");
        static_assert(S011_ok, "S011: capability x a replay-deterministic payload. A capability is an "
                               "authorization token minted for one run, and replay cannot mint the same token "
                               "again, so the payload's claim of the same bits on every replay cannot hold. Carry "
                               "a content-addressed handle instead of the capability, or lower the band.");
        static_assert(D001_ok, "D001: an indirect call whose named signature is not noexcept. A throw out of the "
                               "callee crosses a boundary that promised none and ends the process. Add noexcept "
                               "to the signature, or put a noexcept trampoline in front of the call that turns "
                               "the failure into a std::expected.");
        static_assert(L003_ok, "L003: borrow x a spawn no structured join ties to the frame. A detached or "
                               "raw-cloned child shares the address space and can run after the caller's frame "
                               "has unwound, so the borrow dangles. Spawn through mint_spawn, which joins, or "
                               "move ownership into the child.");
        static_assert(F103_ok, "F103: constant time x FP reassociation by an unrestricted rewrite. The compiler "
                               "can pick the tree of the sum from the magnitudes of the operands, so the count of "
                               "operations follows the data. Forbid reassociation, or bound it to a tree of fixed "
                               "depth.");
        static_assert(F104_ok, "F104: constant time x an FP mode that honours denormal inputs. A denormal input "
                               "costs 30 to 100 times the cycles of a normal one on x86 and ARM, so the time tells "
                               "the magnitude of the input. Name FpDenormalInput::DenormalsAreZero in the mode "
                               "(MXCSR.DAZ, FPCR.FZ).");
        static_assert(F105_ok, "F105: constant time x an FP mode that preserves subnormal results. A subnormal "
                               "result costs 30 to 100 times the cycles of a normal one, so the time tells the "
                               "magnitude of the result. Name FpFtz::FlushToZero in the mode.");
        static_assert(E044_ok, "E044: constant time x a suspension or a Bg row. The scheduler then decides when the "
                               "body runs, and it sees the classified data through the timing. Keep the "
                               "constant-time core synchronous, and put only its boundary in async code.");
        static_assert(S010_ok, "S010: constant time x a staleness window. A stale value makes the body check "
                               "freshness at run time, and that check is a branch whose time follows the data. "
                               "Keep Staleness at Fresh, or drop constant_time.");
        static_assert(I004_ok, "I004: classified x a suspension or a Bg row x a session protocol, without constant "
                               "time. The peer sees each send, and the time of each send follows the classified "
                               "data. Send from a synchronous binding that states atom::constant_time, or "
                               "declassify before the send.");
        static_assert(I003_ok, "I003: constant time x a failure path. The caller branches on success or failure, "
                               "so the discriminant leaks the condition that chose it, and that condition reads "
                               "classified data. Select the result with fixy::ct::select and fail after a "
                               "declassification, or trap as fixy::ct::eq does.");
        static_assert(I002_ok, "I002: classified x a failure whose error type is not fixy::Secret. The error value "
                               "leaves the classified binding and carries what the body knew when it failed. "
                               "Wrap the error type in fixy::Secret, declassify with a named policy, or state "
                               "atom::as_public.");
        return valid;
    }

    // Folded over verdicts() rather than written as a conjunction of the
    // same codes a third time.
    //
    // This file used to carry the list three ways: the verdicts array,
    // validate()'s return, and this.  Adding a rule to two of the three
    // left a rule that reports itself in the tier-5 message and refuses
    // nothing — the exact shape of a gate that asks for nothing, which is
    // what this migration keeps finding.  The fold makes verdicts() the
    // single list, and every_ok_member_has_a_verdict_row_ below is what
    // keeps a new `_ok` member from staying out of it.
    static constexpr bool valid = [] {
        for (const rule_verdict& verdict : verdicts()) {
            if (!verdict.ok) return false;
        }
        return true;
    }();
};

// The pack-only view.  Every rule that reads the payload stands down
// under void, so a cell written against the pack alone means what it
// says: this pack, with any payload, is or is not a contradiction.
template <class... Atoms>
using live_rules = rules_of<void, Atoms...>;

// ---------------------------------------------------------------------
// The pin that reads the implementation rather than the specification.
//
// live_rules names one `<code>_ok` member per rule it implements.  The
// two checks below walk those members: the first asks that every code the
// corpus calls Live has one, the second that no `_ok` member exists
// without a Live entry.  Together they bind the corpus to the code that
// actually runs, which is the step the original `live == roster - pending`
// arithmetic skipped.

namespace detail {

[[nodiscard]] consteval bool member_is_the_gate_for_(std::meta::info member, std::string_view code) noexcept {
    if (!std::meta::has_identifier(member)) return false;
    const std::string_view id = std::meta::identifier_of(member);
    return id.size() == code.size() + 3 && id.starts_with(code) && id.ends_with("_ok");
}

[[nodiscard]] consteval std::size_t implemented_rule_count_() noexcept {
    std::size_t found = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : std::define_static_array(
                      std::meta::members_of(^^rules_of<void>, std::meta::access_context::unchecked()))) {
        if constexpr (std::meta::has_identifier(member)) {
            if constexpr (std::meta::identifier_of(member).ends_with("_ok")) {
                ++found;
            }
        }
    }
#pragma GCC diagnostic pop
    return found;
}

[[nodiscard]] consteval bool every_live_entry_is_implemented_() noexcept {
    for (const corpus_entry& entry : rule_corpus) {
        if (entry.disposition != Disposition::Live) continue;
        bool implemented = false;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
        template for (constexpr auto member : std::define_static_array(
                          std::meta::members_of(^^rules_of<void>, std::meta::access_context::unchecked()))) {
            if (member_is_the_gate_for_(member, entry.code)) implemented = true;
        }
#pragma GCC diagnostic pop
        if (!implemented) return false;
    }
    return true;
}

}  // namespace detail

static_assert(detail::every_live_entry_is_implemented_(),
              "fixy/Collision.h: the corpus records a rule as Live and live_rules defines no <code>_ok member "
              "for it.  A rule is Live when it is written, not when it is listed.");

static_assert(detail::implemented_rule_count_() == live_rule_count,
              "fixy/Collision.h: live_rules defines a different number of <code>_ok members than the corpus "
              "records as Live.  Either a rule was written without a corpus entry, or one entry names a rule "
              "the implementation spells differently.");

// The verdict rows are the third hand-written list of the same set, and
// these two pins bind it to the other two.  The count is against the
// member walk rather than against live_rule_count so that a rule
// written with an `_ok` member and no verdict row is named here even
// before it reaches the corpus.
namespace detail {

[[nodiscard]] consteval bool every_ok_member_has_a_verdict_row_() noexcept {
    bool all_rowed = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : std::define_static_array(
                      std::meta::members_of(^^rules_of<void>, std::meta::access_context::unchecked()))) {
        if constexpr (std::meta::has_identifier(member)) {
            constexpr std::string_view id = std::meta::identifier_of(member);
            if constexpr (id.size() > 3 && id.ends_with("_ok")) {
                // The trailing space is what keeps a code from matching
                // inside a longer one.
                std::string wanted{id.substr(0, id.size() - 3)};
                wanted += ' ';
                if (!::fixy::detail::text_contains(live_rules<>::every_code(), wanted)) all_rowed = false;
            }
        }
    }
#pragma GCC diagnostic pop
    return all_rowed;
}

}  // namespace detail

static_assert(detail::every_ok_member_has_a_verdict_row_(),
              "fixy/Collision.h: live_rules defines a <code>_ok member with no row in verdicts().  fn's "
              "tier-5 message reads those rows, so the rule would refuse a binding without naming itself, "
              "and its negative fixture would have nothing to floor on.");

static_assert(live_rules<>::verdicts().size() == detail::implemented_rule_count_(),
              "fixy/Collision.h: verdicts() holds a different number of rows than live_rules has <code>_ok "
              "members.  A row without a member, or a member without a row.");

static_assert(live_rules<>::failing_codes().empty(),
              "fixy/Collision.h: the empty pack sits at every strict pole and must trip no rule, so the "
              "code list it produces is empty.  A non-empty answer here means a rule fires on a binding "
              "that claims nothing.");

// ---------------------------------------------------------------------
// The shape fn asks.

template <class F>
struct CollisionRules {
    static constexpr bool valid = true;
};

template <class Type, class... Atoms>
struct CollisionRules<::fixy::fn<Type, Atoms...>> : rules_of<Type, Atoms...> {};

}  // namespace collision

namespace detail::collision_self_test {

using ::fixy::collision::grades;
using ::fixy::collision::live_rules;

// The empty pack trips nothing: every axis sits at its strict pole.
static_assert(live_rules<>::valid);
static_assert(live_rules<>::validate());

// grades answers the same question fn::grade_on does, without fn.
static_assert(std::is_same_v<grades<::fixy::atom::copy>::on<Axis::Usage>, ::fixy::atom::copy>);
static_assert(std::is_same_v<grades<>::on<Axis::Usage>, typename axis_traits<Axis::Usage>::strict>);
static_assert(grades<::fixy::atom::copy>::mentions<Axis::Usage>);
static_assert(!grades<>::mentions<Axis::Usage>);

// Each live rule refuses its pair and admits the halves.  Going through
// grades rather than fn is deliberate: fn would refuse to compile, and
// a rule that is only observable as a build failure cannot be tested.
static_assert(!live_rules<::fixy::atom::borrow, ::fixy::atom::coroutine>::L002_ok);
static_assert(live_rules<::fixy::atom::borrow>::L002_ok);
static_assert(live_rules<::fixy::atom::coroutine>::L002_ok);

static_assert(!live_rules<::fixy::atom::ghost, ::fixy::atom::with<::foundation::effects::Effect::Alloc>>::P010_ok);
static_assert(live_rules<::fixy::atom::ghost>::P010_ok);

static_assert(!live_rules<::fixy::atom::capability_usage, ::fixy::atom::trust_unverified>::T001_ok);
static_assert(live_rules<::fixy::atom::capability_usage>::T001_ok);
static_assert(live_rules<::fixy::atom::trust_unverified>::T001_ok);

static_assert(!live_rules<::fixy::atom::coroutine, ::fixy::atom::borrow>::R002_ok);
static_assert(!live_rules<::fixy::atom::borrow, ::fixy::atom::with<::foundation::effects::Effect::Bg>>::L007_ok);

// M012 needs all three premises, so the atomic representation is what
// rescues it.  That is the rule's whole content.
static_assert(!live_rules<::fixy::atom::mut_monotonic, ::fixy::atom::coroutine>::M012_ok);
static_assert(live_rules<::fixy::atom::mut_monotonic, ::fixy::atom::coroutine,
                         ::fixy::atom::repr<::fixy::pole::ReprKind::Atomic>>::M012_ok);

// P002 reaches the two emitting axes P010 does not read, so the pair it
// refuses is one P010 admits.  Both halves alone are fine.
static_assert(!live_rules<::fixy::atom::ghost, ::fixy::atom::stdio::write<::fixy::atom::stdio::streams::Stdout>>::P002_ok);
static_assert(live_rules<::fixy::atom::ghost, ::fixy::atom::stdio::write<::fixy::atom::stdio::streams::Stdout>>::P010_ok,
              "P002 must be the rule that catches this pair; if P010 already did, P002 would be redundant");
static_assert(live_rules<::fixy::atom::stdio::write<::fixy::atom::stdio::streams::Stdout>>::P002_ok);
static_assert(live_rules<::fixy::atom::ghost>::P002_ok);

// The constant-time family.  constant_time alone trips nothing, and each
// rule needs its second premise.
namespace ct_cells {
namespace at = ::fixy::atom;
namespace fp = ::fixy::atom::fp;
using CT = at::constant_time;
using FlushBoth = fp::mode<fp::FpDenormalInput::DenormalsAreZero, fp::FpFtz::FlushToZero>;

static_assert(live_rules<CT>::valid);
static_assert(live_rules<CT, FlushBoth>::valid, "a constant-time mode that names both flushes is legal");

static_assert(!live_rules<CT, fp::mode<fp::FpReassociate::UnrestrictedRewrite, fp::FpDenormalInput::DenormalsAreZero,
                                       fp::FpFtz::FlushToZero>>::F103_ok);
static_assert(live_rules<CT, fp::mode<fp::FpReassociate::BoundedTreeDepth, fp::FpDenormalInput::DenormalsAreZero,
                                      fp::FpFtz::FlushToZero>>::valid,
              "a tree of bounded depth keeps its topology apart from the data, so F103 admits it");
static_assert(live_rules<fp::mode<fp::FpReassociate::UnrestrictedRewrite>>::F103_ok, "no timing claim, no F103");

static_assert(!live_rules<CT, fp::mode<fp::FpFtz::FlushToZero>>::F104_ok, "an unnamed setting honours denormals");
static_assert(!live_rules<CT, fp::mode<fp::FpDenormalInput::HonorDenormals, fp::FpFtz::FlushToZero>>::F104_ok);
static_assert(live_rules<CT, fp::mode<fp::FpDenormalInput::DenormalsAreZero, fp::FpFtz::FlushToZero>>::F104_ok);
static_assert(!live_rules<CT, fp::mode<fp::FpDenormalInput::DenormalsAreZero>>::F105_ok);
static_assert(!live_rules<CT, fp::mode<fp::FpFtz::PreserveSubnormals, fp::FpDenormalInput::DenormalsAreZero>>::F105_ok);
static_assert(live_rules<CT>::F104_ok && live_rules<CT>::F105_ok, "no FP mode stated, no FP claim to refuse");
static_assert(live_rules<fp::mode<>>::F104_ok && live_rules<fp::mode<>>::F105_ok, "no timing claim");

static_assert(!live_rules<CT, at::coroutine>::E044_ok);
static_assert(!live_rules<CT, at::ctrl::coroutine<at::ctrl::async_task>>::E044_ok);
static_assert(!live_rules<CT, at::with<::foundation::effects::Effect::Bg>>::E044_ok);
static_assert(live_rules<CT, at::with<>>::E044_ok);

static_assert(!live_rules<CT, at::stale_to<1>>::S010_ok);
static_assert(live_rules<at::stale_to<1>>::S010_ok);

struct session_witness final {};
using Session = at::protocol<session_witness>;
static_assert(!live_rules<at::coroutine, Session>::I004_ok, "the strict Security pole is classified");
static_assert(!live_rules<at::as_secret, at::ctrl::coroutine<at::ctrl::async_task>, Session>::I004_ok);
static_assert(live_rules<CT, at::coroutine, Session>::I004_ok, "I004 stands down for constant time; E044 fires");
static_assert(!live_rules<CT, at::coroutine, Session>::E044_ok);
static_assert(live_rules<at::as_public, at::coroutine, Session>::valid);
static_assert(live_rules<at::as_internal, at::coroutine, Session>::valid, "internal is below the carrier");
static_assert(live_rules<at::declassify<::fixy::tags::secret_policy::WireSerialize>, at::coroutine, Session>::valid);
static_assert(live_rules<at::coroutine, at::protocol<::fixy::pole::proto::None>>::valid, "no session");
static_assert(live_rules<Session>::valid, "a synchronous session sends at no time the scheduler chooses");

// The failure family.  The payload half stands down under void, so these
// cells name the payload.
struct plain_error final {};
using SecretError = ::fixy::Secret<plain_error>;
using ::fixy::collision::rules_of;

static_assert(!rules_of<std::expected<int, plain_error>>::I002_ok, "the strict Security pole is classified");
static_assert(!rules_of<int, at::ctrl::throws<>>::I002_ok, "an exception family the reader cannot see is plain");
static_assert(!rules_of<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, std::expected<int, plain_error>>>::I002_ok,
              "a band does not hide the failure it grades");
static_assert(rules_of<std::expected<int, SecretError>>::valid);
static_assert(rules_of<int, at::ctrl::throws<SecretError>>::valid);
static_assert(rules_of<std::expected<int, plain_error>, at::as_public>::valid);
static_assert(rules_of<std::expected<int, plain_error>, at::as_internal>::valid);
static_assert(rules_of<::fixy::Secret<std::expected<int, plain_error>>>::valid,
              "the error inside a Secret is secret, and the discriminant with it");
static_assert(live_rules<at::ctrl::throws<SecretError>>::valid);

static_assert(!rules_of<std::expected<int, SecretError>, CT>::I003_ok);
static_assert(rules_of<std::expected<int, SecretError>, CT>::I002_ok, "a Secret error isolates I003");
static_assert(!rules_of<int, CT, at::ctrl::throws<SecretError>>::I003_ok);
static_assert(rules_of<int, CT>::valid);
}  // namespace ct_cells

}  // namespace detail::collision_self_test

}  // namespace fixy
