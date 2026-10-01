// The compile-time checks of fixy/os/SpinLock.h.

#include <fixy/os/SpinLock.h>

namespace fixy::spin {

static_assert(alignof(UnwitnessedSpinLock) >= 64 && alignof(UnwitnessedBlockingLock) >= 64,
              "a lock must be cache-line-aligned to prevent false sharing across embedded array slots and "
              "adjacent struct members");
static_assert(sizeof(UnwitnessedSpinLock) >= 64 && sizeof(UnwitnessedBlockingLock) >= 64,
              "a lock occupies a full cache line; trailing padding is intentional — adjacent locks in an array "
              "must land on distinct lines");
// The spin lock stores a std::atomic_flag, the one type [atomics.flag]
// guarantees lock-free on every conforming implementation.  The blocking
// lock waits on its word, so the word must be a real atomic and not a
// library mutex in disguise.
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "the blocking lock's state word must be lock-free on this target");

// The probe tag must be a complete empty struct. An incomplete type is not
// introspectable by is_empty_v, so PermissionTag cannot be satisfied.
namespace gate_size_probe_ {
struct SizeProbe {};
}  // namespace gate_size_probe_

static_assert(alignof(SpinLock<gate_size_probe_::SizeProbe>) == alignof(UnwitnessedSpinLock)
                  && alignof(BlockingLock<gate_size_probe_::SizeProbe>) == alignof(UnwitnessedBlockingLock),
              "fixy::spin::Gate must inherit substrate alignment (64 bytes). A cross-thread gate relies on "
              "cache-line isolation to stay clear of false sharing.");
static_assert(sizeof(SpinLock<gate_size_probe_::SizeProbe>) == sizeof(UnwitnessedSpinLock)
                  && sizeof(BlockingLock<gate_size_probe_::SizeProbe>) == sizeof(UnwitnessedBlockingLock),
              "fixy::spin::Gate must be zero-overhead over the substrate — the Tag is phantom and must "
              "EBO-collapse to zero bytes.");

}  // namespace fixy::spin

