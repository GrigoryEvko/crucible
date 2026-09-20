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
// Four rules pair a grade with the payload's replay claim, which is the
// DetSafe band the payload carries and nothing in the pack.  So the
// rules take the payload as a template parameter of their own.  That is
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
#include <fixy/atoms/Ctrl.h>
#include <fixy/atoms/Dispatch.h>
#include <fixy/atoms/Global.h>
#include <fixy/atoms/Observe.h>
#include <fixy/atoms/Os.h>
#include <fixy/atoms/Regime.h>
#include <fixy/atoms/Stack.h>
#include <fixy/atoms/Stdio.h>
#include <fixy/atoms/Sync.h>
#include <foundation/Platform.h>
#include <foundation/diag/Catalog.h>
#include <foundation/effects/Row.h>

#include <array>
#include <cstddef>
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
    ::fixy::atom::detail::roster_cat_t<::fixy::atom::detail::core_atom_roster, ::fixy::atom::detail::ctrl_atom_roster,
                                       ::fixy::atom::detail::dispatch_atom_roster,
                                       ::fixy::atom::detail::global_atom_roster,
                                       ::fixy::atom::detail::observe_atom_roster,
                                       ::fixy::atom::detail::os_atom_roster,
                                       ::fixy::atom::detail::regime_atom_roster,
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
// Task #176 is draining this list, one axis per commit, because the
// user's decision was to ship an atom for every axis rather than delete
// any.  fixy/atoms/Regime.h left it first, taking the H, R and S
// families live; fixy/atoms/Sync.h left it second, taking W001 and W002.
inline constexpr Axis pending_axes[] = {
    Axis::FpMode, Axis::HwInstruction, Axis::BarrierStrength, Axis::SimdIsa, Axis::MemoryScope,
};

inline constexpr std::size_t pending_axis_count = sizeof(pending_axes) / sizeof(pending_axes[0]);

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
    F103,
    F104,
    F105,
    V101,
    V102,
    V201,
    V202,
    V203,
    V301,
    V401,
    V402,
};

// ---------------------------------------------------------------------
// The pending roster.  Each entry keeps the theorem and the citation,
// and names the axis that would make it live.

struct pending_rule {
    RuleCode code{};
    Axis waits_on{};
    std::string_view theorem{};
};

inline constexpr pending_rule pending_rules[] = {
    // H001, H002, H003, H010, R001 and S001 left this list when
    // fixy/atoms/Regime.h shipped, W001 and W002 when fixy/atoms/Sync.h
    // did, and B001 with the new B002 when fixy/atoms/Observe.h did.  Each
    // is a live_rules member below and a Live row in rule_corpus.  W001
    // needed two of those commits: it reads a tier AND a wait strategy, so
    // the axis it waited on moved from Regime to Synchronization before it
    // could fire.
    {RuleCode::F101, Axis::FpMode,
     "Replay x FP reassociation permitted: reassociation reorders the sum, so a replayed run produces different "
     "bits and DetSafe fails."},
    {RuleCode::F102, Axis::FpMode,
     "Replay x FP contract fast: cross-statement contraction changes the rounding sequence between builds."},
    {RuleCode::F103, Axis::FpMode,
     "ConstantTime x unrestricted FP reassociation: the rewrite is data-dependent, so the timing is too."},
    {RuleCode::F104, Axis::FpMode,
     "ConstantTime x denormal inputs honored: denormal arithmetic is slower on most silicon, which leaks the "
     "operand through timing."},
    {RuleCode::F105, Axis::FpMode, "ConstantTime x subnormals preserved: same leak as F104 on the result side."},
    {RuleCode::V101, Axis::SimdIsa,
     "Replay x a pinned SIMD ISA: a vector width chosen per host makes the reduction order host-dependent."},
    {RuleCode::V102, Axis::SimdIsa, "SIMD width exceeds the pinned ISA: the emitted vector does not fit the trunk."},
    {RuleCode::V201, Axis::HwInstruction,
     "HotPath x non-deterministic TSC: a serialising timestamp read is both slow and non-deterministic."},
    {RuleCode::V202, Axis::HwInstruction,
     "Privileged MSR access without an Init context: MSR access belongs to startup, where it can be ordered and "
     "audited."},
    {RuleCode::V203, Axis::HwInstruction,
     "Replay x non-deterministic TSC: the timestamp differs per run, so replay "
     "cannot be bit-exact."},
    {RuleCode::V301, Axis::BarrierStrength,
     "HotPath x a full fence: a seq_cst fence drains the store buffer and costs ~30 ns on x86."},
    {RuleCode::V401, Axis::BarrierStrength,
     "Memory scope at or above cluster with a barrier below acq_rel: the publication reaches further than the fence "
     "orders."},
    {RuleCode::V402, Axis::MemoryScope,
     "Memory scope across an architecture trunk: two vendor trunks meet only at the shared bottom and top, so a "
     "scope pinned on one does not compose with the other."},
};

