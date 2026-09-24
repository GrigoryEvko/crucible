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
#include <fixy/os/Spawn.h>
#include <fixy/Secret.h>
#include <fixy/atoms/Os.h>
#include <fixy/atoms/Syscall.h>

#include <foundation/effects/Effect.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <expected>
#include <meta>
#include <tuple>
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

// P010 reads the row of the binding, so an atom that lifts an observable
// effect trips it as a stated with<> does.  A kernel wait lifts Block, a
// futex call lifts Block and a read of process state lifts IO.  A call
// through the vDSO lifts the empty row, so P010 admits it and P002
// refuses it.
static_assert(!live_rules<at::ghost, at::sync::park>::P010_ok, "a park is emitted code");
static_assert(live_rules<at::ghost, at::sync::park>::failing_codes() == "P010",
              "P002 reads the Stdio and SyscallSurface axes, and a park engages neither");
static_assert(!live_rules<at::ghost, at::syscall::per<at::syscall::SyscallId::futex>>::P010_ok);
static_assert(!live_rules<at::ghost, at::syscall::per<at::syscall::SyscallId::getpid>>::P010_ok);
static_assert(live_rules<at::ghost, at::sync::spin_pause>::P010_ok, "a spin lifts the empty row");
static_assert(live_rules<at::ghost, at::syscall::per<at::syscall::SyscallId::clock_gettime>>::P010_ok
              && !live_rules<at::ghost, at::syscall::per<at::syscall::SyscallId::clock_gettime>>::P002_ok);
static_assert(live_rules<at::sync::park>::P010_ok, "not ghost, so a park is ordinary");

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
// The regime family, which reads fixy/atoms/Regime.h.
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

// H003 hot x Alloc or IO in the row of the binding.  The cost is not a
// premise: a bounded allocation on the hot path is refused as an
// unbounded one is.  The cost and refinement atoms silence H001 and
// H002, so each pack below trips H003 alone.
template <class... Extra>
using hot_bounded = live_rules<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>, Extra...>;
static_assert(hot_bounded<at::with<Eff::Alloc>>::failing_codes() == "H003");
static_assert(hot_bounded<at::with<Eff::IO>>::failing_codes() == "H003");
static_assert(hot_bounded<at::syscall::per<at::syscall::SyscallId::getpid>>::failing_codes() == "H003",
              "a read of process state lifts IO, although it cannot block");
static_assert(hot_bounded<at::syscall::per<at::syscall::SyscallId::write>>::failing_codes() == "H003, W001",
              "a file write lifts IO and Block, so H003 and W001 each refuse it");
static_assert(!live_rules<at::regime::hot, at::with<Eff::Alloc>, at::cost_unbounded>::H003_ok);
static_assert(!live_rules<at::regime::hot, at::with<Eff::IO>, at::cost_unbounded>::H003_ok);
static_assert(live_rules<at::regime::hot, at::cost_unbounded>::H003_ok, "no Alloc or IO row, so H001 not H003");
static_assert(live_rules<at::with<Eff::Alloc>, at::cost_unbounded>::H003_ok, "not hot, so no contradiction");
// A read of the clock through the vDSO lifts the empty row, and a spin
// lifts the empty row too, so H003 admits both on the hot path.
static_assert(hot_bounded<at::syscall::per<at::syscall::SyscallId::clock_gettime>>::valid);
static_assert(hot_bounded<at::sync::spin_pause>::valid);
// The positive controls: a warm or a cold binding with the same rows.
static_assert(live_rules<at::regime::warm, at::cost_constant, at::with<Eff::Alloc>>::valid);
static_assert(live_rules<at::regime::warm, at::cost_constant, at::with<Eff::IO>>::valid);
static_assert(live_rules<at::regime::warm, at::syscall::per<at::syscall::SyscallId::getpid>>::valid);
static_assert(live_rules<at::regime::cold, at::with<Eff::Alloc, Eff::IO>>::valid);
// Block is deliberately outside H003: a blocking hot path is W001's
// theorem, which cites the futex cost rather than the allocator's.  The
// cell is here so the boundary is a decision on the record.
static_assert(live_rules<at::regime::hot, at::with<Eff::Block>, at::cost_unbounded>::H003_ok,
              "H003's theorem names Alloc and IO; Block on a hot path belongs to W001");
static_assert(!live_rules<at::regime::hot, at::with<Eff::Block>, at::cost_unbounded>::W001_ok,
              "the other side of the boundary: W001 refuses the same pack");

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
// The wait family, which reads fixy/atoms/Sync.h.
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

// W001 reads the row that the whole pack lifts to, so a kernel wait is
// one way to trip it and not the only one.  A stated Block and a system
// call that can park the caller put Block in the same row.  The cost and
// refinement atoms silence H001 and H002 in each cell.
template <class... Extra>
using hot_pack = live_rules<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>, Extra...>;
using SyscallId = at::syscall::SyscallId;
using SyscallFamily = at::syscall::SyscallFamily;

static_assert(!hot_pack<at::with<Eff::Block>>::W001_ok);
static_assert(!hot_pack<at::with<Eff::IO, Eff::Block>>::W001_ok);
static_assert(!hot_pack<at::syscall::per<SyscallId::futex>>::W001_ok);
static_assert(!hot_pack<at::syscall::per<SyscallId::sched_yield>>::W001_ok, "a yield enters the scheduler");
static_assert(!hot_pack<at::syscall::per<SyscallId::write>>::W001_ok);
static_assert(!hot_pack<at::syscall::per<SyscallId::recvmsg>>::W001_ok);
static_assert(!hot_pack<at::syscall::family<SyscallFamily::ThreadSync>>::W001_ok);
static_assert(!hot_pack<at::fs::durable<::fixy::fs::sync_op::Fsync>>::W001_ok, "an OS atom lifts Block as well");

