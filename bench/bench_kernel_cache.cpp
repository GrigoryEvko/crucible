#include <crucible/KernelCache.h>
#include <foundation/contracts/Pre.h>

#include "bench_harness.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace {

using crucible::CompiledKernel;
using crucible::ContentHash;
using crucible::KernelCache;
using crucible::RowHash;

constexpr ::fixy::FixedArray<std::uint64_t, 8> kRowClusterSlotBases{
    1536, 1793, 2050, 2307, 2564, 2821, 3078, 3335,
};
constexpr std::uint64_t kRowsPerCluster = 128;
// The kernels of each seed: the hot set, and the row siblings of the
// clusters.
constexpr std::size_t kSeedKernels = 1024;

// The tag of Nondet of each lookup and each insert of the bench.
constexpr ::foundation::effects::cap::Nondet kNondet = ::foundation::effects::testing::test().nondet;

struct FakeKernel {
    std::uint64_t id = 0;
};

// The cache never reads a kernel, so the address of a fake object stands
// for one.
[[nodiscard]] KernelCache::KernelRef kernel_ref(FakeKernel& kernel) noexcept {
    return ::fixy::erase_brand(::fixy::to(*static_cast<CompiledKernel*>(static_cast<void*>(&kernel))));
}

}  // namespace

// The raw triple table is the reference of the typed cache: three
// std::atomic words in each entry of a table from calloc, as KernelCache had
// before its typed slot.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the raw triple table of calloc is the reference of the typed kernel cache")
namespace {

class RawKernelCache {
public:
    enum class InsertError : std::uint8_t {
        TableFull,
    };

    explicit RawKernelCache(std::uint32_t capacity = 4096) : capacity_{capacity} {
        table_ = static_cast<Entry*>(std::calloc(capacity_, sizeof(Entry)));
        CRUCIBLE_BENCH_CHECK(table_ != nullptr);
    }

    ~RawKernelCache() { std::free(table_); }

    RawKernelCache(RawKernelCache const&) = delete;
    RawKernelCache& operator=(RawKernelCache const&) = delete;
    RawKernelCache(RawKernelCache&&) = delete;
    RawKernelCache& operator=(RawKernelCache&&) = delete;

    // The raw table takes the tag of Nondet that the typed cache takes, so one
    // body of the bench calls the two caches.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard, gnu::hot]] CompiledKernel*
    lookup(::foundation::effects::cap::Nondet /*nondet*/, ContentHash content_hash, RowHash row_hash) const noexcept
        CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(content_hash.raw() != 0);
        const std::uint64_t lookup_hash = content_hash.raw();
        const std::uint64_t lookup_row = row_hash.raw();
        const std::uint32_t mask = capacity_ - 1;
        const std::uint32_t slot_index = static_cast<std::uint32_t>(lookup_hash) & mask;

        for (std::uint32_t probe = 0; probe < capacity_; ++probe) {
            Entry& entry = table_[(slot_index + probe) & mask];
            const std::uint64_t key = entry.content_hash.load(std::memory_order_acquire);
            if (key == 0) return nullptr;
            if (key != lookup_hash) continue;

            CompiledKernel* kernel = entry.kernel.load(std::memory_order_acquire);
            if (kernel == nullptr) [[unlikely]] {
                kernel = await_claimed_(entry);
                if (kernel == nullptr) continue;
            }

            const std::uint64_t entry_row = entry.row_hash.load(std::memory_order_acquire);
            if (entry_row == lookup_row) [[likely]]
                return kernel;
        }

        return nullptr;
    }

    // The typed cache takes the same kernel reference, so one seed serves the
    // two caches.  The raw table keeps the address of the kernel.
    [[nodiscard]] ::fixy::Result<::fixy::Unit, InsertError> insert(::foundation::effects::cap::Nondet /*nondet*/,
                                                                   ContentHash content_hash, RowHash row_hash,
                                                                   KernelCache::KernelRef kernel_ref) noexcept
        CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(content_hash.raw() != 0);
        CompiledKernel* const kernel = &*kernel_ref;
        const std::uint64_t lookup_hash = content_hash.raw();
        const std::uint64_t lookup_row = row_hash.raw();
        const std::uint32_t mask = capacity_ - 1;
        const std::uint32_t slot_index = static_cast<std::uint32_t>(lookup_hash) & mask;

