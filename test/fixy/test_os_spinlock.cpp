// The witnessed spin gate, acquired and released through every door it
// has, with a foreground context and a Permission.
//
// Old spelling: the runtime_smoke_test inside
// include/crucible/fixy/concurrent/SpinLock.h, which acquired and
// released without a context because both doors were public.
//
// The cells that prove a door is CLOSED cannot live here: a private
// member access is a compile error, not a runtime result. They are the
// neg fixtures named in the header.

#include <fixy/os/SpinLock.h>
#include <foundation/permissions/Permission.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;
namespace spin = fixy::spin;

namespace {

// The shape a foreground caller has: a context that exists and does not
// own the background capability.
using ForegroundCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>>;

struct GateTag {};

[[nodiscard]] int manual_acquire_and_release() {
    ForegroundCtx ctx{eff::testing::test()};
    spin::SpinLock<GateTag> gate{};
    auto proof = perm::mint_permission_root<GateTag>();

    gate.lock_in(ctx, proof);
    gate.unlock(proof);

    if (!gate.try_lock_in(ctx, proof)) {
        std::fprintf(stderr, "try_lock_in failed on an uncontended gate\n");
        return 1;
    }
    gate.unlock(proof);

    // A second acquisition after a clean release must succeed, which is
    // what says unlock actually cleared the flag.
    if (!gate.try_lock_in(ctx, proof)) {
        std::fprintf(stderr, "the gate did not release: try_lock_in failed after unlock\n");
        return 1;
    }
    gate.unlock(proof);
    return 0;
}

[[nodiscard]] int guard_releases_on_scope_exit() {
    ForegroundCtx ctx{eff::testing::test()};
    spin::SpinLock<GateTag> gate{};
    auto proof = perm::mint_permission_root<GateTag>();

    {
        spin::SpinGuard<GateTag> guard{ctx, gate, proof};
        if (!guard.was_acquired()) {
            std::fprintf(stderr, "the plain guard constructor did not acquire\n");
            return 1;
        }
    }
    // The gate must be free again now that the guard has gone.
    if (!gate.try_lock_in(ctx, proof)) {
        std::fprintf(stderr, "the guard did not release on scope exit\n");
        return 1;
    }
    gate.unlock(proof);
    return 0;
}

[[nodiscard]] int try_guard_reports_a_contended_gate() {
    ForegroundCtx ctx{eff::testing::test()};
    spin::SpinLock<GateTag> gate{};
    auto proof = perm::mint_permission_root<GateTag>();

    gate.lock_in(ctx, proof);
    {
        // The gate is held, so the try guard must report that it did not
        // acquire, and must not release on destruction.
        spin::SpinGuard<GateTag> guard{std::try_to_lock, ctx, gate, proof};
        if (guard.was_acquired()) {
            std::fprintf(stderr, "the try guard claimed a gate that was already held\n");
            return 1;
        }
    }
    // The original acquisition is still ours: a guard that did not acquire
    // must not have released it.
    if (gate.try_lock_in(ctx, proof)) {
        std::fprintf(stderr, "a try guard that failed to acquire released the holder's gate anyway\n");
        return 1;
    }
    gate.unlock(proof);
    return 0;
}

// Two threads, one gate: the mutual exclusion the lock exists for. The
// counter is plain, so a torn increment would show up as a short total.
[[nodiscard]] int the_gate_actually_excludes() {
    constexpr int kPerThread = 20000;
    ForegroundCtx ctx{eff::testing::test()};
    spin::SpinLock<GateTag> gate{};
    auto proof_a = perm::mint_permission_root<GateTag>();
    auto proof_b = perm::mint_permission_root<GateTag>();

    long counter = 0;

    {
        std::jthread worker_a{[&] () noexcept {
            for (int i = 0; i < kPerThread; ++i) {
                spin::SpinGuard<GateTag> guard{ctx, gate, proof_a};
                ++counter;
            }
        }};
        std::jthread worker_b{[&] () noexcept {
            for (int i = 0; i < kPerThread; ++i) {
                spin::SpinGuard<GateTag> guard{ctx, gate, proof_b};
                ++counter;
            }
        }};
    }

    if (counter != 2L * kPerThread) {
        std::fprintf(stderr, "the gate did not exclude: counter is %ld, want %ld\n", counter, 2L * kPerThread);
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = manual_acquire_and_release(); rc != 0) return rc;
    if (const int rc = guard_releases_on_scope_exit(); rc != 0) return rc;
    if (const int rc = try_guard_reports_a_contended_gate(); rc != 0) return rc;
    if (const int rc = the_gate_actually_excludes(); rc != 0) return rc;
    return 0;
}
