#pragma once

// Walks a compiled region one operation at a time, checking each guard and
// handing back the preallocated pointers for that operation's tensors.
//
// The position is a cursor of the View of the operations rather than an
// index, so advancing is one increment instead of a multiply and an address
// computation. The cursor knows the end of the region, so a read or a step
// at the end ends the process in each build. The next operation's two guard
// hashes are copied into this object as each advance finishes, so the
// comparison that starts the next one reads them from here and never follows
// a pointer to reach them.
//
// The slot table is a View of the column of addresses of the pool. A slot id
// is a plain SlotId of a trace array, so no type proves it below the count
// of the table. The read of the table compares the id with the count, and
// that one compare also refuses the id with no slot, whose word is above
// each count. Init checks each slot id of the region against the count once,
// so an id of a region never misses the table.

#include <crucible/MerkleDag.h>
#include <crucible/PoolAllocator.h>
#include <fixy/ScopedView.h>
#include <fixy/Tagged.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Decide.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Cursor.h>

#include <cstdint>
#include <type_traits>

namespace crucible {

enum class ReplayStatus : uint8_t {
    MATCH,  // the operation matches the compiled trace and its outputs are ready
    DIVERGED,  // a guard failed: the operation, its inputs or its scalars differ
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

// The result of one advance: the status of the guard and the position of
// the op that it matched.  A MATCH or a COMPLETE matched the op at the
// cursor that the advance read, so the advance gives that position from the
// cursor in its registers, and no reader loads the cursor that it stored.  A
// DIVERGED step matched no op, so it has no matched position.  Only the
// engine makes a step.  The step is eight bytes, so it comes back in one
// register.
class ReplayStep {
public:
    [[nodiscard]] constexpr ReplayStatus status() const noexcept { return status_; }

    // The position of the matched op in the region.  A COMPLETE matched the
    // last op of the region.  A read of a DIVERGED step ends the process in
    // each build.  The member is read into a local, so no contract reads a
    // member through this.
    [[nodiscard]] constexpr ::fixy::Tagged<OpIndex, op_role::Matched> matched_op_index() const noexcept {
        const ReplayStatus status = status_;
        CRUCIBLE_PRE(status != ReplayStatus::DIVERGED);
        return ::fixy::mint_tagged<op_role::Matched>(matched_);
    }

private:
    friend struct ReplayEngine;

    constexpr ReplayStep(ReplayStatus status, OpIndex matched) noexcept : status_{status}, matched_{matched} {}

    ReplayStatus status_ = ReplayStatus::DIVERGED;
    ::fixy::FixedArray<uint8_t, 3> pad_{};
    OpIndex matched_{};
};

// The pool of an activation: a PoolAllocator, with or without a const.
template <class Pool>
concept ReplayPool = std::same_as<std::remove_const_t<Pool>, PoolAllocator>;

struct ReplayEngine {
    ReplayEngine() = default;

    ReplayEngine(const ReplayEngine&) = delete("ReplayEngine tracks mutable cursor position");
    ReplayEngine& operator=(const ReplayEngine&) = delete("ReplayEngine tracks mutable cursor position");
    ReplayEngine(ReplayEngine&&) = delete("non-owning pointers would alias with moved-from cursor");
    ReplayEngine& operator=(ReplayEngine&&) = delete("non-owning pointers would alias with moved-from cursor");

    // An empty region is legitimate, but a non-empty one must have operations.
    // Init runs at the activation of a region, and the check of the slot ids
    // reads each slot id of the region once: O(ops + slot ids).  The engine
    // reads the pool only during the call, so the Ref keeps its brand, and
    // the engine reads it only as const.
    template <ReplayPool Pool, ::foundation::brand::IsBrand Brand>
    void init(const RegionNode& region, ::fixy::Ref<Pool, Brand> pool) CRUCIBLE_NO_THREAD_SAFETY {
        init_(region, *pool);
    }

    // Rewinds to the first operation for the next iteration. A rewind at the
    // end of the region keeps the last matched entry, so the pointers into it
    // stay valid after the caller is told the region is complete and resets
    // on its way out. A rewind from a different position abandons the
    // iteration, and no op stays matched. So the matched op is always the op
    // before the cursor, or the last op when the cursor is at the first op,
    // which is the position that matched_op_index reads.
    CRUCIBLE_INLINE void reset() {
        if (!cursor_.is_at_end()) current_ = ::fixy::none;
        rewind_();
    }