// The two fixtures' packs trip W001 and no other rule.
static_assert(hot_pack<at::with<Eff::Block>>::failing_codes() == "W001");
static_assert(hot_pack<at::syscall::per<SyscallId::futex>>::failing_codes() == "W001");
static_assert(hot_pack<at::sync::acquire_wait>::failing_codes() == "W001", "and the kernel wait still does");

// A call that cannot park the caller puts no Block in the row.
static_assert(hot_pack<at::syscall::per<SyscallId::clock_gettime>>::valid, "a vDSO read enters no kernel");
static_assert(hot_pack<at::syscall::family<SyscallFamily::VdsoOnly>>::valid);
static_assert(hot_pack<at::syscall::per<SyscallId::getpid>>::W001_ok, "a read of process state does not park");
static_assert(hot_pack<at::with<Eff::IO>>::W001_ok, "IO without Block is H003's premise, not W001's");

// The lifted row is the union of every lift in the pack.  A spin and
// UMWAIT lift to the empty row.
static_assert(std::is_same_v<hot_pack<at::sync::park, at::with<Eff::IO>>::lifted_row, fe::Row<Eff::IO, Eff::Block>>);
static_assert(std::is_same_v<live_rules<at::syscall::per<SyscallId::futex>, at::with<Eff::Alloc>>::lifted_row,
                             fe::Row<Eff::Alloc, Eff::Block>>);
static_assert(std::is_same_v<live_rules<>::lifted_row, fe::Row<>>);
static_assert(!live_rules<at::sync::spin_pause>::blocks && !live_rules<at::sync::umwait_c01>::blocks);

// The row of the binding joins the Effect grade with the lifted row.  It
// is the row every rule reads, and it is the row fixy::binding_row_t
// gives for context admission, so a rule and a context gate read one row.
static_assert(std::is_same_v<live_rules<at::syscall::per<SyscallId::futex>>::binding_row, fe::Row<Eff::Block>>);
static_assert(std::is_same_v<live_rules<at::syscall::per<SyscallId::futex>>::effect_row, fe::Row<>>,
              "the Effect grade stays at the strict pole");
static_assert(std::is_same_v<live_rules<at::with<Eff::IO>, at::sync::park>::binding_row, fe::Row<Eff::IO, Eff::Block>>);
static_assert(std::is_same_v<live_rules<>::binding_row, fe::Row<>>);
static_assert(std::is_same_v<live_rules<at::syscall::per<SyscallId::futex>>::binding_row,
                             ::fixy::binding_row_t<::fixy::fn<int, at::syscall::per<SyscallId::futex>>>>);

// The positive controls: the rules admit a warm or a cold binding with
// the same row, and a binding that states no tier.
static_assert(live_rules<at::regime::warm, at::with<Eff::Block>>::valid);
static_assert(live_rules<at::regime::cold, at::syscall::per<SyscallId::futex>>::valid);
static_assert(live_rules<at::regime::warm, at::syscall::per<SyscallId::sched_yield>>::valid);
static_assert(live_rules<at::with<Eff::Block>>::valid);
static_assert(live_rules<at::syscall::per<SyscallId::futex>>::valid);

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
// The observability family, which reads fixy/atoms/Observe.h.
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
// The row that contains the surface is the row of the binding, so an
// effect that an atom lifts is an effect the binding declared.
static_assert(live_rules<at::observe::surface<Eff::IO>, at::syscall::per<SyscallId::getpid>>::B002_ok,
              "a read of process state lifts IO, so the surface names an effect the binding performs");
static_assert(!live_rules<at::observe::surface<Eff::IO>, at::syscall::per<SyscallId::futex>>::B002_ok,
              "a futex call lifts Block and not IO");
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
// The payload read, which the FpMode, SimdIsa and HwInstruction rules
// pair with.
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
// The hardware-instruction family, which reads fixy/atoms/Hw.h.
//
// The ladder is a chain where a tier admits every class below it, so the
// cells read it at the one boundary V201 and V203 care about — at or
// above NonDeterministicTsc — and at the top for V202.

static_assert(col::axis_has_an_atom<Axis::HwInstruction>);

// V201 hot x a tier at or above NonDeterministicTsc.  PrivilegedMsr is
// above the timestamp tier, which is why the old name's "or privileged"
// is one clause, not two.  The cost and refinement atoms silence H001
// and H002; an Init row silences V202 for the privileged cell.
static_assert(!live_rules<at::regime::hot, at::hw::non_deterministic_tsc, at::cost_constant,
                          at::refined_with<hot_invariant>>::V201_ok);
static_assert(!live_rules<at::regime::hot, at::hw::privileged_msr, at::with<Eff::Init>, at::cost_constant,
                          at::refined_with<hot_invariant>>::V201_ok,
              "the privileged tier is at or above the timestamp tier, so the hot path refuses it too");
static_assert(live_rules<at::regime::hot, at::hw::vectorizable, at::cost_constant,
                         at::refined_with<hot_invariant>>::V201_ok,
              "SIMD intrinsics sit below the timestamp tier and are what a hot path is made of");
static_assert(live_rules<at::hw::non_deterministic_tsc>::V201_ok, "not hot, so a timestamp read is ordinary");
static_assert(live_rules<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>>::V201_ok);

