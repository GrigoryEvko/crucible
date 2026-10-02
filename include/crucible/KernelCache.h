#pragma once

// The kernel cache and the one function that reads it while it builds the
// DAG.  The cache maps (ContentHash, RowHash) to a compiled kernel.  A slot
// write needs a writer permission, so this header includes the permission
// layer, and MerkleDag.h does not.

#include <crucible/MerkleDag.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/Types.h>
#include <fixy/Tagged.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <new>
#include <span>
#include <utility>

namespace crucible {

// Open-addressing map from (ContentHash, RowHash) to a compiled kernel.
// Capacity is a power of two so that `(slot + probe) & mask` wraps.
//
// The effect row is half the key, not a payload.  Two regions with identical
// ops but different rows must occupy different slots, because only some rows
// are shareable between machines.  A row mismatch is therefore a probe
// continuation and never a hit.  RowHash{0} is the bare-type baseline and is
// itself a valid key, not an absence.
//
// A slot is EMPTY (content 0), CLAIMED (content claimed by CAS, kernel still
// null) or PUBLISHED (both set).  Slots are insert-only.  row_hash is written
// once per slot.  A later publish under the same pair replaces only the
// kernel, so a concurrent reader sees one kernel or the other, both valid.
//
// The publish order is row_hash then kernel, both release.  kernel.store is
// therefore the last store of the sequence, and a reader that acquire-loads a
// non-null kernel is guaranteed to see the row that was published with it.
//
// Both lookup and insert spin on the kernel, never on row_hash, when they
// meet a CLAIMED slot.  row_hash defaults to 0 and 0 is also a legal row, so
// a spin on the row cannot tell "not published yet" from "published with row
// 0": an insert making that mistake would write into a slot whose row belongs
// to another inserter, and that inserter's own publish would then overwrite
// it, losing an insert that reported success.
//
// When the spin budget runs out the slot is treated as foreign and probing
// continues.  A stalled claim at one probe position must not mask a valid
// match further along the chain.  The worst case is a second slot for one
// pair, which wastes space and loses nothing.
class CRUCIBLE_OWNER KernelCache {
public:
    // A slot write touches only the slot's three atomics in memory, so the
    // writer token incurs no effect.
    struct KernelCompileTag {
        using permission_row = ::foundation::effects::Row<>;
    };
    struct KernelCacheReaderTag {};

    // The three levels of the cache, ordered by portability: the
    // vendor-neutral form, the form for one vendor family, and the
    // compiled bytes for one chip.  A value read from or published to a
    // level carries that level as its provenance tag.  The levels are not
    // residency classes, and no level stands in for another, so the tags
    // have no conversion between them.
    struct VendorNeutralLevel {};
    struct VendorFamilyLevel {};
    struct ChipLevel {};

    template <typename Level, typename T>
    using AtLevel = ::fixy::Tagged<T, Level>;

    struct KernelCacheSlotSnapshot {
        uint64_t content_hash = 0;
        uint64_t row_hash = 0;
        CompiledKernel* kernel = nullptr;
    };

    static_assert(sizeof(KernelCacheSlotSnapshot) == 24, "KernelCacheSlotSnapshot must stay the 24-byte wire triple: "
                                                         "8B content + 8B row + 8B kernel pointer.");
    static_assert(alignof(KernelCacheSlotSnapshot) == 8, "KernelCacheSlotSnapshot must stay naturally 8-byte aligned.");

    // A writer and reader surface over three plain atomics.  A single-writer
    // snapshot session cannot be embedded here: it isolates its sequence
    // counter and storage on separate cache lines and carries a reader pool,
    // both of which break the 24-byte slot.  Keeping the three atomics also
    // lets the hot lookup read the fields one at a time in probe order.
    //
    // A content hash of zero is the EMPTY marker, so every endpoint rejects
    // it.  Admitting zero would claim the wrong slot, publish a kernel that
    // any later claim silently overwrites, or hand a stale kernel back to a
    // fresh lookup.
    class KernelCacheSlot {
        friend class KernelCache;

