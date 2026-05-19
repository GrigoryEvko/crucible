#pragma once

// Registry of "hot" memory regions — mmap'd or heap-backed buffers the
// Crucible runtime wants kept resident on a production node. Each
// component (TraceRing, MetaLog, PoolAllocator, Cipher hot-tier log,
// KernelCache metadata) self-registers its backing memory at
// construction and unregisters at destruction. `crucible::warden::apply()`
// walks the registry and calls mlock2 + MADV_HUGEPAGE on each entry.
//
// Components stay ignorant of Warden internals — they just call the two free
// functions below. No Warden headers bleed into their interface. The
// registry is thread-safe via fixed atomic slots; registration is
// expected at init / teardown time, not in hot paths.
//
// Usage (component side):
//
//     TraceRing::TraceRing() noexcept {
//         crucible::warden::register_hot_region(
//             entries, sizeof(entries), /*huge=*/false, "TraceRing");
//     }
//     TraceRing::~TraceRing() {
//         crucible::warden::unregister_hot_region(entries);
//     }
//
// The registry itself has no opinion about whether mlock actually
// runs — that's the Policy's job via `apply()`. Self-registration is
// cheap (one slot CAS) and safe in every build configuration.

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <inplace_vector>

namespace crucible::warden {

// 2 MB huge page on x86-64 + aarch64. Required alignment for
// MADV_HUGEPAGE since kernel 5.8 (earlier kernels rounded silently).
inline constexpr size_t kHugePageBytes = 2 * 1024 * 1024;

[[nodiscard]] constexpr size_t round_up_huge(size_t n) noexcept {
    return (n + kHugePageBytes - 1) & ~(kHugePageBytes - 1);
}

struct HotRegion {
    void*       addr       = nullptr;
    size_t      len        = 0;
    // True for large mostly-static mappings that benefit from 2 MB
    // hugepages (MemoryPlan pools, KernelCache). False for frequently-
    // resized buffers where THP collapse would stall.
    bool        huge_hint  = false;
    // Optional short label for deploy-health diagnostics. Caller owns
    // the storage; typically a string literal.
    const char* label      = "";
};

class HotRegionRegistry {
 public:
    static constexpr size_t max_regions = 256;

    [[nodiscard]] static HotRegionRegistry& instance() noexcept {
        static HotRegionRegistry r;
        return r;
    }

    // Adding the same (addr) twice is idempotent — the second call
    // silently overwrites the len/huge_hint/label of the first. The
    // Keeper's apply() sees exactly one entry per unique address.
    void register_region(void* addr, size_t len, bool huge_hint, const char* label) noexcept {
        if (addr == nullptr || len == 0) return;
        for (;;) {
            for (auto& slot : slots_) {
                void* current = slot.addr.load(std::memory_order_acquire);
                if (current != addr) continue;
                if (slot.addr.compare_exchange_strong(
                        current, claimed_addr(),
                        std::memory_order_acq_rel,
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
                if (slot.addr.compare_exchange_strong(
                        expected, claimed_addr(),
                        std::memory_order_acq_rel,
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

    // Remove by address. Missing address is silently ignored (destructor
    // ordering relative to apply()/revert() is not guaranteed).
    void unregister_region(void* addr) noexcept {
        if (addr == nullptr) return;
        for (auto& slot : slots_) {
            void* expected = addr;
            if (slot.addr.compare_exchange_strong(
                    expected, nullptr,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return;
            }
        }
    }

    // Snapshot for apply(). Returns by value so the caller can iterate
    // without coupling registry reads to mlock2 syscalls (each ~1 us).
    // New registrations during iteration are handled on the next
    // apply() call.
    //
    // CLAUDE.md §IV: known max → std::inplace_vector<T, N>.  Zero heap,
    // true O(1) push_back, contract-checked overflow.  Capacity bound
    // is max_regions; push_back here is structurally bounded by the
    // outer for-loop (one push per non-empty slot, slots_.size() == N).
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

    HotRegionRegistry(const HotRegionRegistry&)            = delete("singleton — use instance()");
    HotRegionRegistry& operator=(const HotRegionRegistry&) = delete("singleton — use instance()");
    HotRegionRegistry(HotRegionRegistry&&)                 = delete("singleton — use instance()");
    HotRegionRegistry& operator=(HotRegionRegistry&&)      = delete("singleton — use instance()");

 private:
    HotRegionRegistry() = default;
    ~HotRegionRegistry() = default;

    struct Slot {
        std::atomic<void*> addr{nullptr};
        std::atomic<size_t> len{0};
        std::atomic<bool> huge_hint{false};
        std::atomic<const char*> label{""};
    };

    [[nodiscard]] static void* claimed_addr() noexcept {
        return std::bit_cast<void*>(uintptr_t{1});
    }

    std::array<Slot, max_regions> slots_{};
};

// Convenience free functions. Prefer these at call sites — the class
// name is verbose and rarely wanted directly.
inline void register_hot_region(void*       addr,
                                size_t      len,
                                bool        huge_hint = false,
                                const char* label     = "") noexcept {
    HotRegionRegistry::instance().register_region(addr, len, huge_hint, label);
}

inline void unregister_hot_region(void* addr) noexcept {
    HotRegionRegistry::instance().unregister_region(addr);
}

} // namespace crucible::warden