        for (std::uint32_t probe = 0; probe < capacity_; ++probe) {
            Entry& entry = table_[(slot_index + probe) & mask];
            std::uint64_t expected = 0;
            if (entry.content_hash.compare_exchange_strong(expected, lookup_hash, std::memory_order_acq_rel)) {
                entry.row_hash.store(lookup_row, std::memory_order_release);
                entry.kernel.store(kernel, std::memory_order_release);
                return ::fixy::Unit{};
            }

            if (expected == lookup_hash) {
                CompiledKernel* existing = entry.kernel.load(std::memory_order_acquire);
                for (std::uint32_t spin = 0; spin < kClaimedSpinBudget && existing == nullptr; ++spin) {
                    CRUCIBLE_SPIN_PAUSE;
                    existing = entry.kernel.load(std::memory_order_acquire);
                }
                if (existing != nullptr) {
                    const std::uint64_t existing_row = entry.row_hash.load(std::memory_order_acquire);
                    if (existing_row == lookup_row) {
                        entry.kernel.store(kernel, std::memory_order_release);
                        return ::fixy::Unit{};
                    }
                }
            }
        }

        return ::fixy::err(InsertError::TableFull);
    }

private:
    static constexpr std::uint32_t kClaimedSpinBudget = 64;

    struct Entry {
        std::atomic<std::uint64_t> content_hash{0};
        std::atomic<std::uint64_t> row_hash{0};
        std::atomic<CompiledKernel*> kernel{nullptr};
    };

    static_assert(sizeof(Entry) == 24);
    static_assert(alignof(Entry) == 8);

    [[nodiscard]] static CompiledKernel* await_claimed_(Entry const& entry) noexcept {
        for (std::uint32_t spin = 0; spin < kClaimedSpinBudget; ++spin) {
            CRUCIBLE_SPIN_PAUSE;
            CompiledKernel* kernel = entry.kernel.load(std::memory_order_acquire);
            if (kernel != nullptr) return kernel;
        }
        return nullptr;
    }

    Entry* table_ = nullptr;
    std::uint32_t capacity_ = 0;
};

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

template <typename Cache>
void seed_cache(Cache& cache, ::fixy::FixedArray<FakeKernel, kSeedKernels>& kernels) {
    for (auto [index, kernel] : kernels.view().enumerate()) {
        kernel->id = index.raw() + 1;
        auto inserted = cache.insert(kNondet, ContentHash{0x1000 + static_cast<std::uint64_t>(index.raw())}, RowHash{0},
                                     kernel_ref(*kernel));
        CRUCIBLE_BENCH_CHECK(inserted.is_ok());
    }
}

template <typename Cache>
void seed_row_siblings(Cache& cache, ::fixy::FixedArray<FakeKernel, kSeedKernels>& kernels) {
    const std::size_t required = kRowClusterSlotBases.size() * static_cast<std::size_t>(kRowsPerCluster);
    CRUCIBLE_BENCH_CHECK(kernels.size() >= required);

    std::size_t cursor = 0;
    for (auto [cluster, base] : kRowClusterSlotBases.view().enumerate()) {
        const std::uint64_t content = 0xABC00000ULL | *base;
        for (std::uint64_t row = 0; row < kRowsPerCluster; ++row) {
            FakeKernel& kernel = *kernels.get(cursor).expect("the seed holds a kernel for each row of a cluster");
            kernel.id = 0xA000 + cursor;
            auto inserted = cache.insert(kNondet, ContentHash{content},
                                         RowHash{0x200 + (cluster.raw() * kRowsPerCluster) + row}, kernel_ref(kernel));
            CRUCIBLE_BENCH_CHECK(inserted.is_ok());
            ++cursor;
        }
    }
}