    public:
        using snapshot_type = KernelCacheSlotSnapshot;
        using writer_tag = KernelCompileTag;
        using reader_tag = KernelCacheReaderTag;

        KernelCacheSlot() noexcept = default;

        class WriterHandle {
            KernelCacheSlot* slot_ = nullptr;
            [[no_unique_address]] ::foundation::permissions::Permission<writer_tag> perm_;

            constexpr WriterHandle(KernelCacheSlot& slot,
                                   ::foundation::permissions::Permission<writer_tag>&& perm) noexcept
                : slot_{&slot}, perm_{std::move(perm)} {}

            friend class KernelCacheSlot;

        public:
            using value_type = snapshot_type;
            using tag_type = writer_tag;

            WriterHandle(WriterHandle const&) =
                delete("KernelCacheSlot::WriterHandle owns the linear writer permission");
            WriterHandle&
            operator=(WriterHandle const&) = delete("KernelCacheSlot::WriterHandle owns the linear writer permission");
            constexpr WriterHandle(WriterHandle&&) noexcept = default;
            constexpr WriterHandle& operator=(WriterHandle&&) noexcept = default;

            void publish(snapshot_type const& snapshot) noexcept {
                CRUCIBLE_PRE(::foundation::decide::is_non_zero(snapshot.content_hash));
                CRUCIBLE_PRE(snapshot.kernel != nullptr);
                // The content hash is already claimed by CAS before this
                // endpoint exists.  The two stores must stay in this order:
                // the kernel store is what makes the row visible to readers.
                contract_assert(slot_->content_hash_.load(std::memory_order_acquire) == snapshot.content_hash);
                slot_->row_hash_.store(snapshot.row_hash, std::memory_order_release);
                slot_->kernel_.store(snapshot.kernel, std::memory_order_release);
            }

            void publish_kernel_variant(CompiledKernel* kernel) noexcept {
                CRUCIBLE_PRE(kernel != nullptr);
                slot_->kernel_.store(kernel, std::memory_order_release);
            }
        };

        class ReaderHandle {
            KernelCacheSlot const* slot_ = nullptr;

            constexpr explicit ReaderHandle(KernelCacheSlot const& slot) noexcept : slot_{&slot} {}

            friend class KernelCacheSlot;

        public:
            using value_type = snapshot_type;
            using tag_type = reader_tag;

            [[nodiscard]] snapshot_type load() const noexcept {
                CompiledKernel* kernel = slot_->kernel_.load(std::memory_order_acquire);
                uint64_t row = 0;
                if (kernel != nullptr) {
                    row = slot_->row_hash_.load(std::memory_order_acquire);
                }
                return snapshot_type{
                    .content_hash = slot_->content_hash_.load(std::memory_order_acquire),
                    .row_hash = row,
                    .kernel = kernel,
                };
            }

            [[nodiscard]] CRUCIBLE_INLINE uint64_t content_hash() const noexcept {
                return slot_->content_hash_.load(std::memory_order_acquire);
            }

            [[nodiscard]] CRUCIBLE_INLINE uint64_t row_hash() const noexcept {
                return slot_->row_hash_.load(std::memory_order_acquire);
            }

            [[nodiscard]] CRUCIBLE_INLINE CompiledKernel* kernel() const noexcept {
                return slot_->kernel_.load(std::memory_order_acquire);
            }
        };

        [[nodiscard]] WriterHandle writer(::foundation::permissions::Permission<writer_tag>&& perm) noexcept {
            return WriterHandle{*this, std::move(perm)};
        }

        [[nodiscard]] ReaderHandle reader() const noexcept { return ReaderHandle{*this}; }

        [[nodiscard]] CRUCIBLE_INLINE uint64_t content_hash() const noexcept {
            return content_hash_.load(std::memory_order_acquire);
        }

