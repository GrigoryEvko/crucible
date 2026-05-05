#include <crucible/MerkleDag.h>

#include "bench_harness.h"

#include <atomic>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <span>
#include <utility>

namespace {

using crucible::CompiledKernel;
using crucible::ContentHash;
using crucible::KernelCache;
using crucible::RowHash;

constexpr std::array<std::uint64_t, 8> kRowClusterSlotBases{
    1536, 1793, 2050, 2307, 2564, 2821, 3078, 3335,
};
constexpr std::uint64_t kRowsPerCluster = 128;

struct FakeKernel {
    std::uint64_t id = 0;
};

[[nodiscard]] CompiledKernel* fk_ptr(FakeKernel& kernel) noexcept {
    return reinterpret_cast<CompiledKernel*>(&kernel);
}

class RawKernelCache {
public:
    enum class InsertError : std::uint8_t {
        TableFull,
    };

    explicit RawKernelCache(std::uint32_t capacity = 4096)
        : capacity_{capacity}
    {
        table_ = static_cast<Entry*>(std::calloc(capacity_, sizeof(Entry)));
        if (table_ == nullptr) std::abort();
    }

    ~RawKernelCache() { std::free(table_); }

    RawKernelCache(RawKernelCache const&) = delete;
    RawKernelCache& operator=(RawKernelCache const&) = delete;
    RawKernelCache(RawKernelCache&&) = delete;
    RawKernelCache& operator=(RawKernelCache&&) = delete;

    CRUCIBLE_UNSAFE_BUFFER_USAGE
    [[nodiscard, gnu::hot]] CompiledKernel*
    lookup(ContentHash content_hash, RowHash row_hash) const noexcept
        CRUCIBLE_NO_THREAD_SAFETY
        pre (content_hash.raw() != 0)
    {
        [[assume(content_hash.raw() != 0)]];
        const std::uint64_t lookup_hash = content_hash.raw();
        const std::uint64_t lookup_row = row_hash.raw();
        const std::uint32_t mask = capacity_ - 1;
        const std::uint32_t slot_index =
            static_cast<std::uint32_t>(lookup_hash) & mask;

        for (std::uint32_t probe = 0; probe < capacity_; ++probe) {
            Entry& entry = table_[(slot_index + probe) & mask];
            const std::uint64_t key =
                entry.content_hash.load(std::memory_order_acquire);
            if (key == 0) return nullptr;
            if (key != lookup_hash) continue;

            CompiledKernel* kernel =
                entry.kernel.load(std::memory_order_acquire);
            if (kernel == nullptr) [[unlikely]] {
                kernel = await_claimed_(entry);
                if (kernel == nullptr) continue;
            }

            const std::uint64_t entry_row =
                entry.row_hash.load(std::memory_order_acquire);
            if (entry_row == lookup_row) [[likely]] return kernel;
        }

        return nullptr;
    }