namespace fixy::spin::detail::gate_self_test {

struct ProbeTag {};
struct ProbeBrand {};

static_assert(std::is_same_v<SpinLock<ProbeTag>::substrate_t, UnwitnessedSpinLock>
                  && std::is_same_v<BlockingLock<ProbeTag>::substrate_t, UnwitnessedBlockingLock>,
              "each gate must embed the bare lock of its strategy — substrate drift would break the alignas(64) "
              "and acquire/release contract.");

static_assert(std::is_same_v<SpinGuard<ProbeTag, ProbeBrand>::permission_t,
                             ::foundation::permissions::Permission<ProbeTag, ProbeBrand>>,
              "a guard must hold the Permission of its gate's tag under the brand it was given — drift would "
              "break the Permission-witness-at-acquire discipline.");

static_assert(SpinLock<ProbeTag>::cache_tier == cache_tier_t::Hot
                  && BlockingLock<ProbeTag>::cache_tier == cache_tier_t::Cold,
              "a spin gate is Hot and a blocking gate is Cold.  The annotation is how a gate is located.");

static_assert(std::is_same_v<SpinLock<ProbeTag>::tag_type, ProbeTag>);

// The kernel split of design note 2, read off the lattice.
static_assert(waits_in_kernel(WaitStrategy::Block) && waits_in_kernel(WaitStrategy::Park)
              && waits_in_kernel(WaitStrategy::AcquireWait));
static_assert(!waits_in_kernel(WaitStrategy::UmwaitC01) && !waits_in_kernel(WaitStrategy::BoundedSpin)
              && !waits_in_kernel(WaitStrategy::SpinPause));

// Neither a gate nor a guard can be copied or moved.
static_assert(!std::is_copy_constructible_v<SpinLock<ProbeTag>> && !std::is_move_constructible_v<SpinLock<ProbeTag>>);
static_assert(!std::is_copy_constructible_v<BlockingLock<ProbeTag>>
              && !std::is_move_constructible_v<BlockingLock<ProbeTag>>);
static_assert(!std::is_copy_constructible_v<SpinGuard<ProbeTag, ProbeBrand>>
              && !std::is_move_constructible_v<SpinGuard<ProbeTag, ProbeBrand>>);
static_assert(!std::is_copy_constructible_v<BlockingGuard<ProbeTag, ProbeBrand>>
              && !std::is_move_constructible_v<BlockingGuard<ProbeTag, ProbeBrand>>);
static_assert(!std::is_copy_constructible_v<UnwitnessedSpinLock> && !std::is_move_constructible_v<UnwitnessedSpinLock>);
static_assert(!std::is_copy_constructible_v<UnwitnessedBlockingLock>
              && !std::is_move_constructible_v<UnwitnessedBlockingLock>);

// Design note 2 as cells: each clause admits the contexts its wait allows
// and refuses the rest.  The contexts are built here, the way the sibling
// os headers build theirs.
using ForegroundCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>>;
using BackgroundCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
using BackgroundBlockCtx =
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

static_assert(CtxMayAcquire<ForegroundCtx, WaitStrategy::BoundedSpin>,
              "a context without Bg must be able to spin: that is the shape a foreground caller has.");
static_assert(!CtxMayAcquire<BackgroundCtx, WaitStrategy::BoundedSpin>,
              "a context that owns Bg must NOT spin. A background context may also allocate, syscall or block, "
              "and none of the three belongs inside a section another thread spins on.");
static_assert(CtxMayAcquire<BackgroundBlockCtx, WaitStrategy::AcquireWait>,
              "a context that owns Block may sleep on a blocking gate: that is the shape background work has.");
static_assert(!CtxMayAcquire<BackgroundCtx, WaitStrategy::AcquireWait>,
              "a context without Block must NOT sleep on a blocking gate: the wait is a block.");
static_assert(!CtxMayAcquire<ForegroundCtx, WaitStrategy::AcquireWait>,
              "a foreground context owns no Block, so it cannot wait on a blocking gate.");
static_assert(!CtxMayAcquire<int, WaitStrategy::BoundedSpin> && !CtxMayAcquire<int, WaitStrategy::AcquireWait>,
              "a non-context must not pass for one.");

// A cold init context owns Alloc and IO, and a test runner owns Block.
// Neither may spin, and each may take the blocking gate when it owns Block.
using ColdInitShape = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::Alloc, eff::Effect::IO>>;
using AllocOnlyShape = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc>>;
using TestRunnerShape =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;
static_assert(!CtxMayAcquire<ColdInitShape, WaitStrategy::BoundedSpin>,
              "a context that owns IO must NOT spin: the holder could make a system call inside the section.");
static_assert(!CtxMayAcquire<AllocOnlyShape, WaitStrategy::BoundedSpin>,
              "a context that owns Alloc must NOT spin: the holder could enter the allocator inside the section.");
static_assert(!CtxMayAcquire<TestRunnerShape, WaitStrategy::BoundedSpin>
              && CtxMayAcquire<TestRunnerShape, WaitStrategy::AcquireWait>);

// Seven claims about this header cannot be written here.  An access
// failure inside a requires-expression is a hard error in GCC 16 rather
// than a substitution failure, so `!requires { gate.lock(); }` does not
// compile even when the answer is the one wanted.  The claims are carried
// by negative-compile fixtures instead:
//
//   neg_spin_lock_door_is_private       — gate.lock() from outside
//   neg_spin_try_lock_door_is_private   — gate.try_lock() from outside
//   neg_spin_unlock_door_is_private     — gate.unlock() from outside
//   neg_spin_guard_under_bg_ctx         — a Bg context at the spin guard
//   neg_spin_guard_under_init_ctx       — a cold init context at the spin
//                                         guard
//   neg_spin_lock_without_permission    — a guard with no Permission at all
//   neg_blocking_guard_without_block    — a context without Block at the
//                                         blocking guard
//   neg_blocking_guard_without_permission — a blocking guard with no
//                                         Permission at all

}  // namespace fixy::spin::detail::gate_self_test
