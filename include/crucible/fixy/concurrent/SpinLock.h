#pragma once

#include <crucible/Platform.h>
#include <crucible/algebra/lattices/HotPathLattice.h>
#include <crucible/concurrent/SpinLock.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/permissions/Permission.h>

#include <mutex>
#include <type_traits>

namespace crucible::fixy::concurrent {

using cache_tier_t = ::crucible::algebra::lattices::HotPathTier;
inline constexpr cache_tier_t cache_tier_hot = cache_tier_t::Hot;

template <typename Tag>
class alignas(64) SpinLock {
    static_assert(::crucible::safety::PermissionTag<Tag>, "fixy::concurrent::SpinLock<Tag>: Tag must satisfy the "
                                                          "PermissionTag concept, that is an empty non-union class "
                                                          "type. Typically an empty struct nested in the owning "
                                                          "class.");

public:
    using tag_type = Tag;
    using substrate_t = ::crucible::concurrent::SpinLock;
    using permission_t = ::crucible::safety::Permission<Tag>;

    static constexpr cache_tier_t cache_tier = cache_tier_t::Hot;

    constexpr SpinLock() noexcept = default;

    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;
    SpinLock(SpinLock&&) = delete;
    SpinLock& operator=(SpinLock&&) = delete;

    // The proof parameter is a compile-time witness. The body ignores it.
    void lock(permission_t& /*proof*/) noexcept { substrate_.lock(); }

    [[nodiscard]] bool try_lock(permission_t& /*proof*/) noexcept { return substrate_.try_lock(); }

    // The Bg exclusion is the point: a background context may also allocate,
    // syscall or block, none of which belong inside this gate's critical
    // section.
    template <class Ctx>
        requires ::crucible::effects::IsExecCtx<Ctx>
              && (!::crucible::effects::CtxOwnsCapability<Ctx, ::crucible::effects::Effect::Bg>)
    void lock_in(Ctx const& /*ctx*/, permission_t& proof) noexcept {
        lock(proof);
    }

    template <class Ctx>
        requires ::crucible::effects::IsExecCtx<Ctx>
              && (!::crucible::effects::CtxOwnsCapability<Ctx, ::crucible::effects::Effect::Bg>)
    [[nodiscard]] bool try_lock_in(Ctx const& /*ctx*/, permission_t& proof) noexcept {
        return try_lock(proof);
    }

    // Release takes no witness: acquiring the gate already required one.
    void unlock() noexcept { substrate_.unlock(); }

    // Escape hatch for interoperating with lock adaptors that take the bare
    // substrate lock.
    [[nodiscard]] substrate_t& substrate() noexcept { return substrate_; }
    [[nodiscard]] substrate_t const& substrate() const noexcept { return substrate_; }

private:
    [[no_unique_address]] substrate_t substrate_{};
};

// The probe tag must be a complete empty struct. An incomplete type is not
// introspectable by is_empty_v, so PermissionTag cannot be satisfied.
namespace spinlock_size_probe_ {
struct SizeProbe {};
}  // namespace spinlock_size_probe_
static_assert(alignof(SpinLock<spinlock_size_probe_::SizeProbe>) == alignof(::crucible::concurrent::SpinLock),
              "fixy::concurrent::SpinLock<Tag> must inherit substrate "
              "alignment (64 bytes). A cross-thread spin gate relies on "
              "cache-line isolation to stay clear of false sharing.");
static_assert(sizeof(SpinLock<spinlock_size_probe_::SizeProbe>) == sizeof(::crucible::concurrent::SpinLock),
              "fixy::concurrent::SpinLock<Tag> must be zero-overhead "
              "over the substrate — the Tag is phantom and must EBO-"
              "collapse to zero bytes.");

// Copy and move are deleted: a second guard over the same lock would release
// it twice and break the acquire/release pairing.
template <typename Tag>
class SpinGuard {
public:
    using lock_type = SpinLock<Tag>;
    using permission_t = typename lock_type::permission_t;

    explicit SpinGuard(lock_type& lock, permission_t& proof) noexcept : lock_{lock} { lock_.lock(proof); }

    explicit SpinGuard(std::try_to_lock_t, lock_type& lock, permission_t& proof) noexcept
        : lock_{lock}, acquired_{lock.try_lock(proof)} {}

    SpinGuard(const SpinGuard&) = delete;
    SpinGuard& operator=(const SpinGuard&) = delete;
    SpinGuard(SpinGuard&&) = delete;
    SpinGuard& operator=(SpinGuard&&) = delete;

    ~SpinGuard() noexcept {
        if (acquired_) {
            lock_.unlock();
        }
    }

    [[nodiscard]] bool was_acquired() const noexcept { return acquired_; }

private:
    lock_type& lock_;
    bool acquired_ = true;  // the plain constructor always acquires
};

inline void fixy_spinlock_runtime_smoke_test() noexcept {
    struct ProbeTag {};
    SpinLock<ProbeTag> gate{};
    auto perm = ::crucible::safety::mint_permission_root<ProbeTag>();

    gate.lock(perm);
    gate.unlock();

    if (gate.try_lock(perm)) {
        gate.unlock();
    }

    {
        SpinGuard<ProbeTag> guard{gate, perm};
        (void)guard;
    }

    {
        SpinGuard<ProbeTag> guard{std::try_to_lock, gate, perm};
        if (guard.was_acquired()) {
            // RAII releases on scope exit.
        }
    }
}

}  // namespace crucible::fixy::concurrent

namespace crucible::fixy::concurrent::self_test {

struct SpinLockProbeTag {};

static_assert(std::is_same_v<SpinLock<SpinLockProbeTag>::substrate_t, ::crucible::concurrent::SpinLock>,
              "fixy::concurrent::SpinLock<Tag>::substrate_t must alias "
              "::crucible::concurrent::SpinLock — substrate identity drift "
              "would break the alignas(64) + acquire/release contract.");

static_assert(
    std::is_same_v<SpinLock<SpinLockProbeTag>::permission_t, ::crucible::safety::Permission<SpinLockProbeTag>>,
    "fixy::concurrent::SpinLock<Tag>::permission_t must alias "
    "::crucible::safety::Permission<Tag> — drift would break the "
    "Permission-witness-at-acquire discipline.");

static_assert(SpinLock<SpinLockProbeTag>::cache_tier == ::crucible::algebra::lattices::HotPathTier::Hot,
              "fixy::concurrent::SpinLock<Tag>::cache_tier must be Hot. The "
              "annotation is how a hot-path spin gate is located.");

}  // namespace crucible::fixy::concurrent::self_test