    // The completion of the last operation is reported by this call itself.
    // The caller resets before advancing again: the read of the cursor ends
    // the process at the end of the region, in each build, before the guard
    // compares a hash.  The step holds the position of the matched op, which
    // this call computes from the cursor that it read (ReplayStep).
    [[nodiscard]] CRUCIBLE_HOT ReplayStep advance(SchemaHash schema_hash, ShapeHash shape_hash) {
        const TraceEntry& entry = *cursor_;

        if (schema_hash != expected_schema_) [[unlikely]]
            return ReplayStep{ReplayStatus::DIVERGED, OpIndex{}};

        if (shape_hash != expected_shape_) [[unlikely]]
            return ReplayStep{ReplayStatus::DIVERGED, OpIndex{}};

        current_ = ::fixy::some(::fixy::erase_brand(::fixy::to(entry)));
        // The position of the entry, before the step of the cursor.
        const OpIndex matched{ops_matched()};

        // The pointers of the slot ids and their counts end on the line after
        // the hashes of this entry, and the caller asks for them almost
        // immediately. Byte 64 of the entry is on that line.
        ::fixy::prefetch_byte<::fixy::PrefetchAccess::read, 3, 64>(entry);

        // The read above proved the cursor before its end, so the check of
        // the step folds into it.
        ++cursor_;

        if (cursor_.is_at_end()) [[unlikely]]
            return ReplayStep{ReplayStatus::COMPLETE, matched};

        // The next operation's guard values, loaded here so the comparison
        // that opens the next call reads them from this object. The load can
        // miss, but it sits after the comparisons and overlaps whatever the
        // caller does with the pointers it is about to ask for.  The test
        // above proved the cursor before its end, so the check of the read
        // folds into it.
        const TraceEntry& next = *cursor_;
        expected_schema_ = next.schema_hash;
        expected_shape_ = next.shape_hash;
        return ReplayStep{ReplayStatus::MATCH, matched};
    }

    // The address of the slot of one of the last matched operation's
    // outputs: the first byte of the storage of the output.  A view at an
    // offset starts storage_offset elements later.  It points into the pool,
    // and the engine does not own the pool.  An output with no slot, or an external slot that its owner did
    // not register, gives no address.  An op with no array of slot ids gives
    // the empty View, so the bound refuses each index of it.  The window
    // folds into the bound, so the read has no second check.  The member is
    // read into a local, so no contract reads a member through this.
    [[nodiscard]] CRUCIBLE_HOT ::fixy::Option<::fixy::RunAddress> output_ptr(uint16_t j) const {
        ::fixy::View<const SlotId> const slot_ids = matched_entry_().output_slot_ids();
        CRUCIBLE_PRE(j < slot_ids.size());
        SlotId const sid =
            slot_ids.window<1>(j).expect("the precondition bounds the output index").at(::fixy::index<0>());
        return address_of_slot_(sid);
    }

    // The same for one of the inputs.
    [[nodiscard]] CRUCIBLE_HOT ::fixy::Option<::fixy::RunAddress> input_ptr(uint16_t j) const {
        ::fixy::View<const SlotId> const slot_ids = matched_entry_().input_slot_ids();
        CRUCIBLE_PRE(j < slot_ids.size());
        SlotId const sid =
            slot_ids.window<1>(j).expect("the precondition bounds the input index").at(::fixy::index<0>());
        return address_of_slot_(sid);
    }

    // The entry is in the region, and the engine only points at it.
    [[nodiscard]] const TraceEntry& current_entry() const { return matched_entry_(); }

    // The position of the last matched op in the region.  The reset at the
    // end of an iteration keeps the matched op, so after a COMPLETE this is
    // the last op of the region, also when the cursor is back at the first.
    [[nodiscard]] ::fixy::Tagged<OpIndex, op_role::Matched> matched_op_index() const {
        // Armed in every build.  An empty current_ has no position, and the
        // index then reaches a caller that indexes with it.  The local is for
        // the same folding reason as in matched_entry_.
        const ::fixy::Option<::fixy::Ref<const TraceEntry>> matched = current_;
        CRUCIBLE_PRE(matched.is_some());
        // The writers of the cursor and of the matched op keep the matched op
        // before the cursor, or at the last op after the rewind at the end of
        // an iteration (reset).  So the position of the cursor gives the
        // position of the matched op, and no address of an op is read.  A
        // matched op proves at least one op, so the last op exists.  At the
        // first op, the position before the cursor wraps to the largest word,
        // and the minimum with the last op gives the last op with no branch.
        const uint32_t before_cursor = ops_matched() - uint32_t{1};
        const uint32_t position = ::fixy::min(before_cursor, num_ops() - uint32_t{1});
        return ::fixy::mint_tagged<op_role::Matched>(OpIndex{position});
    }

