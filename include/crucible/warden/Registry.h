#pragma once

// The memory regions a production node should keep resident. A
// component names its own backing memory when it is constructed and
// withdraws it when it is destroyed, through the two free functions
// below, so no component needs to know anything else about this layer.
// A component gives its memory as a View of bytes, and storage_of of
// fixy/os/Pages.h gives that View for an object or a run of elements.
//
// The registry decides nothing. It records regions, and applying a
// policy is what locks, advises or faults them in, if the policy says so.
//
// Registration is safe from any thread, and belongs to construction
// and teardown rather than to a hot path.  A lock of the registry orders
// each registration, withdrawal and snapshot, so a snapshot gives each
// region with the length of its own registration.

#include <fixy/Core.h>
#include <fixy/os/SpinLock.h>
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

// The count n rounded up to whole huge pages, or no value when the rounded
// count does not fit in size_t.
[[nodiscard]] constexpr ::fixy::Option<size_t> round_up_huge(size_t n) noexcept {
    return ::fixy::align_up(n, kHugePageBytes);
}

// pad holds the seven bytes after the hint, so a region has no padding
// byte.  A snapshot returns up to 256 regions, and with
// -ftrivial-auto-var-init=zero GCC writes zero to each padding hole of each
// region of the returned list, one store for each hole.
// test/test_warden_policy.cpp makes sure that the region has no padding byte.
struct HotRegion {
    // The bytes of the region.  The View borrows the memory of the
    // component that registered it, and the component keeps that memory
    // alive until it withdraws the region.
    ::fixy::View<std::byte const> bytes{};
    // True for a large mapping that rarely changes shape. False for a
    // buffer that is resized often, where collapsing it into huge
    // pages would stall the resize.
    bool huge_hint = false;
    ::fixy::FixedArray<std::uint8_t, 7> pad{};
};

class HotRegionRegistry {
public:
    static constexpr size_t max_regions = 256;

    // The regions of a snapshot.  The list holds at most the regions of the
    // registry, so it needs no heap.
    using Snapshot = ::fixy::FixedList<HotRegion, max_regions>;

    // One registry for the process.  The two vessel libraries both reach it.
    CRUCIBLE_PROCESS_WIDE [[nodiscard]] static HotRegionRegistry& instance() noexcept {
        static HotRegionRegistry r;
        return r;
    }

    // A region is known by its first byte.  Registering a region whose first
    // byte is the first byte of a registered region replaces that region
    // rather than adding a second one, so a reader of the table sees exactly
    // one region for each first byte.  An empty region is ignored.  A
    // registration when the registry holds 256 regions ends the process.
    //
    // A registration and a withdrawal are the work of a constructor and of a
    // destructor, so no caller inlines them.  The destructor of an owner
    // then pays one call and keeps its code small.
    CRUCIBLE_NOINLINE void register_region(::fixy::View<std::byte const> bytes, bool huge_hint) noexcept {
        if (bytes.size() == 0) return;
        HotRegion const region{.bytes = bytes, .huge_hint = huge_hint};
        bool const is_stored = regions_.with_at_owner_start_or_end(
            [&region](Snapshot& regions) noexcept { return store_(regions, region); });
        if (!is_stored) {
            ::fixy::fatal("HotRegionRegistry: the registry holds 256 regions, so the registration has no room");
        }
    }

    // Withdraws the region whose first byte is the first byte of bytes.  A
    // region that is not registered is ignored, because nothing orders a
    // component's destruction against a policy being undone.
    CRUCIBLE_NOINLINE void unregister_region(::fixy::View<std::byte const> bytes) noexcept {
        if (bytes.size() == 0) return;
        regions_.with_at_owner_start_or_end([bytes](Snapshot& regions) noexcept { remove_(regions, bytes); });
    }

    // Returned by value so that a caller can work through the entries
    // without holding the table while it issues system calls. A region
    // registered during that work is picked up the next time a policy
    // is applied.  The body copies the regions into the list of the call:
    // each View of a region borrows the memory of its component, not the
    // table, so the copy outlives the lock.  Complexity: a copy of at most
    // 256 regions.
    [[nodiscard]] Snapshot snapshot() const noexcept {
        Snapshot copy{};
        regions_.with_at_owner_start_or_end([&copy](Snapshot const& regions) noexcept { copy = regions; });
        return copy;
    }