template <typename Cache>
[[nodiscard]] bench::Report bench_hot_hit(::fixy::TextView name, Cache& cache) {
    std::uint64_t i = 0;
    return bench::Run{name}.samples(100000).warmup(10000).max_wall_ms(3000).measure([&] {
        const std::uint64_t idx = (i++ & 1023U);
        auto hit = cache.lookup(kNondet, ContentHash{0x1000 + idx}, RowHash{0});
        bench::do_not_optimize(hit);
    });
}

template <typename Cache>
[[nodiscard]] bench::Report bench_row_sibling_hit(::fixy::TextView name, Cache& cache) {
    std::uint64_t i = 0;
    return bench::Run{name}.samples(100000).warmup(10000).max_wall_ms(3000).measure([&] {
        const std::uint64_t idx = i++ & 1023U;
        const std::uint64_t cluster = idx / kRowsPerCluster;
        const std::uint64_t row = idx % kRowsPerCluster;
        // idx / 128 is below 8, so the compiler folds the check of the index,
        // and at() of a proven index reads the base with no test.
        auto hit = cache.lookup(
            kNondet,
            ContentHash{0xABC00000ULL
                        | kRowClusterSlotBases.at(::fixy::Index<8>::check(cluster).expect("idx / 128 is below 8"))},
            RowHash{0x200 + (cluster * kRowsPerCluster) + row});
        bench::do_not_optimize(hit);
    });
}

template <typename Cache>
[[nodiscard]] bench::Report bench_clean_miss(::fixy::TextView name, Cache& cache) {
    std::uint64_t i = 0;
    return bench::Run{name}.samples(100000).warmup(10000).max_wall_ms(3000).measure([&] {
        auto miss = cache.lookup(kNondet, ContentHash{0x90000000ULL + 1024 + (i++ & 255U)}, RowHash{0xFF});
        bench::do_not_optimize(miss);
    });
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    static_assert(sizeof(KernelCache::KernelCacheSlot) == 24);
    static_assert(alignof(KernelCache::KernelCacheSlot) == 8);

    RawKernelCache raw{4096};
    KernelCache typed{::foundation::effects::testing::test().alloc, 4096};

    ::fixy::FixedArray<FakeKernel, kSeedKernels> raw_hot{};
    ::fixy::FixedArray<FakeKernel, kSeedKernels> typed_hot{};
    seed_cache(raw, raw_hot);
    seed_cache(typed, typed_hot);

    ::fixy::FixedArray<FakeKernel, kSeedKernels> raw_rows{};
    ::fixy::FixedArray<FakeKernel, kSeedKernels> typed_rows{};
    seed_row_siblings(raw, raw_rows);
    seed_row_siblings(typed, typed_rows);

    ::fixy::FixedArray<bench::Report, 6> reports{
        bench_hot_hit("raw triple KernelCache.lookup hit", raw),
        bench_hot_hit("typed KernelCacheSlot.lookup hit", typed),
        bench_row_sibling_hit("raw triple KernelCache row-sibling hit", raw),
        bench_row_sibling_hit("typed KernelCacheSlot row-sibling hit", typed),
        bench_clean_miss("raw triple KernelCache clean miss", raw),
        bench_clean_miss("typed KernelCacheSlot clean miss", typed),
    };

    bench::emit_reports_text(reports.view());

    bench::print("\n=== comparisons ===\n");
    bench::compare(reports.at(::fixy::index<0>()), reports.at(::fixy::index<1>())).print_text();
    bench::compare(reports.at(::fixy::index<2>()), reports.at(::fixy::index<3>())).print_text();
    bench::compare(reports.at(::fixy::index<4>()), reports.at(::fixy::index<5>())).print_text();
    bench::emit_reports_json(reports.view(), bench::env_json());

    return 0;
}
