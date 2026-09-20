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
// Old spelling: include/crucible/safety/CollisionCatalog.h.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/atoms/Ctrl.h>
#include <fixy/atoms/Dispatch.h>
#include <fixy/atoms/Global.h>
#include <fixy/atoms/Os.h>
#include <fixy/atoms/Stack.h>
#include <fixy/atoms/Stdio.h>
#include <foundation/Platform.h>
#include <foundation/diag/Catalog.h>
#include <foundation/effects/Row.h>

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
                                       ::fixy::atom::detail::global_atom_roster, ::fixy::atom::detail::os_atom_roster,
                                       ::fixy::atom::detail::stack_atom_roster,
                                       ::fixy::atom::detail::stdio_atom_roster>;

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

// The eight axes no shipped atom reaches.  A rule reading one of them
// is registered as pending below.  Axis::Type is not here: it is
// caller-supplied by design, being the payload itself.
inline constexpr Axis pending_axes[] = {
    Axis::Observability, Axis::Synchronization, Axis::Regime,  Axis::FpMode,
    Axis::HwInstruction, Axis::BarrierStrength, Axis::SimdIsa, Axis::MemoryScope,
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
    {RuleCode::B001, Axis::Observability,
     "Bg observable surface with unbounded resource: declare space::Bounded<N> + cost::Linear<N>; a Bg-observable "
     "surface that may run unbounded is a back-pressure trap."},
    {RuleCode::H001, Axis::Regime,
     "HotPath x cost::Unstated/Unbounded: declare cost::Constant or cost::Linear<N>; the hot path must justify its "
     "compute envelope."},
    {RuleCode::H002, Axis::Regime,
     "HotPath x pred::True refinement (no witness floor): attach a Refined<predicate, Type> input that proves an "
     "invariant the hot body assumes; pred::True is review-rejected on hot paths."},
    {RuleCode::H003, Axis::Regime,
     "HotPath x (Alloc or IO) x unbounded cost: move Alloc/IO outside the hot path, or attach an Init/Bg context "
     "that owns the unbounded surface."},
    {RuleCode::H010, Axis::Regime,
     "HotPath x Row<Bg>: a function cannot be both on the hot path (<=40 ns intra-socket, CLAUDE.md SIX) and in "
     "background context. H001 and H003 both miss a HotPath x Bg x cost::Constant binding, which is still a context "
     "contradiction."},
    {RuleCode::R001, Axis::Regime,
     "Coroutine x HotPath: a coroutine frame costs an indirect call plus a state spill at every suspension point, "
     "and one resume breaches the hot-path budget."},
    {RuleCode::S001, Axis::Regime,
     "Stdio x HotPath: buffered stdio takes a lock and may block; neither belongs on a path budgeted in "
     "nanoseconds."},
    {RuleCode::W001, Axis::Regime,
     "HotPath x kernel wait: a park or a futex wait costs 1-5 us against a 40 ns budget."},
    {RuleCode::W002, Axis::Synchronization,
     "Bg row x active spin: a background-context body that spins burns a core that the scheduler could have given "
     "to foreground work."},
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

    // The twenty-two waiting on an atomless axis.  pending_rules above
    // carries the theorem and names the axis for each.
    {"B001", Disposition::Pending, "Axis::Observability"},
    {"H001", Disposition::Pending, "Axis::Regime"},
    {"H002", Disposition::Pending, "Axis::Regime"},
    {"H003", Disposition::Pending, "Axis::Regime"},
    {"H010", Disposition::Pending, "Axis::Regime"},
    {"R001", Disposition::Pending, "Axis::Regime"},
    {"S001", Disposition::Pending, "Axis::Regime"},
    {"W001", Disposition::Pending, "Axis::Regime"},
    {"W002", Disposition::Pending, "Axis::Synchronization"},
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

static_assert(rule_corpus_size == 54,
              "fixy/Collision.h: the rule corpus must account for all 54 codes in the RuleCode enum of "
              "include/crucible/safety/CollisionCatalog.h.  A code dropped from this list stops being "
              "reported as absent, which is the failure this list exists to prevent.");

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

template <class... Atoms>
struct live_rules {
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

    // P010 reads the effect row.  Two other axes also force emitted
    // code, and a ghost binding that engages either is the same
    // contradiction through a different door.
    static constexpr bool emits_outside_the_row = G::template mentions<Axis::Stdio>
                                               || G::template mentions<Axis::SyscallSurface>;
    static constexpr bool P002_ok = !(ghost && emits_outside_the_row);

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
        return L002_ok && M012_ok && P010_ok && L007_ok && T001_ok && R002_ok && R003_ok && L006_ok && G002_ok
            && D002_ok && P002_ok;
    }

    static constexpr bool valid = L002_ok && M012_ok && P010_ok && L007_ok && T001_ok && R002_ok && R003_ok && L006_ok
                               && G002_ok && D002_ok && P002_ok;
};

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
                      std::meta::members_of(^^live_rules<>, std::meta::access_context::unchecked()))) {
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
                          std::meta::members_of(^^live_rules<>, std::meta::access_context::unchecked()))) {
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

// ---------------------------------------------------------------------
// The shape fn asks.

template <class F>
struct CollisionRules {
    static constexpr bool valid = true;
};

template <class Type, class... Atoms>
struct CollisionRules<::fixy::fn<Type, Atoms...>> : live_rules<Atoms...> {};

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