        [[nodiscard]] CRUCIBLE_INLINE uint64_t row_hash() const noexcept {
            return row_hash_.load(std::memory_order_acquire);
        }

        [[nodiscard]] CRUCIBLE_INLINE CompiledKernel* kernel() const noexcept {
            return kernel_.load(std::memory_order_acquire);
        }

        [[nodiscard]] CRUCIBLE_INLINE bool try_claim_content_hash(uint64_t& expected, uint64_t desired) noexcept {
            CRUCIBLE_PRE(::foundation::decide::is_non_zero(desired));
            return content_hash_.compare_exchange_strong(expected, desired, std::memory_order_acq_rel);
        }

    private:
        // Every access is acquire or release, never relaxed: a relaxed load
        // would let a reader match the content and then read an incoherent
        // row and kernel, which serves a kernel under the wrong row.
        //
        // An EMPTY slot also leaves row_hash at 0, which is harmless because
        // EMPTY is decided from content_hash == 0 before the row is read.
        std::atomic<uint64_t> content_hash_{0};
        std::atomic<uint64_t> row_hash_{0};
        std::atomic<CompiledKernel*> kernel_{nullptr};
    };

    static_assert(sizeof(KernelCacheSlot) == 24, "KernelCacheSlot must stay exactly 8B content + 8B row + "
                                                 "8B kernel pointer — the wire format matches this.");
    static_assert(alignof(KernelCacheSlot) == 8, "KernelCacheSlot must stay 8-byte aligned: atomic<uint64_t> and "
                                                 "atomic<ptr> need it, and over-alignment wastes cache.");
    static_assert(sizeof(KernelCacheSlot::WriterHandle) == sizeof(KernelCacheSlot*),
                  "KernelCacheSlot::WriterHandle must EBO-collapse its Permission.");

    enum class SlotState : uint8_t {
        Empty = 0,
        Claimed = 1,
        Published = 2,
    };

    explicit KernelCache(uint32_t capacity = 4096) : capacity_(capacity) {
        // A power of two makes `(slot + probe) & mask` the wrap-around, and
        // the 2^31 ceiling keeps `slot_index + probe` inside uint32_t.
        //
        // The precondition is armed in a release build as well as a debug
        // one: the release preset evaluates contracts under the `observe`
        // semantic, and the project's violation handler is noreturn and ends
        // in std::abort, so a violation stops the process either way.  It is
        // a contract_assert and not a CRUCIBLE_PRE: under the `ignore`
        // semantic, CRUCIBLE_PRE tells the optimizer to assume its predicate,
        // and that assumption would let the optimizer delete the repeat
        // below.  A contract_assert under `ignore` gives the optimizer no
        // assumption.  The constructor is not constexpr, so no constant
        // evaluation reaches the check.
        contract_assert(::foundation::decide::is_power_of_two_le<std::uint32_t>(capacity, std::uint32_t{1u << 31}));

        // The repeat that follows is not redundant. A translation unit can
        // take the `ignore` semantic through CRUCIBLE_CONTRACT_IGNORE_OPTIONS,
        // and then no contract in the headers it includes checks anything. In
        // a Release build with CRUCIBLE_BENCH, bench/bench_pool_allocator.cpp
        // is one such unit, and it includes this header. This check does not
        // depend on the semantic, and the whole probe sequence rests on the
        // property it states:
        // `(slot + probe) & mask` only wraps back into the table when the
        // capacity is a power of two, so a capacity that is not one makes
        // every probe past the first read and write outside the allocation.
        // One construction per cache, so the repeat costs nothing.
        CRUCIBLE_FATAL_INVARIANT(capacity != 0 && (capacity & (capacity - 1)) == 0);
        table_ = allocate_table_(capacity_);
        if (!table_) [[unlikely]]
            std::abort();  // OOM is unrecoverable
        // No other thread holds a reference yet, so the relaxed load below
        // reads this thread's own store.
        size_.store(0, std::memory_order_relaxed);
        CRUCIBLE_POST(0, capacity_ == capacity);
        CRUCIBLE_POST(0, table_ != nullptr);
        CRUCIBLE_POST(0, size_.load(std::memory_order_relaxed) == 0);
    }

