// The compile-time checks of fixy/os/SpinLock.h.

#include <fixy/os/SpinLock.h>

#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>

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
using SpinnerCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Div>>;
using ForegroundCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>>;
using BackgroundCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
using BackgroundSpinCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::Div>>;
using BackgroundBlockCtx =
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

static_assert(CtxMayAcquire<SpinnerCtx, WaitStrategy::BoundedSpin>,
              "a context that owns Div and neither IO nor Block must be able to spin on a gate.");
static_assert(!CtxMayAcquire<ForegroundCtx, WaitStrategy::BoundedSpin>,
              "a context without Div must NOT spin on a gate: the wait has no exit until the release, and the hot "
              "path owns no Div.");
static_assert(CtxMayAcquire<BackgroundSpinCtx, WaitStrategy::BoundedSpin>,
              "a context that owns Bg and Alloc may spin when it owns Div: an allocation inside the section is no "
              "system call and no sleep.");
static_assert(!CtxMayAcquire<BackgroundCtx, WaitStrategy::BoundedSpin>,
              "a background context without Div must NOT spin on a gate.");
static_assert(CtxMayAcquire<BackgroundBlockCtx, WaitStrategy::AcquireWait>,
              "a context that owns Block may sleep on a blocking gate: that is the shape background work has.");
static_assert(!CtxMayAcquire<BackgroundCtx, WaitStrategy::AcquireWait>,
              "a context without Block must NOT sleep on a blocking gate: the wait is a block.");
static_assert(!CtxMayAcquire<ForegroundCtx, WaitStrategy::AcquireWait>,
              "a foreground context owns no Block, so it cannot wait on a blocking gate.");
static_assert(!CtxMayAcquire<int, WaitStrategy::BoundedSpin> && !CtxMayAcquire<int, WaitStrategy::AcquireWait>,
              "a non-context must not pass for one.");

// An init context that owns IO and a test runner that owns Block may not
// spin, even with Div, and each may take the blocking gate when it owns
// Block.  A context that owns Alloc and Div may spin.
using InitIoDivShape =
    eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Div>>;
using AllocDivShape = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::Div>>;
using TestRunnerShape = eff::ExecCtx<
    eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block, eff::Effect::Div>>;
static_assert(!CtxMayAcquire<InitIoDivShape, WaitStrategy::BoundedSpin>,
              "a context that owns IO must NOT spin: the holder could make a system call inside the section.");
static_assert(CtxMayAcquire<AllocDivShape, WaitStrategy::BoundedSpin>,
              "a context that owns Alloc and Div may spin: an allocation is no system call and no sleep.");
static_assert(!CtxMayAcquire<TestRunnerShape, WaitStrategy::BoundedSpin>
              && CtxMayAcquire<TestRunnerShape, WaitStrategy::AcquireWait>);

// A Locked (design note 7) holds the bare lock of its strategy on its own
// line, and then its data.  It does not copy or move, and neither does its
// guard.  A body that gives a plain value passes, and a body that gives the
// lent reference does not.
struct LockedData {
    std::uint64_t count = 0;
};
static_assert(alignof(SpinLocked<LockedData, ProbeTag>) == 64 && alignof(BlockingLocked<LockedData, ProbeTag>) == 64);
static_assert(sizeof(BlockingLocked<LockedData, ProbeTag>) == 128,
              "the lock word fills one line, and the data starts the next line");
static_assert(!std::is_copy_constructible_v<BlockingLocked<LockedData, ProbeTag>>
              && !std::is_move_constructible_v<BlockingLocked<LockedData, ProbeTag>>);
static_assert(!std::is_copy_constructible_v<LockedGuard<LockedData, UnwitnessedBlockingLock>>
              && !std::is_move_constructible_v<LockedGuard<LockedData, UnwitnessedBlockingLock>>);
static_assert(SpinLocked<LockedData, ProbeTag>::cache_tier == cache_tier_t::Hot
              && BlockingLocked<LockedData, ProbeTag>::cache_tier == cache_tier_t::Cold);
struct GivesCount {
    std::uint64_t operator()(LockedData const& data) const noexcept { return data.count; }
};
struct GivesData {
    LockedData& operator()(LockedData& data) const noexcept { return data; }
};
static_assert(LockedBody<GivesCount, LockedData const> && LockedBody<GivesCount, LockedData>);
static_assert(!LockedBody<GivesData, LockedData> && LockedBorrowGivingBody<GivesData, LockedData>);

// A body that copies the data into a variable of the caller passes.  A body
// that keeps a Ref of the data in a variable of the caller binds to the store
// twin: the Ref would name the data after the unlock, in a data race with the
// next holder (StoresNoBorrowOutside of foundation/core/CaptureStore.h).
struct LockedBrand final {};
struct CopiesCount {
    std::uint64_t& copied;
    void operator()(LockedData& data) const noexcept { copied = data.count; }
};
struct KeepsData {
    ::foundation::core::Option<::foundation::core::Ref<LockedData, LockedBrand>>& kept;
    void operator()(LockedData& data) const noexcept { static_cast<void>(data); }
};
static_assert(LockedBody<CopiesCount, LockedData> && !LockedStoringBody<CopiesCount, LockedData>);
static_assert(!LockedBody<KeepsData, LockedData> && LockedStoringBody<KeepsData, LockedData>
              && !LockedBorrowGivingBody<KeepsData, LockedData>);

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
//   neg_locked_read_with_no_guard       — a read of the data of a Locked
//                                         with no guard
//   neg_locked_body_gives_the_data_out  — a body that gives the lent
//                                         reference out
//   neg_locked_start_or_end_door_of_another_class
//                                       — the door with no context from a
//                                         class that the tag does not name
//   neg_locked_bare_lock_door_is_private
//                                       — a bare lock taken by hand

}  // namespace fixy::spin::detail::gate_self_test
