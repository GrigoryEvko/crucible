// Sentinel TU for fixy/Collision.h: every live rule admits the empty
// pack, refuses its own pair, and admits each half of that pair; the
// pending roster is exactly the axes no atom reaches; and the rules
// reach fn's gate.
//
// The cells read the colliding packs through collision::pack_verdict_v and
// collision::grades, and not through fn.  Naming fn with a colliding
// pack is a build failure by design, so a rule that could only be
// observed that way could not be tested at all — only the whole TU
// failing would show it, which says nothing about which rule fired.

#include <fixy/Bands.h>
#include <fixy/Collision.h>
#include <fixy/collision/Codes.h>
#include <fixy/Fn.h>
#include <fixy/concurrent/Spawn.h>
#include <fixy/Secret.h>
#include <fixy/atoms/Os.h>
#include <fixy/atoms/Syscall.h>

#include <foundation/core/Choice.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <meta>
#include <tuple>
#include <type_traits>
#include <utility>

// The namespace has a name.  The tags below are arguments of atoms, and an
// atom whose argument has internal linkage has no stable identity, so the
// gate refuses it.
namespace collision_test {

namespace col = ::fixy::collision;
namespace at = ::fixy::atom;
namespace trust_level = ::fixy::tags::trust;
using ::fixy::Axis;
using ::fixy::strict_pole_t;
using col::grades;
using col::pack_verdict_v;
using col::binding_verdict_v;
using enum ::fixy::collision::RuleCode;
using Eff = ::foundation::effects::Effect;
namespace fe = ::foundation::effects;

// The test brings its own tag: the sample tag the family roster uses
// lives in a detail namespace and is not the test's to name.
struct tls_tag final {};

// H002 asks a hot binding to carry a refinement witness, and
// atom::refined_with is parametric on the predicate that was proved.  The
// test brings its own for the same reason as tls_tag: which predicate a
// hot body needs is the caller's business, and H002 only asks that there
// IS one.  The predicate has a call on an int, so a binding over an int
// admits it, and its mint checks it.
struct hot_invariant final {
    constexpr bool operator()(std::same_as<int> auto count) const noexcept { return count >= 0; }
};

// ---------------------------------------------------------------------
// grades answers without fn.

static_assert(std::is_same_v<grades<>::on<Axis::Usage>, strict_pole_t<Axis::Usage>>);
static_assert(std::is_same_v<grades<at::copy>::on<Axis::Usage>, at::copy>);
static_assert(std::is_same_v<grades<at::copy>::on<Axis::Effect>, strict_pole_t<Axis::Effect>>);
static_assert(grades<at::copy>::mentions<Axis::Usage>);
static_assert(!grades<at::copy>::mentions<Axis::Effect>);

// The same answer fn gives, which is the point of keeping them separate
// rather than the point of keeping them different.
static_assert(std::is_same_v<grades<at::copy>::on<Axis::Usage>, ::fixy::fn<int, at::copy>::grade_on<Axis::Usage>>);
static_assert(std::is_same_v<grades<>::on<Axis::Mutation>, ::fixy::fn<int>::grade_on<Axis::Mutation>>);

// ---------------------------------------------------------------------
// Every live rule admits the strictest binding.

static_assert(pack_verdict_v<>.is_valid());
static_assert(pack_verdict_v<>.failing_codes().empty());
static_assert(pack_verdict_v<>.admits(L002) && pack_verdict_v<>.admits(M012) && pack_verdict_v<>.admits(P010)
              && pack_verdict_v<>.admits(L007) && pack_verdict_v<>.admits(T001) && pack_verdict_v<>.admits(R002)
              && pack_verdict_v<>.admits(R003) && pack_verdict_v<>.admits(L006) && pack_verdict_v<>.admits(G002)
              && pack_verdict_v<>.admits(D002) && pack_verdict_v<>.admits(P002));

// ---------------------------------------------------------------------
// Each rule refuses its pair and admits each half.  Both directions per
// rule, because a rule that only ever says yes proves nothing.

// L002 borrow x async
static_assert(!pack_verdict_v<at::borrow, at::coroutine>.admits(L002));
static_assert(!pack_verdict_v<at::borrow, at::with<Eff::Bg>>.admits(L002));
static_assert(pack_verdict_v<at::borrow>.admits(L002));
static_assert(pack_verdict_v<at::coroutine>.admits(L002));

// M012 monotonic x concurrent without an atomic representation
static_assert(!pack_verdict_v<at::mut_monotonic, at::coroutine>.admits(M012));
static_assert(pack_verdict_v<at::mut_monotonic>.admits(M012));
static_assert(pack_verdict_v<at::mut_monotonic, at::coroutine, at::repr<::fixy::pole::ReprKind::Atomic>>.admits(M012));
static_assert(!pack_verdict_v<at::mut_monotonic, at::coroutine, at::repr<::fixy::pole::ReprKind::C>>.admits(M012));

// P010 ghost x an effect that must emit code
static_assert(!pack_verdict_v<at::ghost, at::with<Eff::Alloc>>.admits(P010));
static_assert(!pack_verdict_v<at::ghost, at::with<Eff::IO>>.admits(P010));
static_assert(!pack_verdict_v<at::ghost, at::with<Eff::Block>>.admits(P010));
static_assert(pack_verdict_v<at::ghost>.admits(P010));
static_assert(pack_verdict_v<at::ghost, at::with<Eff::Test>>.admits(P010), "Test is not an emitted-code effect");

// P010 reads the row of the binding, so an atom that lifts an observable
// effect trips it as a stated with<> does.  A kernel wait lifts Block, a
// futex call lifts Block and a read of process state lifts IO.  A call
// through the vDSO lifts the empty row, so P010 admits it and P002
// refuses it.
static_assert(!pack_verdict_v<at::ghost, at::sync::park>.admits(P010), "a park is emitted code");
static_assert(pack_verdict_v<at::ghost, at::sync::park>.failing_codes() == "P010",
              "P002 reads the Stdio and SyscallSurface axes, and a park engages neither");
static_assert(!pack_verdict_v<at::ghost, at::syscall::per<at::syscall::SyscallId::futex>>.admits(P010));
static_assert(!pack_verdict_v<at::ghost, at::syscall::per<at::syscall::SyscallId::getpid>>.admits(P010));
static_assert(!pack_verdict_v<at::ghost, at::sync::spin_pause>.admits(P010),
              "a spin with no exit lifts Div, and a drop of it changes whether the program ends");
static_assert(pack_verdict_v<at::ghost, at::sync::bounded_spin>.admits(P010),
              "a bounded spin lifts Nondet, an atom of input that elision can drop");
static_assert(pack_verdict_v<at::ghost, at::syscall::per<at::syscall::SyscallId::clock_gettime>>.admits(P010)
              && !pack_verdict_v<at::ghost, at::syscall::per<at::syscall::SyscallId::clock_gettime>>.admits(P002));
static_assert(pack_verdict_v<at::sync::park>.admits(P010), "not ghost, so a park is ordinary");

// L007 borrow x Bg row
static_assert(!pack_verdict_v<at::borrow, at::with<Eff::Bg>>.admits(L007));
static_assert(pack_verdict_v<at::borrow>.admits(L007));
static_assert(pack_verdict_v<at::with<Eff::Bg>>.admits(L007));

// T001 capability x unverified provenance.  The strict Trust pole is
// unverified, so the rule refuses a capability with no trust grade.
static_assert(!pack_verdict_v<at::capability_usage, at::trust<trust_level::Unverified>>.admits(T001));
static_assert(!pack_verdict_v<at::capability_usage>.admits(T001));
static_assert(pack_verdict_v<at::trust<trust_level::Unverified>>.admits(T001));
static_assert(pack_verdict_v<at::capability_usage, at::trust<trust_level::Verified>>.admits(T001));
static_assert(pack_verdict_v<at::capability_usage, at::trust<trust_level::Tested>>.admits(T001));
static_assert(pack_verdict_v<at::capability_usage, at::trust<trust_level::External>>.admits(T001));

// R002 / R003 coroutine x borrow, coroutine x Bg row
static_assert(!pack_verdict_v<at::coroutine, at::borrow>.admits(R002));
static_assert(pack_verdict_v<at::coroutine>.admits(R002));
static_assert(!pack_verdict_v<at::coroutine, at::with<Eff::Bg>>.admits(R003));
static_assert(pack_verdict_v<at::coroutine>.admits(R003));

// G002 thread-local x atomic representation x no reader on another thread.
// A per-thread counter that a monitor thread reads states the scope that
// its stores reach, and the rule admits it.  The scope thread reaches no
// other thread, so the rule still refuses it.
using AtomicRepr = at::repr<::fixy::pole::ReprKind::Atomic>;
static_assert(!pack_verdict_v<at::global::thread_local_<tls_tag>, AtomicRepr>.admits(G002));
static_assert(!pack_verdict_v<at::global::thread_local_<tls_tag>, AtomicRepr, at::scope::thread>.admits(G002));
static_assert(pack_verdict_v<at::global::thread_local_<tls_tag>, AtomicRepr, at::scope::inner>.admits(G002),
              "a per-thread counter that a monitor thread reads is observed");
static_assert(::fixy::IsAccepted<int, at::global::thread_local_<tls_tag>, AtomicRepr, at::scope::inner>);
// The scope also meets V401 and V402.  The host shareability scopes are the
// ARM trunk, so an x86 binding states the system scope, and the system
// scope reaches past a cluster, so it states a fence at acq_rel.
static_assert(
    !pack_verdict_v<at::global::thread_local_<tls_tag>, AtomicRepr, at::scope::inner, at::simd::sse2>.admits(V402));
static_assert(!pack_verdict_v<at::global::thread_local_<tls_tag>, AtomicRepr, at::scope::system>.admits(V401));
static_assert(::fixy::IsAccepted<int, at::global::thread_local_<tls_tag>, AtomicRepr, at::scope::system,
                                 at::barrier::acq_rel, at::simd::sse2>,
              "the remedy of G002 holds for a binding that pins an x86 ISA");
static_assert(pack_verdict_v<at::global::thread_local_<tls_tag>>.admits(G002));
static_assert(pack_verdict_v<AtomicRepr>.admits(G002));

// D002 unbounded recursion x an unbounded or unstated cost.  A proven depth
// bounds the stack, so the rule admits it with each cost.
static_assert(!pack_verdict_v<at::dispatch::recurses_unbounded, at::cost_unbounded>.admits(D002));
static_assert(!pack_verdict_v<at::dispatch::recurses_unbounded>.admits(D002), "an unstated cost bounds nothing");
static_assert(pack_verdict_v<at::dispatch::recurses_unbounded, at::cost_linear<4>>.admits(D002));
static_assert(pack_verdict_v<at::dispatch::recurses<64>, at::cost_unbounded>.admits(D002),
              "a bounded recursion has a bounded stack");
static_assert(pack_verdict_v<at::dispatch::recurses<0>>.admits(D002));
static_assert(pack_verdict_v<at::cost_unbounded>.admits(D002));
static_assert(pack_verdict_v<at::dispatch::recurses<0>, at::cost_linear<4>>.admits(D002));

// P002 ghost x an emitting surface.  A call through the vDSO lifts the
// empty row, so P010 admits the pair and P002 refuses it, which is why
// both exist.  A stdio write lifts IO and Block, so both rules refuse a
// ghost write.
static_assert(!pack_verdict_v<at::ghost, at::stdio::write<at::stdio::streams::Stdout>>.admits(P002));
static_assert(!pack_verdict_v<at::ghost, at::stdio::write<at::stdio::streams::Stderr>>.admits(P002));
static_assert(!pack_verdict_v<at::ghost, at::stdio::write<at::stdio::streams::Stdout>>.admits(P010),
              "a stdio write lifts IO and Block, so P010 sees it in the row of the binding");
static_assert(pack_verdict_v<at::ghost, at::syscall::per<at::syscall::SyscallId::clock_gettime>>.failing_codes()
                  == "P002",
              "a call through the vDSO lifts the empty row, so P002 alone refuses a ghost call");
static_assert(pack_verdict_v<at::ghost>.admits(P002));
static_assert(pack_verdict_v<at::stdio::write<at::stdio::streams::Stdout>>.admits(P002));
static_assert(pack_verdict_v<at::copy, at::stdio::write<at::stdio::streams::Stdout>>.admits(P002),
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
static_assert(!pack_verdict_v<at::regime::hot>.admits(H001), "hot with no Complexity grade is an unstated envelope");
static_assert(!pack_verdict_v<at::regime::hot, at::cost_unbounded>.admits(H001));
static_assert(pack_verdict_v<at::regime::hot, at::cost_constant>.admits(H001));
static_assert(pack_verdict_v<at::regime::hot, at::cost_linear<8>>.admits(H001));
static_assert(pack_verdict_v<at::cost_unbounded>.admits(H001),
              "an unbounded cost off the hot path is not H001's business");
static_assert(pack_verdict_v<at::regime::warm>.admits(H001), "H001 reads hot, not any regime grade");
static_assert(pack_verdict_v<at::regime::cold>.admits(H001));

// H002 hot x no refinement witness
static_assert(!pack_verdict_v<at::regime::hot>.admits(H002));
static_assert(pack_verdict_v<at::regime::hot, at::refined_with<hot_invariant>>.admits(H002));
static_assert(pack_verdict_v<at::refined_with<hot_invariant>>.admits(H002));
static_assert(pack_verdict_v<at::regime::warm>.admits(H002));

// H003 hot x Alloc or IO in the row of the binding.  The cost is not a
// premise: a bounded allocation on the hot path is refused as an
// unbounded one is.  The cost and refinement atoms silence H001 and
// H002, so each pack below trips H003 alone.
template <class... Extra>
constexpr const ::fixy::collision::binding_verdict& hot_bounded =
    pack_verdict_v<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>, Extra...>;
static_assert(hot_bounded<at::with<Eff::Alloc>>.failing_codes() == "H003");
static_assert(hot_bounded<at::with<Eff::IO>>.failing_codes() == "H003");
static_assert(hot_bounded<at::syscall::per<at::syscall::SyscallId::getpid>>.failing_codes() == "H003",
              "a read of process state lifts IO, although it cannot block");
static_assert(hot_bounded<at::syscall::per<at::syscall::SyscallId::write>>.failing_codes() == "H003, W001",
              "a file write lifts IO and Block, so H003 and W001 each refuse it");
static_assert(!pack_verdict_v<at::regime::hot, at::with<Eff::Alloc>, at::cost_unbounded>.admits(H003));
static_assert(!pack_verdict_v<at::regime::hot, at::with<Eff::IO>, at::cost_unbounded>.admits(H003));
static_assert(pack_verdict_v<at::regime::hot, at::cost_unbounded>.admits(H003), "no Alloc or IO row, so H001 not H003");
static_assert(pack_verdict_v<at::with<Eff::Alloc>, at::cost_unbounded>.admits(H003), "not hot, so no contradiction");
// A read of the clock through the vDSO lifts the empty row, and a spin
// lifts the empty row too, so H003 admits both on the hot path.
static_assert(hot_bounded<at::syscall::per<at::syscall::SyscallId::clock_gettime>>.is_valid());
static_assert(hot_bounded<at::sync::spin_pause>.is_valid());
// The positive controls: a warm or a cold binding with the same rows.
static_assert(pack_verdict_v<at::regime::warm, at::cost_constant, at::with<Eff::Alloc>>.is_valid());
static_assert(pack_verdict_v<at::regime::warm, at::cost_constant, at::with<Eff::IO>>.is_valid());
static_assert(pack_verdict_v<at::regime::warm, at::syscall::per<at::syscall::SyscallId::getpid>>.is_valid());
static_assert(pack_verdict_v<at::regime::cold, at::with<Eff::Alloc, Eff::IO>>.is_valid());
// Block is deliberately outside H003: a blocking hot path is W001's
// theorem, which cites the futex cost rather than the allocator's.  The
// cell is here so the boundary is a decision on the record.
static_assert(pack_verdict_v<at::regime::hot, at::with<Eff::Block>, at::cost_unbounded>.admits(H003),
              "H003's theorem names Alloc and IO; Block on a hot path belongs to W001");
static_assert(!pack_verdict_v<at::regime::hot, at::with<Eff::Block>, at::cost_unbounded>.admits(W001),
              "the other side of the boundary: W001 refuses the same pack");

// H010 hot x Row<Bg>.  The cost_constant and refined_with atoms are what
// make this cell prove H010 rather than H001 or H002 in disguise.
static_assert(
    !pack_verdict_v<at::regime::hot, at::with<Eff::Bg>, at::cost_constant, at::refined_with<hot_invariant>>.admits(
        H010));
static_assert(pack_verdict_v<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>>.admits(H010));
static_assert(pack_verdict_v<at::with<Eff::Bg>>.admits(H010));
static_assert(pack_verdict_v<at::regime::cold, at::with<Eff::Bg>>.admits(H010), "a cold background body is ordinary");

// R001 coroutine x hot
static_assert(!pack_verdict_v<at::coroutine, at::regime::hot>.admits(R001));
static_assert(pack_verdict_v<at::coroutine>.admits(R001));
static_assert(pack_verdict_v<at::regime::hot>.admits(R001));
static_assert(pack_verdict_v<at::coroutine, at::regime::cold>.admits(R001));

// S001 stdio x hot
static_assert(!pack_verdict_v<at::stdio::write<at::stdio::streams::Stdout>, at::regime::hot>.admits(S001));
static_assert(pack_verdict_v<at::stdio::write<at::stdio::streams::Stdout>>.admits(S001));
static_assert(pack_verdict_v<at::regime::hot>.admits(S001));
static_assert(pack_verdict_v<at::stdio::write<at::stdio::streams::Stdout>, at::regime::warm>.admits(S001));
// A stdio write lifts IO and Block, so a hot write trips H003 and W001
// beside S001.  A warm write trips none of the three.
static_assert(hot_bounded<at::stdio::write<at::stdio::streams::Stderr>>.failing_codes() == "H003, S001, W001");
static_assert(
    pack_verdict_v<at::stdio::write<at::stdio::streams::Stderr>, at::regime::warm, at::cost_constant>.is_valid());
static_assert(std::is_same_v<
              ::fixy::atom::binding_row_of_t<grades<at::stdio::write<at::stdio::streams::Stderr>>::on<Axis::Effect>,
                                             at::stdio::write<at::stdio::streams::Stderr>>,
              fe::Row<Eff::IO, Eff::Block>>);

// The whole family stands down for a binding that claims no tier, which
// is what keeps the axis's Unconstrained strict pole honest: most of the
// tree is neither hot nor cold in any sense worth typing.
static_assert(pack_verdict_v<>.admits(H001) && pack_verdict_v<>.admits(H002) && pack_verdict_v<>.admits(H003)
              && pack_verdict_v<>.admits(H010) && pack_verdict_v<>.admits(R001) && pack_verdict_v<>.admits(S001));

// A hot binding that answers every rule is accepted, so the family is a
// gate and not a ban on the hot path.
static_assert(pack_verdict_v<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>>.is_valid());

// ---------------------------------------------------------------------
// The wait family, which reads fixy/atoms/Sync.h.
//
// Both rules read one axis from opposite ends of its ladder, so the
// interesting cells are the ones that show the LINE, not just the ends.

// W001 hot x a kernel wait.  The three lowest grades trip it and the
// three highest do not, which is the WaitLattice's own division.
static_assert(
    !pack_verdict_v<at::regime::hot, at::sync::block, at::cost_constant, at::refined_with<hot_invariant>>.admits(W001));
static_assert(
    !pack_verdict_v<at::regime::hot, at::sync::park, at::cost_constant, at::refined_with<hot_invariant>>.admits(W001));
static_assert(
    !pack_verdict_v<at::regime::hot, at::sync::acquire_wait, at::cost_constant, at::refined_with<hot_invariant>>.admits(
        W001));
static_assert(
    pack_verdict_v<at::regime::hot, at::sync::umwait_c01, at::cost_constant, at::refined_with<hot_invariant>>.admits(
        W001));
static_assert(
    pack_verdict_v<at::regime::hot, at::sync::bounded_spin, at::cost_constant, at::refined_with<hot_invariant>>.admits(
        W001));
static_assert(
    pack_verdict_v<at::regime::hot, at::sync::spin_pause, at::cost_constant, at::refined_with<hot_invariant>>.admits(
        W001));
// Each half alone is fine: a cold body may block, and a hot body may wait
// however it likes as long as it stays out of the kernel.
static_assert(pack_verdict_v<at::sync::block>.admits(W001));
static_assert(pack_verdict_v<at::regime::cold, at::sync::block>.admits(W001));
static_assert(pack_verdict_v<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>>.admits(W001));

// W001 reads the row that the whole pack lifts to, so a kernel wait is
// one way to trip it and not the only one.  A stated Block and a system
// call that can park the caller put Block in the same row.  The cost and
// refinement atoms silence H001 and H002 in each cell.
template <class... Extra>
constexpr const ::fixy::collision::binding_verdict& hot_pack =
    pack_verdict_v<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>, Extra...>;
using SyscallId = at::syscall::SyscallId;
using SyscallFamily = at::syscall::SyscallFamily;

static_assert(!hot_pack<at::with<Eff::Block>>.admits(W001));
static_assert(!hot_pack<at::with<Eff::IO, Eff::Block>>.admits(W001));
static_assert(!hot_pack<at::syscall::per<SyscallId::futex>>.admits(W001));
static_assert(!hot_pack<at::syscall::per<SyscallId::sched_yield>>.admits(W001), "a yield enters the scheduler");
static_assert(!hot_pack<at::syscall::per<SyscallId::write>>.admits(W001));
static_assert(!hot_pack<at::syscall::per<SyscallId::recvmsg>>.admits(W001));
static_assert(!hot_pack<at::syscall::family<SyscallFamily::ThreadSync>>.admits(W001));
static_assert(!hot_pack<at::fs::durable<::fixy::fs::sync_op::Fsync>>.admits(W001), "an OS atom lifts Block as well");

// The waits on a descriptor, the sleeps and eventfd are ThreadSync calls,
// so each lifts Block and W001 refuses each alone on the hot path.  A warm
// binding with the same call is admitted.
static_assert(hot_pack<at::syscall::per<SyscallId::poll>>.failing_codes() == "W001");
static_assert(hot_pack<at::syscall::per<SyscallId::epoll_wait>>.failing_codes() == "W001");
static_assert(hot_pack<at::syscall::per<SyscallId::eventfd>>.failing_codes() == "W001");
static_assert(hot_pack<at::syscall::per<SyscallId::nanosleep>>.failing_codes() == "W001");
static_assert(hot_pack<at::syscall::per<SyscallId::clock_nanosleep>>.failing_codes() == "W001");
static_assert(pack_verdict_v<at::regime::warm, at::syscall::per<SyscallId::epoll_wait>>.is_valid());
static_assert(pack_verdict_v<at::regime::cold, at::syscall::per<SyscallId::clock_nanosleep>>.is_valid());

// The two fixtures' packs trip W001 and no other rule.
static_assert(hot_pack<at::with<Eff::Block>>.failing_codes() == "W001");
static_assert(hot_pack<at::syscall::per<SyscallId::futex>>.failing_codes() == "W001");
static_assert(hot_pack<at::sync::acquire_wait>.failing_codes() == "W001", "and the kernel wait still does");

// A call that cannot park the caller puts no Block in the row.
static_assert(hot_pack<at::syscall::per<SyscallId::clock_gettime>>.is_valid(), "a vDSO read enters no kernel");
static_assert(hot_pack<at::syscall::family<SyscallFamily::VdsoOnly>>.is_valid());
static_assert(hot_pack<at::syscall::per<SyscallId::getpid>>.admits(W001), "a read of process state does not park");
static_assert(hot_pack<at::with<Eff::IO>>.admits(W001), "IO without Block is H003's premise, not W001's");

// The lifted row is the union of every lift in the pack.  A spin and
// UMWAIT lift to the empty row.
static_assert(
    std::is_same_v<::fixy::atom::lifted_row_of_t<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>,
                                                 at::sync::park, at::with<Eff::IO>>,
                   fe::Row<Eff::IO, Eff::Block>>);
static_assert(std::is_same_v<::fixy::atom::lifted_row_of_t<at::syscall::per<SyscallId::futex>, at::with<Eff::Alloc>>,
                             fe::Row<Eff::Alloc, Eff::Block, Eff::Nondet>>);
static_assert(std::is_same_v<::fixy::atom::lifted_row_of_t<>, fe::Row<>>);
static_assert(!pack_verdict_v<at::sync::spin_pause>.premises.blocks
              && !pack_verdict_v<at::sync::umwait_c01>.premises.blocks);

// The row of the binding joins the Effect grade with the lifted row.  It
// is the row every rule reads, and it is the row fixy::binding_row_t
// gives for context admission, so a rule and a context gate read one row.
static_assert(
    std::is_same_v<::fixy::atom::binding_row_of_t<grades<at::syscall::per<SyscallId::futex>>::on<Axis::Effect>,
                                                  at::syscall::per<SyscallId::futex>>,
                   fe::Row<Eff::Block, Eff::Nondet>>);
static_assert(
    std::is_same_v<::fixy::atom::effect_row_of_t<grades<at::syscall::per<SyscallId::futex>>::on<Axis::Effect>>,
                   fe::Row<>>,
    "the Effect grade stays at the strict pole");
static_assert(std::is_same_v<::fixy::atom::binding_row_of_t<grades<at::with<Eff::IO>, at::sync::park>::on<Axis::Effect>,
                                                            at::with<Eff::IO>, at::sync::park>,
                             fe::Row<Eff::IO, Eff::Block>>);
static_assert(std::is_same_v<::fixy::atom::binding_row_of_t<grades<>::on<Axis::Effect>>, fe::Row<>>);
static_assert(
    std::is_same_v<::fixy::atom::binding_row_of_t<grades<at::syscall::per<SyscallId::futex>>::on<Axis::Effect>,
                                                  at::syscall::per<SyscallId::futex>>,
                   ::fixy::binding_row_t<::fixy::fn<int, at::syscall::per<SyscallId::futex>>>>);

// The positive controls: the rules admit a warm or a cold binding with
// the same row, and a binding that states no tier.
static_assert(pack_verdict_v<at::regime::warm, at::with<Eff::Block>>.is_valid());
static_assert(pack_verdict_v<at::regime::cold, at::syscall::per<SyscallId::futex>>.is_valid());
static_assert(pack_verdict_v<at::regime::warm, at::syscall::per<SyscallId::sched_yield>>.is_valid());
static_assert(pack_verdict_v<at::with<Eff::Block>>.is_valid());
static_assert(pack_verdict_v<at::syscall::per<SyscallId::futex>>.is_valid());

// W002 Row<Bg> x a spin that burns the core.  This is narrower than "not
// a kernel wait" by exactly one grade, and UMWAIT is that grade: it halts
// the core in C0.1 rather than spinning it, so a background body may use
// it.  These three cells are the whole reason fixy/atoms/Sync.h carries
// two predicates rather than one negating the other.
static_assert(!pack_verdict_v<at::with<Eff::Bg>, at::sync::spin_pause>.admits(W002));
static_assert(!pack_verdict_v<at::with<Eff::Bg>, at::sync::bounded_spin>.admits(W002));
static_assert(pack_verdict_v<at::with<Eff::Bg>, at::sync::umwait_c01>.admits(W002),
              "UMWAIT halts the core instead of burning it, so a background body may wait that way");
static_assert(pack_verdict_v<at::with<Eff::Bg>, at::sync::park>.admits(W002));
static_assert(pack_verdict_v<at::sync::spin_pause>.admits(W002), "a foreground spin is the intended hot-path wait");

// The lift, read through the same atoms.  A kernel wait carries
// Effect::Block into the row a context has to admit.  A spin never carries
// Block: a spin with a give-up carries Nondet, and a spin with no exit
// carries Div.  This is what makes the two rules and the context gates
// agree rather than merely coexist.
static_assert(std::is_same_v<fe::lift_row_t<at::sync::block>, fe::Row<Eff::Block>>);
static_assert(std::is_same_v<fe::lift_row_t<at::sync::acquire_wait>, fe::Row<Eff::Block>>);
static_assert(std::is_same_v<fe::lift_row_t<at::sync::spin_pause>, fe::Row<Eff::Div>>);
static_assert(std::is_same_v<fe::lift_row_t<at::sync::umwait_c01>, fe::Row<Eff::Nondet>>);

// Neither rule fires on a binding that names no strategy, which is what
// keeps the Synchronization strict pole from meaning "spins".
static_assert(pack_verdict_v<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>>.admits(W001)
              && pack_verdict_v<at::with<Eff::Bg>>.admits(W002));

// ---------------------------------------------------------------------
// The observability family, which reads fixy/atoms/Observe.h.
//
// Two theorems on one axis, and the cells below show they are two: each
// pack trips one and not the other.

// B002, the containment.  An observability row must be a Subrow of the
// binding's effect row, because Observability names which PART of that row
// is observation.  It is not a second row and cannot widen the first.
static_assert(!pack_verdict_v<at::observe::surface<Eff::IO>>.admits(B002),
              "a surface naming IO on a binding whose effect row is empty claims an effect it never declared");
static_assert(pack_verdict_v<at::observe::surface<Eff::IO>, at::with<Eff::IO>>.admits(B002),
              "the same surface is admitted once the binding declares IO");
static_assert(pack_verdict_v<at::observe::surface<Eff::IO>, at::with<Eff::IO, Eff::Bg>>.admits(B002),
              "a proper subset is containment too");
static_assert(!pack_verdict_v<at::observe::surface<Eff::IO, Eff::Bg>, at::with<Eff::IO>>.admits(B002),
              "one effect of the surface is outside the row, which is enough");
static_assert(pack_verdict_v<at::observe::surface<>>.admits(B002),
              "the empty surface observes nothing and is contained");
// The row that contains the surface is the row of the binding, so an
// effect that an atom lifts is an effect the binding declared.
static_assert(pack_verdict_v<at::observe::surface<Eff::IO>, at::syscall::per<SyscallId::getpid>>.admits(B002),
              "a read of process state lifts IO, so the surface names an effect the binding performs");
static_assert(!pack_verdict_v<at::observe::surface<Eff::IO>, at::syscall::per<SyscallId::futex>>.admits(B002),
              "a futex call lifts Block and not IO");
static_assert(pack_verdict_v<at::with<Eff::IO>>.admits(B002), "a binding with no surface observes nothing");
static_assert(pack_verdict_v<>.admits(B002));

// The strict pole on this axis is derived from Effect's, which is the
// empty row, so "observes nothing" is what a binding claims by saying
// nothing.  These two cells are that pole read from both sides.
static_assert(!col::grades<>::mentions<Axis::Observability>);
static_assert(col::grades<at::observe::surface<Eff::IO>>::mentions<Axis::Observability>);

// B001, the back-pressure trap, which is the theorem this catalog already
// recorded for the axis.  It is a different premise from B002's: a
// background observable surface whose resource use is unbounded, where the
// remedy the theorem names is space_bounded plus cost_linear.
static_assert(!pack_verdict_v<at::with<Eff::Bg>, at::observe::surface<Eff::Bg>>.admits(B001),
              "no cost grade at all is an unstated envelope, which is one of the four unbounded readings");
static_assert(!pack_verdict_v<at::with<Eff::Bg>, at::observe::surface<Eff::Bg>, at::cost_linear<8>>.admits(B001),
              "a stated cost with no space grade states no bound on what the surface holds");
static_assert(!pack_verdict_v<at::with<Eff::Bg>, at::observe::surface<Eff::Bg>, at::cost_unbounded>.admits(B001));
static_assert(
    !pack_verdict_v<at::with<Eff::Bg>, at::observe::surface<Eff::Bg>, at::cost_linear<8>, at::space_unbounded>.admits(
        B001));
static_assert(
    pack_verdict_v<at::with<Eff::Bg>, at::observe::surface<Eff::Bg>, at::cost_linear<8>, at::space_bounded<4096>>.admits(
        B001),
    "the remedy the theorem names: a bounded space and a linear cost");
// Each premise alone stands down.
static_assert(pack_verdict_v<at::with<Eff::Bg>, at::cost_unbounded>.admits(B001),
              "no surface, so nothing is observable");
static_assert(pack_verdict_v<at::observe::surface<Eff::IO>, at::with<Eff::IO>, at::cost_unbounded>.admits(B001),
              "an unbounded foreground surface is not a back-pressure trap: the caller is the consumer");

// The two theorems are independent, which is why both codes exist. The
// first pack trips B002 and not B001; the second trips B001 and not B002.
static_assert(!pack_verdict_v<at::observe::surface<Eff::IO>>.admits(B002)
              && pack_verdict_v<at::observe::surface<Eff::IO>>.admits(B001));
static_assert(pack_verdict_v<at::with<Eff::Bg>, at::observe::surface<Eff::Bg>>.admits(B002)
              && !pack_verdict_v<at::with<Eff::Bg>, at::observe::surface<Eff::Bg>>.admits(B001));

// A surface atom declares no lift, so it contributes nothing to the row a
// context must admit.  That is the whole point of the containment: the
// Effect grade stays the single authority for what the binding may do.
static_assert(!fe::LiftsToRow<at::observe::surface<Eff::IO>>);

// ---------------------------------------------------------------------
// The payload read, which the FpMode, SimdIsa and HwInstruction rules
// pair with.
//
// Four rules pair a grade with the payload's replay claim, and the claim
// is the DetSafe band the payload carries.
// binding_verdict_v<Payload, Atoms...> reads it.  pack_verdict_v<Atoms...>
// is the pack-only view, with void for the payload.  These cells are the
// witness that the read exists and reads the right thing, ahead of any
// rule consuming it: a structural change with nothing observable is not
// accepted as tested.
using DetTier = ::fixy::DetSafeTier_v;
template <DetTier Tier>
using det = ::fixy::DetSafe<Tier, int>;

// The two replay-deterministic tiers claim; the five below them do not.
static_assert(binding_verdict_v<det<DetTier::Pure>>.premises.replay_deterministic);
static_assert(binding_verdict_v<det<DetTier::PhiloxRng>>.premises.replay_deterministic);
static_assert(!binding_verdict_v<det<DetTier::MonotonicClockRead>>.premises.replay_deterministic,
              "a monotonic clock is bounded within one run and says nothing across runs");
static_assert(!binding_verdict_v<det<DetTier::WallClockRead>>.premises.replay_deterministic);
static_assert(!binding_verdict_v<det<DetTier::NonDeterministicSyscall>>.premises.replay_deterministic);

// A payload with no band claims nothing, and so does the pack-only view.
static_assert(!binding_verdict_v<int>.premises.replay_deterministic);
static_assert(!binding_verdict_v<void>.premises.replay_deterministic);
static_assert(!pack_verdict_v<>.premises.replay_deterministic, "pack_verdict_v is binding_verdict_v<void, ...>");
static_assert(&pack_verdict_v<at::copy> == &binding_verdict_v<void, at::copy>);

// The read alone refuses nothing, so a replay-deterministic payload with
// an empty pack is accepted.  Each rule that consumes the read pairs it
// with an atom of its own axis, and names this member.
static_assert(binding_verdict_v<det<DetTier::Pure>>.is_valid());

// ---------------------------------------------------------------------
// The hardware-instruction family, which reads fixy/atoms/Hw.h.
//
// The ladder is a chain where a tier admits every class below it, so the
// cells read it at the one boundary V201 and V203 care about — at or
// above NonDeterministicTsc — and at the top for V202.

static_assert(col::axis_has_an_atom<Axis::HwInstruction>);

// V201 hot x a tier at or above NonDeterministicTsc.  PrivilegedMsr is
// above the timestamp tier, so the one clause of V201 also covers the
// privileged tier.  The cost and refinement atoms silence H001
// and H002; an Init row silences V202 for the privileged cell.
static_assert(
    !pack_verdict_v<at::regime::hot, at::hw::non_deterministic_tsc, at::cost_constant, at::refined_with<hot_invariant>>.admits(
        V201));
static_assert(!pack_verdict_v<at::regime::hot, at::hw::privileged_msr, at::with<Eff::Init>, at::cost_constant,
                          at::refined_with<hot_invariant>>.admits(V201),
              "the privileged tier is at or above the timestamp tier, so the hot path refuses it too");
static_assert(
    pack_verdict_v<at::regime::hot, at::hw::vectorizable, at::cost_constant, at::refined_with<hot_invariant>>.admits(
        V201),
    "SIMD intrinsics sit below the timestamp tier and are what a hot path is made of");
static_assert(pack_verdict_v<at::hw::non_deterministic_tsc>.admits(V201), "not hot, so a timestamp read is ordinary");
static_assert(pack_verdict_v<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>>.admits(V201));

// V202 the PrivilegedMsr tier x an effect row with no Init.  The twin the
// fixture names: add Init to the row and the tier is admitted.
static_assert(!pack_verdict_v<at::hw::privileged_msr>.admits(V202));
static_assert(!pack_verdict_v<at::hw::privileged_msr, at::with<Eff::IO>>.admits(V202),
              "a row without Init is not enough");
static_assert(pack_verdict_v<at::hw::privileged_msr, at::with<Eff::Init>>.admits(V202), "the twin: reached from Init");
static_assert(pack_verdict_v<at::hw::privileged_msr, at::with<Eff::Init, Eff::IO>>.admits(V202));
static_assert(pack_verdict_v<at::hw::non_deterministic_tsc>.admits(V202), "only the privileged tier needs Init");

// V203 a replay-deterministic payload x a tier at or above
// NonDeterministicTsc.  This is the payload read consumed: the same pack
// is refused under a Pure payload and admitted under a payload with no
// band or under the pack-only view.
static_assert(!binding_verdict_v<det<DetTier::Pure>, at::hw::non_deterministic_tsc>.admits(V203));
static_assert(!binding_verdict_v<det<DetTier::PhiloxRng>, at::hw::privileged_msr, at::with<Eff::Init>>.admits(V203));
static_assert(binding_verdict_v<det<DetTier::WallClockRead>, at::hw::non_deterministic_tsc>.admits(V203),
              "a payload that already admits wall-clock reads claims nothing a timestamp could break");
static_assert(binding_verdict_v<int, at::hw::non_deterministic_tsc>.admits(V203), "no band, no claim");
static_assert(pack_verdict_v<at::hw::non_deterministic_tsc>.admits(V203), "the pack-only view cannot see a payload");
static_assert(binding_verdict_v<det<DetTier::Pure>, at::hw::vectorizable>.admits(V203), "below the timestamp tier");

// The three rules are three: each pack below trips exactly one of them.
static_assert(!pack_verdict_v<at::regime::hot, at::hw::non_deterministic_tsc, at::cost_constant,
                          at::refined_with<hot_invariant>>.admits(V201)
              && pack_verdict_v<at::regime::hot, at::hw::non_deterministic_tsc, at::cost_constant,
                            at::refined_with<hot_invariant>>.admits(V202));
static_assert(!pack_verdict_v<at::hw::privileged_msr>.admits(V202)
              && pack_verdict_v<at::hw::privileged_msr>.admits(V201));
static_assert(!binding_verdict_v<det<DetTier::Pure>, at::hw::non_deterministic_tsc>.admits(V203)
              && binding_verdict_v<det<DetTier::Pure>, at::hw::non_deterministic_tsc>.admits(V201));

// The read alone still refuses nothing: a Pure payload with a tier below
// the timestamp one is accepted through the bound view.
static_assert(binding_verdict_v<det<DetTier::Pure>, at::hw::vectorizable>.is_valid());

// ---------------------------------------------------------------------
// The barrier-strength family, which reads fixy/atoms/Barrier.h.
//
// One rule at one floor of the order.  The cost and refinement atoms
// silence H001 and H002 on every hot cell.

static_assert(col::axis_has_an_atom<Axis::BarrierStrength>);

// V301 hot x a fence at or above SeqCst.
static_assert(
    !pack_verdict_v<at::regime::hot, at::barrier::seq_cst, at::cost_constant, at::refined_with<hot_invariant>>.admits(
        V301));
static_assert(
    !pack_verdict_v<at::regime::hot, at::barrier::full_fence, at::cost_constant, at::refined_with<hot_invariant>>.admits(
        V301),
    "the standalone fence is above seq_cst on the ladder, so the hot path refuses it too");
static_assert(
    pack_verdict_v<at::regime::hot, at::barrier::acq_rel, at::cost_constant, at::refined_with<hot_invariant>>.admits(
        V301),
    "acq_rel is one MOV each way on x86 and is what a hot SPSC ring is made of");
static_assert(
    pack_verdict_v<at::regime::hot, at::barrier::release_store, at::cost_constant, at::refined_with<hot_invariant>>.admits(
        V301));
static_assert(
    pack_verdict_v<at::regime::hot, at::barrier::compiler_barrier, at::cost_constant, at::refined_with<hot_invariant>>.admits(
        V301),
    "a compiler barrier emits no instruction");
static_assert(pack_verdict_v<at::barrier::seq_cst>.admits(V301), "not hot, so a full fence is ordinary");
static_assert(pack_verdict_v<at::regime::warm, at::barrier::full_fence>.admits(V301));
static_assert(pack_verdict_v<at::regime::hot, at::cost_constant, at::refined_with<hot_invariant>>.admits(V301),
              "no strength named: the binding provides no fence, and there is nothing to refuse");
static_assert(pack_verdict_v<>.admits(V301));

// The fixture's pack trips V301 and nothing else: the two hot rules its
// cost and refinement atoms answer, the wait rule, and the hardware rule
// all stand down.
static_assert(
    pack_verdict_v<at::regime::hot, at::barrier::seq_cst, at::cost_constant, at::refined_with<hot_invariant>>.admits(
        H001)
    && pack_verdict_v<at::regime::hot, at::barrier::seq_cst, at::cost_constant, at::refined_with<hot_invariant>>.admits(
        H002)
    && pack_verdict_v<at::regime::hot, at::barrier::seq_cst, at::cost_constant, at::refined_with<hot_invariant>>.admits(
        W001)
    && pack_verdict_v<at::regime::hot, at::barrier::seq_cst, at::cost_constant, at::refined_with<hot_invariant>>.admits(
        V201));

// The two `tier` families do not answer for each other: a hardware tier
// is not a fence strength, and a fence strength is not a hardware tier.
static_assert(pack_verdict_v<at::regime::hot, at::hw::privileged_msr, at::with<Eff::Init>, at::cost_constant,
                         at::refined_with<hot_invariant>>.admits(V301),
              "the top of the hardware ladder is not a fence");
static_assert(pack_verdict_v<at::barrier::full_fence>.admits(V201)
                  && pack_verdict_v<at::barrier::full_fence>.admits(V202),
              "the top of the fence ladder is not a hardware tier");

// Acquire and release are incomparable, so neither tag satisfies a floor
// at the other.  No live rule reads a floor at either tag, so the change
// from a chain moves no verdict: V301 reads SeqCst and V401 reads AcqRel,
// and each tag sits below both floors in either order.
static_assert(!col::detail::HasBarrierAtOrAbove<at::barrier::release_store,
                                                ::foundation::algebra::lattices::BarrierStrength::AcquireLoad>);
static_assert(!col::detail::HasBarrierAtOrAbove<at::barrier::acquire_load,
                                                ::foundation::algebra::lattices::BarrierStrength::ReleaseStore>);
static_assert(col::detail::HasBarrierAtOrAbove<at::barrier::acq_rel,
                                               ::foundation::algebra::lattices::BarrierStrength::AcquireLoad>
                  && col::detail::HasBarrierAtOrAbove<at::barrier::acq_rel,
                                                      ::foundation::algebra::lattices::BarrierStrength::ReleaseStore>,
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
static_assert(!pack_verdict_v<at::scope::cluster, at::barrier::release_store>.admits(V401));
static_assert(!pack_verdict_v<at::scope::gpu, at::barrier::acquire_load>.admits(V401));
static_assert(!pack_verdict_v<at::scope::system, at::barrier::compiler_barrier>.admits(V401),
              "the shared top reaches everywhere, so it is at or above the floor");
static_assert(!pack_verdict_v<at::scope::cluster>.admits(V401),
              "no strength named provides no fence, which is the same trap with nothing said");
static_assert(pack_verdict_v<at::scope::cluster, at::barrier::acq_rel>.admits(V401), "the twin: acq_rel is the floor");
static_assert(pack_verdict_v<at::scope::gpu, at::barrier::seq_cst>.admits(V401));
static_assert(pack_verdict_v<at::scope::system, at::barrier::full_fence>.admits(V401));
static_assert(pack_verdict_v<at::scope::cta, at::barrier::release_store>.admits(V401), "below the cluster floor");
static_assert(pack_verdict_v<at::scope::warp, at::barrier::none>.admits(V401));
static_assert(pack_verdict_v<at::scope::thread>.admits(V401), "the shared bottom reaches nobody else");
static_assert(pack_verdict_v<at::scope::inner, at::barrier::release_store>.admits(V401),
              "Inner is incomparable with Cluster, so it is not at or above it and the rule stands down");
static_assert(pack_verdict_v<at::scope::outer>.admits(V401), "Outer is on the host trunk too");
static_assert(pack_verdict_v<at::barrier::none>.admits(V401), "no scope named publishes to nobody in particular");
static_assert(pack_verdict_v<>.admits(V401));

// V401 and V301 read the same strength axis and are two rules: the
// fixture's pack trips V401 alone, and a hot seq_cst publication to the
// whole system trips V301 alone.
static_assert(pack_verdict_v<at::scope::cluster, at::barrier::release_store>.admits(V301)
              && !pack_verdict_v<at::scope::cluster, at::barrier::release_store>.admits(V401));
static_assert(!pack_verdict_v<at::regime::hot, at::scope::system, at::barrier::seq_cst, at::cost_constant,
                          at::refined_with<hot_invariant>>.admits(V301)
              && pack_verdict_v<at::regime::hot, at::scope::system, at::barrier::seq_cst, at::cost_constant,
                            at::refined_with<hot_invariant>>.admits(V401));

// ---------------------------------------------------------------------
// The SIMD-ISA family, which reads fixy/atoms/Simd.h.
//
// Two trunks again, and two rules.  V101 reads the payload, so its cells
// use binding_verdict_v.  V402 reads two axes of the pack, so its cells use
// pack_verdict_v.

static_assert(col::axis_has_an_atom<Axis::SimdIsa>);

// V101 a replay-deterministic payload x an ISA pinned to one trunk.
static_assert(!binding_verdict_v<det<DetTier::Pure>, at::simd::avx2>.admits(V101));
static_assert(!binding_verdict_v<det<DetTier::Pure>, at::simd::sve2>.admits(V101), "either trunk pins a width");
static_assert(!binding_verdict_v<det<DetTier::PhiloxRng>, at::simd::avx512bw>.admits(V101));
static_assert(binding_verdict_v<det<DetTier::Pure>, at::simd::scalar>.admits(V101), "the shared bottom pins no trunk");
static_assert(binding_verdict_v<det<DetTier::Pure>, at::simd::portable>.admits(V101),
              "the shared top is one kernel for every set, so it pins no trunk either");
static_assert(binding_verdict_v<det<DetTier::WallClockRead>, at::simd::avx2>.admits(V101),
              "a payload that already admits a wall-clock read claims nothing a vector width could break");
static_assert(binding_verdict_v<int, at::simd::avx2>.admits(V101), "no band, no claim");
static_assert(pack_verdict_v<at::simd::avx2>.admits(V101), "the pack-only view cannot see a payload");

// V402 a trunk-pinned scope x a trunk-pinned ISA that do not cohere.
static_assert(!pack_verdict_v<at::simd::avx2, at::scope::inner>.admits(V402),
              "the host shareability scopes are the ARM fence family, and this ISA is x86");
static_assert(!pack_verdict_v<at::simd::avx2, at::scope::cta>.admits(V402), "an accelerator scope with a host ISA");
static_assert(!pack_verdict_v<at::simd::sve2, at::scope::gpu>.admits(V402),
              "an accelerator scope with an ARM host ISA");
static_assert(pack_verdict_v<at::simd::neon, at::scope::inner>.admits(V402), "the one coherent pairing");
static_assert(pack_verdict_v<at::simd::sve, at::scope::outer>.admits(V402));
static_assert(pack_verdict_v<at::simd::avx2, at::scope::thread>.admits(V402),
              "the shared bottom coheres with anything");
static_assert(pack_verdict_v<at::simd::avx2, at::scope::system>.admits(V402), "and so does the shared top");
static_assert(pack_verdict_v<at::simd::scalar, at::scope::cta>.admits(V402), "an unpinned ISA coheres with anything");
static_assert(pack_verdict_v<at::simd::portable, at::scope::inner>.admits(V402));
static_assert(pack_verdict_v<at::scope::cta>.admits(V402), "no ISA named");
static_assert(pack_verdict_v<at::simd::avx2>.admits(V402), "no scope named");
static_assert(pack_verdict_v<>.admits(V402));

// The one coherent pairing still answers to V401, which is the other
// rule reading a scope: Inner is on the host trunk, so it is below the
// cluster floor and V401 stands down whatever the fence.
static_assert(pack_verdict_v<at::simd::neon, at::scope::inner>.admits(V401)
              && pack_verdict_v<at::simd::neon, at::scope::inner>.admits(V402));

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
static_assert(!binding_verdict_v<det<DetTier::Pure>, fp_mode<Reassoc::UnrestrictedRewrite>>.admits(F101));
static_assert(!binding_verdict_v<det<DetTier::Pure>, fp_mode<Reassoc::BoundedTreeDepth>>.admits(F101),
              "a tree of any shape but the one the source wrote is a different sum");
static_assert(binding_verdict_v<det<DetTier::Pure>, fp_mode<Reassoc::Forbidden>>.admits(F101));
static_assert(binding_verdict_v<det<DetTier::Pure>, fp_mode<Contract::Fast>>.admits(F101),
              "a mode that names only contraction leaves reassociation at Forbidden");
static_assert(binding_verdict_v<det<DetTier::Pure>, fp_mode<>>.admits(F101),
              "the empty mode is every setting at its strict pole");
static_assert(binding_verdict_v<int, fp_mode<Reassoc::UnrestrictedRewrite>>.admits(F101), "no band, no claim");
static_assert(pack_verdict_v<fp_mode<Reassoc::UnrestrictedRewrite>>.admits(F101),
              "the pack-only view cannot see a payload");

// F102 a replay-deterministic payload x contraction across statements.
static_assert(!binding_verdict_v<det<DetTier::Pure>, fp_mode<Contract::Fast>>.admits(F102));
static_assert(binding_verdict_v<det<DetTier::Pure>, fp_mode<Contract::OnInExpr>>.admits(F102),
              "contraction within one expression is visible in the source and the same on every build");
static_assert(binding_verdict_v<det<DetTier::Pure>, fp_mode<Contract::Off>>.admits(F102));
static_assert(binding_verdict_v<det<DetTier::Pure>, fp_mode<Reassoc::UnrestrictedRewrite>>.admits(F102),
              "a mode that names only reassociation leaves contraction at Off");
static_assert(binding_verdict_v<det<DetTier::Pure>, fp_mode<>>.admits(F102));

// The two are two rules: a mode naming both trips both, and each of the
// single-setting modes trips exactly one.
static_assert(
    !binding_verdict_v<det<DetTier::Pure>, fp_mode<Contract::Fast, Reassoc::UnrestrictedRewrite>>.admits(F101)
    && !binding_verdict_v<det<DetTier::Pure>, fp_mode<Contract::Fast, Reassoc::UnrestrictedRewrite>>.admits(F102));

// The settings no rule reads are admitted, which is the atom's own
// claim: a mode may name them, and naming them refuses nothing.
static_assert(binding_verdict_v<det<DetTier::Pure>, fp_mode<::foundation::core::FpFtz::FlushToZero>>.is_valid());
static_assert(
    binding_verdict_v<det<DetTier::Pure>, fp_mode<::foundation::core::FpDenormalInput::DenormalsAreZero>>.is_valid());

// The read alone still refuses nothing: a Pure payload under the strict
// mode is accepted through the bound view.
static_assert(binding_verdict_v<det<DetTier::Pure>, fp_mode<>>.is_valid());

// ---------------------------------------------------------------------
// The capability rule, the indirect-call rule and the spawn rule.

// S011 a capability x a replay-deterministic payload.  Both replay tiers
// trip it, and a payload below the replay floor, or with no band, does
// not.  The trust grade keeps T001 out of every cell.
using TrustVerified = at::trust<trust_level::Verified>;
static_assert(!binding_verdict_v<det<DetTier::Pure>, at::capability_usage, TrustVerified>.admits(S011));
static_assert(!binding_verdict_v<det<DetTier::PhiloxRng>, at::capability_usage, TrustVerified>.admits(S011));
static_assert(binding_verdict_v<det<DetTier::MonotonicClockRead>, at::capability_usage, TrustVerified>.admits(S011),
              "a payload that reads the clock claims no replay, so a capability costs it nothing");
static_assert(binding_verdict_v<int, at::capability_usage, TrustVerified>.is_valid(),
              "a capability with no replay claim and a verified trust trips nothing");
static_assert(binding_verdict_v<det<DetTier::Pure>, at::copy>.is_valid(), "a replay payload that holds no capability");
static_assert(binding_verdict_v<det<DetTier::Pure>, at::capability_usage, TrustVerified>.admits(T001),
              "S011 must be the rule that catches this pack, not T001");
static_assert(pack_verdict_v<at::capability_usage, TrustVerified>.admits(S011), "the pack-only view claims no replay");

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
static_assert(!pack_verdict_v<at::dispatch::indirect_call<void (*)(int)>>.admits(D001));
static_assert(!pack_verdict_v<at::dispatch::indirect_call<int(void*)>>.admits(D001));
static_assert(!pack_verdict_v<at::dispatch::indirect_call<void (callback_owner::*)() const>>.admits(D001));
static_assert(pack_verdict_v<at::dispatch::indirect_call<void (*)(int) noexcept>>.admits(D001));
static_assert(pack_verdict_v<at::dispatch::indirect_call<void (callback_owner::*)() const noexcept>>.admits(D001));
static_assert(!pack_verdict_v<at::dispatch::indirect_call<opaque_family>>.admits(D001),
              "a family that names no signature is read as one that throws");
static_assert(pack_verdict_v<at::dispatch::indirect_call<stated_family>>.is_valid(),
              "a tag class that names a noexcept signature is admitted");
static_assert(!pack_verdict_v<at::dispatch::indirect_call<stated_throwing_family>>.admits(D001));

// L003 a borrow x a spawn no structured join reaches.  A detached child
// and a raw clone trip it; a subprocess runs in its own copy of the
// address space and does not; neither half alone trips it.
static_assert(!pack_verdict_v<at::borrow, at::spawn::detach_with<"drain outlives the owner">>.admits(L003));
static_assert(!pack_verdict_v<at::borrow, at::spawn::syscall_only<"loader needs CLONE_VM">>.admits(L003));
static_assert(pack_verdict_v<at::borrow, at::spawn::subprocess<"exec a helper">>.is_valid(),
              "a forked child borrows from its own copy, so the borrow cannot dangle into the parent");
static_assert(pack_verdict_v<at::spawn::detach_with<"drain outlives the owner">>.is_valid());
static_assert(pack_verdict_v<at::borrow>.admits(L003));
static_assert(pack_verdict_v<at::borrow, at::spawn::detach_with<"drain outlives the owner">>.admits(L002),
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

// The walk above does not look at Axis::Type.  The grade on that axis is
// the payload, and fixy/Fn.h reads it from the payload and never from the
// pack.  An atom on Axis::Type passes the gate and changes nothing.
static_assert(!col::axis_has_an_atom<Axis::Type>, "an atom names Axis::Type, and fixy::fn ignores such an atom");

// ---------------------------------------------------------------------
// The corpus accounts for all 59 codes.
//
// The count below is the one number worth stating here, and it is stated
// against the catalog rather than against the roster.  A pin of
// live == roster - pending holds for any roster, so it cannot catch a
// missing rule.  The dispositions are counted separately and must sum to
// the catalog's size.
//
// Axis::Observability carries two theorems.  B001 is the back-pressure
// trap, and B002 is the containment of the observability row in the
// effect row.  B001 keeps its theorem, because the codes are stable API.
// R004 covers a continuation that holds a live session handle, and W003
// covers a hot binding that holds one and states no wait.  A001 and W004
// hold a fine grade of the payload to the row.

static_assert(col::rule_corpus_size == 59);
static_assert(col::live_rule_count == 45);

[[nodiscard]] consteval std::size_t corpus_entries_with(col::Disposition wanted) noexcept {
    std::size_t found = 0;
    for (const col::corpus_entry& entry : col::rule_corpus) {
        if (entry.disposition == wanted) ++found;
    }
    return found;
}
static_assert(corpus_entries_with(col::Disposition::Live) == 45);
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

// No code appears twice, so the 57 are 57 distinct codes rather than a
// list that happens to be 57 long.
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
// why it is retired, so neither is a silence.
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
// fixy/atoms/Regime.h gives the Regime axis its atoms, and the H, R and
// S rule cells read them.
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
static_assert(!::fixy::IsAccepted<int, at::capability_usage, at::trust<trust_level::Unverified>>);
static_assert(col::binding_verdict_v<int, at::copy>.is_valid());
static_assert(!col::binding_verdict_v<int, at::borrow, at::coroutine>.is_valid());

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
static_assert(
    ::fixy::IsAccepted<int, at::regime::warm, at::syscall::per<at::syscall::SyscallId::getpid>, at::as_public>);

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

// A Result and an Option are failure paths, as the std spelling is.  An
// Option carries no error value, so I002 stands down for it, and its
// discriminant alone still refuses constant time.
enum class gate_fault : std::uint8_t {
    refused = 1,
};
using GateResult = ::foundation::core::Result<int, gate_fault>;
using GateOption = ::foundation::core::Option<int>;
static_assert(!binding_verdict_v<GateResult>.admits(I002) && binding_verdict_v<GateResult, at::as_public>.admits(I002));
static_assert(!binding_verdict_v<GateResult, at::constant_time>.admits(I003));
static_assert(binding_verdict_v<GateOption>.admits(I002) && binding_verdict_v<GateOption>.premises.fails);
static_assert(!binding_verdict_v<GateOption, at::constant_time>.admits(I003));
static_assert(!::fixy::IsAccepted<GateResult>, "I002 on the strict Security pole");
static_assert(::fixy::IsAccepted<GateResult, at::as_public>);
static_assert(::fixy::IsAccepted<GateOption>);
static_assert(!::fixy::IsAccepted<GateOption, at::constant_time>, "I003");

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
// A payload whose DetSafe band sits under another band still claims
// replay, so each replay rule refuses its pair for it.  The outermost
// DetSafe band decides.

using PureUnderVendor = ::fixy::Vendor<::fixy::VendorBackend_v::NV, ::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>>;
using PureUnderArena = ::fixy::alloc_class::Arena<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>>;
using ClockUnderVendor =
    ::fixy::Vendor<::fixy::VendorBackend_v::NV, ::fixy::DetSafe<::fixy::DetSafeTier_v::MonotonicClockRead, int>>;
using ClockOverPure =
    ::fixy::DetSafe<::fixy::DetSafeTier_v::MonotonicClockRead, ::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>>;

static_assert(binding_verdict_v<PureUnderVendor>.premises.replay_deterministic);
static_assert(binding_verdict_v<PureUnderArena>.premises.replay_deterministic);
static_assert(!binding_verdict_v<ClockUnderVendor>.premises.replay_deterministic);
static_assert(!binding_verdict_v<ClockOverPure>.premises.replay_deterministic,
              "the outermost DetSafe band is the claim, and a weaker outer band takes the claim back");
static_assert(!binding_verdict_v<::fixy::Vendor<::fixy::VendorBackend_v::NV, int>>.premises.replay_deterministic);

static_assert(
    !binding_verdict_v<PureUnderVendor, at::fp::mode<at::fp::FpReassociate::UnrestrictedRewrite>>.admits(F101));
static_assert(!binding_verdict_v<PureUnderArena, at::simd::avx2>.admits(V101));
static_assert(binding_verdict_v<ClockUnderVendor, at::simd::avx2>.admits(V101));

// A fine grade claims no more than the row.  A001 refuses a Pool or Stack
// grade with Alloc in the row, and admits an Arena grade there.  W004
// refuses a user-space wait grade with Block in the row, and admits a
// kernel wait grade there.  Each reads the band through other bands.
static_assert(!binding_verdict_v<::fixy::alloc_class::Stack<int>, at::with<Eff::Alloc>>.admits(A001));
static_assert(!binding_verdict_v<::fixy::alloc_class::Pool<int>, at::with<Eff::Alloc>>.admits(A001));
static_assert(binding_verdict_v<::fixy::alloc_class::Arena<int>, at::with<Eff::Alloc>>.admits(A001));
static_assert(binding_verdict_v<::fixy::alloc_class::Stack<int>>.admits(A001),
              "a Stack grade with no Alloc in the row is true");
static_assert(!binding_verdict_v<::fixy::Vendor<::fixy::VendorBackend_v::NV, ::fixy::alloc_class::Stack<int>>,
                        at::with<Eff::Alloc>>.admits(A001));
static_assert(!binding_verdict_v<::fixy::wait::SpinPause<int>, at::with<Eff::Block>>.admits(W004));
static_assert(!binding_verdict_v<::fixy::wait::UmwaitC01<int>, at::with<Eff::Block>>.admits(W004));
static_assert(binding_verdict_v<::fixy::wait::AcquireWait<int>, at::with<Eff::Block>>.admits(W004));
static_assert(binding_verdict_v<::fixy::wait::SpinPause<int>>.admits(W004),
              "a spin grade with no Block in the row is true");
static_assert(pack_verdict_v<at::with<Eff::Alloc>>.admits(A001) && pack_verdict_v<at::with<Eff::Block>>.admits(W004),
              "the pack-only view claims no band");

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
static_assert(!col::detail::HasHwTierAtOrAbove<at::barrier::full_fence, at::hw::HwInstruction::Scalar>);
static_assert(
    !col::detail::HasBarrierAtOrAbove<at::hw::privileged_msr, ::foundation::algebra::lattices::BarrierStrength::None>);
// A host scope is not at or above an accelerator floor.
static_assert(!col::detail::HasScopeAtOrAbove<at::scope::outer, ::foundation::algebra::lattices::MemoryScope::Cluster>);
// UMWAIT is neither a kernel entry nor a busy wait.
static_assert(!pack_verdict_v<at::sync::umwait_c01>.premises.blocks
              && !col::detail::BurnsTheCore<at::sync::umwait_c01>);
// A setting of a different enum with the same underlying value is not named.
static_assert(!col::detail::NamesFpSetting<at::fp::mode<at::fp::FpContract{}>, ::foundation::core::FpFtz{}>);
// A const member function names its signature, and it is not noexcept.
static_assert(!col::detail::names_noexcept_signature_(^^void (attack_owner::*)() const));

// Every form of a function type is read by one query, so a form the
// reader has no arm for cannot pass as one that states nothing.  Each
// throwing form is refused, and each noexcept twin is admitted: a
// ref-qualified, volatile or const volatile member function, a C
// variadic function, and a reference or a const pointer to a function.
using throwing_forms = std::tuple<void (attack_owner::*)() &, void (attack_owner::*)() &&,
                                  void (attack_owner::*)() volatile, void (attack_owner::*)() const volatile&,
                                  int (*)(char, ...), int (*&)(char), int (*const)(char), int (&)(char)>;
using noexcept_forms = std::tuple<void (attack_owner::*)() & noexcept, void (attack_owner::*)() && noexcept,
                                  void (attack_owner::*)() volatile noexcept,
                                  void (attack_owner::*)() const volatile & noexcept, int (*)(char, ...) noexcept,
                                  int (*&)(char) noexcept, int (*const)(char) noexcept, int (&)(char) noexcept>;

template <class Forms, std::size_t... Index>
[[nodiscard]] consteval bool every_form_refused_(std::index_sequence<Index...>) noexcept {
    return (!pack_verdict_v<at::dispatch::indirect_call<std::tuple_element_t<Index, Forms>>>.admits(D001) && ...);
}
template <class Forms, std::size_t... Index>
[[nodiscard]] consteval bool every_form_admitted_(std::index_sequence<Index...>) noexcept {
    return (pack_verdict_v<at::dispatch::indirect_call<std::tuple_element_t<Index, Forms>>>.admits(D001) && ...);
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
static_assert(!pack_verdict_v<at::dispatch::indirect_call<attack_owner>>.admits(D001));
static_assert(!pack_verdict_v<at::dispatch::indirect_call<void (**)() noexcept>>.admits(D001));
static_assert(!pack_verdict_v<at::dispatch::indirect_call<int attack_owner::*>>.admits(D001));
static_assert(!pack_verdict_v<at::dispatch::indirect_call<signature_is_data>>.admits(D001));
static_assert(!pack_verdict_v<at::dispatch::indirect_call<signature_is_itself>>.admits(D001));

// ── the ledger ──────────────────────────────────────────────────────
//
// Each entry pins a wrong answer with its reproducer.  The ledger can only
// shrink: a repair flips the assertion, and the entry leaves in the same
// edit as the repair.  It is empty: every attack above gets the answer the
// rule states.

// A static_assert proves the constant-evaluated path only.
[[nodiscard]] static int check_runtime_paths() {
    if (col::pending_axis_count != axes_without_an_atom()) return 1;
    if (col::pending_rule_count != 0) return 2;
    if (col::live_rule_count != 45) return 3;

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
            case col::Disposition::Live:
                ++live;
                break;
            case col::Disposition::Pending:
                ++pending;
                break;
            case col::Disposition::Absent:
                ++absent;
                break;
            case col::Disposition::Retired:
                ++retired;
                break;
            default:
                return 8;
        }
    }
    if (live != 45 || pending != 0 || absent != 6 || retired != 8) return 9;
    if (live + pending + absent + retired != col::rule_corpus_size) return 10;
    if (col::rule_corpus_size != 59) return 11;

    // The binding the rules admit still carries its value.
    const auto bound = ::fixy::mint_fn<int, at::borrow>(11);
    if (bound.value() != 11) return 6;
    return 0;
}

}  // namespace collision_test

int main() {
    if (int rc = collision_test::check_runtime_paths(); rc != 0) return rc;
    return 0;
}
