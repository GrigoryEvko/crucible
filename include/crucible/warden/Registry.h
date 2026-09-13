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

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/Pinned.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <inplace_vector>

namespace crucible::warden {

// The huge page size on both supported architectures. From Linux 5.8
// the huge-page advice call demands this alignment, where an earlier
// kernel rounded to it without saying so.
inline constexpr size_t kHugePageBytes = 2 * 1024 * 1024;

[[nodiscard]] constexpr size_t round_up_huge(size_t n) noexcept {
    return (n + kHugePageBytes - 1) & ~(kHugePageBytes - 1);
}

struct HotRegion {
    void* addr = nullptr;
    size_t len = 0;
    // True for a large mapping that rarely changes shape. False for a
    // buffer that is resized often, where collapsing it into huge
    // pages would stall the resize.
    bool huge_hint = false;
    // Borrowed, and usually a string literal. The caller keeps it
    // alive for as long as the region stays registered.
    const char* label = "";
};

class HotRegionRegistry {
public:
    static constexpr size_t max_regions = 256;

    [[nodiscard]] static HotRegionRegistry& instance() noexcept {
        static HotRegionRegistry r;
        return r;
    }

    // Registering one address twice replaces the first description
    // rather than adding a second entry, so a reader of the table sees
    // exactly one entry per address.
    void register_region(void* addr, size_t len, bool huge_hint, const char* label) noexcept {
        if (addr == nullptr || len == 0) return;
        for (;;) {
            for (auto& slot : slots_) {
                void* current = slot.addr.load(std::memory_order_acquire);
                if (current != addr) continue;
                if (slot.addr.compare_exchange_strong(current, claimed_addr(), std::memory_order_acq_rel,
                                                      std::memory_order_acquire)) {
                    slot.len.store(len, std::memory_order_relaxed);
                    slot.huge_hint.store(huge_hint, std::memory_order_relaxed);
                    slot.label.store(label, std::memory_order_relaxed);
                    slot.addr.store(addr, std::memory_order_release);
                    return;
                }
            }

            for (auto& slot : slots_) {
                void* expected = nullptr;
                if (slot.addr.compare_exchange_strong(expected, claimed_addr(), std::memory_order_acq_rel,
                                                      std::memory_order_acquire)) {
                    slot.len.store(len, std::memory_order_relaxed);
                    slot.huge_hint.store(huge_hint, std::memory_order_relaxed);
                    slot.label.store(label, std::memory_order_relaxed);
                    slot.addr.store(addr, std::memory_order_release);
                    return;
                }
            }

            std::abort();
        }
    }

    // An address that is not registered is ignored, because nothing
    // orders a component's destruction against a policy being undone.
    void unregister_region(void* addr) noexcept {
        if (addr == nullptr) return;
        for (auto& slot : slots_) {
            void* expected = addr;
            if (slot.addr.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel,
                                                  std::memory_order_acquire)) {
                return;
            }
        }
    }

    // Returned by value so that a caller can work through the entries
    // without holding the table while it issues system calls. A region
    // registered during that work is picked up the next time a policy
    // is applied.
    //
    // The result needs no heap: one entry is pushed per occupied slot,
    // and the slot count is the capacity.
    [[nodiscard]] std::inplace_vector<HotRegion, max_regions> snapshot() const noexcept {
        std::inplace_vector<HotRegion, max_regions> out;
        for (const auto& slot : slots_) {
            void* addr = slot.addr.load(std::memory_order_acquire);
            if (addr == nullptr || addr == claimed_addr()) continue;
            out.push_back(HotRegion{
                .addr = addr,
                .len = slot.len.load(std::memory_order_relaxed),
                .huge_hint = slot.huge_hint.load(std::memory_order_relaxed),
                .label = slot.label.load(std::memory_order_relaxed),
            });
        }
        return out;
    }

    [[nodiscard]] size_t size() const noexcept {
        size_t count = 0;
        for (const auto& slot : slots_) {
            void* addr = slot.addr.load(std::memory_order_acquire);
            if (addr != nullptr && addr != claimed_addr()) ++count;
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

    struct Slot {
        std::atomic<void*> addr{nullptr};
        std::atomic<size_t> len{0};
        std::atomic<bool> huge_hint{false};
        std::atomic<const char*> label{""};
    };

    // A slot is written by whoever registers a region and read by
    // everyone else. On an architecture lacking the instruction, the
    // library substitutes a mutex-backed atomic without a word, which
    // would put a lock behind every read of this table. Refuse to
    // build instead.
    static_assert(std::atomic<void*>::is_always_lock_free, "std::atomic<void*> is not lock-free on this target.");
    static_assert(std::atomic<size_t>::is_always_lock_free, "std::atomic<size_t> is not lock-free on this target.");
    static_assert(std::atomic<bool>::is_always_lock_free, "std::atomic<bool> is not lock-free on this target.");
    static_assert(std::atomic<const char*>::is_always_lock_free,
                  "std::atomic<const char*> is not lock-free on this target.");

    [[nodiscard]] static void* claimed_addr() noexcept { return std::bit_cast<void*>(uintptr_t{1}); }

    std::array<Slot, max_regions> slots_{};
};

inline void register_hot_region(void* addr, size_t len, bool huge_hint = false, const char* label = "") noexcept {
    HotRegionRegistry::instance().register_region(addr, len, huge_hint, label);
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

class HotRegionRegistryHandle final : public ::crucible::safety::Pinned<HotRegionRegistryHandle> {
public:
    HotRegionRegistryHandle() noexcept = default;

    void register_region(void* addr, size_t len, bool huge_hint = false, const char* label = "") const noexcept {
        HotRegionRegistry::instance().register_region(addr, len, huge_hint, label);
    }

    void unregister_region(void* addr) const noexcept { HotRegionRegistry::instance().unregister_region(addr); }

    [[nodiscard]] std::inplace_vector<HotRegion, HotRegionRegistry::max_regions> snapshot() const noexcept {
        return HotRegionRegistry::instance().snapshot();
    }

    [[nodiscard]] size_t size() const noexcept { return HotRegionRegistry::instance().size(); }
};

static_assert(sizeof(HotRegionRegistryHandle) == 1, "HotRegionRegistryHandle must be the 1-byte authorization token; "
                                                    "the underlying registry state lives in the Pinned singleton.");

template <class Ctx>
concept CtxFitsHotRegionRegistryMint =
    effects::IsExecCtx<Ctx> && effects::CtxOwnsCapability<Ctx, effects::Effect::Init>;

template <effects::IsExecCtx Ctx>
    requires CtxFitsHotRegionRegistryMint<Ctx>
[[nodiscard]] constexpr HotRegionRegistryHandle mint_hot_region_registry_handle(Ctx const&) noexcept {
    return HotRegionRegistryHandle{};
}

static_assert(CtxFitsHotRegionRegistryMint<effects::ColdInitCtx>);
static_assert(!CtxFitsHotRegionRegistryMint<effects::BgDrainCtx>);
static_assert(!CtxFitsHotRegionRegistryMint<effects::HotFgCtx>);

}  // namespace crucible::warden