inline constexpr std::size_t pending_rule_count = sizeof(pending_rules) / sizeof(pending_rules[0]);

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

    // The regime family, live since fixy/atoms/Regime.h shipped the
    // three HotPathTier atoms (task #176).
    {"H001", Disposition::Live, "hot x unstated or unbounded cost"},
    {"H002", Disposition::Live, "hot x no refinement witness"},
    {"H003", Disposition::Live, "hot x an Alloc or IO row x unbounded cost"},
    {"H010", Disposition::Live, "hot x Row<Bg>"},
    {"R001", Disposition::Live, "coroutine x hot"},
    {"S001", Disposition::Live, "stdio x hot"},

    // The wait family, live since fixy/atoms/Sync.h shipped the six
    // WaitStrategy atoms (task #176).  W001 reads a tier and a wait, so
    // it needed both that commit and the Regime one.
    {"W001", Disposition::Live, "hot x a kernel wait"},
    {"W002", Disposition::Live, "Row<Bg> x a spin that burns the core"},

    // The observability family, live since fixy/atoms/Observe.h shipped
    // the surface atom (task #176).  Two theorems, not one: B002 is the
    // containment of the observability row in the effect row, and B001 is
    // the back-pressure trap this catalog already recorded.  B002 is a new
    // code rather than a rereading of B001, because the codes are stable
    // API and the negative corpus greps them.
    {"B001", Disposition::Live, "Row<Bg> x an observable surface x an unbounded resource"},
    {"B002", Disposition::Live, "an observability row outside the binding's effect row"},

    // The rest wait on an axis with no atom.  pending_rules above carries
    // the theorem and names the axis for each.
    {"F101", Disposition::Pending, "Axis::FpMode"},
    {"F102", Disposition::Pending, "Axis::FpMode"},
    {"F103", Disposition::Pending, "Axis::FpMode"},
    {"F104", Disposition::Pending, "Axis::FpMode"},
    {"F105", Disposition::Pending, "Axis::FpMode"},
    {"V101", Disposition::Pending, "Axis::SimdIsa"},
    {"V102", Disposition::Pending, "Axis::SimdIsa"},
    {"V201", Disposition::Pending, "Axis::HwInstruction"},
    {"V202", Disposition::Pending, "Axis::HwInstruction"},
    {"V203", Disposition::Pending, "Axis::HwInstruction"},
    {"V301", Disposition::Pending, "Axis::BarrierStrength"},
    {"V401", Disposition::Pending, "Axis::BarrierStrength"},
    {"V402", Disposition::Pending, "Axis::MemoryScope"},

    // The twenty-one this layer cannot state.  Each note names the thing
    // that is missing, so the entry is a claim someone can check rather
    // than a gap someone has to notice.
    //
    // Four of them read a grade on the payload type.  live_rules receives
    // the atom pack and not the payload: CollisionRules<fn<Type, Atoms...>>
    // passes Atoms... alone, and Axis::Type is excluded from grades by
    // design.  Wiring the payload in is a change to the shape of this
    // file, not a rule.
    {"C001", Disposition::Absent, "reads a ControlFlow tier on the payload; fixy/Bands.h ships no ControlFlowPinned"},
    {"S011", Disposition::Absent, "reads a replay requirement, which rides on the payload as a DetSafe band"},
    {"E044", Disposition::Absent, "reads a constant-time grade; fixy ships ct:: free functions, not a band or an axis"},
    {"S010", Disposition::Absent, "reads a constant-time grade, as E044 does"},

    // Five read an axis the 33 do not contain.
    {"I002", Disposition::Absent, "reads an error-payload grade; no axis carries failure"},
    {"I003", Disposition::Absent, "reads a constant-time grade and an error grade; neither exists"},
    {"I004", Disposition::Absent, "reads a constant-time grade on a session send"},
    {"M011", Disposition::Absent, "reads a failure path; no axis carries failure"},
    {"F002", Disposition::Absent, "reads a termination budget; no axis carries one"},

    // Five read an atom nobody has written.
    {"D001", Disposition::Absent, "reads the callable family's signature; indirect_call<F> takes F as an opaque class"},
    {"L003", Disposition::Absent, "separates a scoped from an unscoped spawn; no atom names a spawn"},
    {"L004", Disposition::Absent, "reads whether the binding carries a Permission proof; no atom names one"},
    {"P003", Disposition::Absent, "reads a fork-worker marker; no atom names one"},
    {"N002", Disposition::Absent, "reads an exact-decimal kind; Axis::Precision carries f32, f64 and higham only"},

    // Two are already covered by the atomless-axis list: their axis has
    // no atom AND the rule compares two bindings, so neither half is
    // available.  They are recorded here rather than in pending_rules
    // because an atom on Axis::SimdIsa would still not make them fire.
    {"V001", Disposition::Absent, "compares two vendor intrinsics in one pack; Axis::SimdIsa also has no atom"},
    {"V002", Disposition::Absent, "compares two architecture trunks in one pack; Axis::SimdIsa also has no atom"},

    // Four hold across several bindings.  live_rules is handed one
    // binding's pack, so a corpus-level relation belongs to A11.3.
    {"F001", Disposition::Absent, "a frame-level agreement across several bindings"},
    {"L005", Disposition::Absent, "compares two linear bindings that share a region tag"},
    {"S004", Disposition::Absent, "walks the init-dependency graph across every registered singleton"},

    // One is discharged by construction, and one never had a theorem.
    {"G001", Disposition::Absent,
     "discharged: atom::global::thread_local_<StaticTag> requires the tag, so the untagged form this rule refused "
     "cannot be named"},
    {"M001", Disposition::Absent,
     "the old catalog declares M001_DontNeedRequiresReleaseAware and ships no CRUCIBLE_COLLISION_DIAGNOSTIC for it, "
     "so the code has a name and no theorem to port"},
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
        if (entry.code == code) return entry.disposition != Disposition::Absent;
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

