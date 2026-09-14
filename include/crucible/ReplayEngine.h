#pragma once

// Walks a compiled region one operation at a time, checking each guard and
// handing back the preallocated pointers for that operation's tensors.
//
// The position is a sliding pointer rather than an index, so advancing is one
// increment instead of a multiply and an address computation. The next
// operation's two guard hashes are copied into this object as each advance
// finishes, so the comparison that starts the next one reads them from here
// and never follows a pointer to reach them.

#include <crucible/MerkleDag.h>
#include <crucible/Platform.h>
#include <crucible/PoolAllocator.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/safety/Decide.h>
#include <crucible/safety/Post.h>
#include <crucible/safety/Pre.h>

#include <cassert>
#include <cstdint>
#include <type_traits>

namespace crucible {

enum class ReplayStatus : uint8_t {
    MATCH,  // the operation matches the compiled trace and its outputs are ready
    DIVERGED,  // a guard failed: either the operation or its shapes differ
    COMPLETE,  // the region is exhausted, which ends the iteration
};

// The only state worth a tag is the one that lasts: a region and a pool are
// bound from initialisation until the engine is rebound. Having matched an
// operation lasts one operation, so the methods that need it check instead.
namespace engine_state {
struct Active {};
}  // namespace engine_state

// Two operation indices with the same underlying type and different meanings.
// One is the last operation that passed its guard, which always names a live
// entry. The other is where a guard failed, and it can equal the operation
// count when the last operation in the region is the one that diverged, so a
// caller that indexes with it checks that first. Swapping the two reports a
// divergence one operation early, or resubmits an operation that matched.
namespace op_role {
struct Matched {};
struct Diverged {};
}  // namespace op_role

struct ReplayEngine {
    using PoolBorrow = crucible::fixy::wrap::BorrowedRef<const PoolAllocator>;

    static_assert(crucible::fixy::wrap::IsBorrowedRef<PoolBorrow>);

    ReplayEngine() = default;

    ReplayEngine(const ReplayEngine&) = delete("ReplayEngine tracks mutable cursor position");
    ReplayEngine& operator=(const ReplayEngine&) = delete("ReplayEngine tracks mutable cursor position");
    ReplayEngine(ReplayEngine&&) = delete("non-owning pointers would alias with moved-from cursor");
    ReplayEngine& operator=(ReplayEngine&&) = delete("non-owning pointers would alias with moved-from cursor");

    // An empty region is legitimate, but a non-empty one must have operations.
    void init(const RegionNode* region, PoolBorrow pool) CRUCIBLE_NO_THREAD_SAFETY pre(region != nullptr)
        pre(pool->is_initialized()) pre(::crucible::decide::valid_span(region->num_ops, region->ops)) {
        ops_ = region->ops;
        end_ = region->ops + region->num_ops;
        cursor_ = ops_;
        current_ = nullptr;
        // The precondition above already established what minting this view
        // checks, and the call it gates returns a pointer promised non-null,
        // which is what the postcondition below rests on.
        const auto pool_view = pool->mint_initialized_view();
        slot_table_ = pool->table(pool_view);
        // Load the first operation's guard values, so the first advance finds
        // them here like every later one does.
        if (ops_ != end_) [[likely]] {
            expected_schema_ = ops_[0].schema_hash;
            expected_shape_ = ops_[0].shape_hash;
        } else {
            expected_schema_ = SchemaHash{};
            expected_shape_ = ShapeHash{};
        }

        // A contract predicate that reads a member through `this` is skipped
        // when the compiler folds the body at compile time, so every such
        // check in this file runs from the body rather than a clause. These
        // three fold trivially true, which is exactly why they need that.
        CRUCIBLE_POST(0, cursor_ == ops_);
        CRUCIBLE_POST(0, slot_table_ != nullptr);
        CRUCIBLE_POST(0, end_ == ops_ + region->num_ops);
    }

    // Rewinds to the first operation for the next iteration. The last matched
    // entry is deliberately kept, so the pointers into it stay valid after the
    // caller is told the region is complete and resets on its way out.
    void reset() {
        cursor_ = ops_;
        if (ops_ != end_) [[likely]] {
            expected_schema_ = ops_[0].schema_hash;
            expected_shape_ = ops_[0].shape_hash;
        }

        // Failing to rewind leaves the next iteration resuming from the middle
        // of the region, which is a silent break in determinism. The last
        // matched entry is deliberately absent from this: it survives.
        CRUCIBLE_POST(0, cursor_ == ops_);
    }