    ~KernelCache() { destroy_table_(table_, capacity_); }

    KernelCache(const KernelCache&) = delete("lock-free hash map with atomic state cannot be copied");
    KernelCache& operator=(const KernelCache&) = delete("lock-free hash map with atomic state cannot be copied");
    KernelCache(KernelCache&&) = delete("lock-free hash map with atomic state cannot be moved");
    KernelCache& operator=(KernelCache&&) = delete("lock-free hash map with atomic state cannot be moved");

    // Spin iterations a reader tolerates on a CLAIMED slot before treating it
    // as foreign.
    static constexpr uint32_t kClaimedSpinBudget = 64;

    // A lookup of the zero hash would walk the entire table: it matches no
    // slot and terminates on no empty one.  The row needs no such guard,
    // because RowHash{0} is a real key that can coexist with any other.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard, gnu::hot]] CompiledKernel* lookup(ContentHash content_hash,
                                                                                RowHash row_hash) const noexcept
        CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        const uint64_t lookup_hash = content_hash.raw();
        const uint64_t lookup_row = row_hash.raw();
        const uint32_t mask = capacity_ - 1;
        const uint32_t slot_index = static_cast<uint32_t>(lookup_hash) & mask;
        for (uint32_t probe = 0; probe < capacity_; probe++) {
            auto& entry = table_[(slot_index + probe) & mask];
            uint64_t key = entry.content_hash_.load(std::memory_order_acquire);
            if (key == 0) return nullptr;
            if (key != lookup_hash) continue;
            // The kernel must be loaded before the row: the release-acquire
            // pair on the kernel is what makes the published row visible.
            CompiledKernel* kernel_ptr = entry.kernel_.load(std::memory_order_acquire);
            if (kernel_ptr == nullptr) [[unlikely]] {
                kernel_ptr = await_claimed_(entry);
                if (kernel_ptr == nullptr) [[unlikely]] {
                    // One content hash can occupy several slots, one per row,
                    // so a stalled claim here must not hide a matching slot
                    // further along the chain.
                    continue;
                }
            }
            uint64_t entry_row = entry.row_hash_.load(std::memory_order_acquire);
            if (entry_row == lookup_row) [[likely]]
                return kernel_ptr;
        }
        return nullptr;
    }

    enum class InsertError : uint8_t {
        TableFull,
        NotYetImplemented,
    };

    // Zero collides with the EMPTY marker, and UINT64_MAX is reserved as the
    // end-of-region marker, so neither can be a key.  The row again needs no
    // guard: RowHash{0} is a real key.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard]] std::expected<void, InsertError>
    insert(ContentHash content_hash, RowHash row_hash, CompiledKernel* kernel) CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        CRUCIBLE_PRE(::foundation::decide::not_sentinel_hash(content_hash));
        CRUCIBLE_PRE(kernel != nullptr);
        const uint64_t lookup_hash = content_hash.raw();
        const uint64_t lookup_row = row_hash.raw();
        const uint32_t mask = capacity_ - 1;
        const uint32_t slot_index = static_cast<uint32_t>(lookup_hash) & mask;
        for (uint32_t probe = 0; probe < capacity_; probe++) {
            auto& entry = table_[(slot_index + probe) & mask];
            uint64_t expected = 0;
            // A successful CAS claims the slot for this thread.
            if (entry.try_claim_content_hash(expected, lookup_hash)) {
                auto writer = entry.writer(fresh_writer_permission_());
                writer.publish(KernelCacheSlotSnapshot{
                    .content_hash = lookup_hash,
                    .row_hash = lookup_row,
                    .kernel = kernel,
                });
                // Relaxed: nothing branches on the exact count.  The content
                // hash CAS carries the real synchronization.
                size_.fetch_add(1, std::memory_order_relaxed);
                return {};
            }
            if (expected == lookup_hash) {
                // The content matches, so the row decides.  Spin on the
                // kernel, never on the row: a CLAIMED slot reads row 0, which
                // is indistinguishable from a published row of 0.
                CompiledKernel* existing_kernel = entry.kernel();
                for (uint32_t spin = 0; spin < kClaimedSpinBudget && existing_kernel == nullptr; ++spin) {
                    CRUCIBLE_SPIN_PAUSE;
                    existing_kernel = entry.kernel();
                }
                if (existing_kernel != nullptr) {
                    // A published kernel makes the row visible too.
                    uint64_t existing_row = entry.row_hash();
                    if (existing_row == lookup_row) {
                        // Variant update: the row stays pinned and only the
                        // kernel changes, so a concurrent reader observes one
                        // kernel or the other and both are valid.
                        auto writer = entry.writer(fresh_writer_permission_());
                        writer.publish_kernel_variant(kernel);
                        return {};
                    }
                }
            }
        }
        return std::unexpected(InsertError::TableFull);
    }

    // The cache has three levels, ordered by portability: L1 holds the
    // vendor-neutral form, L2 the form for one vendor family, and L3 the
    // compiled bytes for one chip.  Only L1 has a backing store, the table
    // above.  The other two exist at the type level so call sites already
    // name a level.  Their lookups find nothing and their publishes report
    // NotYetImplemented, which is what a caller must branch on.  A vacuous
    // success would let a path that depends on persistence miss every later
    // lookup in silence.
    //
    // The level in the return type stops a value from one level from being
    // handed to a path that requires another.  The level is where the value
    // came from, not how near it is to the core, so it is a provenance tag
    // and not a residency class.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard, gnu::hot]] AtLevel<VendorNeutralLevel, CompiledKernel*>
    lookup_l1(ContentHash content_hash, RowHash row_hash) const noexcept CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        return ::fixy::mint_tagged<VendorNeutralLevel>(lookup(content_hash, row_hash));
    }

    // The preconditions on the two levels below match L1's even though the
    // bodies ignore their arguments: a backing store added later inherits the
    // contract, whereas relaxing it later would have to be renegotiated at
    // every call site.
    [[nodiscard]] AtLevel<VendorFamilyLevel, CompiledKernel*> lookup_l2(ContentHash content_hash,
                                                                        RowHash /*row_hash*/) const noexcept
        CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        return ::fixy::mint_tagged<VendorFamilyLevel>(static_cast<CompiledKernel*>(nullptr));
    }

    [[nodiscard]] AtLevel<ChipLevel, CompiledKernel*> lookup_l3(ContentHash content_hash,
                                                                RowHash /*row_hash*/) const noexcept
        CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        return ::fixy::mint_tagged<ChipLevel>(static_cast<CompiledKernel*>(nullptr));
    }

    // A caller derives row_hash by projecting a typed effect row, so that the
    // cache key stays expressible in the row vocabulary.  A literal RowHash
    // is accepted for the row-blind baseline of RowHash{0}.
    //
    // Both hash guards are needed here and on the two levels below: UINT64_MAX
    // is non-zero, and zero is not the reserved sentinel.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[nodiscard]]
    AtLevel<VendorNeutralLevel, std::expected<void, InsertError>> publish_l1(ContentHash content_hash, RowHash row_hash,
                                                                             CompiledKernel* kernel)
        CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        CRUCIBLE_PRE(::foundation::decide::not_sentinel_hash(content_hash));
        CRUCIBLE_PRE(kernel != nullptr);
        return ::fixy::mint_tagged<VendorNeutralLevel>(insert(content_hash, row_hash, kernel));
    }

    [[nodiscard]]
    AtLevel<VendorFamilyLevel, std::expected<void, InsertError>>
    publish_l2(ContentHash content_hash, RowHash /*row_hash*/, CompiledKernel* kernel) noexcept {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        CRUCIBLE_PRE(::foundation::decide::not_sentinel_hash(content_hash));
        CRUCIBLE_PRE(kernel != nullptr);
        return ::fixy::mint_tagged<VendorFamilyLevel>(
            std::expected<void, InsertError>{std::unexpected(InsertError::NotYetImplemented)});
    }

    [[nodiscard]]
    AtLevel<ChipLevel, std::expected<void, InsertError>> publish_l3(ContentHash content_hash, RowHash /*row_hash*/,
                                                                    CompiledKernel* kernel) noexcept {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        CRUCIBLE_PRE(::foundation::decide::not_sentinel_hash(content_hash));
        CRUCIBLE_PRE(kernel != nullptr);
        return ::fixy::mint_tagged<ChipLevel>(
            std::expected<void, InsertError>{std::unexpected(InsertError::NotYetImplemented)});
    }

    // Relaxed: an informational counter that nothing orders against.
    [[nodiscard]] uint32_t size() const CRUCIBLE_NO_THREAD_SAFETY { return size_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint32_t capacity() const { return capacity_; }

    // The capacity guard is what keeps `capacity_ - 1u` below from wrapping
    // to UINT32_MAX on an unconstructed cache.
    [[nodiscard]] SlotState diag_slot_state(uint32_t slot_index) const noexcept CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(capacity_ > 0u);
        CRUCIBLE_PRE(::foundation::decide::in_range<uint32_t>(slot_index, 0u, capacity_ - 1u));
        const KernelCacheSlot& entry = table_[slot_index];
        return classify_(entry.content_hash_.load(std::memory_order_acquire),
                         entry.kernel_.load(std::memory_order_acquire));
    }

