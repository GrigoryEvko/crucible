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
#include <foundation/core/CyclicCursor.h>

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
    // first op keeps the last matched entry: the advance that completes the
    // region goes back to the first op itself, so the pointers into the last
    // op stay valid after the caller is told the region is complete and
    // resets on its way out. A rewind from a different position abandons the
    // iteration, and no op stays matched. So the matched op is always the op
    // before the cursor, or the last op when the cursor is at the first op,
    // which is the position that matched_op_index reads.
    CRUCIBLE_INLINE void reset() {
        if (cursor_.position() != 0) current_ = ::fixy::none;
        rewind_();
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

    // The position of the cursor in the region: the count of the ops of this
    // iteration that matched.  The advance that completes the region takes
    // the cursor back to the first op, so the position is then 0.  Init
    // bounds the count of ops by 32 bits.
    [[nodiscard]] uint32_t ops_matched() const { return static_cast<uint32_t>(cursor_.position()); }

    [[nodiscard]] uint32_t num_ops() const { return num_ops_; }

    // True after an init with a region of at least one op.
    [[nodiscard]] constexpr bool is_initialized() const { return num_ops_ > 0; }

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
    // already passed, so it reaches the view without a context.  It also
    // calls the advance with no view, only in its compiled mode, so the
    // engine is initialized there.
    friend struct CrucibleContext;

    // The match of the op at the cursor.  The cursor goes around the ops of
    // the region (CyclicCursor), so the read needs no test, and the step that
    // passes the last op takes the cursor back to the first op and tells the
    // completion of the region.  The step holds the position of the matched
    // op, which this call computes from the cursor that it read (ReplayStep).
    // Only an initialized engine advances: the public advance takes the view
    // of an active engine, and the context calls this one in its compiled
    // mode only.
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

        // The guard values of the next op, or of the first op after the last
        // one, loaded here so the comparison that opens the next call reads
        // them from this object.  The load can miss, but it sits after the
        // comparisons and overlaps whatever the caller does with the pointers
        // it is about to ask for.
        const ::foundation::core::CycleStep stepped = cursor_.step();
        load_guard_();
        return ReplayStep{
            stepped == ::foundation::core::CycleStep::wrapped ? ReplayStatus::COMPLETE : ReplayStatus::MATCH, matched};
    }

    // The body of init.  It is no template, so the callers of each brand
    // share one copy of it.
    void init_(const RegionNode& region, const PoolAllocator& pool) CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(pool.is_initialized());
        // A region counts its ops in 32 bits, so each position below fits the
        // word of an OpIndex.  num_ops() of the region checks that bound.
        const uint32_t op_count = region.num_ops();
        num_ops_ = op_count;
        // The cursor goes around the ops of the region.  An empty region keeps
        // the run of no region, and the engine then stays uninitialized.  The
        // door of the cyclic cursor refuses an empty run, and the test of the
        // count above it folds that check.
        cursor_ = op_count == 0
                    ? no_region_cursor_()
                    : ::foundation::core::CyclicCursor<const TraceEntry>::cycle_from(region.ops.view().begin());
        current_ = ::fixy::none;
        // The precondition above established that the pool is live, and an
        // initialized pool holds at least one slot.  The engine is a friend of
        // the pool, so it takes the column itself.
        slot_table_ = pool.addresses_.view();
        check_slot_ids_(region.ops.view(), pool.num_slots());
        // Load the first operation's guard values, so the first advance finds
        // them here like every later one does.
        load_guard_();

        // A contract predicate that reads a member through `this` is skipped
        // when the compiler folds the body at compile time, so every such
        // check in this file runs from the body rather than a clause.
        CRUCIBLE_POST(0, slot_table_.size() == pool.num_slots());
    }

    // Takes the engine back to its state before an init: no region, no matched
    // op and no slot table.  The context calls it before its pool ends, so no
    // read of the engine names the ended column of the pool, and a stale proof
    // of the active mode ends the process at its precondition.
    void clear_() noexcept {
        num_ops_ = 0;
        cursor_ = no_region_cursor_();
        current_ = ::fixy::none;
        slot_table_ = SlotTable{};
        load_guard_();
    }

    // The check runs in each build and under each contract semantic, and its
    // failure path does not return.  So the precondition of mint_view folds
    // into it, and an address read of the context tests the count one time.
    // The door gives the view on the erased identity through erase_brand.  The
    // brand of mint_view names the door and not the caller, so each engine
    // gets the same brand, and the rule brand-escape refuses a fresh brand that
    // leaves the door.  A brand for each engine waits for the brand of an arena
    // or a store.
    [[nodiscard]] CRUCIBLE_INLINE constexpr ActiveView active_view_() const noexcept {
        CRUCIBLE_FATAL_INVARIANT(is_initialized());
        return ::fixy::erase_brand(::fixy::mint_view<engine_state::Active>(*this));
    }

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
    // matched op does not change.  A rewind that left the cursor in the
    // middle of the region would break the determinism of the next
    // iteration.  test_replay_engine and test_crucible_context read the
    // position after the rewinds (ops_matched), so no check of the
    // assignment runs here.
    CRUCIBLE_INLINE void rewind_() {
        cursor_.rewind();
        load_guard_();
    }

    // The guard values of the op at the cursor.  The cursor is never at the
    // end of its run, so the read has no test.
    CRUCIBLE_INLINE void load_guard_() {
        const TraceEntry& at = *cursor_;
        expected_schema_ = at.schema_hash;
        expected_shape_ = at.shape_hash;
    }

    // The run of an engine with no region: one entry that no advance reads,
    // because only an initialized engine advances.  Its cursor stands in for
    // the cursor of a region before an init and after an init of an empty
    // region.
    static constexpr ::fixy::FixedArray<TraceEntry, 1> kNoRegionRun{};

    [[nodiscard]] static constexpr ::foundation::core::CyclicCursor<const TraceEntry> no_region_cursor_() noexcept {
        return ::foundation::core::CyclicCursor<const TraceEntry>::cycle_from(kNoRegionRun.view().begin());
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
    // members that each operation reads take bytes 0 to 63: the guard
    // values, the cursor, the matched op and the slot table.  The context
    // puts the engine at the start of a cache line, so they share one line.
    // The count of the ops, which only the position queries read, comes last
    // on the next line, beside the mode of the context that each dispatch
    // reads too.  The check file of this header pins the size.
    SchemaHash expected_schema_{};
    ShapeHash expected_shape_{};
    // Where the next operation is read: its first op, its position and its end.
    ::foundation::core::CyclicCursor<const TraceEntry> cursor_ = no_region_cursor_();
    ::fixy::Option<::fixy::Ref<const TraceEntry>> current_{};  // the last operation that matched
    SlotTable slot_table_{};
    uint32_t num_ops_ = 0;  // the count of the ops of the region, 0 before an init
    [[maybe_unused]] ::fixy::FixedArray<uint8_t, 4> pad_{};
};

}  // namespace crucible