static_assert(every_pending_axis_is_still_empty(),
              "fixy/Collision.h: the pending-rule roster is out of date.  Either an axis listed in "
              "collision::pending_axes has gained its first atom — in which case the rules registered against it in "
              "collision::pending_rules can now fire and must be written as live rules, and the axis removed from "
              "pending_axes — or a family roster was added under fixy/atoms/ and never joined into "
              "collision::all_atom_roster, which makes its axes look empty.");

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
// of an inherited code.  Task #176 gave Axis::Observability its atoms and
// found two theorems where the old catalog recorded one: B001's
// back-pressure trap, which it kept, and the containment of the
// observability row in the effect row, which had no code.  Reusing B001
// for the second would have discarded a theorem that has its own remedy,
// and the codes are stable API precisely so that cannot happen quietly.
static_assert(rule_corpus_size == 55,
              "fixy/Collision.h: the rule corpus must account for the 54 codes inherited from the RuleCode "
              "enum of include/crucible/safety/CollisionCatalog.h, plus B002, which is written here.  A code "
              "dropped from this list stops being reported as absent.");

// Every code the corpus says ships has an enumerator, and every code it
// says is absent has none.  A rule written without a corpus entry, or
// recorded as absent after being written, reddens here.
//
// Both checks answer with the offending code rather than with a bool, so
// the diagnostic names the rule instead of asking the reader to diff two
// lists of fifty-four.

