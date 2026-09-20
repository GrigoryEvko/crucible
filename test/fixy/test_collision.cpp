// Sentinel TU for fixy/Collision.h: every live rule admits the empty
// pack, refuses its own pair, and admits each half of that pair; the
// pending roster is exactly the axes no atom reaches; and the rules
// reach fn's gate.
//
// The colliding packs are exercised through collision::live_rules and
// collision::grades rather than through fn.  Naming fn with a colliding
// pack is a build failure by design, so a rule that could only be
// observed that way could not be tested at all — only the whole TU
// failing would show it, which says nothing about which rule fired.

#include <fixy/Bands.h>
#include <fixy/Collision.h>
#include <fixy/Fn.h>

#include <foundation/effects/Effect.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>

namespace {

namespace col = ::fixy::collision;
namespace at = ::fixy::atom;
using ::fixy::Axis;
using ::fixy::axis_traits;
using col::grades;
using col::live_rules;
using col::rules_of;
using Eff = ::foundation::effects::Effect;
namespace fe = ::foundation::effects;

// The test brings its own tag: the sample tag the family roster uses
// lives in a detail namespace and is not the test's to name.
struct tls_tag final {};

// H002 asks a hot binding to carry a refinement witness, and
// atom::refined_with is parametric on the predicate that was proved.  The
// test brings its own for the same reason as tls_tag: which predicate a
// hot body needs is the caller's business, and H002 only asks that there
// IS one.
struct hot_invariant final {};

// ---------------------------------------------------------------------
// grades answers without fn.

static_assert(std::is_same_v<grades<>::on<Axis::Usage>, typename axis_traits<Axis::Usage>::strict>);
static_assert(std::is_same_v<grades<at::copy>::on<Axis::Usage>, at::copy>);
static_assert(std::is_same_v<grades<at::copy>::on<Axis::Effect>, typename axis_traits<Axis::Effect>::strict>);
static_assert(grades<at::copy>::mentions<Axis::Usage>);
static_assert(!grades<at::copy>::mentions<Axis::Effect>);

// The same answer fn gives, which is the point of keeping them separate
// rather than the point of keeping them different.
static_assert(std::is_same_v<grades<at::copy>::on<Axis::Usage>, ::fixy::fn<int, at::copy>::grade_on<Axis::Usage>>);
static_assert(std::is_same_v<grades<>::on<Axis::Mutation>, ::fixy::fn<int>::grade_on<Axis::Mutation>>);

// ---------------------------------------------------------------------
// Every live rule admits the strictest binding.

static_assert(live_rules<>::valid);
static_assert(live_rules<>::validate());
static_assert(live_rules<>::L002_ok && live_rules<>::M012_ok && live_rules<>::P010_ok && live_rules<>::L007_ok
              && live_rules<>::T001_ok && live_rules<>::R002_ok && live_rules<>::R003_ok && live_rules<>::L006_ok
              && live_rules<>::G002_ok && live_rules<>::D002_ok && live_rules<>::P002_ok);

// ---------------------------------------------------------------------
// Each rule refuses its pair and admits each half.  Both directions per
// rule, because a rule that only ever says yes proves nothing.

// L002 borrow x async
static_assert(!live_rules<at::borrow, at::coroutine>::L002_ok);
static_assert(!live_rules<at::borrow, at::with<Eff::Bg>>::L002_ok);
static_assert(live_rules<at::borrow>::L002_ok);
static_assert(live_rules<at::coroutine>::L002_ok);

// M012 monotonic x concurrent without an atomic representation
static_assert(!live_rules<at::mut_monotonic, at::coroutine>::M012_ok);
static_assert(live_rules<at::mut_monotonic>::M012_ok);
static_assert(live_rules<at::mut_monotonic, at::coroutine, at::repr<::fixy::pole::ReprKind::Atomic>>::M012_ok);
static_assert(!live_rules<at::mut_monotonic, at::coroutine, at::repr<::fixy::pole::ReprKind::C>>::M012_ok);

// P010 ghost x an effect that must emit code
static_assert(!live_rules<at::ghost, at::with<Eff::Alloc>>::P010_ok);
static_assert(!live_rules<at::ghost, at::with<Eff::IO>>::P010_ok);
static_assert(!live_rules<at::ghost, at::with<Eff::Block>>::P010_ok);
static_assert(live_rules<at::ghost>::P010_ok);
static_assert(live_rules<at::ghost, at::with<Eff::Test>>::P010_ok, "Test is not an emitted-code effect");

// L007 borrow x Bg row
static_assert(!live_rules<at::borrow, at::with<Eff::Bg>>::L007_ok);
static_assert(live_rules<at::borrow>::L007_ok);
static_assert(live_rules<at::with<Eff::Bg>>::L007_ok);

// T001 capability x unverified provenance
static_assert(!live_rules<at::capability_usage, at::trust_unverified>::T001_ok);
static_assert(live_rules<at::capability_usage>::T001_ok);
static_assert(live_rules<at::trust_unverified>::T001_ok);
static_assert(live_rules<at::capability_usage, at::trust_verified>::T001_ok);

// R002 / R003 coroutine x borrow, coroutine x Bg row
static_assert(!live_rules<at::coroutine, at::borrow>::R002_ok);
static_assert(live_rules<at::coroutine>::R002_ok);
static_assert(!live_rules<at::coroutine, at::with<Eff::Bg>>::R003_ok);
static_assert(live_rules<at::coroutine>::R003_ok);

// G002 thread-local x atomic representation
static_assert(!live_rules<at::global::thread_local_<tls_tag>, at::repr<::fixy::pole::ReprKind::Atomic>>::G002_ok);
static_assert(live_rules<at::global::thread_local_<tls_tag>>::G002_ok);
static_assert(live_rules<at::repr<::fixy::pole::ReprKind::Atomic>>::G002_ok);

// D002 unbounded recursion x unbounded cost
static_assert(!live_rules<at::dispatch::recurses<0>, at::cost_unbounded>::D002_ok);
static_assert(live_rules<at::dispatch::recurses<0>>::D002_ok);
static_assert(live_rules<at::cost_unbounded>::D002_ok);
static_assert(live_rules<at::dispatch::recurses<0>, at::cost_linear<4>>::D002_ok);

// P002 ghost x an emitting surface the effect row does not name.  The
// pair P010 admits is the pair P002 refuses, which is why both exist.
static_assert(!live_rules<at::ghost, at::stdio::write<at::stdio::streams::Stdout>>::P002_ok);
static_assert(!live_rules<at::ghost, at::stdio::write<at::stdio::streams::Stderr>>::P002_ok);
static_assert(live_rules<at::ghost, at::stdio::write<at::stdio::streams::Stdout>>::P010_ok,
              "a stdio write is not an effect-row effect, so P010 cannot see it");
static_assert(live_rules<at::ghost>::P002_ok);
static_assert(live_rules<at::stdio::write<at::stdio::streams::Stdout>>::P002_ok);
static_assert(live_rules<at::copy, at::stdio::write<at::stdio::streams::Stdout>>::P002_ok,
              "only the ghost grade makes an emitted write a contradiction");

// ---------------------------------------------------------------------
// The regime family, live since fixy/atoms/Regime.h (task #176).
//
// Each cell pair is the rule refusing its pair and admitting each half,
// the same shape as the eleven above.  Two things are worth reading for
// rather than assuming.
//
// First, `hot` alone trips H001 AND H002, because both premises are
// ABSENCES: an unstated cost and a missing refinement witness are the
// strict poles of Complexity and Refinement.  So the "admits the half"
// cell for the other H rules has to satisfy those two first, which is
// what the cost_constant and refined_with atoms in them are doing.  They
// are not decoration; without them the cell would pass for the wrong
// reason.
//
// Second, `warm` and `cold` trip nothing.  Every rule here reads `hot`
// specifically, not "mentions Regime", so declaring a cold path is free.

// H001 hot x an unstated or unbounded cost
static_assert(!live_rules<at::regime::hot>::H001_ok, "hot with no Complexity grade is an unstated envelope");
static_assert(!live_rules<at::regime::hot, at::cost_unbounded>::H001_ok);
static_assert(live_rules<at::regime::hot, at::cost_constant>::H001_ok);
static_assert(live_rules<at::regime::hot, at::cost_linear<8>>::H001_ok);
static_assert(live_rules<at::cost_unbounded>::H001_ok, "an unbounded cost off the hot path is not H001's business");
static_assert(live_rules<at::regime::warm>::H001_ok, "H001 reads hot, not any regime grade");
static_assert(live_rules<at::regime::cold>::H001_ok);

// H002 hot x no refinement witness
static_assert(!live_rules<at::regime::hot>::H002_ok);
static_assert(live_rules<at::regime::hot, at::refined_with<hot_invariant>>::H002_ok);
static_assert(live_rules<at::refined_with<hot_invariant>>::H002_ok);
static_assert(live_rules<at::regime::warm>::H002_ok);

// H003 hot x an Alloc or IO row x unbounded cost.  Three premises, so
// dropping any one admits the binding.
static_assert(!live_rules<at::regime::hot, at::with<Eff::Alloc>, at::cost_unbounded>::H003_ok);
static_assert(!live_rules<at::regime::hot, at::with<Eff::IO>, at::cost_unbounded>::H003_ok);
static_assert(live_rules<at::regime::hot, at::with<Eff::Alloc>, at::cost_constant>::H003_ok);
static_assert(live_rules<at::regime::hot, at::cost_unbounded>::H003_ok, "no Alloc or IO row, so H001 not H003");
static_assert(live_rules<at::with<Eff::Alloc>, at::cost_unbounded>::H003_ok, "not hot, so no contradiction");
// Block is deliberately outside H003: a blocking hot path is W001's
// theorem, which cites the futex cost rather than the allocator's.  The
// cell is here so the boundary is a decision on the record.
static_assert(live_rules<at::regime::hot, at::with<Eff::Block>, at::cost_unbounded>::H003_ok,
              "H003's theorem names Alloc and IO; Block on a hot path belongs to W001");

// H010 hot x Row<Bg>.  The cost_constant and refined_with atoms are what
// make this cell prove H010 rather than H001 or H002 in disguise.
static_assert(!live_rules<at::regime::hot, at::with<Eff::Bg>, at::cost_constant, at::refined_with<hot_invariant>>::H010_ok);
static_assert(live_rules<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>>::H010_ok);
static_assert(live_rules<at::with<Eff::Bg>>::H010_ok);
static_assert(live_rules<at::regime::cold, at::with<Eff::Bg>>::H010_ok, "a cold background body is ordinary");

// R001 coroutine x hot
static_assert(!live_rules<at::coroutine, at::regime::hot>::R001_ok);
static_assert(live_rules<at::coroutine>::R001_ok);
static_assert(live_rules<at::regime::hot>::R001_ok);
static_assert(live_rules<at::coroutine, at::regime::cold>::R001_ok);

// S001 stdio x hot
static_assert(!live_rules<at::stdio::write<at::stdio::streams::Stdout>, at::regime::hot>::S001_ok);
static_assert(live_rules<at::stdio::write<at::stdio::streams::Stdout>>::S001_ok);
static_assert(live_rules<at::regime::hot>::S001_ok);
static_assert(live_rules<at::stdio::write<at::stdio::streams::Stdout>, at::regime::warm>::S001_ok);

// The whole family stands down for a binding that claims no tier, which
// is what keeps the axis's Unconstrained strict pole honest: most of the
// tree is neither hot nor cold in any sense worth typing.
static_assert(live_rules<>::H001_ok && live_rules<>::H002_ok && live_rules<>::H003_ok && live_rules<>::H010_ok
              && live_rules<>::R001_ok && live_rules<>::S001_ok);

// A hot binding that answers every rule is accepted, so the family is a
// gate and not a ban on the hot path.
static_assert(live_rules<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>>::valid);

// ---------------------------------------------------------------------
// The wait family, live since fixy/atoms/Sync.h (task #176).
//
// Both rules read one axis from opposite ends of its ladder, so the
// interesting cells are the ones that show the LINE, not just the ends.

// W001 hot x a kernel wait.  The three lowest grades trip it and the
// three highest do not, which is the WaitLattice's own division.
static_assert(!live_rules<at::regime::hot, at::sync::block, at::cost_constant,
                          at::refined_with<hot_invariant>>::W001_ok);
static_assert(!live_rules<at::regime::hot, at::sync::park, at::cost_constant,
                          at::refined_with<hot_invariant>>::W001_ok);
static_assert(!live_rules<at::regime::hot, at::sync::acquire_wait, at::cost_constant,
                          at::refined_with<hot_invariant>>::W001_ok);
static_assert(live_rules<at::regime::hot, at::sync::umwait_c01, at::cost_constant,
                         at::refined_with<hot_invariant>>::W001_ok);
static_assert(live_rules<at::regime::hot, at::sync::bounded_spin, at::cost_constant,
                         at::refined_with<hot_invariant>>::W001_ok);
static_assert(live_rules<at::regime::hot, at::sync::spin_pause, at::cost_constant,
                         at::refined_with<hot_invariant>>::W001_ok);
// Each half alone is fine: a cold body may block, and a hot body may wait
// however it likes as long as it stays out of the kernel.
static_assert(live_rules<at::sync::block>::W001_ok);
static_assert(live_rules<at::regime::cold, at::sync::block>::W001_ok);
static_assert(live_rules<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>>::W001_ok);

// W002 Row<Bg> x a spin that burns the core.  This is narrower than "not
// a kernel wait" by exactly one grade, and UMWAIT is that grade: it halts
// the core in C0.1 rather than spinning it, so a background body may use
// it.  These three cells are the whole reason fixy/atoms/Sync.h carries
// two predicates rather than one negating the other.
static_assert(!live_rules<at::with<Eff::Bg>, at::sync::spin_pause>::W002_ok);
static_assert(!live_rules<at::with<Eff::Bg>, at::sync::bounded_spin>::W002_ok);
static_assert(live_rules<at::with<Eff::Bg>, at::sync::umwait_c01>::W002_ok,
              "UMWAIT halts the core instead of burning it, so a background body may wait that way");
static_assert(live_rules<at::with<Eff::Bg>, at::sync::park>::W002_ok);
static_assert(live_rules<at::sync::spin_pause>::W002_ok, "a foreground spin is the intended hot-path wait");

// The lift, read through the same atoms.  A kernel wait carries
// Effect::Block into the row a context has to admit; a spin carries
// nothing.  This is what makes the two rules and the context gates agree
// rather than merely coexist.
static_assert(std::is_same_v<fe::lift_row_t<at::sync::block>, fe::Row<Eff::Block>>);
static_assert(std::is_same_v<fe::lift_row_t<at::sync::acquire_wait>, fe::Row<Eff::Block>>);
static_assert(std::is_same_v<fe::lift_row_t<at::sync::spin_pause>, fe::Row<>>);
static_assert(std::is_same_v<fe::lift_row_t<at::sync::umwait_c01>, fe::Row<>>);

// Neither rule fires on a binding that names no strategy, which is what
// keeps the Synchronization strict pole from meaning "spins".
static_assert(live_rules<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>>::W001_ok
              && live_rules<at::with<Eff::Bg>>::W002_ok);

// ---------------------------------------------------------------------
// The observability family, live since fixy/atoms/Observe.h (task #176).
//
// Two theorems on one axis, and the cells below show they are two: each
// pack trips one and not the other.

// B002, the containment.  An observability row must be a Subrow of the
// binding's effect row, because Observability names which PART of that row
// is observation.  It is not a second row and cannot widen the first.
static_assert(!live_rules<at::observe::surface<Eff::IO>>::B002_ok,
              "a surface naming IO on a binding whose effect row is empty claims an effect it never declared");
static_assert(live_rules<at::observe::surface<Eff::IO>, at::with<Eff::IO>>::B002_ok,
              "the same surface is admitted once the binding declares IO");
static_assert(live_rules<at::observe::surface<Eff::IO>, at::with<Eff::IO, Eff::Bg>>::B002_ok,
              "a proper subset is containment too");
static_assert(!live_rules<at::observe::surface<Eff::IO, Eff::Bg>, at::with<Eff::IO>>::B002_ok,
              "one effect of the surface is outside the row, which is enough");
static_assert(live_rules<at::observe::surface<>>::B002_ok, "the empty surface observes nothing and is contained");
static_assert(live_rules<at::with<Eff::IO>>::B002_ok, "a binding with no surface observes nothing");
static_assert(live_rules<>::B002_ok);

// The strict pole on this axis is derived from Effect's, which is the
// empty row, so "observes nothing" is what a binding claims by saying
// nothing.  These two cells are that pole read from both sides.
static_assert(!col::grades<>::mentions<Axis::Observability>);
static_assert(col::grades<at::observe::surface<Eff::IO>>::mentions<Axis::Observability>);

// B001, the back-pressure trap, which is the theorem this catalog already
// recorded for the axis.  It is a different premise from B002's: a
// background observable surface whose resource use is unbounded, where the
// remedy the theorem names is space::Bounded plus cost::Linear.
static_assert(!live_rules<at::with<Eff::Bg>, at::observe::surface<Eff::Bg>>::B001_ok,
              "no cost grade at all is an unstated envelope, which is one of the three unbounded readings");
static_assert(!live_rules<at::with<Eff::Bg>, at::observe::surface<Eff::Bg>, at::cost_unbounded>::B001_ok);
static_assert(!live_rules<at::with<Eff::Bg>, at::observe::surface<Eff::Bg>, at::cost_linear<8>,
                          at::space_unbounded>::B001_ok);
static_assert(live_rules<at::with<Eff::Bg>, at::observe::surface<Eff::Bg>, at::cost_linear<8>,
                         at::space_bounded<4096>>::B001_ok,
              "the remedy the theorem names: a bounded space and a linear cost");
// Each premise alone stands down.
static_assert(live_rules<at::with<Eff::Bg>, at::cost_unbounded>::B001_ok, "no surface, so nothing is observable");
static_assert(live_rules<at::observe::surface<Eff::IO>, at::with<Eff::IO>, at::cost_unbounded>::B001_ok,
              "an unbounded foreground surface is not a back-pressure trap: the caller is the consumer");

// The two theorems are independent, which is why both codes exist. The
// first pack trips B002 and not B001; the second trips B001 and not B002.
static_assert(!live_rules<at::observe::surface<Eff::IO>>::B002_ok
              && live_rules<at::observe::surface<Eff::IO>>::B001_ok);
static_assert(live_rules<at::with<Eff::Bg>, at::observe::surface<Eff::Bg>>::B002_ok
              && !live_rules<at::with<Eff::Bg>, at::observe::surface<Eff::Bg>>::B001_ok);

// A surface atom declares no lift, so it contributes nothing to the row a
// context must admit.  That is the whole point of the containment: the
// Effect grade stays the single authority for what the binding may do.
static_assert(!fe::LiftsToRow<at::observe::surface<Eff::IO>>);

// ---------------------------------------------------------------------
// The payload read (task #176, ahead of the FpMode, SimdIsa and
// HwInstruction atoms that pair with it).
//
// Four rules pair a grade with the payload's replay claim, and the claim
// is the DetSafe band the payload carries.  rules_of<Payload, Atoms...>
// reads it; live_rules<Atoms...> is the pack-only view with void for the
// payload.  These cells are the witness that the read exists and reads
// the right thing, ahead of any rule consuming it: a structural change
// with nothing observable is the shape this migration keeps refusing.
using DetTier = ::fixy::DetSafeTier_v;
template <DetTier Tier>
using det = ::fixy::DetSafe<Tier, int>;

// The two replay-deterministic tiers claim; the five below them do not.
static_assert(rules_of<det<DetTier::Pure>>::replay_deterministic);
static_assert(rules_of<det<DetTier::PhiloxRng>>::replay_deterministic);
static_assert(!rules_of<det<DetTier::MonotonicClockRead>>::replay_deterministic,
              "a monotonic clock is bounded within one run and says nothing across runs");
static_assert(!rules_of<det<DetTier::WallClockRead>>::replay_deterministic);
static_assert(!rules_of<det<DetTier::NonDeterministicSyscall>>::replay_deterministic);

// A payload with no band claims nothing, and so does the pack-only view.
static_assert(!rules_of<int>::replay_deterministic);
static_assert(!rules_of<void>::replay_deterministic);
static_assert(!live_rules<>::replay_deterministic, "live_rules is rules_of<void, ...>");
static_assert(std::is_same_v<live_rules<at::copy>, rules_of<void, at::copy>>);

// The read alone refuses nothing: no live rule consumes it yet, so a
// replay-deterministic payload with an empty pack is accepted.  The
// rules that consume it arrive with their axes, and each names this
// member.
static_assert(rules_of<det<DetTier::Pure>>::valid);

// ---------------------------------------------------------------------
// The pending roster.

// Fourteen, down from twenty-two: fixy/atoms/Regime.h took the six H, R
// and S rules live and fixy/atoms/Sync.h took W001 and W002 (task #176).
// The number moves once per axis this task drains, and it is a literal
// rather than a floor because the three dispositions partition the
// catalog — a floor here would let a rule fall out of all three and go
// unnoticed.
static_assert(col::pending_rule_count == 13);
static_assert(col::every_pending_axis_is_still_empty());

// pending_axes is a hand-written list, so the pin on its length compares
// it against the atom catalog rather than against its own size.  Counting
// the atomless axes independently also catches a duplicate entry, which a
// length of 8 would otherwise hide.
[[nodiscard]] consteval std::size_t axes_without_an_atom() noexcept {
    std::size_t found = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        constexpr Axis axis = [:member:];
        if constexpr (axis != Axis::Type) {
            if constexpr (!col::axis_has_an_atom<axis>) ++found;
        }
    }
#pragma GCC diagnostic pop
    return found;
}
static_assert(col::pending_axis_count == axes_without_an_atom(),
              "pending_axes must list exactly the axes with no atom, once each");