private:
    [[nodiscard, gnu::const]] static constexpr SlotState classify_(uint64_t hash_bits,
                                                                   const CompiledKernel* k) noexcept {
        if (hash_bits == 0) return SlotState::Empty;
        if (k == nullptr) return SlotState::Claimed;
        return SlotState::Published;
    }

    static constexpr std::size_t kTableAlignment = 64;
    static_assert(kTableAlignment % alignof(KernelCacheSlot) == 0);

    [[nodiscard]] static KernelCacheSlot* allocate_table_(uint32_t capacity) noexcept {
        const auto bytes = static_cast<std::size_t>(capacity) * sizeof(KernelCacheSlot);
        void* raw = ::operator new(bytes, std::align_val_t{kTableAlignment}, std::nothrow);
        if (raw == nullptr) return nullptr;
        auto* slots = static_cast<KernelCacheSlot*>(raw);
        for (uint32_t i = 0; i < capacity; ++i) {
            ::new(static_cast<void*>(&slots[i])) KernelCacheSlot();
        }
        return slots;
    }

    static void destroy_table_(KernelCacheSlot* table, uint32_t capacity) noexcept {
        if (table == nullptr) return;
        for (uint32_t i = 0; i < capacity; ++i) {
            table[i].~KernelCacheSlot();
        }
        ::operator delete(static_cast<void*>(table), std::align_val_t{kTableAlignment});
    }

    // Outlined so that the hot lookup body stays compact.
    CRUCIBLE_UNSAFE_BUFFER_USAGE [[gnu::cold, gnu::noinline]]
    static CompiledKernel* await_claimed_(const KernelCacheSlot& entry) noexcept {
        for (uint32_t spin = 0; spin < kClaimedSpinBudget; ++spin) {
            CRUCIBLE_SPIN_PAUSE;
            CompiledKernel* kernel_ptr = entry.kernel_.load(std::memory_order_acquire);
            if (kernel_ptr != nullptr) return kernel_ptr;
        }
        return nullptr;
    }

    // A writer token for one slot write.  The handle names its token type
    // before a root exists, so the root drops its brand through the one
    // door that drops a brand.  A brand gives no protection here, because
    // the token writes only the slot that the content-hash claim gives it.
    [[nodiscard]] static constexpr ::foundation::permissions::Permission<KernelCompileTag>
    fresh_writer_permission_() noexcept {
        return ::foundation::permissions::permission_erase_brand(
            ::foundation::permissions::mint_permission_root<KernelCompileTag>());
    }

    KernelCacheSlot* table_;
    uint32_t capacity_;
    std::atomic<uint32_t> size_;
};