    // The completion of the last operation is reported by this call itself.
    // The caller resets before advancing again.
    [[nodiscard]] CRUCIBLE_HOT ReplayStatus advance(SchemaHash schema_hash, ShapeHash shape_hash) {
        CRUCIBLE_PRE(cursor_ < end_);

        if (schema_hash != expected_schema_) [[unlikely]]
            return ReplayStatus::DIVERGED;

        if (shape_hash != expected_shape_) [[unlikely]]
            return ReplayStatus::DIVERGED;

        current_ = cursor_;

        // The slot identifiers live on this entry's second cache line, and the
        // caller asks for them almost immediately. The casts are byte
        // arithmetic for the builtin's address. No array of characters begins
        // life here.
        __builtin_prefetch(static_cast<const char*>(static_cast<const void*>(cursor_)) + 64, 0, 3);

        ++cursor_;

        if (cursor_ == end_) [[unlikely]]
            return ReplayStatus::COMPLETE;

        // The next operation's guard values, loaded here so the comparison
        // that opens the next call reads them from this object. The load can
        // miss, but it sits after the comparisons and overlaps whatever the
        // caller does with the pointers it is about to ask for.
        expected_schema_ = cursor_->schema_hash;
        expected_shape_ = cursor_->shape_hash;
        return ReplayStatus::MATCH;
    }

    // The pointer for one of the last matched operation's outputs.
    [[nodiscard]] CRUCIBLE_HOT void* output_ptr(uint16_t j) const CRUCIBLE_LIFETIMEBOUND pre(current_ != nullptr)
        pre(current_->output_slot_ids != nullptr) {
        // The count guard is not redundant with the range check: at a count of
        // zero the upper bound below underflows to the largest value and the
        // range admits everything. No index into a zero-output operation
        // exists, so this catches a later change that opens the path.
        CRUCIBLE_PRE(current_->num_outputs > 0u);
        CRUCIBLE_PRE(
            ::crucible::decide::in_range<std::uint16_t>(j, 0u, static_cast<std::uint16_t>(current_->num_outputs - 1u)));
        SlotId const sid = current_->output_slot_ids[j];
        void* const result = sid.is_valid() ? slot_table_[sid.raw()] : nullptr;
        // Dropping the validity test would index the table with the sentinel
        // and return either a stale slot or a read past the end of the table.
        CRUCIBLE_POST(result, result == nullptr || sid.is_valid());
        return result;
    }

    // The same for one of the inputs.
    [[nodiscard]] CRUCIBLE_HOT void* input_ptr(uint16_t j) const CRUCIBLE_LIFETIMEBOUND pre(current_ != nullptr)
        pre(current_->input_slot_ids != nullptr) {
        CRUCIBLE_PRE(current_->num_inputs > 0u);
        CRUCIBLE_PRE(
            ::crucible::decide::in_range<std::uint16_t>(j, 0u, static_cast<std::uint16_t>(current_->num_inputs - 1u)));
        SlotId const sid = current_->input_slot_ids[j];
        void* const result = sid.is_valid() ? slot_table_[sid.raw()] : nullptr;
        CRUCIBLE_POST(result, result == nullptr || sid.is_valid());
        return result;
    }

    [[nodiscard]] const TraceEntry& current_entry() const CRUCIBLE_LIFETIMEBOUND {
        // Debug-only: current_ is null only before the first advance
        // returns MATCH, which is a caller-sequencing mistake rather than
        // untrusted input. The dereference on the next line faults on its
        // own in a release build, so the failure stays loud without a check.
        //
        // The member is read into a local first because a contract predicate
        // that reaches a member through `this` is rejected as non-constant
        // when the compiler folds this body.
        const TraceEntry* const matched = current_;
        CRUCIBLE_DEBUG_ASSERT(matched != nullptr);
        return *matched;
    }

    [[nodiscard]] crucible::fixy::wrap::Tagged<OpIndex, op_role::Matched> matched_op_index() const {
        // Debug-only, and weaker than the one above: a null current_ makes
        // the subtraction produce a garbage index rather than fault. Nothing
        // in the recording chain calls this — only the replay-engine tests
        // do — so the silent path is not reachable from production. The
        // local is for the same folding reason as in current_entry.
        const TraceEntry* const matched = current_;
        CRUCIBLE_DEBUG_ASSERT(matched != nullptr);
        return crucible::fixy::wrap::Tagged<OpIndex, op_role::Matched>{OpIndex{static_cast<uint32_t>(matched - ops_)}};
    }