// ---------------------------------------------------------------------
// The corpus accounts for all 54 codes.
//
// The count below is the one number worth stating here, and it is stated
// against the external catalog rather than against the roster: the first
// shape of this file pinned live == roster - pending, which holds for any
// roster and held while 22 rules were missing.  The three dispositions
// are counted separately and must sum to the catalog's size.
//
// That size is 55, not 54: the 54 are inherited from the old catalog's
// RuleCode enum and B002 was written in fixy/Collision.h by task #176,
// which found two theorems on Axis::Observability where the old catalog
// recorded one.  B001 kept its back-pressure theorem rather than being
// reread as the containment rule, because the codes are stable API.

static_assert(col::rule_corpus_size == 55);
static_assert(col::live_rule_count == 21);

[[nodiscard]] consteval std::size_t corpus_entries_with(col::Disposition wanted) noexcept {
    std::size_t found = 0;
    for (const col::corpus_entry& entry : col::rule_corpus) {
        if (entry.disposition == wanted) ++found;
    }
    return found;
}
static_assert(corpus_entries_with(col::Disposition::Live) == 21);
static_assert(corpus_entries_with(col::Disposition::Pending) == 13);
static_assert(corpus_entries_with(col::Disposition::Absent) == 21);
static_assert(corpus_entries_with(col::Disposition::Live) + corpus_entries_with(col::Disposition::Pending)
                  + corpus_entries_with(col::Disposition::Absent)
              == col::rule_corpus_size);