    // Distinct in type from the matched index, so a caller that could just as
    // well have been handed the matched index cannot be given this one.
    [[nodiscard]] ::fixy::Tagged<OpIndex, op_role::Diverged> diverged_op_index_tagged() const {
        return ::fixy::mint_tagged<op_role::Diverged>(OpIndex{ops_matched()});
    }

    // The cursor and ops_ end at one place, the end of the region, so the
    // count of ops minus the ops after the cursor is the position of the
    // cursor.  Init bounds the count of ops by 32 bits.
    [[nodiscard]] uint32_t ops_matched() const { return static_cast<uint32_t>(ops_.size() - cursor_.remaining()); }

    [[nodiscard]] uint32_t num_ops() const { return static_cast<uint32_t>(ops_.size()); }

    [[nodiscard]] bool is_complete() const { return cursor_.is_at_end(); }
    // True after an init with a region of at least one op.
    [[nodiscard]] constexpr bool is_initialized() const { return ops_.size() > 0; }

    using ActiveView = ::fixy::ScopedView<ReplayEngine, engine_state::Active>;

    // Found by argument-dependent lookup from the view factory.
    [[nodiscard]] friend constexpr bool view_ok(ReplayEngine const& e,
                                                std::type_identity<engine_state::Active>) noexcept {
        return e.is_initialized();
    }

    // The cursor is foreground state, so the proof that the engine is live
    // is minted only on the thread that holds the producer claim of its
    // Vigil.
    [[nodiscard]] CRUCIBLE_INLINE constexpr ActiveView mint_active_view(VigilFgCtx const&) const noexcept {
        CRUCIBLE_PRE(is_initialized());
        return active_view_();
    }

    // These overloads take a proof that the engine is initialised and then
    // delegate, rather than repeating the bodies. The proof carries no runtime
    // state, so the delegation compiles to the same code as a direct call, and
    // there is one body to fix rather than two that can drift apart. Their
    // preconditions live in the body they delegate to, so repeating them here
    // would only duplicate a check.
    [[nodiscard]] CRUCIBLE_HOT ReplayStep advance(SchemaHash schema_hash, ShapeHash shape_hash, ActiveView const&) {
        return advance(schema_hash, shape_hash);
    }

    [[nodiscard]] CRUCIBLE_HOT ::fixy::Option<::fixy::RunAddress> output_ptr(uint16_t j, ActiveView const&) const {
        return output_ptr(j);
    }

    [[nodiscard]] CRUCIBLE_HOT ::fixy::Option<::fixy::RunAddress> input_ptr(uint16_t j, ActiveView const&) const {
        return input_ptr(j);
    }

    CRUCIBLE_INLINE void reset(ActiveView const&) { reset(); }

private:
    // The context mints the view on paths that the gate of its Vigil
    // already passed, so it reaches the view without a context.
    friend struct CrucibleContext;

    // The body of init.  It is no template, so the callers of each brand
    // share one copy of it.
    void init_(const RegionNode& region, const PoolAllocator& pool) CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(pool.is_initialized());
        // A region counts its ops in 32 bits, so each position below fits the
        // word of an OpIndex.  num_ops() of the region checks that bound.
        const uint32_t op_count = region.num_ops();
        ops_ = region.ops;
        cursor_ = ops_.begin();
        current_ = ::fixy::none;
        // The precondition above already established what minting this view
        // checks.  An initialized pool holds at least one slot.
        const auto pool_view = pool.initialized_view_();
        slot_table_ = pool.table(pool_view);
        check_slot_ids_(ops_, pool.num_slots());
        // Load the first operation's guard values, so the first advance finds
        // them here like every later one does.
        expected_schema_ = SchemaHash{};
        expected_shape_ = ShapeHash{};
        load_first_guard_();

        // A contract predicate that reads a member through `this` is skipped
        // when the compiler folds the body at compile time, so every such
        // check in this file runs from the body rather than a clause. These
        // fold trivially true, which is exactly why they need that.
        CRUCIBLE_POST(0, cursor_ == ops_.begin());
        CRUCIBLE_POST(0, num_ops() == op_count);
        CRUCIBLE_POST(0, slot_table_.size() == pool.num_slots());
    }

    // The precondition runs in the body, so it also fires when the compiler
    // folds a call at compile time, which a clause reading a member through
    // `this` would skip.
    [[nodiscard]] CRUCIBLE_INLINE constexpr ActiveView active_view_() const noexcept {
        CRUCIBLE_PRE(is_initialized());
        return ::fixy::mint_view<engine_state::Active>(*this);
    }

    // The rewind of the context after an advance that gave COMPLETE.  That
    // advance left the cursor at the end of the region, so the rewind keeps
    // the last matched op, as reset does there, with no test of the end.
    // CrucibleContext::advance_ calls it only in its case of COMPLETE.
    CRUCIBLE_INLINE void rewind_after_complete_() { rewind_(); }

