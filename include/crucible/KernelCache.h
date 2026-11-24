#pragma once

// The kernel cache and the one function that reads it while it builds the
// DAG.  The cache maps (ContentHash, RowHash) to a compiled kernel.  A slot
// write needs a writer permission, so this header includes the permission
// layer, and MerkleDag.h does not.

#include <crucible/MerkleDag.h>
#include <crucible/NumericalRecipe.h>
#include <crucible/Types.h>
#include <fixy/Core.h>
#include <fixy/Tagged.h>
#include <fixy/os/SpinBudget.h>
#include <foundation/Brand.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible {

// Open-addressing map from (ContentHash, RowHash) to a compiled kernel.
// The capacity is a power of two, so the mask of the table wraps each probe.
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
//
// The budget is a time, kClaimedSpinBudget, so it is the same on each core.
// The end of the spin decides the answer: a miss, or a second slot for one
// pair.  Under a concurrent insert, the answer thus depends on the timing of
// another thread.  So lookup, insert and the doors of the three levels take
// the bare tag of Nondet, and a replay or kernel context, which holds no
// Nondet, cannot call them.  A miss or a second slot costs a compile and some
// space, and it changes no bit of a result: the content hash holds the
// recipe, and each kernel of one recipe gives the same bits (DetSafe).
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

    // A compiled kernel in a slot.  The cache does not own the kernel, a
    // reference is never null, and a reader of the cache gets the kernel as
    // const.
    using KernelRef = ::fixy::Ref<CompiledKernel const>;
    // The kernel of a key, or no value when no slot holds the key.
    using FoundKernel = ::fixy::Option<KernelRef>;

    struct KernelCacheSlotSnapshot {
        uint64_t content_hash = 0;
        uint64_t row_hash = 0;
        FoundKernel kernel{};
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

        // The writer handle is linear (foundation/Linear.h).  Its door is
        // release(), which gives the token back to the caller.  The handle
        // keeps the brand of its token, so no erased token opens a writer.
        template <class Brand>
        class[[= ::foundation::linear::linear{}]] WriterHandle {
            ::fixy::Ref<KernelCacheSlot> slot_;
            [[no_unique_address]] ::foundation::permissions::Permission<writer_tag, Brand> perm_;

            constexpr WriterHandle(KernelCacheSlot& slot,
                                   ::foundation::permissions::Permission<writer_tag, Brand>&& perm) noexcept
                : slot_{::fixy::erase_brand(::fixy::to(slot))}, perm_{std::move(perm)} {}

            friend class KernelCacheSlot;

        public:
            using value_type = snapshot_type;
            using tag_type = writer_tag;

            WriterHandle(WriterHandle const&) =
                delete("KernelCacheSlot::WriterHandle owns the linear writer permission");
            WriterHandle&
            operator=(WriterHandle const&) = delete("KernelCacheSlot::WriterHandle owns the linear writer permission");
            // The move of the token is trivial and its move assignment is
            // user-provided.  So GCC 16 counts the handle as not trivially
            // copyable, and the ABI passes it in a register.  An assignment
            // would drop the token of the target.  The check file of this
            // header asserts the three facts.
            constexpr WriterHandle(WriterHandle&&) noexcept = default;
            WriterHandle& operator=(WriterHandle&&) = delete(
                "KernelCacheSlot::WriterHandle owns the linear writer permission, and an assignment would drop "
                "the token of the target");

            // The release door.  The handle gives the token back to the
            // caller, and the call spends the handle.
            [[nodiscard]] constexpr ::foundation::permissions::Permission<writer_tag, Brand> release() && noexcept {
                return std::move(perm_);
            }

            void publish(snapshot_type const& snapshot) noexcept {
                CRUCIBLE_PRE(::foundation::decide::is_non_zero(snapshot.content_hash));
                CRUCIBLE_PRE(snapshot.kernel.is_some());
                // The content hash is already claimed by CAS before this
                // endpoint exists.  The two stores must stay in this order:
                // the kernel store is what makes the row visible to readers.
                CRUCIBLE_FATAL_INVARIANT(slot_->content_hash_.load_acquire() == snapshot.content_hash);
                slot_->row_hash_.store_release(snapshot.row_hash);
                slot_->kernel_.store_release(snapshot.kernel);
            }

            void publish_kernel_variant(KernelRef kernel) noexcept {
                slot_->kernel_.store_release(::fixy::some(kernel));
            }
        };

        class ReaderHandle {
            ::fixy::Ref<const KernelCacheSlot> slot_;

            constexpr explicit ReaderHandle(KernelCacheSlot const& slot) noexcept
                : slot_{::fixy::erase_brand(::fixy::to(slot))} {}

            friend class KernelCacheSlot;

        public:
            using value_type = snapshot_type;
            using tag_type = reader_tag;

            [[nodiscard]] snapshot_type load() const noexcept {
                const FoundKernel kernel = slot_->kernel_.load_acquire();
                uint64_t row = 0;
                if (kernel.is_some()) {
                    row = slot_->row_hash_.load_acquire();
                }
                return snapshot_type{
                    .content_hash = slot_->content_hash_.load_acquire(),
                    .row_hash = row,
                    .kernel = kernel,
                };
            }

            [[nodiscard]] CRUCIBLE_INLINE uint64_t content_hash() const noexcept {
                return slot_->content_hash_.load_acquire();
            }

            [[nodiscard]] CRUCIBLE_INLINE uint64_t row_hash() const noexcept { return slot_->row_hash_.load_acquire(); }

            [[nodiscard]] CRUCIBLE_INLINE FoundKernel kernel() const noexcept { return slot_->kernel_.load_acquire(); }
        };

        template <class Brand>
            requires ::foundation::brand::IsFreshBrand<Brand>
        [[nodiscard]] WriterHandle<Brand>
        writer(::foundation::permissions::Permission<writer_tag, Brand>&& perm) noexcept {
            return WriterHandle<Brand>{*this, std::move(perm)};
        }

        [[nodiscard]] ReaderHandle reader() const noexcept { return ReaderHandle{*this}; }

        [[nodiscard]] CRUCIBLE_INLINE uint64_t content_hash() const noexcept { return content_hash_.load_acquire(); }

        [[nodiscard]] CRUCIBLE_INLINE uint64_t row_hash() const noexcept { return row_hash_.load_acquire(); }

        [[nodiscard]] CRUCIBLE_INLINE FoundKernel kernel() const noexcept { return kernel_.load_acquire(); }

        // A failed claim writes the content hash that the slot holds into
        // expected.
        [[nodiscard]] CRUCIBLE_INLINE bool try_claim_content_hash(uint64_t& expected, uint64_t desired) noexcept {
            CRUCIBLE_PRE(::foundation::decide::is_non_zero(desired));
            auto claimed = content_hash_.cas_acq_rel(expected, desired);
            if (claimed.is_ok()) return true;
            expected = std::move(claimed).err().expect("a refused swap gives the value that it read").observed;
            return false;
        }

    private:
        // Every access is acquire or release, never relaxed: a relaxed load
        // would let a reader match the content and then read an incoherent
        // row and kernel, which serves a kernel under the wrong row.
        //
        // An EMPTY slot also leaves row_hash at 0, which is harmless because
        // EMPTY is decided from content_hash == 0 before the row is read.
        ::fixy::Atomic<uint64_t> content_hash_{};
        ::fixy::Atomic<uint64_t> row_hash_{};
        ::fixy::Atomic<FoundKernel> kernel_{};
    };

    static_assert(sizeof(KernelCacheSlot) == 24, "KernelCacheSlot must stay exactly 8B content + 8B row + "
                                                 "8B kernel pointer — the wire format matches this.");
    static_assert(alignof(KernelCacheSlot) == 8, "KernelCacheSlot must stay 8-byte aligned: atomic<uint64_t> and "
                                                 "atomic<ptr> need it, and over-alignment wastes cache.");

    enum class SlotState : uint8_t {
        Empty = 0,
        Claimed = 1,
        Published = 2,
    };

    // The table holds capacity slots on a cache line boundary.  The mint ends
    // the process unless the capacity is a power of two from 1 to 2^31, in
    // each build and under each contract semantic.  Complexity: one
    // allocation and O(capacity) slot constructions.
    explicit KernelCache(::foundation::effects::Alloc alloc, uint32_t capacity = 4096) noexcept
        : table_{::fixy::mint_masked_array<KernelCacheSlot, 64>(alloc, capacity)} {
        CRUCIBLE_POST(0, table_.size() == capacity);
        CRUCIBLE_POST(0, size_.load_acquire() == 0);
    }

    KernelCache(const KernelCache&) = delete("lock-free hash map with atomic state cannot be copied");
    KernelCache& operator=(const KernelCache&) = delete("lock-free hash map with atomic state cannot be copied");
    KernelCache(KernelCache&&) = delete("lock-free hash map with atomic state cannot be moved");
    KernelCache& operator=(KernelCache&&) = delete("lock-free hash map with atomic state cannot be moved");

    // The time that a lookup or an insert waits on a CLAIMED slot before it
    // takes the slot as foreign.  A claim and its publish are a compare and
    // two release stores, so a longer wait means that the thread of the claim
    // was preempted.
    static constexpr ::fixy::time::SpinBudget kClaimedSpinBudget{2000};

    // A lookup of the zero hash would walk the entire table: it matches no
    // slot and terminates on no empty one.  The row needs no such guard,
    // because RowHash{0} is a real key that can coexist with any other.
    [[nodiscard, gnu::hot]] FoundKernel lookup(::foundation::effects::cap::Nondet nondet, ContentHash content_hash,
                                               RowHash row_hash) const noexcept CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        const uint64_t lookup_hash = content_hash.raw();
        const uint64_t lookup_row = row_hash.raw();
        const std::size_t slots = table_.size();
        for (std::size_t probe = 0; probe < slots; probe++) {
            const KernelCacheSlot& entry = table_.at_masked(lookup_hash + probe);
            uint64_t key = entry.content_hash_.load_acquire();
            if (key == 0) return ::fixy::none;
            if (key != lookup_hash) continue;
            // The kernel must be loaded before the row: the release-acquire
            // pair on the kernel is what makes the published row visible.
            FoundKernel kernel = entry.kernel_.load_acquire();
            if (kernel.is_none()) [[unlikely]] {
                kernel = await_claimed_(nondet, entry);
                if (kernel.is_none()) [[unlikely]] {
                    // One content hash can occupy several slots, one per row,
                    // so a stalled claim here must not hide a matching slot
                    // further along the chain.
                    continue;
                }
            }
            uint64_t entry_row = entry.row_hash_.load_acquire();
            if (entry_row == lookup_row) [[likely]]
                return kernel;
        }
        return ::fixy::none;
    }

    enum class InsertError : uint8_t {
        TableFull,
        NotYetImplemented,
    };

    // A publish into a level: no value, or the error that refused it.
    using InsertResult = ::fixy::Result<::fixy::Unit, InsertError>;

    // Zero collides with the EMPTY marker, and UINT64_MAX is reserved as the
    // end-of-region marker, so neither can be a key.  The row again needs no
    // guard: RowHash{0} is a real key.
    [[nodiscard]] InsertResult insert(::foundation::effects::cap::Nondet nondet, ContentHash content_hash,
                                      RowHash row_hash, KernelRef kernel) CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        CRUCIBLE_PRE(::foundation::decide::not_sentinel_hash(content_hash));
        const uint64_t lookup_hash = content_hash.raw();
        const uint64_t lookup_row = row_hash.raw();
        // The element door of each probe lends the slot in place.  The slots
        // are atomic cells, and the door writes no state of the table, so two
        // inserts can probe one table at the same time.
        enum class ProbeStep : uint8_t {
            Placed,
            Next,
        };
        // The body takes the Ref of the kernel by value.  The Ref names a
        // const kernel, so the body writes no part of the kernel.
        const std::size_t slots = table_.size();
        for (std::size_t probe = 0; probe < slots; probe++) {
            const ProbeStep step =
                table_.with_masked(lookup_hash + probe, [&size = size_, kernel, nondet, lookup_hash,
                                                         lookup_row](KernelCacheSlot& entry) noexcept {
                    uint64_t expected = 0;
                    // A successful CAS claims the slot for this thread.  The
                    // writer token keeps the brand of its root.  Two inserts can
                    // run at one time, so each takes a root of its own, and the
                    // claim gives each token its one slot.
                    if (entry.try_claim_content_hash(expected, lookup_hash)) {
                        auto writer = entry.writer(::foundation::permissions::mint_permission_root<KernelCompileTag>());
                        writer.publish(KernelCacheSlotSnapshot{
                            .content_hash = lookup_hash,
                            .row_hash = lookup_row,
                            .kernel = ::fixy::some(kernel),
                        });
                        ::foundation::linear::drop(std::move(writer).release());
                        // Nothing branches on the exact count.  The content hash
                        // CAS carries the real synchronization.
                        (void)size.fetch_add_acq_rel(1);
                        return ProbeStep::Placed;
                    }
                    if (expected != lookup_hash) return ProbeStep::Next;
                    // The content matches, so the row decides.  Spin on the
                    // kernel, never on the row: a CLAIMED slot reads row 0, which
                    // is indistinguishable from a published row of 0.
                    bool is_published = entry.kernel_.load_acquire().is_some();
                    if (!is_published) [[unlikely]] {
                        is_published = await_claimed_(nondet, entry).is_some();
                    }
                    // A published kernel makes the row visible too.
                    if (!is_published || entry.row_hash() != lookup_row) return ProbeStep::Next;
                    // Variant update: the row stays pinned and only the kernel
                    // changes, so a concurrent reader observes one kernel or the
                    // other and both are valid.
                    auto writer = entry.writer(::foundation::permissions::mint_permission_root<KernelCompileTag>());
                    writer.publish_kernel_variant(kernel);
                    ::foundation::linear::drop(std::move(writer).release());
                    return ProbeStep::Placed;
                });
            if (step == ProbeStep::Placed) return ::fixy::Unit{};
        }
        return ::fixy::err(InsertError::TableFull);
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
    [[nodiscard, gnu::hot]] AtLevel<VendorNeutralLevel, FoundKernel>
    lookup_l1(::foundation::effects::cap::Nondet nondet, ContentHash content_hash, RowHash row_hash) const noexcept
        CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        return ::fixy::mint_tagged<VendorNeutralLevel>(lookup(nondet, content_hash, row_hash));
    }

    // The preconditions and the tag of Nondet on the two levels below match
    // L1's even though the bodies ignore their arguments: a backing store
    // added later inherits the contract, whereas relaxing it later would have
    // to be renegotiated at every call site.
    [[nodiscard]] AtLevel<VendorFamilyLevel, FoundKernel> lookup_l2(::foundation::effects::cap::Nondet /*nondet*/,
                                                                    ContentHash content_hash,
                                                                    RowHash /*row_hash*/) const noexcept
        CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        return ::fixy::mint_tagged<VendorFamilyLevel>(FoundKernel{});
    }

    [[nodiscard]] AtLevel<ChipLevel, FoundKernel> lookup_l3(::foundation::effects::cap::Nondet /*nondet*/,
                                                            ContentHash content_hash,
                                                            RowHash /*row_hash*/) const noexcept
        CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        return ::fixy::mint_tagged<ChipLevel>(FoundKernel{});
    }

    // A caller derives row_hash by projecting a typed effect row, so that the
    // cache key stays expressible in the row vocabulary.  A literal RowHash
    // is accepted for the row-blind baseline of RowHash{0}.
    //
    // Both hash guards are needed here and on the two levels below: UINT64_MAX
    // is non-zero, and zero is not the reserved sentinel.
    [[nodiscard]]
    AtLevel<VendorNeutralLevel, InsertResult> publish_l1(::foundation::effects::cap::Nondet nondet,
                                                         ContentHash content_hash, RowHash row_hash, KernelRef kernel)
        CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        CRUCIBLE_PRE(::foundation::decide::not_sentinel_hash(content_hash));
        return ::fixy::mint_tagged<VendorNeutralLevel>(insert(nondet, content_hash, row_hash, kernel));
    }

    [[nodiscard]]
    AtLevel<VendorFamilyLevel, InsertResult> publish_l2(::foundation::effects::cap::Nondet /*nondet*/,
                                                        ContentHash content_hash, RowHash /*row_hash*/,
                                                        KernelRef /*kernel*/) noexcept {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        CRUCIBLE_PRE(::foundation::decide::not_sentinel_hash(content_hash));
        return ::fixy::mint_tagged<VendorFamilyLevel>(InsertResult{::fixy::err(InsertError::NotYetImplemented)});
    }

    [[nodiscard]]
    AtLevel<ChipLevel, InsertResult> publish_l3(::foundation::effects::cap::Nondet /*nondet*/, ContentHash content_hash,
                                                RowHash /*row_hash*/, KernelRef /*kernel*/) noexcept {
        CRUCIBLE_PRE(::foundation::decide::is_non_zero(content_hash));
        CRUCIBLE_PRE(::foundation::decide::not_sentinel_hash(content_hash));
        return ::fixy::mint_tagged<ChipLevel>(InsertResult{::fixy::err(InsertError::NotYetImplemented)});
    }

    // An informational counter that nothing orders against.
    [[nodiscard]] uint32_t size() const CRUCIBLE_NO_THREAD_SAFETY { return size_.load_acquire(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return table_.size(); }

    [[nodiscard]] SlotState diag_slot_state(std::size_t slot_index) const noexcept CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(slot_index < table_.size());
        const KernelCacheSlot& entry = table_.at_masked(slot_index);
        return classify_(entry.content_hash_.load_acquire(), entry.kernel_.load_acquire().is_some());
    }

