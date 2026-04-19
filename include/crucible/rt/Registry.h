#pragma once

// Registry of "hot" memory regions — mmap'd or heap-backed buffers the
// Crucible runtime wants kept resident on a production node. Each
// component (TraceRing, MetaLog, PoolAllocator, Cipher hot-tier log,
// KernelCache metadata) self-registers its backing memory at
// construction and unregisters at destruction. `crucible::rt::apply()`
// walks the registry and calls mlock2 + MADV_HUGEPAGE on each entry.
//
// Components stay ignorant of `rt` — they just call the two free
// functions below. No rt headers bleed into their interface. The
// registry is thread-safe via a single coarse mutex; registration is
// expected at init / teardown time, not in hot paths.
//
// Usage (component side):
//
//     TraceRing::TraceRing() noexcept {
//         crucible::rt::register_hot_region(
//             entries, sizeof(entries), /*huge=*/false, "TraceRing");
//     }
//     TraceRing::~TraceRing() {
//         crucible::rt::unregister_hot_region(entries);
//     }
//
// The registry itself has no opinion about whether mlock actually
// runs — that's the Policy's job via `apply()`. Self-registration is
// cheap (one mutex acquisition) and safe in every build configuration.

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace crucible::rt {

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
    [[nodiscard]] static HotRegionRegistry& instance() noexcept {
        static HotRegionRegistry r;
        return r;
    }

    // Adding the same (addr) twice is idempotent — the second call
    // silently overwrites the len/huge_hint/label of the first. The
    // Keeper's apply() sees exactly one entry per unique address.
    void register_region(void* addr, size_t len, bool huge_hint, const char* label) noexcept {
        if (addr == nullptr || len == 0) return;
        std::scoped_lock lk{mu_};
        for (auto& r : regions_) {
            if (r.addr == addr) { r.len = len; r.huge_hint = huge_hint; r.label = label; return; }
        }
        regions_.push_back({addr, len, huge_hint, label});
    }

    // Remove by address. Missing address is silently ignored (destructor
    // ordering relative to apply()/revert() is not guaranteed).
    void unregister_region(void* addr) noexcept {
        if (addr == nullptr) return;
        std::scoped_lock lk{mu_};
        for (auto it = regions_.begin(); it != regions_.end(); ++it) {
            if (it->addr == addr) { regions_.erase(it); return; }
        }
    }

    // Snapshot for apply(). Returns by value so the caller can iterate
    // without holding the mutex while it hits mlock2 syscalls (each
    // ~1 µs). New registrations during iteration are handled on the
    // next apply() call.
    [[nodiscard]] std::vector<HotRegion> snapshot() const noexcept {
        std::scoped_lock lk{mu_};
        return regions_;
    }

    [[nodiscard]] size_t size() const noexcept {
        std::scoped_lock lk{mu_};
        return regions_.size();
    }

    HotRegionRegistry(const HotRegionRegistry&)            = delete("singleton — use instance()");
    HotRegionRegistry& operator=(const HotRegionRegistry&) = delete("singleton — use instance()");
    HotRegionRegistry(HotRegionRegistry&&)                 = delete("singleton — use instance()");
    HotRegionRegistry& operator=(HotRegionRegistry&&)      = delete("singleton — use instance()");

 private:
    HotRegionRegistry() = default;
    ~HotRegionRegistry() = default;

    mutable std::mutex     mu_{};
    std::vector<HotRegion> regions_{};
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

} // namespace crucible::rt