    // The last matched op.  The engine has none only before the first advance
    // returns MATCH, which is a mistake of the order of the calls of the
    // caller.  The precondition refuses it in each build, and the unwrap after
    // it folds into the precondition.  The member is read into a local first,
    // because a contract predicate that reaches a member through `this` is
    // rejected as non-constant when the compiler folds this body.
    [[nodiscard]] CRUCIBLE_INLINE const TraceEntry& matched_entry_() const noexcept {
        const ::fixy::Option<::fixy::Ref<const TraceEntry>> matched = current_;
        CRUCIBLE_PRE(matched.is_some());
        return *::fixy::Option<::fixy::Ref<const TraceEntry>>{matched}.expect("the precondition found the matched op");
    }

    // The cursor at the first op, and the guard values of that op.  The
    // matched op does not change.
    CRUCIBLE_INLINE void rewind_() {
        cursor_ = ops_.begin();
        load_first_guard_();

        // Failing to rewind leaves the next iteration resuming from the middle
        // of the region, which is a silent break in determinism. The last
        // matched entry is deliberately absent from this: it survives.
        CRUCIBLE_POST(0, cursor_ == ops_.begin());
    }

    // The guard values of the op at the cursor, or no change at the end of
    // the region.  The test proves the cursor before its end, so the check
    // of the read folds into it.
    CRUCIBLE_INLINE void load_first_guard_() {
        if (cursor_.is_at_end()) [[unlikely]]
            return;
        const TraceEntry& first = *cursor_;
        expected_schema_ = first.schema_hash;
        expected_shape_ = first.shape_hash;
    }

    // The address of the slot sid, or no address for the id with no slot and
    // for an external slot that its owner did not register.  The word of the
    // id with no slot is the largest word, and the count of the table is at
    // most kMaxNumSlots, so the one compare of the window refuses that id
    // too.  The window of one entry is the read of the table that needs no
    // second test: its View is never the empty value of its Option.
    [[nodiscard]] CRUCIBLE_INLINE ::fixy::Option<::fixy::RunAddress> address_of_slot_(SlotId sid) const noexcept {
        const SlotTable table = slot_table_;
        const auto entry = table.window<1>(sid.raw());
        return entry.match(
            [](::fixy::View<const ::fixy::Option<::fixy::RunAddress>, 1> found) noexcept {
                return found.at(::fixy::index<0>());
            },
            [] noexcept { return ::fixy::Option<::fixy::RunAddress>{::fixy::none}; });
    }

    // Each slot id of each op of ops is the id with no slot or an id below
    // slot_count.  A region and its plan come from one build, so an id at or
    // past the count is a defect of the build, and it ends the process in
    // each build.  O(ops + slot ids), once for each init.
    CRUCIBLE_COLD static void check_slot_ids_(::fixy::View<const TraceEntry> ops, uint32_t slot_count) noexcept {
        uint32_t op_position = 0;
        for (const TraceEntry& op : ops) {
            for (const SlotId sid : op.output_slot_ids()) {
                if (sid.is_valid() && sid.raw() >= slot_count) [[unlikely]]
                    ::fixy::fatal("ReplayEngine: op {} has the output slot id {}, and the plan has {} slots",
                                  op_position, sid.raw(), slot_count);
            }
            for (const SlotId sid : op.input_slot_ids()) {
                if (sid.is_valid() && sid.raw() >= slot_count) [[unlikely]]
                    ::fixy::fatal("ReplayEngine: op {} has the input slot id {}, and the plan has {} slots",
                                  op_position, sid.raw(), slot_count);
            }
            ++op_position;
        }
    }

    using SlotTable = ::fixy::View<const ::fixy::Option<::fixy::RunAddress>>;

    // Ordered so that an advance reads and writes them front to back.  The
    // members that each operation reads take bytes 0 to 56: the guard
    // values, the cursor, the matched op and the slot table.  The context
    // puts the engine at the start of a cache line, so they share one line.
    // The View of the ops, which only the rewind and the position queries
    // read, comes last and ends on the next line, beside the mode of the
    // context that each dispatch reads too.  The check file of this header
    // pins the size.
    SchemaHash expected_schema_{};
    ShapeHash expected_shape_{};
    // Where the next operation is read, and the end of the region.
    ::foundation::core::ViewCursor<const TraceEntry> cursor_ = ::fixy::View<const TraceEntry>{}.begin();
    ::fixy::Option<::fixy::Ref<const TraceEntry>> current_{};  // the last operation that matched
    SlotTable slot_table_{};
    ::fixy::View<const TraceEntry> ops_{};  // the region, for rewinding and for the positions
};

}  // namespace crucible