private:
    [[nodiscard, gnu::const]] static constexpr SlotState classify_(uint64_t hash_bits, bool is_published) noexcept {
        if (hash_bits == 0) return SlotState::Empty;
        if (!is_published) return SlotState::Claimed;
        return SlotState::Published;
    }

    // Outlined so that the hot lookup body stays compact.
    // The kernel of a claimed slot, or no value when kClaimedSpinBudget
    // passes first.
    [[gnu::cold, gnu::noinline]]
    static FoundKernel await_claimed_(::foundation::effects::cap::Nondet nondet,
                                      const KernelCacheSlot& entry) noexcept {
        FoundKernel kernel{};
        const ::fixy::time::SpinDeadline deadline = ::fixy::time::SpinDeadline::after(nondet, kClaimedSpinBudget);
        static_cast<void>(::fixy::time::spin_until(nondet, deadline, [&entry, &kernel]() noexcept {
            kernel = entry.kernel_.load_acquire();
            return kernel.is_some();
        }));
        return kernel;
    }

    ::fixy::MaskedArray<KernelCacheSlot, 64> table_;
    ::fixy::Atomic<uint32_t> size_{};
};

// Splits a straight-line trace at a divergence point into a two-arm branch
// that rejoins where the continuations become identical again.
//
// The recipe is required, and ::fixy::none is how a caller says it selects no
// numerics. This function holds the only KernelCache::lookup in the tree, so
// the hash it builds is the one the compiler's cache is keyed on, and a hash
// that leaves the numerics out keys a kernel by what it computes and not by
// how. Two regions with byte-identical ops under different recipes would take
// one slot, and whichever kernel landed there first would serve both.
//
// So this is the one parameter on this signature that cannot be defaulted.
// It has no caller today -- this whole function does not -- and a default
// would hand the first one a wrong-kernel path it never had to think about.
[[nodiscard]] inline DagRef<BranchNode>
add_branch(::foundation::effects::Alloc a, ::foundation::effects::cap::Nondet nondet,
           ::fixy::BumpArena<DagArena>& arena CRUCIBLE_LIFETIMEBOUND, KernelCache& kernel_cache,
           TraceNode& divergence_point, ::fixy::ArenaRun<TraceEntry const, DagArena> new_ops, int64_t old_guard_value,
           int64_t new_guard_value, Guard guard, TraceLink existing_suffix, RecipeChoice const& recipe) {
    CRUCIBLE_PRE(old_guard_value != new_guard_value);
    CRUCIBLE_PRE(is_foldable_recipe(recipe));
    const DagRef<RegionNode> new_region = recipe.match(
        [a, &arena, &new_ops](RecipeRef chosen) noexcept { return make_region(a, arena, new_ops, *chosen); },
        [a, &arena, &new_ops]() noexcept { return make_region(a, arena, new_ops); });

    const TraceLink merge = find_merge_point(new_ops.view(), existing_suffix, recipe);

    new_region->next = merge;

    // The dispatch path does not yet carry an effect row down to here, so the
    // lookup uses the bare-type row.  Every untyped caller therefore agrees
    // on one slot, and row-tagged callers land in disjoint slots rather than
    // sharing this one.  A miss leaves compiled at its default null, which is
    // the right state for a region with no kernel yet.
    //
    // The content hash half of the key carries the recipe, because the region
    // above was built under it.
    kernel_cache.lookup(nondet, new_region->content_hash, RowHash{0})
        .match([new_region](KernelCache::KernelRef kernel) noexcept { new_region->compiled.publish(kernel); },
               []() noexcept {});

    const DagRef<BranchNode> branch = mint_dag_node<BranchNode>(a, arena);
    branch->kind = TraceNodeKind::BRANCH;
    branch->guard = guard;
    // Arms go in sorted by value: replay binary-searches them.
    const BranchNode::Arm old_arm{old_guard_value, make_link(divergence_point)};
    const BranchNode::Arm new_arm{new_guard_value, make_link(*new_region)};
    const bool is_old_first = old_guard_value <= new_guard_value;
    branch->set_arms(arena.array<BranchNode::Arm>(a, 2, old_arm));
    BranchNode::Arm& first_arm = *branch->arms().get(0).expect("a branch of two arms holds arm 0");
    BranchNode::Arm& second_arm = *branch->arms().get(1).expect("a branch of two arms holds arm 1");
    first_arm = is_old_first ? old_arm : new_arm;
    second_arm = is_old_first ? new_arm : old_arm;
    branch->next = merge;

    recompute_merkle(*branch);

    CRUCIBLE_POST(branch, branch->kind == TraceNodeKind::BRANCH);
    CRUCIBLE_POST(branch, branch->num_arms() == 2u);
    return branch;
}

// The twin of add_branch.  A temporary arena releases the branch at the end
// of the statement.  The pack holds the other arguments.
template <class... Rest>
void add_branch(::foundation::effects::Alloc, ::foundation::effects::cap::Nondet, ::fixy::BumpArena<DagArena>&&,
                Rest&&...) = delete("a temporary arena releases the branch at the end of the statement. Use an arena "
                                    "that has a name");

}  // namespace crucible