// V202 the PrivilegedMsr tier x an effect row with no Init.  The twin the
// fixture names: add Init to the row and the tier is admitted.
static_assert(!live_rules<at::hw::privileged_msr>::V202_ok);
static_assert(!live_rules<at::hw::privileged_msr, at::with<Eff::IO>>::V202_ok, "a row without Init is not enough");
static_assert(live_rules<at::hw::privileged_msr, at::with<Eff::Init>>::V202_ok, "the twin: reached from Init");
static_assert(live_rules<at::hw::privileged_msr, at::with<Eff::Init, Eff::IO>>::V202_ok);
static_assert(live_rules<at::hw::non_deterministic_tsc>::V202_ok, "only the privileged tier needs Init");

// V203 a replay-deterministic payload x a tier at or above
// NonDeterministicTsc.  This is the payload read consumed: the same pack
// is refused under a Pure payload and admitted under a payload with no
// band or under the pack-only view.
static_assert(!rules_of<det<DetTier::Pure>, at::hw::non_deterministic_tsc>::V203_ok);
static_assert(!rules_of<det<DetTier::PhiloxRng>, at::hw::privileged_msr, at::with<Eff::Init>>::V203_ok);
static_assert(rules_of<det<DetTier::WallClockRead>, at::hw::non_deterministic_tsc>::V203_ok,
              "a payload that already admits wall-clock reads claims nothing a timestamp could break");
static_assert(rules_of<int, at::hw::non_deterministic_tsc>::V203_ok, "no band, no claim");
static_assert(live_rules<at::hw::non_deterministic_tsc>::V203_ok, "the pack-only view cannot see a payload");
static_assert(rules_of<det<DetTier::Pure>, at::hw::vectorizable>::V203_ok, "below the timestamp tier");

// The three rules are three: each pack below trips exactly one of them.
static_assert(!live_rules<at::regime::hot, at::hw::non_deterministic_tsc, at::cost_constant,
                          at::refined_with<hot_invariant>>::V201_ok
              && live_rules<at::regime::hot, at::hw::non_deterministic_tsc, at::cost_constant,
                            at::refined_with<hot_invariant>>::V202_ok);
static_assert(!live_rules<at::hw::privileged_msr>::V202_ok && live_rules<at::hw::privileged_msr>::V201_ok);
static_assert(!rules_of<det<DetTier::Pure>, at::hw::non_deterministic_tsc>::V203_ok
              && rules_of<det<DetTier::Pure>, at::hw::non_deterministic_tsc>::V201_ok);

// The read alone still refuses nothing: a Pure payload with a tier below
// the timestamp one is accepted through the bound view.
static_assert(rules_of<det<DetTier::Pure>, at::hw::vectorizable>::valid);

// ---------------------------------------------------------------------
// The barrier-strength family, which reads fixy/atoms/Barrier.h.
//
// One rule at one floor of the order.  The cost and refinement atoms
// silence H001 and H002 on every hot cell.

static_assert(col::axis_has_an_atom<Axis::BarrierStrength>);

// V301 hot x a fence at or above SeqCst.
static_assert(!live_rules<at::regime::hot, at::barrier::seq_cst, at::cost_constant,
                          at::refined_with<hot_invariant>>::V301_ok);
static_assert(!live_rules<at::regime::hot, at::barrier::full_fence, at::cost_constant,
                          at::refined_with<hot_invariant>>::V301_ok,
              "the standalone fence is above seq_cst on the ladder, so the hot path refuses it too");
static_assert(live_rules<at::regime::hot, at::barrier::acq_rel, at::cost_constant,
                         at::refined_with<hot_invariant>>::V301_ok,
              "acq_rel is one MOV each way on x86 and is what a hot SPSC ring is made of");
static_assert(live_rules<at::regime::hot, at::barrier::release_store, at::cost_constant,
                         at::refined_with<hot_invariant>>::V301_ok);
static_assert(live_rules<at::regime::hot, at::barrier::compiler_barrier, at::cost_constant,
                         at::refined_with<hot_invariant>>::V301_ok,
              "a compiler barrier emits no instruction");
static_assert(live_rules<at::barrier::seq_cst>::V301_ok, "not hot, so a full fence is ordinary");
static_assert(live_rules<at::regime::warm, at::barrier::full_fence>::V301_ok);
static_assert(live_rules<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>>::V301_ok,
              "no strength named: the binding provides no fence, and there is nothing to refuse");
static_assert(live_rules<>::V301_ok);

// The fixture's pack trips V301 and nothing else: the two hot rules its
// cost and refinement atoms answer, the wait rule, and the hardware rule
// all stand down.
static_assert(live_rules<at::regime::hot, at::barrier::seq_cst, at::cost_constant,
                         at::refined_with<hot_invariant>>::H001_ok
              && live_rules<at::regime::hot, at::barrier::seq_cst, at::cost_constant,
                            at::refined_with<hot_invariant>>::H002_ok
              && live_rules<at::regime::hot, at::barrier::seq_cst, at::cost_constant,
                            at::refined_with<hot_invariant>>::W001_ok
              && live_rules<at::regime::hot, at::barrier::seq_cst, at::cost_constant,
                            at::refined_with<hot_invariant>>::V201_ok);

// The two `tier` families do not answer for each other: a hardware tier
// is not a fence strength, and a fence strength is not a hardware tier.
static_assert(live_rules<at::regime::hot, at::hw::privileged_msr, at::with<Eff::Init>, at::cost_constant,
                         at::refined_with<hot_invariant>>::V301_ok,
              "the top of the hardware ladder is not a fence");
