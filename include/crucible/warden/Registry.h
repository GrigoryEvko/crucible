#pragma once

// The memory regions a production node should keep resident. A
// component names its own backing memory when it is constructed and
// withdraws it when it is destroyed, through the two free functions
// below, so no component needs to know anything else about this layer.
//
// The registry decides nothing. It records addresses, and applying a
// policy is what locks or advises them, if the policy says so.
//
// Registration is safe from any thread, and belongs to construction
// and teardown rather than to a hot path.

#include <fixy/Core.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

namespace crucible::warden {

// The huge page size on both supported architectures. From Linux 5.8
// the huge-page advice call demands this alignment, where an earlier
// kernel rounded to it without saying so.
inline constexpr size_t kHugePageBytes = 2 * 1024 * 1024;

[[nodiscard]] constexpr size_t round_up_huge(size_t n) noexcept {
    return (n + kHugePageBytes - 1) & ~(kHugePageBytes - 1);
}

// pad holds the seven bytes after the hint, so a region has no padding
// byte.  A snapshot returns up to 256 regions, and with
// -ftrivial-auto-var-init=zero GCC writes zero to each padding hole of each
// region of the returned list, one store for each hole.  The slot of the
// registry below has the same pad for the same reason.
// test/test_warden_policy.cpp makes sure that the region and the slot have
// no padding byte.
struct HotRegion {
    // The bytes of the region.  The View borrows the memory of the
    // component that registered it, and the component keeps that memory
    // alive until it withdraws the region.
    ::fixy::View<std::byte const> bytes{};
    // True for a large mapping that rarely changes shape. False for a
    // buffer that is resized often, where collapsing it into huge
    // pages would stall the resize.
    bool huge_hint = false;
    ::foundation::core::FixedArray<std::uint8_t, 7> pad{};
};

namespace detail {

// A slot of the registry.  The thread that registers a region writes it,
// and each other thread reads it.  An Atomic refuses a type with no
// lock-free operations, so no read of the table waits for a lock.  No
// thread writes pad after the construction of the registry.  The class
// is outside the registry, so its member initializers are complete when
// the array of the registry asks for its default constructor.
struct HotRegionSlot {
    ::fixy::Atomic<void*> addr{nullptr};
    ::fixy::Atomic<size_t> len{0};
    ::fixy::Atomic<bool> huge_hint{false};
    ::foundation::core::FixedArray<std::uint8_t, 7> pad{};
};

}  // namespace detail

class HotRegionRegistry {
public:
    static constexpr size_t max_regions = 256;

    // The regions of a snapshot.  Each occupied slot gives one region, and
    // the slot count is the capacity, so the list needs no heap.
    using Snapshot = ::fixy::FixedList<HotRegion, max_regions>;

    // One registry for the process.  The two vessel libraries both reach it.
    CRUCIBLE_PROCESS_WIDE [[nodiscard]] static HotRegionRegistry& instance() noexcept {
        static HotRegionRegistry r;
        return r;
    }

    // Registering one address twice replaces the first description
    // rather than adding a second entry, so a reader of the table sees
    // exactly one entry per address.  A registration when each slot
    // holds a region ends the process.
    void register_region(void* addr, size_t len, bool huge_hint) noexcept {
        if (addr == nullptr || len == 0) return;
        for (Slot& slot : slots_) {
            if (slot.addr.cas_acq_rel(addr, claim_marker_(slot)).is_ok()) {
                publish_(slot, addr, len, huge_hint);
                return;
            }
        }
        for (Slot& slot : slots_) {
            if (slot.addr.cas_acq_rel(nullptr, claim_marker_(slot)).is_ok()) {
                publish_(slot, addr, len, huge_hint);
                return;
            }
        }
        ::fixy::fatal("HotRegionRegistry: each of the 256 slots holds a region, so the registration has no slot");
    }

    // An address that is not registered is ignored, because nothing
    // orders a component's destruction against a policy being undone.
    void unregister_region(void* addr) noexcept {
        if (addr == nullptr) return;
        for (Slot& slot : slots_) {
            if (slot.addr.cas_acq_rel(addr, nullptr).is_ok()) {
                return;
            }
        }
    }