// No code appears twice, so the 54 are 54 distinct codes rather than a
// list that happens to be 54 long.
[[nodiscard]] consteval bool every_corpus_code_is_unique() noexcept {
    for (std::size_t i = 0; i < col::rule_corpus_size; ++i) {
        for (std::size_t j = i + 1; j < col::rule_corpus_size; ++j) {
            if (col::rule_corpus[i].code == col::rule_corpus[j].code) return false;
        }
    }
    return true;
}
static_assert(every_corpus_code_is_unique());

// Every absent entry says what is missing, so an absence is a claim a
// reader can check rather than a silence.
[[nodiscard]] consteval bool every_absent_entry_gives_a_reason() noexcept {
    for (const col::corpus_entry& entry : col::rule_corpus) {
        if (entry.disposition == col::Disposition::Absent && entry.note.empty()) return false;
    }
    return true;
}
static_assert(every_absent_entry_gives_a_reason());

// Each pending axis really has no atom, and each axis carrying a live
// rule really has one.  Both halves, so the roster is not merely
// self-consistent.
static_assert(!col::axis_has_an_atom<Axis::FpMode>);
static_assert(!col::axis_has_an_atom<Axis::MemoryScope>);
static_assert(col::axis_has_an_atom<Axis::Usage>);
static_assert(col::axis_has_an_atom<Axis::Effect>);
// Regime moved sides when fixy/atoms/Regime.h shipped.  The cell stays
// rather than being deleted: it is the witness that the axis crossed, and
// the H, R and S cells below are what it bought.
static_assert(col::axis_has_an_atom<Axis::Regime>);
static_assert(col::axis_has_an_atom<Axis::ControlFlow>);
static_assert(col::axis_has_an_atom<Axis::GlobalState>);