static_assert(live_rules<at::barrier::full_fence>::V201_ok && live_rules<at::barrier::full_fence>::V202_ok,
              "the top of the fence ladder is not a hardware tier");

// Acquire and release are incomparable, so neither tag satisfies a floor
// at the other.  No live rule reads a floor at either tag, so the change
// from a chain moves no verdict: V301 reads SeqCst and V401 reads AcqRel,
// and each tag sits below both floors in either order.
static_assert(!col::detail::is_barrier_at_or_above_<::foundation::algebra::lattices::BarrierStrength::AcquireLoad,
                                                    at::barrier::release_store>::value);
static_assert(!col::detail::is_barrier_at_or_above_<::foundation::algebra::lattices::BarrierStrength::ReleaseStore,
                                                    at::barrier::acquire_load>::value);
static_assert(col::detail::is_barrier_at_or_above_<::foundation::algebra::lattices::BarrierStrength::AcquireLoad,
                                                   at::barrier::acq_rel>::value
              && col::detail::is_barrier_at_or_above_<::foundation::algebra::lattices::BarrierStrength::ReleaseStore,
                                                      at::barrier::acq_rel>::value,
              "acq_rel is both an acquire and a release");

// ---------------------------------------------------------------------
// The memory-scope family, which reads fixy/atoms/Scope.h.
//
// V401 is the first rule to read two of the new axes together, and the
// lattice under the scope axis is two trunks, so the cells cover the
// floor on the accelerator trunk, the shared top, the host trunk that
// is incomparable with the floor, and the unnamed strength.

static_assert(col::axis_has_an_atom<Axis::MemoryScope>);

// V401 a scope at or above Cluster x a fence below AcqRel.
static_assert(!live_rules<at::scope::cluster, at::barrier::release_store>::V401_ok);
static_assert(!live_rules<at::scope::gpu, at::barrier::acquire_load>::V401_ok);
static_assert(!live_rules<at::scope::system, at::barrier::compiler_barrier>::V401_ok,
              "the shared top reaches everywhere, so it is at or above the floor");
static_assert(!live_rules<at::scope::cluster>::V401_ok,
              "no strength named provides no fence, which is the same trap with nothing said");
static_assert(live_rules<at::scope::cluster, at::barrier::acq_rel>::V401_ok, "the twin: acq_rel is the floor");
static_assert(live_rules<at::scope::gpu, at::barrier::seq_cst>::V401_ok);
static_assert(live_rules<at::scope::system, at::barrier::full_fence>::V401_ok);
static_assert(live_rules<at::scope::cta, at::barrier::release_store>::V401_ok, "below the cluster floor");
static_assert(live_rules<at::scope::warp, at::barrier::none>::V401_ok);
static_assert(live_rules<at::scope::thread>::V401_ok, "the shared bottom reaches nobody else");
static_assert(live_rules<at::scope::inner, at::barrier::release_store>::V401_ok,
              "Inner is incomparable with Cluster, so it is not at or above it and the rule stands down");
static_assert(live_rules<at::scope::outer>::V401_ok, "Outer is on the host trunk too");
static_assert(live_rules<at::barrier::none>::V401_ok, "no scope named publishes to nobody in particular");
static_assert(live_rules<>::V401_ok);

// V401 and V301 read the same strength axis and are two rules: the
// fixture's pack trips V401 alone, and a hot seq_cst publication to the
// whole system trips V301 alone.
static_assert(live_rules<at::scope::cluster, at::barrier::release_store>::V301_ok
              && !live_rules<at::scope::cluster, at::barrier::release_store>::V401_ok);
static_assert(!live_rules<at::regime::hot, at::scope::system, at::barrier::seq_cst, at::cost_constant,
                          at::refined_with<hot_invariant>>::V301_ok
              && live_rules<at::regime::hot, at::scope::system, at::barrier::seq_cst, at::cost_constant,
                            at::refined_with<hot_invariant>>::V401_ok);

// ---------------------------------------------------------------------
// The SIMD-ISA family, which reads fixy/atoms/Simd.h.
//
// Two trunks again, and two rules.  V101 reads the payload, so its cells
// use rules_of; V402 reads two axes of the pack, so its use live_rules.

static_assert(col::axis_has_an_atom<Axis::SimdIsa>);

// V101 a replay-deterministic payload x an ISA pinned to one trunk.
static_assert(!rules_of<det<DetTier::Pure>, at::simd::avx2>::V101_ok);
static_assert(!rules_of<det<DetTier::Pure>, at::simd::sve2>::V101_ok, "either trunk pins a width");
static_assert(!rules_of<det<DetTier::PhiloxRng>, at::simd::avx512bw>::V101_ok);
static_assert(rules_of<det<DetTier::Pure>, at::simd::scalar>::V101_ok, "the shared bottom pins no trunk");
static_assert(rules_of<det<DetTier::Pure>, at::simd::portable>::V101_ok,
              "the shared top is one kernel for every set, so it pins no trunk either");
static_assert(rules_of<det<DetTier::WallClockRead>, at::simd::avx2>::V101_ok,
              "a payload that already admits a wall-clock read claims nothing a vector width could break");
static_assert(rules_of<int, at::simd::avx2>::V101_ok, "no band, no claim");
static_assert(live_rules<at::simd::avx2>::V101_ok, "the pack-only view cannot see a payload");