// Splits a straight-line trace at a divergence point into a two-arm branch
// that rejoins where the continuations become identical again.
//
// The recipe is required, and null is how a caller says it selects no
// numerics. This function holds the only KernelCache::lookup in the tree, so
// the hash it builds is the one the compiler's cache is keyed on, and a hash
// that leaves the numerics out keys a kernel by what it computes and not by
// how. Two regions with byte-identical ops under different recipes would take
// one slot, and whichever kernel landed there first would serve both.
//
// So this is the one parameter on this signature that cannot be defaulted.
// It has no caller today -- this whole function does not -- and a default
// would hand the first one a wrong-kernel path it never had to think about.
[[nodiscard]] inline BranchNode* add_branch(::foundation::effects::Alloc a, Arena& arena, KernelCache& kernel_cache,
                                            TraceNode* divergence_point, TraceEntry* new_ops, uint32_t new_n,
                                            int64_t old_guard_value, int64_t new_guard_value, Guard guard,
                                            TraceNode* existing_suffix, const NumericalRecipe* recipe) {
    CRUCIBLE_PRE(divergence_point != nullptr);
    CRUCIBLE_PRE(old_guard_value != new_guard_value);
    CRUCIBLE_PRE(recipe == nullptr || ::foundation::decide::is_non_zero(recipe->hash));
    CRUCIBLE_PRE(recipe == nullptr || !recipe->hash.is_sentinel());
    auto* new_region =
        (recipe == nullptr) ? make_region(a, arena, new_ops, new_n) : make_region(a, arena, new_ops, new_n, recipe);

    TraceNode* merge = find_merge_point(std::span{new_ops, new_n}, existing_suffix, recipe);

    new_region->next = merge;

    // The dispatch path does not yet carry an effect row down to here, so the
    // lookup uses the bare-type row.  Every untyped caller therefore agrees
    // on one slot, and row-tagged callers land in disjoint slots rather than
    // sharing this one.  A miss leaves compiled at its default null, which is
    // the right state for a region with no kernel yet.
    //
    // The content hash half of the key carries the recipe, because the region
    // above was built under it.
    if (auto* cached_kernel = kernel_cache.lookup(new_region->content_hash, RowHash{0})) {
        new_region->compiled.publish(cached_kernel);
    }

    auto* branch = arena.alloc_obj<BranchNode>(a);
    ::new(branch) BranchNode{};
    branch->kind = TraceNodeKind::BRANCH;
    branch->guard = guard;
    branch->num_arms = 2;
    branch->arms = arena.alloc_array<BranchNode::Arm>(a, 2);
    // Arms go in sorted by value: replay binary-searches them.
    if (old_guard_value <= new_guard_value) {
        branch->arms[0] = {.value = old_guard_value, .target = divergence_point};
        branch->arms[1] = {.value = new_guard_value, .target = new_region};
    } else {
        branch->arms[0] = {.value = new_guard_value, .target = new_region};
        branch->arms[1] = {.value = old_guard_value, .target = divergence_point};
    }
    branch->next = merge;

    recompute_merkle(branch);

    CRUCIBLE_POST(branch, branch != nullptr);
    CRUCIBLE_POST(branch, branch->kind == TraceNodeKind::BRANCH);
    CRUCIBLE_POST(branch, branch->num_arms == 2u);
    CRUCIBLE_POST(branch, branch->arms != nullptr);
    return branch;
}

}  // namespace crucible