    // Returned by value so that a caller can work through the entries
    // without holding the table while it issues system calls. A region
    // registered during that work is picked up the next time a policy
    // is applied.
    [[nodiscard]] Snapshot snapshot() const noexcept {
        Snapshot regions{};
        for (Slot const& slot : slots_) {
            void* const addr = slot.addr.load_acquire();
            if (!is_published_(slot, addr)) continue;
            static_cast<void>(regions
                                  .push(HotRegion{
                                      .bytes = ::fixy::mint_foreign_view(static_cast<std::byte const*>(addr),
                                                                         slot.len.load_acquire()),
                                      .huge_hint = slot.huge_hint.load_acquire(),
                                  })
                                  .expect("the snapshot holds one region for each slot"));
        }
        return regions;
    }

    [[nodiscard]] size_t size() const noexcept {
        size_t count = 0;
        for (Slot const& slot : slots_) {
            if (is_published_(slot, slot.addr.load_acquire())) ++count;
        }
        return count;
    }

    HotRegionRegistry(const HotRegionRegistry&) = delete("singleton — use instance()");
    HotRegionRegistry& operator=(const HotRegionRegistry&) = delete("singleton — use instance()");
    HotRegionRegistry(HotRegionRegistry&&) = delete("singleton — use instance()");
    HotRegionRegistry& operator=(HotRegionRegistry&&) = delete("singleton — use instance()");

private:
    HotRegionRegistry() = default;
    ~HotRegionRegistry() = default;

    using Slot = detail::HotRegionSlot;

    // A writer that holds a slot stores the address of that slot in it.
    // No registered region is a slot of the registry, and the registry is
    // one object for the process, so each library sees the same marker.
    [[nodiscard]] static void* claim_marker_(Slot& slot) noexcept { return &slot; }

    [[nodiscard]] static bool is_published_(Slot const& slot, void const* addr) noexcept {
        return addr != nullptr && addr != &slot;
    }

    // Writes the description of a slot that the caller holds, and then
    // publishes its address with release order.
    static void publish_(Slot& slot, void* addr, size_t len, bool huge_hint) noexcept {
        slot.len.store_release(len);
        slot.huge_hint.store_release(huge_hint);
        slot.addr.store_release(addr);
    }

    ::foundation::core::FixedArray<Slot, max_regions> slots_{};
};

inline void register_hot_region(void* addr, size_t len, bool huge_hint = false) noexcept {
    HotRegionRegistry::instance().register_region(addr, len, huge_hint);
}

inline void unregister_hot_region(void* addr) noexcept { HotRegionRegistry::instance().unregister_region(addr); }

// The registry is a pinned singleton, so nothing can hand out a fresh
// one. The handle below is the authorization instead: holding one is
// proof that a start-up context granted access, and reaching the
// singleton directly bypasses that proof.
//
// Registering and withdrawing a region both write a table the whole
// process shares, which is start-up work. The read-only members are
// routed through the handle as well, so that every path to the
// registry looks the same.

template <class Ctx>
concept CtxFitsHotRegionRegistryMint =
    ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

class HotRegionRegistryHandle final : public ::foundation::Pinned<HotRegionRegistryHandle> {
    // The constructor is private, so the mint below is the only way to
    // hold a handle, and the handle proves what the mint checked.
    HotRegionRegistryHandle() noexcept = default;

    template <::foundation::effects::IsExecCtx FriendCtx>
        requires CtxFitsHotRegionRegistryMint<FriendCtx>
    friend constexpr HotRegionRegistryHandle mint_hot_region_registry_handle(FriendCtx const&) noexcept;

public:
    void register_region(void* addr, size_t len, bool huge_hint = false) const noexcept {
        HotRegionRegistry::instance().register_region(addr, len, huge_hint);
    }

    void unregister_region(void* addr) const noexcept { HotRegionRegistry::instance().unregister_region(addr); }

    [[nodiscard]] HotRegionRegistry::Snapshot snapshot() const noexcept {
        return HotRegionRegistry::instance().snapshot();
    }

    [[nodiscard]] size_t size() const noexcept { return HotRegionRegistry::instance().size(); }
};

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsHotRegionRegistryMint<Ctx>
[[nodiscard]] constexpr HotRegionRegistryHandle mint_hot_region_registry_handle(Ctx const&) noexcept {
    return HotRegionRegistryHandle{};
}

}  // namespace crucible::warden