// V402 a trunk-pinned scope x a trunk-pinned ISA that do not cohere.
static_assert(!live_rules<at::simd::avx2, at::scope::inner>::V402_ok,
              "the host shareability scopes are the ARM fence family, and this ISA is x86");
static_assert(!live_rules<at::simd::avx2, at::scope::cta>::V402_ok, "an accelerator scope with a host ISA");
static_assert(!live_rules<at::simd::sve2, at::scope::gpu>::V402_ok, "an accelerator scope with an ARM host ISA");
static_assert(live_rules<at::simd::neon, at::scope::inner>::V402_ok, "the one coherent pairing");
static_assert(live_rules<at::simd::sve, at::scope::outer>::V402_ok);
static_assert(live_rules<at::simd::avx2, at::scope::thread>::V402_ok, "the shared bottom coheres with anything");
static_assert(live_rules<at::simd::avx2, at::scope::system>::V402_ok, "and so does the shared top");
static_assert(live_rules<at::simd::scalar, at::scope::cta>::V402_ok, "an unpinned ISA coheres with anything");
static_assert(live_rules<at::simd::portable, at::scope::inner>::V402_ok);
static_assert(live_rules<at::scope::cta>::V402_ok, "no ISA named");
static_assert(live_rules<at::simd::avx2>::V402_ok, "no scope named");
static_assert(live_rules<>::V402_ok);

// The one coherent pairing still answers to V401, which is the other
// rule reading a scope: Inner is on the host trunk, so it is below the
// cluster floor and V401 stands down whatever the fence.
static_assert(live_rules<at::simd::neon, at::scope::inner>::V401_ok
              && live_rules<at::simd::neon, at::scope::inner>::V402_ok);

// ---------------------------------------------------------------------
// The floating-point-mode family, which reads fixy/atoms/Fp.h.
//
// The atom is a product, so a mode names some settings and leaves the
// rest at their enum's first enumerator.  The cells read both halves:
// what a mode says, and what it leaves unsaid.

static_assert(col::axis_has_an_atom<Axis::FpMode>);

using Reassoc = at::fp::FpReassociate;
using Contract = at::fp::FpContract;
template <auto... Settings>
using fp_mode = at::fp::mode<Settings...>;

// F101 a replay-deterministic payload x reassociation permitted.
static_assert(!rules_of<det<DetTier::Pure>, fp_mode<Reassoc::UnrestrictedRewrite>>::F101_ok);
static_assert(!rules_of<det<DetTier::Pure>, fp_mode<Reassoc::BoundedTreeDepth>>::F101_ok,
              "a tree of any shape but the one the source wrote is a different sum");
static_assert(rules_of<det<DetTier::Pure>, fp_mode<Reassoc::Forbidden>>::F101_ok);
static_assert(rules_of<det<DetTier::Pure>, fp_mode<Contract::Fast>>::F101_ok,
              "a mode that names only contraction leaves reassociation at Forbidden");
static_assert(rules_of<det<DetTier::Pure>, fp_mode<>>::F101_ok, "the empty mode is every setting at its strict pole");
static_assert(rules_of<int, fp_mode<Reassoc::UnrestrictedRewrite>>::F101_ok, "no band, no claim");
static_assert(live_rules<fp_mode<Reassoc::UnrestrictedRewrite>>::F101_ok, "the pack-only view cannot see a payload");

// F102 a replay-deterministic payload x contraction across statements.
static_assert(!rules_of<det<DetTier::Pure>, fp_mode<Contract::Fast>>::F102_ok);
static_assert(rules_of<det<DetTier::Pure>, fp_mode<Contract::OnInExpr>>::F102_ok,
              "contraction within one expression is visible in the source and the same on every build");
static_assert(rules_of<det<DetTier::Pure>, fp_mode<Contract::Off>>::F102_ok);
static_assert(rules_of<det<DetTier::Pure>, fp_mode<Reassoc::UnrestrictedRewrite>>::F102_ok,
              "a mode that names only reassociation leaves contraction at Off");
static_assert(rules_of<det<DetTier::Pure>, fp_mode<>>::F102_ok);

// The two are two rules: a mode naming both trips both, and each of the
// single-setting modes trips exactly one.
static_assert(!rules_of<det<DetTier::Pure>, fp_mode<Contract::Fast, Reassoc::UnrestrictedRewrite>>::F101_ok
              && !rules_of<det<DetTier::Pure>, fp_mode<Contract::Fast, Reassoc::UnrestrictedRewrite>>::F102_ok);

// The settings no rule reads are admitted, which is the atom's own
// claim: a mode may name them, and naming them refuses nothing.
static_assert(rules_of<det<DetTier::Pure>, fp_mode<at::fp::FpFtz::FlushToZero>>::valid);
static_assert(rules_of<det<DetTier::Pure>, fp_mode<at::fp::FpDenormalInput::DenormalsAreZero>>::valid);

// The read alone still refuses nothing: a Pure payload under the strict
// mode is accepted through the bound view.
static_assert(rules_of<det<DetTier::Pure>, fp_mode<>>::valid);

// ---------------------------------------------------------------------
// The three rules the old tree carried as marker traits.

// S011 a capability x a replay-deterministic payload.  Both replay tiers
// trip it, and a payload below the replay floor, or with no band, does
// not.  The trust grade keeps T001 out of every cell.
static_assert(!rules_of<det<DetTier::Pure>, at::capability_usage, at::trust_verified>::S011_ok);
static_assert(!rules_of<det<DetTier::PhiloxRng>, at::capability_usage, at::trust_verified>::S011_ok);
static_assert(rules_of<det<DetTier::MonotonicClockRead>, at::capability_usage, at::trust_verified>::S011_ok,
              "a payload that reads the clock claims no replay, so a capability costs it nothing");
