// The witnessed gates, the spin gate and the blocking gate, acquired and
// released through the guard, which is the one door each gate has.  Each
// case gives the guard a context its wait strategy admits and a
// Permission.
//
// Old spelling: the runtime_smoke_test inside
// include/crucible/fixy/concurrent/_SpinLock.h, which acquired and
// released without a context because both doors were public.
//
// The cells that prove a door is CLOSED cannot live here: a private
// member access is a compile error, not a runtime result. They are the
// neg fixtures named in the header.

#include <fixy/os/SpinLock.h>
#include <foundation/permissions/Permission.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <optional>
#include <thread>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;
namespace spin = fixy::spin;

namespace {

// The shape a foreground caller has: a context that exists and owns none
// of Bg, Alloc, IO and Block.
using ForegroundCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test>>;

// The shape background work that may wait has: it owns Block.
using BackgroundBlockCtx =
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

struct GateTag {
    using permission_row = eff::Row<>;
};

// One case per gate: the gate and the context its strategy admits.
struct SpinCase {
    using gate_type = spin::SpinLock<GateTag>;
    static constexpr const char* name = "spin";
    [[nodiscard]] static ForegroundCtx context() noexcept { return ForegroundCtx{eff::testing::test()}; }
};

struct BlockingCase {
    using gate_type = spin::BlockingLock<GateTag>;
    static constexpr const char* name = "blocking";
    [[nodiscard]] static BackgroundBlockCtx context() noexcept { return BackgroundBlockCtx{eff::testing::bg()}; }
};

// Whether a try guard acquires the gate now.  The try guard releases on
// its way out, so the gate is as it was after the call.
template <typename Ctx, typename Gate, typename Proof>
[[nodiscard]] bool is_free(Ctx const& ctx, Gate& gate, Proof& proof) {
    spin::GateGuard probe{std::try_to_lock, ctx, gate, proof};
    return probe.was_acquired();
}

template <typename Case>
[[nodiscard]] int guard_releases_on_scope_exit() {
    auto ctx = Case::context();
    typename Case::gate_type gate{};
    auto proof = perm::mint_permission_root<GateTag>();

    {
        spin::GateGuard guard{ctx, gate, proof};
        if (!guard.was_acquired()) {
            std::fprintf(stderr, "%s: the plain guard constructor did not acquire\n", Case::name);
            return 1;
        }
        if (is_free(ctx, gate, proof)) {
            std::fprintf(stderr, "%s: a try guard acquired a gate that a guard held\n", Case::name);
            return 1;
        }
    }
    // The gate must be free again now that the guard has gone, and a
    // second time, which says that the try guard released it as well.
    if (!is_free(ctx, gate, proof) || !is_free(ctx, gate, proof)) {
        std::fprintf(stderr, "%s: a guard did not release on scope exit\n", Case::name);
        return 1;
    }
    return 0;
}

template <typename Case>
[[nodiscard]] int try_guard_reports_a_contended_gate() {
    auto ctx = Case::context();
    typename Case::gate_type gate{};
    auto proof = perm::mint_permission_root<GateTag>();

    using HolderGuard = decltype(spin::GateGuard{ctx, gate, proof});
    std::optional<HolderGuard> holder;
    holder.emplace(ctx, gate, proof);
    {
        // The gate is held, so the try guard must report that it did not
        // acquire, and must not release on destruction.
        spin::GateGuard guard{std::try_to_lock, ctx, gate, proof};
        if (guard.was_acquired()) {
            std::fprintf(stderr, "%s: the try guard claimed a gate that was already held\n", Case::name);
            return 1;
        }
    }
    // The first acquisition is still held: a guard that did not acquire
    // must not have released it.
    if (is_free(ctx, gate, proof)) {
        std::fprintf(stderr, "%s: a try guard that failed to acquire released the holder's gate anyway\n",
                     Case::name);
        return 1;
    }
    holder.reset();
    if (!is_free(ctx, gate, proof)) {
        std::fprintf(stderr, "%s: the holder's guard did not release\n", Case::name);
        return 1;
    }
    return 0;
}

// Four threads, one gate: the mutual exclusion the lock exists for. The
// counter is plain, so a torn increment would show up as a short total.
// On the blocking gate, four threads against one holder put waiters to
// sleep, so the release path that wakes a sleeper runs too.
template <typename Case>
[[nodiscard]] int the_gate_actually_excludes() {
    constexpr int kThreads = 4;
    constexpr int kPerThread = 20000;
    auto ctx = Case::context();
    typename Case::gate_type gate{};

    long counter = 0;

    {
        std::array<std::jthread, kThreads> workers;
        for (std::jthread& worker : workers) {
            worker = std::jthread{[&] () noexcept {
                auto proof = perm::mint_permission_root<GateTag>();
                for (int iteration = 0; iteration < kPerThread; ++iteration) {
                    spin::GateGuard guard{ctx, gate, proof};
                    ++counter;
                }
            }};
        }
    }

    constexpr long kWant = static_cast<long>(kThreads) * kPerThread;
    if (counter != kWant) {
        std::fprintf(stderr, "%s: the gate did not exclude: counter is %ld, want %ld\n", Case::name, counter, kWant);
        return 1;
    }
    return 0;
}

// A waiter that sleeps on a held blocking gate must wake when the holder
// releases.  The holder keeps the gate until the waiter has announced
// that it is about to wait, so the waiter reaches the contended path
// rather than finding the gate free.
[[nodiscard]] int a_sleeping_waiter_wakes_on_release() {
    auto ctx = BlockingCase::context();
    spin::BlockingLock<GateTag> gate{};
    auto holder_proof = perm::mint_permission_root<GateTag>();
    std::atomic<bool> is_waiter_ready{false};
    std::atomic<bool> has_waiter_acquired{false};

    using HolderGuard = decltype(spin::GateGuard{ctx, gate, holder_proof});
    std::optional<HolderGuard> holder;
    holder.emplace(ctx, gate, holder_proof);
    std::jthread waiter{[&] () noexcept {
        auto waiter_proof = perm::mint_permission_root<GateTag>();
        is_waiter_ready.store(true, std::memory_order_release);
        spin::GateGuard guard{ctx, gate, waiter_proof};
        has_waiter_acquired.store(true, std::memory_order_release);
    }};
    while (!is_waiter_ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    // The waiter may be asleep or about to sleep; it must not hold the gate.
    if (has_waiter_acquired.load(std::memory_order_acquire)) {
        std::fprintf(stderr, "blocking: the waiter acquired a gate the holder still held\n");
        return 1;
    }
    holder.reset();
    waiter.join();
    if (!has_waiter_acquired.load(std::memory_order_acquire)) {
        std::fprintf(stderr, "blocking: the waiter did not acquire after the release\n");
        return 1;
    }
    return 0;
}

template <typename Case>
[[nodiscard]] int run_case() {
    if (const int rc = guard_releases_on_scope_exit<Case>(); rc != 0) return rc;
    if (const int rc = try_guard_reports_a_contended_gate<Case>(); rc != 0) return rc;
    return the_gate_actually_excludes<Case>();
}

}  // namespace

int main() {
    if (const int rc = run_case<SpinCase>(); rc != 0) return rc;
    if (const int rc = run_case<BlockingCase>(); rc != 0) return rc;
    return a_sleeping_waiter_wakes_on_release();
}