// Every pending rule names an axis that is on the pending roster.  A
// rule filed against a live axis would be a rule someone forgot to
// write.
[[nodiscard]] consteval bool every_pending_rule_waits_on_a_pending_axis() noexcept {
    for (const col::pending_rule& rule : col::pending_rules) {
        if (!col::axis_is_pending(rule.waits_on)) return false;
        if (rule.theorem.empty()) return false;
    }
    return true;
}
static_assert(every_pending_rule_waits_on_a_pending_axis());

// ---------------------------------------------------------------------
// The rules reach fn's gate.

static_assert(::fixy::IsAccepted<int, at::borrow>);
static_assert(::fixy::IsAccepted<int, at::coroutine>);
static_assert(!::fixy::IsAccepted<int, at::borrow, at::coroutine>);
static_assert(!::fixy::IsAccepted<int, at::capability_usage, at::trust_unverified>);
static_assert(col::CollisionRules<::fixy::fn<int, at::copy>>::valid);
static_assert(!col::CollisionRules<::fixy::fn<int, at::borrow, at::coroutine>>::valid);

// A static_assert proves the constant-evaluated path only.
[[nodiscard]] int check_runtime_paths() {
    if (col::pending_axis_count != axes_without_an_atom()) return 1;
    if (col::pending_rule_count != 13) return 2;
    if (col::live_rule_count != 21) return 3;

    std::size_t seen = 0;
    for (const col::pending_rule& rule : col::pending_rules) {
        if (rule.theorem.empty()) return 4;
        ++seen;
    }
    if (seen != col::pending_rule_count) return 5;

    // The corpus accounts for every code, and says something about each.
    std::size_t live = 0;
    std::size_t pending = 0;
    std::size_t absent = 0;
    for (const col::corpus_entry& entry : col::rule_corpus) {
        if (entry.code.empty() || entry.note.empty()) return 7;
        switch (entry.disposition) {
            case col::Disposition::Live: ++live; break;
            case col::Disposition::Pending: ++pending; break;
            case col::Disposition::Absent: ++absent; break;
            default: return 8;
        }
    }
    if (live != 21 || pending != 13 || absent != 21) return 9;
    if (live + pending + absent != col::rule_corpus_size) return 10;
    if (col::rule_corpus_size != 55) return 11;

    // The binding the rules admit still carries its value.
    const auto bound = ::fixy::mint_fn<int, at::borrow>(11);
    if (bound.value() != 11) return 6;
    return 0;
}

}  // namespace

int main() {
    if (int rc = check_runtime_paths(); rc != 0) return rc;
    return 0;
}