static_assert(rules_of<int, at::capability_usage, at::trust_verified>::valid,
              "a capability with no replay claim and a verified trust trips nothing");
static_assert(rules_of<det<DetTier::Pure>, at::copy>::valid, "a replay payload that holds no capability");
static_assert(rules_of<det<DetTier::Pure>, at::capability_usage, at::trust_verified>::T001_ok,
              "S011 must be the rule that catches this pack, not T001");
static_assert(live_rules<at::capability_usage, at::trust_verified>::S011_ok, "the pack-only view claims no replay");

// D001 an indirect call whose family names no noexcept signature.  A free
// function pointer, a function type and a member function pointer each
// name one, and the rule reads its noexcept.  An opaque tag class names
// none, and an unknown callee is potentially-throwing, so the rule
// refuses it.  A tag class that names its signature as a member type is
// read through it.
struct callback_owner final {};
struct opaque_family final {};
struct stated_family final {
    using signature = void(void*) noexcept;
};
struct stated_throwing_family final {
    using signature = void(void*);
};
static_assert(!live_rules<at::dispatch::indirect_call<void (*)(int)>>::D001_ok);
static_assert(!live_rules<at::dispatch::indirect_call<int(void*)>>::D001_ok);
static_assert(!live_rules<at::dispatch::indirect_call<void (callback_owner::*)() const>>::D001_ok);
static_assert(live_rules<at::dispatch::indirect_call<void (*)(int) noexcept>>::D001_ok);
static_assert(live_rules<at::dispatch::indirect_call<void (callback_owner::*)() const noexcept>>::D001_ok);
static_assert(!live_rules<at::dispatch::indirect_call<opaque_family>>::D001_ok,
              "a family that names no signature is read as one that throws");
static_assert(live_rules<at::dispatch::indirect_call<stated_family>>::valid,
              "a tag class that names a noexcept signature is admitted");
static_assert(!live_rules<at::dispatch::indirect_call<stated_throwing_family>>::D001_ok);
static_assert(live_rules<at::dispatch::tail_call>::valid);

// L003 a borrow x a spawn no structured join reaches.  A detached child
// and a raw clone trip it; a subprocess runs in its own copy of the
// address space and does not; neither half alone trips it.
static_assert(!live_rules<at::borrow, at::spawn::detach_with<"drain outlives the owner">>::L003_ok);
static_assert(!live_rules<at::borrow, at::spawn::syscall_only<"loader needs CLONE_VM">>::L003_ok);
static_assert(live_rules<at::borrow, at::spawn::subprocess<"exec a helper">>::valid,
              "a forked child borrows from its own copy, so the borrow cannot dangle into the parent");
static_assert(live_rules<at::spawn::detach_with<"drain outlives the owner">>::valid);
static_assert(live_rules<at::borrow>::L003_ok);
static_assert(live_rules<at::borrow, at::spawn::detach_with<"drain outlives the owner">>::L002_ok,
              "L003 must be the rule that catches this pack, not L002: a detached spawn is neither a suspension "
              "nor a Bg row");

// ---------------------------------------------------------------------
// The pending roster.

// Zero, because every axis but Type has an atom.  It is a literal rather
// than a floor because the dispositions partition the catalog, and a
// floor here would let a rule fall out of all of them and go unnoticed.
static_assert(col::pending_rule_count == 0,
              "the pending roster is empty: every axis but Type has an atom, so a rule that still cannot fire "
              "is Absent for a reason this layer can name, not Pending on an atom");
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
// That size is 57, not 54: the 54 are inherited from the old catalog's
// RuleCode enum, and B002 is written in fixy/Collision.h, because
// Axis::Observability carries two theorems where the old catalog recorded
// one.  B001 keeps its back-pressure theorem rather than being reread as
// the containment rule, because the codes are stable API.  R004 and W003
// are also written in fixy/Collision.h: the old catalog has no rule for a
// continuation that holds a live session handle, and none for a hot
// binding that holds one and states no wait.

static_assert(col::rule_corpus_size == 57);
static_assert(col::live_rule_count == 43);

[[nodiscard]] consteval std::size_t corpus_entries_with(col::Disposition wanted) noexcept {
    std::size_t found = 0;
    for (const col::corpus_entry& entry : col::rule_corpus) {
        if (entry.disposition == wanted) ++found;
    }
    return found;
}
static_assert(corpus_entries_with(col::Disposition::Live) == 43);
static_assert(corpus_entries_with(col::Disposition::Pending) == 0);
static_assert(corpus_entries_with(col::Disposition::Absent) == 6);
static_assert(corpus_entries_with(col::Disposition::Retired) == 8);
static_assert(corpus_entries_with(col::Disposition::Live) + corpus_entries_with(col::Disposition::Pending)
                  + corpus_entries_with(col::Disposition::Absent) + corpus_entries_with(col::Disposition::Retired)
              == col::rule_corpus_size);

// The Absent list and the Absent rows are the same set, so the list's
// length is the Absent count.  The header pins the set by name; this
// cell ties that name list to the count above.
static_assert(sizeof(col::absent_rule_codes) / sizeof(col::absent_rule_codes[0])
              == corpus_entries_with(col::Disposition::Absent));

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