    [[nodiscard]] std::expected<void, InsertError>
    insert(ContentHash content_hash, RowHash row_hash,
           CompiledKernel* kernel) noexcept
        CRUCIBLE_NO_THREAD_SAFETY
        pre (content_hash.raw() != 0)
        pre (kernel != nullptr)
    {
        const std::uint64_t lookup_hash = content_hash.raw();
        const std::uint64_t lookup_row = row_hash.raw();
        const std::uint32_t mask = capacity_ - 1;
        const std::uint32_t slot_index =
            static_cast<std::uint32_t>(lookup_hash) & mask;

        for (std::uint32_t probe = 0; probe < capacity_; ++probe) {
            Entry& entry = table_[(slot_index + probe) & mask];
            std::uint64_t expected = 0;
            if (entry.content_hash.compare_exchange_strong(
                    expected, lookup_hash, std::memory_order_acq_rel)) {
                entry.row_hash.store(lookup_row, std::memory_order_release);
                entry.kernel.store(kernel, std::memory_order_release);
                return {};
            }

            if (expected == lookup_hash) {
                CompiledKernel* existing =
                    entry.kernel.load(std::memory_order_acquire);
                for (std::uint32_t spin = 0;
                     spin < kClaimedSpinBudget && existing == nullptr;
                     ++spin) {
                    CRUCIBLE_SPIN_PAUSE;
                    existing = entry.kernel.load(std::memory_order_acquire);
                }
                if (existing != nullptr) {
                    const std::uint64_t existing_row =
                        entry.row_hash.load(std::memory_order_acquire);
                    if (existing_row == lookup_row) {
                        entry.kernel.store(kernel, std::memory_order_release);
                        return {};
                    }
                }
            }
        }

        return std::unexpected(InsertError::TableFull);
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

    [[nodiscard]] static CompiledKernel*
    await_claimed_(Entry const& entry) noexcept {
        for (std::uint32_t spin = 0; spin < kClaimedSpinBudget; ++spin) {
            CRUCIBLE_SPIN_PAUSE;
            CompiledKernel* kernel =
                entry.kernel.load(std::memory_order_acquire);
            if (kernel != nullptr) return kernel;
        }
        return nullptr;
    }

    Entry* table_ = nullptr;
    std::uint32_t capacity_ = 0;
};

template <typename Cache>
void seed_cache(Cache& cache, std::span<FakeKernel> kernels) {
    for (std::size_t i = 0; i < kernels.size(); ++i) {
        kernels[i].id = i + 1;
        auto inserted = cache.insert(
            ContentHash{0x1000 + static_cast<std::uint64_t>(i)},
            RowHash{0},
            fk_ptr(kernels[i]));
        if (!inserted.has_value()) std::abort();
    }
}

template <typename Cache>
void seed_row_siblings(Cache& cache, std::span<FakeKernel> kernels) {
    const std::size_t required =
        kRowClusterSlotBases.size() * static_cast<std::size_t>(kRowsPerCluster);
    if (kernels.size() < required) std::abort();

    std::size_t cursor = 0;
    for (std::size_t cluster = 0; cluster < kRowClusterSlotBases.size();
         ++cluster) {
        const std::uint64_t content =
            0xABC00000ULL | kRowClusterSlotBases[cluster];
        for (std::uint64_t row = 0; row < kRowsPerCluster; ++row) {
            kernels[cursor].id = 0xA000 + cursor;
            auto inserted = cache.insert(
                ContentHash{content},
                RowHash{0x200 + (cluster * kRowsPerCluster) + row},
                fk_ptr(kernels[cursor]));
            if (!inserted.has_value()) std::abort();
            ++cursor;
        }
    }
}

template <typename Cache>
[[nodiscard]] bench::Report bench_hot_hit(char const* name, Cache& cache) {
    std::uint64_t i = 0;
    return bench::Run{name}
        .samples(100'000)
        .warmup(10'000)
        .max_wall_ms(3'000)
        .measure([&] {
            const std::uint64_t idx = (i++ & 1023U);
            CompiledKernel* hit = cache.lookup(
                ContentHash{0x1000 + idx}, RowHash{0});
            bench::do_not_optimize(hit);
        });
}

template <typename Cache>
[[nodiscard]] bench::Report bench_row_sibling_hit(
    char const* name, Cache& cache) {
    std::uint64_t i = 0;
    return bench::Run{name}
        .samples(100'000)
        .warmup(10'000)
        .max_wall_ms(3'000)
        .measure([&] {
            const std::uint64_t idx = i++ & 1023U;
            const std::uint64_t cluster = idx / kRowsPerCluster;
            const std::uint64_t row = idx % kRowsPerCluster;
            CompiledKernel* hit = cache.lookup(
                ContentHash{0xABC00000ULL |
                            kRowClusterSlotBases[cluster]},
                RowHash{0x200 + (cluster * kRowsPerCluster) + row});
            bench::do_not_optimize(hit);
        });
}

template <typename Cache>
[[nodiscard]] bench::Report bench_clean_miss(char const* name, Cache& cache) {
    std::uint64_t i = 0;
    return bench::Run{name}
        .samples(100'000)
        .warmup(10'000)
        .max_wall_ms(3'000)
        .measure([&] {
            CompiledKernel* miss = cache.lookup(
                ContentHash{0x90000000ULL + 1024 + (i++ & 255U)},
                RowHash{0xFF});
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
    KernelCache typed{4096};

    std::array<FakeKernel, 1024> raw_hot{};
    std::array<FakeKernel, 1024> typed_hot{};
    seed_cache(raw, raw_hot);
    seed_cache(typed, typed_hot);

    std::array<FakeKernel, 1024> raw_rows{};
    std::array<FakeKernel, 1024> typed_rows{};
    seed_row_siblings(raw, raw_rows);
    seed_row_siblings(typed, typed_rows);

    std::array reports{
        bench_hot_hit("raw triple KernelCache.lookup hit", raw),
        bench_hot_hit("typed KernelCacheSlot.lookup hit", typed),
        bench_row_sibling_hit("raw triple KernelCache row-sibling hit", raw),
        bench_row_sibling_hit("typed KernelCacheSlot row-sibling hit", typed),
        bench_clean_miss("raw triple KernelCache clean miss", raw),
        bench_clean_miss("typed KernelCacheSlot clean miss", typed),
    };

    bench::emit_reports_text(reports);

    std::puts("\n=== comparisons ===");
    bench::compare(reports[0], reports[1]).print_text();
    bench::compare(reports[2], reports[3]).print_text();
    bench::compare(reports[4], reports[5]).print_text();
    bench::emit_reports_json(reports, bench::env_json());

    return EXIT_SUCCESS;
}