namespace detail {

[[nodiscard]] consteval std::string_view code_the_enum_disagrees_about_() noexcept {
    for (const corpus_entry& entry : rule_corpus) {
        const bool shipped = entry.disposition != Disposition::Absent;
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

// pending_rules and the corpus are two hand-written lists of the same
// set.  Comparing them catches an edit to one and not the other.
static_assert(pending_rule_count == detail::corpus_count_(Disposition::Pending),
              "fixy/Collision.h: pending_rules and the corpus disagree about how many rules are waiting on an "
              "atomless axis.");

// There is deliberately no `pending_axis_count == 8` pin here.  It would
// be derived from the array it counts, and the biconditional above
// already fixes the SET by name against the atom catalog: an axis dropped
// from pending_axes while still atomless fails it, and an axis added while
// it has an atom fails it too.  The count could therefore never fail on
// its own, and a pin that cannot fail reads as a second check.

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

template <class G>
struct row_admits_observable_ : std::false_type {};
template <::foundation::effects::Effect... Es>
struct row_admits_observable_<::fixy::atom::with<Es...>>
    : std::bool_constant<((Es == ::foundation::effects::Effect::Alloc || Es == ::foundation::effects::Effect::IO
                           || Es == ::foundation::effects::Effect::Block)
                          || ...)> {};

// H003's theorem names Alloc and IO specifically, so it gets its own
// predicate rather than reusing row_admits_observable_ above, which also
// admits Block.  A blocking hot path is just as wrong, but it is W001's
// theorem and W001 cites the futex cost, not the allocator's.
// The payload's replay claim.
//
// A payload carrying a DetSafe band at PhiloxRng or Pure claims that its
// bytes are replay-deterministic: the same inputs give the same bits on
// any host.  That claim is what F101, F102, V101 and V203 pair with a
// grade that falsifies it — an FP mode that reorders a sum, an ISA pin
// that makes the reduction order host-dependent, a timestamp read.
//
// The old catalog carried this premise as marks_replay_required, a
// marker trait with a false_type primary that nothing ever specialised,
// so none of those four rules fired structurally in the old tree either.
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

// The Effect row and the Observability row, each extracted from its
// grade.
//
// Effect's strict pole IS a Row already (axis_traits<Axis::Effect>::strict
// is Row<>), so the pole and the atom both need an arm and the pole's is
// the identity.  Observability's pole is derived from Effect's, so its
// pole is that same empty row: a binding that names no surface observes
// nothing.
//
// Neither primary fails open.  Both answer Row<> for a grade they do not
// recognise, and Row<> is the strongest answer on the Effect side (a
// binding that may do nothing) and the weakest on the Observability side
// (a binding that observes nothing).  B002 compares them in the direction
// that makes both of those safe: an unrecognised observability grade
// claims to observe nothing and passes, and an unrecognised effect grade
// permits nothing and refuses any surface.
template <class G>
struct effect_row_of_ {
    using type = ::foundation::effects::Row<>;
};
template <::foundation::effects::Effect... Es>
struct effect_row_of_<::fixy::atom::with<Es...>> {
    using type = ::foundation::effects::Row<Es...>;
};
template <::foundation::effects::Effect... Es>
struct effect_row_of_<::foundation::effects::Row<Es...>> {
    using type = ::foundation::effects::Row<Es...>;
};

template <class G>
struct observability_row_of_ {
    using type = ::foundation::effects::Row<>;
};
template <::foundation::effects::Effect... Es>
struct observability_row_of_<::fixy::atom::observe::surface<Es...>> {
    using type = ::foundation::effects::Row<Es...>;
};

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

}  // namespace detail

// ---------------------------------------------------------------------
// The live rules.
//
// Each reads grades<Atoms...> and nothing else.  The message is the
// theorem, carried from the old catalog with its citation, because the
// message is what a reader gets.

// The rules over one binding: its payload and its pack.
//
// Most rules read the pack alone, through grades<Atoms...>.  Four read
// the PAYLOAD as well — F101, F102, V101 and V203 each pair a grade with
// a replay claim, and the replay claim is the DetSafe band the payload
// carries, not anything in the pack.  So the struct takes the payload as
// its first parameter, and `live_rules<Atoms...>` below is the pack-only
// view with the payload set to void, under which every payload rule
// stands down.  The two names are one struct: there is one verdicts()
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
    // and a Bg row runs elsewhere; either makes the body concurrent with
    // respect to the caller's frame.
    static constexpr bool concurrent = coroutine || row_bg;

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
    using effect_row = typename detail::effect_row_of_<typename G::template on<Axis::Effect>>::type;

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

    // P010 reads the effect row.  Two other axes also force emitted
    // code, and a ghost binding that engages either is the same
    // contradiction through a different door.
    static constexpr bool emits_outside_the_row = G::template mentions<Axis::Stdio>
                                               || G::template mentions<Axis::SyscallSurface>;
    static constexpr bool P002_ok = !(ghost && emits_outside_the_row);

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

}  // namespace detail::collision_self_test

}  // namespace fixy