// Every absent entry says what is missing, and every retired entry says
// why it is not carried, so neither is a silence.
[[nodiscard]] consteval bool every_absent_entry_gives_a_reason() noexcept {
    for (const col::corpus_entry& entry : col::rule_corpus) {
        const bool needs_reason =
            entry.disposition == col::Disposition::Absent || entry.disposition == col::Disposition::Retired;
        if (needs_reason && entry.note.empty()) return false;
    }
    return true;
}
static_assert(every_absent_entry_gives_a_reason());

// Each pending axis really has no atom, and each axis carrying a live
// rule really has one.  Both halves, so the roster is not merely
// self-consistent.
// Every axis is on this side, because the pending roster is empty.  Each
// cell is the witness that its axis has an atom, and the rule cells above
// read those atoms.
static_assert(col::axis_has_an_atom<Axis::FpMode>);
static_assert(col::axis_has_an_atom<Axis::SimdIsa>);
static_assert(col::axis_has_an_atom<Axis::BarrierStrength>);
static_assert(col::axis_has_an_atom<Axis::HwInstruction>);
static_assert(col::axis_has_an_atom<Axis::MemoryScope>);
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

// W001 reaches the gate through the lifted row.  The gate admits a cold
// or a warm binding with the same row.
static_assert(!::fixy::IsAccepted<int, at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>,
                                  at::syscall::per<at::syscall::SyscallId::futex>>);
static_assert(!::fixy::IsAccepted<int, at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>,
                                  at::with<Eff::Block>>);
static_assert(::fixy::IsAccepted<int, at::regime::cold, at::syscall::per<at::syscall::SyscallId::futex>>);
static_assert(::fixy::IsAccepted<int, at::regime::warm, at::with<Eff::Block>>);

// H003 reaches the gate with a bounded cost.  The gate admits a warm
// binding with the same row.
static_assert(!::fixy::IsAccepted<int, at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>,
                                  at::with<Eff::Alloc>>);
static_assert(!::fixy::IsAccepted<int, at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>,
                                  at::syscall::per<at::syscall::SyscallId::getpid>, at::as_public>);
static_assert(::fixy::IsAccepted<int, at::regime::warm, at::cost_constant, at::with<Eff::Alloc>>);
static_assert(::fixy::IsAccepted<int, at::regime::warm, at::syscall::per<at::syscall::SyscallId::getpid>,
                                 at::as_public>);

// The constant-time family and the failure family reach the same gate.
// The failure rules read the payload, which only the bound view carries,
// so these cells name it.
struct gate_error final {};
static_assert(::fixy::IsAccepted<int, at::constant_time>);
static_assert(!::fixy::IsAccepted<int, at::constant_time, at::coroutine>, "E044");
static_assert(!::fixy::IsAccepted<std::expected<int, gate_error>>, "I002 on the strict Security pole");
static_assert(::fixy::IsAccepted<std::expected<int, gate_error>, at::as_public>);
static_assert(::fixy::IsAccepted<std::expected<int, ::fixy::Secret<gate_error>>>);
static_assert(!::fixy::IsAccepted<std::expected<int, ::fixy::Secret<gate_error>>, at::constant_time>, "I003");

// A constant-time binding takes a cache slot of its own.  A kernel built
// without the discipline must never be served to a caller that claims
// it, so constant_time may not share a key with as_classified, with the
// strict pole, or with as_secret.
static_assert(::foundation::diag::row_hash_contribution_v<::fixy::fn<int, at::constant_time>>
              != ::foundation::diag::row_hash_contribution_v<::fixy::fn<int>>);
static_assert(::foundation::diag::row_hash_contribution_v<::fixy::fn<int, at::constant_time>>
              != ::foundation::diag::row_hash_contribution_v<::fixy::fn<int, at::as_classified>>);
static_assert(::foundation::diag::row_hash_contribution_v<::fixy::fn<int, at::constant_time>>
              != ::foundation::diag::row_hash_contribution_v<::fixy::fn<int, at::as_secret>>);
static_assert(::foundation::diag::row_hash_contribution_v<::fixy::fn<int, at::constant_time>> != 0);

// ---------------------------------------------------------------------
// The replay claim is read through bands of other lattices.
//
// The canonical order puts HotPath outside DetSafe.  A payload whose
// DetSafe band sits under another band still claims replay, so each replay
// rule refuses its pair for it.  The outermost DetSafe band decides.

using PureUnderHotPath = ::fixy::HotPath<::fixy::HotPathTier_v::Hot, ::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>>;
using PureUnderArena = ::fixy::alloc_class::Arena<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>>;
using ClockUnderHotPath =
    ::fixy::HotPath<::fixy::HotPathTier_v::Hot, ::fixy::DetSafe<::fixy::DetSafeTier_v::MonotonicClockRead, int>>;
using ClockOverPure = ::fixy::DetSafe<::fixy::DetSafeTier_v::MonotonicClockRead,
                                      ::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>>;

static_assert(col::detail::is_replay_deterministic_<PureUnderHotPath>::value);
static_assert(col::detail::is_replay_deterministic_<PureUnderArena>::value);
static_assert(!col::detail::is_replay_deterministic_<ClockUnderHotPath>::value);
static_assert(!col::detail::is_replay_deterministic_<ClockOverPure>::value,
              "the outermost DetSafe band is the claim, and a weaker outer band takes the claim back");
static_assert(!col::detail::is_replay_deterministic_<::fixy::HotPath<::fixy::HotPathTier_v::Hot, int>>::value);