    // Carries the region's operation ceiling in the return type, so a caller
    // that indexes with the result has the bound without re-deriving it.
    [[nodiscard]] crucible::fixy::wrap::Refined<
        crucible::fixy::wrap::bounded_above<uint32_t{1u << 22}>,  // mirrors the loader's own ceiling
        uint32_t> diverged_op_index_refined() const {
        return crucible::fixy::wrap::Refined<crucible::fixy::wrap::bounded_above<uint32_t{1u << 22}>, uint32_t>{
            static_cast<uint32_t>(cursor_ - ops_)};
    }

    // Preferred wherever the value reaches something that could just as well
    // have been handed the matched index.
    [[nodiscard]] crucible::fixy::wrap::Tagged<OpIndex, op_role::Diverged> diverged_op_index_tagged() const {
        return crucible::fixy::wrap::Tagged<OpIndex, op_role::Diverged>{OpIndex{static_cast<uint32_t>(cursor_ - ops_)}};
    }

    [[nodiscard]] uint32_t ops_matched() const { return static_cast<uint32_t>(cursor_ - ops_); }

    [[nodiscard]] uint32_t num_ops() const { return static_cast<uint32_t>(end_ - ops_); }

    [[nodiscard]] bool is_complete() const { return cursor_ == end_; }
    [[nodiscard]] bool is_initialized() const { return ops_ != nullptr; }

    using ActiveView = crucible::fixy::wrap::ScopedView<ReplayEngine, engine_state::Active>;

    // Found by argument-dependent lookup from the view factory.
    [[nodiscard]] friend constexpr bool view_ok(ReplayEngine const& e,
                                                std::type_identity<engine_state::Active>) noexcept {
        return e.is_initialized();
    }

    [[nodiscard]] CRUCIBLE_INLINE ActiveView mint_active_view() const noexcept pre(is_initialized()) {
        return crucible::fixy::wrap::mint_view<engine_state::Active>(*this);
    }

    // These overloads take a proof that the engine is initialised and then
    // delegate, rather than repeating the bodies. The proof carries no runtime
    // state, so the delegation compiles to the same code as a direct call, and
    // there is one body to fix rather than two that can drift apart. Their
    // preconditions live in the body they delegate to, so repeating them here
    // would only duplicate a check.
    [[nodiscard]] CRUCIBLE_HOT ReplayStatus advance(SchemaHash schema_hash, ShapeHash shape_hash, ActiveView const&) {
        CRUCIBLE_PRE(cursor_ < end_);
        return advance(schema_hash, shape_hash);
    }

    [[nodiscard]] CRUCIBLE_HOT void* output_ptr(uint16_t j, ActiveView const&) const CRUCIBLE_LIFETIMEBOUND {
        return output_ptr(j);
    }

    [[nodiscard]] CRUCIBLE_HOT void* input_ptr(uint16_t j, ActiveView const&) const CRUCIBLE_LIFETIMEBOUND {
        return input_ptr(j);
    }

    CRUCIBLE_INLINE void reset(ActiveView const&) {
        cursor_ = ops_;
        if (ops_ != end_) [[likely]] {
            expected_schema_ = ops_[0].schema_hash;
            expected_shape_ = ops_[0].shape_hash;
        }
    }

private:
    // Ordered so that an advance reads and writes them front to back, and
    // padded so the whole object is one cache line. The assertion below pins
    // that. A ninth field would cost a second line on every operation.
    SchemaHash expected_schema_{};
    ShapeHash expected_shape_{};
    const TraceEntry* cursor_ = nullptr;  // where the next operation is read
    const TraceEntry* end_ = nullptr;  // one past the last operation
    const TraceEntry* current_ = nullptr;  // the last operation that matched
    void* const* slot_table_ = nullptr;
    const TraceEntry* ops_ = nullptr;  // the base, for rewinding
    uint64_t pad_replay_ = 0;
};

static_assert(sizeof(ReplayEngine) == 64, "ReplayEngine: 8 × 8B = 64 bytes (one cache line)");

// A view must not outlive the frame that minted it, so storing one in a field
// would let it escape.
static_assert(crucible::fixy::wrap::no_scoped_view_field_check<ReplayEngine>());

}  // namespace crucible