    [[nodiscard]] size_t size() const noexcept {
        return regions_.with_at_owner_start_or_end([](Snapshot const& regions) noexcept { return regions.size(); });
    }

    HotRegionRegistry(const HotRegionRegistry&) = delete("singleton — use instance()");
    HotRegionRegistry& operator=(const HotRegionRegistry&) = delete("singleton — use instance()");
    HotRegionRegistry(HotRegionRegistry&&) = delete("singleton — use instance()");
    HotRegionRegistry& operator=(HotRegionRegistry&&) = delete("singleton — use instance()");

private:
    HotRegionRegistry() = default;
    ~HotRegionRegistry() = default;

    // True when the two Views start at one byte.  An empty View starts at
    // no byte.
    [[nodiscard]] static bool is_same_start_(::fixy::View<std::byte const> first,
                                             ::fixy::View<std::byte const> second) noexcept {
        auto const first_start = first.get(0);
        auto const second_start = second.get(0);
        for (auto const first_byte : first_start) {
            for (auto const second_byte : second_start) {
                return ::fixy::same_object(first_byte, second_byte);
            }
        }
        return false;
    }

    // The position of the region that starts at the first byte of bytes.
    // The caller holds the lock: it gives the list of the lock.
    // Complexity: O(regions).
    [[nodiscard]] static ::fixy::Option<size_t> position_of_(Snapshot const& regions,
                                                             ::fixy::View<std::byte const> bytes) noexcept {
        size_t position = 0;
        for (HotRegion const& region : regions) {
            if (is_same_start_(region.bytes, bytes)) return ::fixy::some(position);
            ++position;
        }
        return ::fixy::none;
    }

    // Replaces the region with the first byte of region, or adds region.
    // False when the list is full.  The caller holds the lock.
    [[nodiscard]] static bool store_(Snapshot& regions, HotRegion const& region) noexcept {
        for (size_t const position : position_of_(regions, region.bytes)) {
            *regions.get(position).expect("the search found the region at the position") = region;
            return true;
        }
        return regions.append(region).is_ok();
    }

    // Moves the last region into the place of the withdrawn region.  The
    // caller holds the lock.
    static void remove_(Snapshot& regions, ::fixy::View<std::byte const> bytes) noexcept {
        for (size_t const position : position_of_(regions, bytes)) {
            HotRegion const last = regions.take_last().expect("the list holds the region that the search found");
            if (position < regions.size()) {
                *regions.get(position).expect("the position is below the size of the list") = last;
            }
        }
    }

    // The tag of the list.  It names this class as the one owner of the door
    // of the lock with no context (with_at_owner_start_or_end of
    // fixy/os/SpinLock.h): a registration runs in the constructor of a
    // component and a withdrawal in its destructor, and neither holds a
    // context.  The residual: a registration and a withdrawal block on the
    // lock with no Block row in the type of their caller.
    struct RegionsTag final {
        using start_or_end_owner = HotRegionRegistry;
    };

    // The list of the regions, which only a body inside the lock reaches.  A
    // waiter sleeps in the kernel, because a holder can lose its core during
    // a registration.  An uncontended lock and unlock make no system call.
    ::fixy::spin::BlockingLocked<Snapshot, RegionsTag> regions_{};
};

inline void register_hot_region(::fixy::View<std::byte const> bytes, bool huge_hint = false) noexcept {
    HotRegionRegistry::instance().register_region(bytes, huge_hint);
}

inline void unregister_hot_region(::fixy::View<std::byte const> bytes) noexcept {
    HotRegionRegistry::instance().unregister_region(bytes);
}

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
    void register_region(::fixy::View<std::byte const> bytes, bool huge_hint = false) const noexcept {
        HotRegionRegistry::instance().register_region(bytes, huge_hint);
    }

    void unregister_region(::fixy::View<std::byte const> bytes) const noexcept {
        HotRegionRegistry::instance().unregister_region(bytes);
    }

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