static_assert(!rules_of<PureUnderHotPath, at::fp::mode<at::fp::FpReassociate::UnrestrictedRewrite>>::F101_ok);
static_assert(!rules_of<PureUnderArena, at::simd::avx2>::V101_ok);
static_assert(rules_of<ClockUnderHotPath, at::simd::avx2>::V101_ok);

// ---------------------------------------------------------------------
// The grade readers under attack.
//
// Each attack is a legal grade that tries to make a reader give the wrong
// answer.  The attacks that failed are asserted as the answer the reader
// gives.  The attacks that succeeded are on the ledger below.

struct attack_owner {
    int value = 0;
};

// A grade of a different axis that carries a member of the same name is
// refused by the type check of each floor reader.
static_assert(!col::detail::is_hw_at_or_above_<at::hw::HwInstruction::Scalar, at::barrier::full_fence>::value);
static_assert(!col::detail::is_barrier_at_or_above_<::foundation::algebra::lattices::BarrierStrength::None,
                                                    at::hw::privileged_msr>::value);
// A host scope is not at or above an accelerator floor.
static_assert(!col::detail::is_scope_at_or_above_<::foundation::algebra::lattices::MemoryScope::Cluster,
                                                  at::scope::outer>::value);
// UMWAIT is neither a kernel entry nor a busy wait.
static_assert(!live_rules<at::sync::umwait_c01>::blocks && !col::detail::is_busy_wait_<at::sync::umwait_c01>::value);
// A setting of a different enum with the same underlying value is not named.
static_assert(!col::detail::fp_mode_has_setting_<at::fp::FpFtz{}, at::fp::mode<at::fp::FpContract{}>>::value);
// A const member function names its signature, and it is not noexcept.
static_assert(!col::detail::is_signature_noexcept_<void (attack_owner::*)() const>::value);

// Every form of a function type is read by one query, so a form the
// reader has no arm for cannot pass as one that states nothing.  Each
// throwing form is refused, and each noexcept twin is admitted: a
// ref-qualified, volatile or const volatile member function, a C
// variadic function, and a reference or a const pointer to a function.
using throwing_forms = std::tuple<void (attack_owner::*)() &, void (attack_owner::*)() &&,
                                  void (attack_owner::*)() volatile, void (attack_owner::*)() const volatile &,
                                  int (*)(char, ...), int (*&)(char), int (*const)(char), int(&)(char)>;
using noexcept_forms =
    std::tuple<void (attack_owner::*)() & noexcept, void (attack_owner::*)() && noexcept,
               void (attack_owner::*)() volatile noexcept, void (attack_owner::*)() const volatile & noexcept,
               int (*)(char, ...) noexcept, int (*&)(char) noexcept, int (*const)(char) noexcept,
               int (&)(char) noexcept>;

template <class Forms, std::size_t... Index>
[[nodiscard]] consteval bool every_form_refused_(std::index_sequence<Index...>) noexcept {
    return (!live_rules<at::dispatch::indirect_call<std::tuple_element_t<Index, Forms>>>::D001_ok && ...);
}
template <class Forms, std::size_t... Index>
[[nodiscard]] consteval bool every_form_admitted_(std::index_sequence<Index...>) noexcept {
    return (live_rules<at::dispatch::indirect_call<std::tuple_element_t<Index, Forms>>>::D001_ok && ...);
}
static_assert(every_form_refused_<throwing_forms>(std::make_index_sequence<std::tuple_size_v<throwing_forms>>{}));
static_assert(every_form_admitted_<noexcept_forms>(std::make_index_sequence<std::tuple_size_v<noexcept_forms>>{}));

// A family that names no function type reads as one that throws: a tag
// class, a pointer to a function pointer, a pointer to a data member, a
// tag class whose `signature` is not a function type, and a tag class
// whose `signature` names itself.
struct signature_is_data final {
    using signature = int;
};
struct signature_is_itself final {
    using signature = signature_is_itself;
};
static_assert(!live_rules<at::dispatch::indirect_call<attack_owner>>::D001_ok);
static_assert(!live_rules<at::dispatch::indirect_call<void (**)() noexcept>>::D001_ok);
static_assert(!live_rules<at::dispatch::indirect_call<int attack_owner::*>>::D001_ok);
static_assert(!live_rules<at::dispatch::indirect_call<signature_is_data>>::D001_ok);
static_assert(!live_rules<at::dispatch::indirect_call<signature_is_itself>>::D001_ok);

// ── the ledger ──────────────────────────────────────────────────────
//
// Each entry pins a wrong answer with its reproducer.  The ledger can only
// shrink: a repair flips the assertion, and the entry leaves in the same
// edit as the repair.  It is empty: every attack above gets the answer the
// rule states.

// A static_assert proves the constant-evaluated path only.
[[nodiscard]] int check_runtime_paths() {
    if (col::pending_axis_count != axes_without_an_atom()) return 1;
    if (col::pending_rule_count != 0) return 2;
    if (col::live_rule_count != 43) return 3;

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
    std::size_t retired = 0;
    for (const col::corpus_entry& entry : col::rule_corpus) {
        if (entry.code.empty() || entry.note.empty()) return 7;
        switch (entry.disposition) {
            case col::Disposition::Live: ++live; break;
            case col::Disposition::Pending: ++pending; break;
            case col::Disposition::Absent: ++absent; break;
            case col::Disposition::Retired: ++retired; break;
            default: return 8;
        }
    }
    if (live != 43 || pending != 0 || absent != 6 || retired != 8) return 9;
    if (live + pending + absent + retired != col::rule_corpus_size) return 10;
    if (col::rule_corpus_size != 57) return 11;

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
