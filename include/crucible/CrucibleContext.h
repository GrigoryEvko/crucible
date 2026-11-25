#pragma once

#include <crucible/PoolAllocator.h>
#include <crucible/ReplayEngine.h>
#include <crucible/TensorMeta.h>
#include <crucible/Types.h>
#include <crucible/ValidatedOp.h>
#include <fixy/Core.h>
#include <fixy/Mutation.h>
#include <fixy/Refined.h>
#include <fixy/ScopedView.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>

#include <cstdint>
#include <type_traits>

namespace crucible {

enum class ContextMode : uint8_t {
    RECORD,  // record the operation and execute it eagerly
    COMPILED,  // replay the operation into preallocated outputs
};

// The compiled mode is the only one worth a tag: every mode-gated method
// requires it, and recording has no method of its own. A divergence is a
// status an advance returns, not a state to hold a proof of.
namespace ctx_mode {
struct Compiled {};
}  // namespace ctx_mode

// A discriminated pair. The action is the discriminant and the other two
// fields are the compiled arm's payload. On the recording arm they carry
// nothing. The accessors below are what make reading the payload from the
// wrong arm fail rather than return a meaningless value.
struct DispatchResult {
    enum class Action : uint8_t {
        RECORD,
        COMPILED,
    };

    Action action = Action::RECORD;
    ReplayStatus status = ReplayStatus::MATCH;
    // True when the next op starts an iteration of a region: this op
    // completed the region, or its dispatch activated a region.  The front
    // end then reads its eager numerics again and gives them to the Vigil
    // (Vigil::observe_eager_numerics), once for each iteration.
    bool enters_region = false;
    uint8_t pad = 0;
    // On the compiled arm, the position of the matched op in the region that
    // the Vigil replays.  After a COMPLETE, it is the last op of the region.
    OpIndex op_index;

    [[nodiscard, gnu::pure]] constexpr bool is_record() const noexcept { return action == Action::RECORD; }
    [[nodiscard, gnu::pure]] constexpr bool is_compiled() const noexcept { return action == Action::COMPILED; }

    // A contract predicate that reads a member through `this` is skipped when
    // the compiler folds the body at compile time, so every such check in this
    // file runs from the body rather than a clause.  The two accessors are
    // constexpr, so a constant evaluation reaches the check, and a read from
    // the recording arm fails there.
    [[nodiscard, gnu::pure]] constexpr ReplayStatus compiled_status() const noexcept {
        CRUCIBLE_PRE(action == Action::COMPILED);
        return status;
    }
    [[nodiscard, gnu::pure]] constexpr OpIndex compiled_op_index() const noexcept {
        CRUCIBLE_PRE(action == Action::COMPILED);
        return op_index;
    }

    [[nodiscard]] static constexpr DispatchResult record() noexcept { return DispatchResult{}; }
    [[nodiscard]] static constexpr DispatchResult compiled(ReplayStatus s, OpIndex idx) noexcept {
        return DispatchResult{.action = Action::COMPILED,
                              .status = s,
                              .enters_region = s == ReplayStatus::COMPLETE,
                              .pad = 0,
                              .op_index = idx};
    }
};

// The position of an input of the matched op, below the count of its inputs.
// input_view takes it in place of a raw number.
using InputPosition = ::fixy::Id<struct InputPositionTag, uint16_t>;

// Why with_output_views refused the outputs of the matched op.
enum class OutputRefusal : uint8_t {
    ExternalOutput,  // an output is an external slot, whose owner holds its bytes
    NoSlot,  // an output has no slot in the plan
    TooManyOutputs,  // the op has more outputs than max_block_windows
    OutsidePool,  // the window of an output ends past the last byte of the pool
    Misaligned,  // the first byte of an output is not aligned for the element
    PartialElement,  // the bytes of an output hold no whole count of elements
    Overlap,  // two outputs share a byte
};

// Why register_external_view refused a View.
enum class ViewRefusal : uint8_t {
    PooledSlot,  // the plan puts the slot in the pool, which owns its bytes
    TooSmall,  // the View holds fewer bytes than the slot of the plan
    Misaligned,  // the first byte of the View is not aligned for an element of the slot
};

// Why register_external refused an address.
enum class ExternalRefusal : uint8_t {
    OutsidePlan,  // the plan of the active region has no slot of this id
    PooledSlot,  // the plan puts the slot in the pool, which owns its bytes
};

struct CrucibleContext {
    // The caller gives the allocation capability of its context.  The pool
    // of each region that the context activates allocates through it.
    explicit CrucibleContext(::foundation::effects::Alloc allocation) noexcept : allocation_{allocation} {}

    CrucibleContext(const CrucibleContext&) = delete("owns PoolAllocator with interior pointers");
    CrucibleContext& operator=(const CrucibleContext&) = delete("owns PoolAllocator with interior pointers");
    CrucibleContext(CrucibleContext&&) = delete("PoolAllocator has interior pointers into pool");
    CrucibleContext& operator=(CrucibleContext&&) = delete("PoolAllocator has interior pointers into pool");

    // Replays region from its first op.  A region with no memory plan cannot
    // replay, so the context stays as it is and the call returns false.
    [[nodiscard]] bool activate(const RegionNode& region) CRUCIBLE_NO_THREAD_SAFETY {
        if (region.plan.is_none()) [[unlikely]]
            return false;
        const DagRef<MemoryPlan const> plan = PlanLink{region.plan}.expect("the test above found the plan");

        if (mode_ == ContextMode::COMPILED) deactivate();

        pool_.init(allocation_, *plan);
        engine_.init_(region, pool_);
        active_region_ = link_active_region_(region);
        mode_ = ContextMode::COMPILED;

        // The mode and the active region move together. Every compiled-mode
        // method requires the first and dereferences the second, so publishing
        // one without the other leaves a pair that no method can safely act on.
        CRUCIBLE_POST(0, mode_ == ContextMode::COMPILED);
        CRUCIBLE_POST(0, ::fixy::same_target(active_region_.value(), ::fixy::some(::fixy::to(region))));
        return true;
    }

    void deactivate() {
        mode_ = ContextMode::RECORD;
        drop_external_reads_();
        // The slot table of the engine is a View of the column of the pool, so
        // the engine lets it go before the pool ends.  A stale CompiledView
        // then ends the process at a check of the engine.
        engine_.clear_();
        pool_.destroy();
        active_region_ = ActiveRegionLink{};

        // The dual of the pair above: clearing the mode while leaving the
        // region pointer behind would let the next region switch replay a
        // prefix against a region that is gone.
        CRUCIBLE_POST(0, mode_ == ContextMode::RECORD);
        CRUCIBLE_POST(0, active_region_.value().is_none());
    }

    // Stops the replay at a divergence that no guard of an op saw, such as
    // a change of the numerics of the run, and counts the divergence as an
    // advance that gives DIVERGED does.
    void deactivate_on_divergence() {
        CRUCIBLE_PRE(mode_ == ContextMode::COMPILED);
        diverged_count_.bump();
        deactivate();
    }

    [[nodiscard, gnu::flatten]] CRUCIBLE_HOT ReplayStep advance(SchemaHash schema_hash, ShapeHash shape_hash) {
        CRUCIBLE_PRE(mode_ == ContextMode::COMPILED);
        return advance_(schema_hash, shape_hash);
    }

    // Binds one of the operation's tensors to its slot in the memory plan.
    //
    // Each address points into the pool, and the pool is a member of this
    // context.  A temporary context releases its pool at the end of the
    // statement.  A deleted `const&&` twin refuses each call on a temporary
    // context.
    [[nodiscard]] CRUCIBLE_HOT ::fixy::Option<::fixy::RunAddress> output_ptr(uint16_t j) const& {
        CRUCIBLE_PRE(mode_ == ContextMode::COMPILED);
        return engine_.output_ptr(j);
    }

    ::fixy::Option<::fixy::RunAddress> output_ptr(uint16_t) const&& =
        delete("a temporary context releases its pool at the end of the statement. Use a context that has a name");

    [[nodiscard]] CRUCIBLE_HOT ::fixy::Option<::fixy::RunAddress> input_ptr(uint16_t j) const& {
        CRUCIBLE_PRE(mode_ == ContextMode::COMPILED);
        return engine_.input_ptr(j);
    }

    ::fixy::Option<::fixy::RunAddress> input_ptr(uint16_t) const&& =
        delete("a temporary context releases its pool at the end of the statement. Use a context that has a name");

    // Safe to call part-way through an iteration.  A region with no memory
    // plan cannot replay, so the context stays as it is and the call returns
    // false.
    [[nodiscard]] bool switch_region(const RegionNode& alt, uint32_t div_pos) CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(mode_ == ContextMode::COMPILED);
        if (alt.plan.is_none()) [[unlikely]]
            return false;
        const DagRef<MemoryPlan const> new_plan = PlanLink{alt.plan}.expect("the test above found the plan");

        if (div_pos == 0) return activate(alt);

        // Armed in a release build. A compiled context replays a region that
        // has a plan, and the migration reads the slots of the two plans. The
        // path runs once per shape change, so an always-on check costs
        // nothing worth counting.
        CRUCIBLE_FATAL_INVARIANT(active_region_.value().is_some_and(
            [](DagRef<RegionNode const> const& active) noexcept { return active->plan.is_some(); }));
        const RegionNode& old_region =
            *::fixy::Option<DagRef<RegionNode const>>{active_region_.value()}.expect("the check above found it");

        // The engine holds a View of the column of the pool that detach ends,
        // and the init of the engine below gives it the column of the new one.
        engine_.clear_();
        // Detaching empties the pool, which makes the view stale, but it goes
        // out of scope before anything touches the pool again.
        auto pv = pool_.initialized_view_();
        const ::foundation::AlignedBytes old_pool = pool_.detach(pv);
        // The listed reads name slots of the old plan.
        drop_external_reads_();
        pool_.init(allocation_, *new_plan);
        migrate_prefix_slots_(old_region, alt, old_pool.view(), div_pos);

        engine_.init_(alt, pool_);
        active_region_ = link_active_region_(alt);

        // Replay the prefix the two regions share, so the new region resumes
        // where the old one diverged.
        auto av = engine_.active_view_();
        for (uint32_t i = 0; i < div_pos; i++) {
            const ReplayStatus s = engine_.advance(alt.op_at(i).schema_hash, alt.op_at(i).shape_hash, av).status();
            // The engine was just pointed at alt, and these are alt's own
            // hashes read back in order, so anything but a match means that
            // the cursor of the engine is out of step.  The data is not bad.
            // The check stays in each build, and it runs once for each op of
            // the shared prefix, at the switch of a region only.
            CRUCIBLE_FATAL_INVARIANT(s == ReplayStatus::MATCH || s == ReplayStatus::COMPLETE);
        }

        // Leaving the region pointer on the old region while the engine has
        // moved to the new one is the torn pair the activation contract
        // guards against. The early exit above reaches it through activate.
        CRUCIBLE_POST(0, ::fixy::same_target(active_region_.value(), ::fixy::some(::fixy::to(alt))));
        CRUCIBLE_POST(0, mode_ == ContextMode::COMPILED);
        return true;
    }

    [[nodiscard]] ContextMode mode() const { return mode_; }
    [[nodiscard]] bool is_compiled() const { return mode_ == ContextMode::COMPILED; }
    [[nodiscard]] bool is_recording() const { return mode_ == ContextMode::RECORD; }

    using CompiledView = ::fixy::ScopedView<CrucibleContext, ctx_mode::Compiled>;

    [[nodiscard]] friend constexpr bool view_ok(CrucibleContext const& c,
                                                std::type_identity<ctx_mode::Compiled>) noexcept {
        return c.mode_ == ContextMode::COMPILED;
    }

    // The context is foreground state, so the proof that it is compiled is
    // minted only on the thread that holds the producer claim of its Vigil.
    // The door gives the view on the erased identity through erase_brand.
    // The brand of mint_view names the door and not the caller, so each
    // context gets the same brand, and the rule brand-escape refuses a fresh
    // brand that leaves the door.  A brand for each context waits for the
    // brand of an arena or a store.
    [[nodiscard]] CRUCIBLE_INLINE constexpr CompiledView mint_compiled_view(VigilFgCtx const&) const noexcept {
        CRUCIBLE_PRE(mode_ == ContextMode::COMPILED);
        return ::fixy::erase_brand(::fixy::mint_view<ctx_mode::Compiled>(*this));
    }

    // These overloads take the proof in place of the mode check. A proof that
    // the context is compiled implies both the engine and the pool are live,
    // because activation initialises the two together, so the inner proofs
    // can be minted from the outer one.

    [[nodiscard, gnu::flatten]] CRUCIBLE_HOT ReplayStep advance(SchemaHash schema_hash, ShapeHash shape_hash,
                                                                CompiledView const&) {
        return advance_(schema_hash, shape_hash);
    }

    [[nodiscard]] CRUCIBLE_HOT ::fixy::Option<::fixy::RunAddress> output_ptr(uint16_t j, CompiledView const&) const& {
        auto av = engine_.active_view_();
        return engine_.output_ptr(j, av);
    }

    ::fixy::Option<::fixy::RunAddress> output_ptr(uint16_t, CompiledView const&) const&& =
        delete("a temporary context releases its pool at the end of the statement. Use a context that has a name");

    [[nodiscard]] CRUCIBLE_HOT ::fixy::Option<::fixy::RunAddress> input_ptr(uint16_t j, CompiledView const&) const& {
        auto av = engine_.active_view_();
        return engine_.input_ptr(j, av);
    }

    ::fixy::Option<::fixy::RunAddress> input_ptr(uint16_t, CompiledView const&) const&& =
        delete("a temporary context releases its pool at the end of the statement. Use a context that has a name");

    // The address of an external slot and the device of its owner
    // (PoolAllocator::register_external).  The door reads the flag of the
    // slot in the plan of the active region, so it writes no pooled slot,
    // and the window of each pooled slot stays the window of the plan.  A
    // refusal gives its ExternalRefusal, and the pool keeps the slot as it
    // was.  The replay of an op binds its external slots through
    // bind_external_reads, which reads no plan.  Complexity: O(1).
    [[nodiscard, gnu::cold]] ::fixy::Result<::fixy::Unit, ExternalRefusal>
    register_external(SlotId sid, TensorData data, CompiledView const&) {
        using Registered = ::fixy::Result<::fixy::Unit, ExternalRefusal>;
        const ::fixy::View<const TensorSlot> plan_slots = active_plan_().slots.view();
        if (plan_slots.get(sid.raw()).is_none()) return ::fixy::err(ExternalRefusal::OutsidePlan);
        return external_slot_(plan_slots, sid)
            .match(
                [this, data](ExternalSlotId external) noexcept -> Registered {
                    register_external_(external, data);
                    return ::fixy::Unit{};
                },
                []() noexcept -> Registered { return ::fixy::err(ExternalRefusal::PooledSlot); });
    }

    // Lists each read of an external slot by an op of the active region, in
    // the order of the ops and of the inputs, for bind_external_reads: each
    // input of an op that has an external slot of the plan and no producer
    // op.  An input with no producer can also read an internal slot, such as
    // the new result that a nested copy_ writes in place before its parent
    // returns it, and the pool owns the bytes of that slot.  The replay
    // resumes at op resume_op, so the cursor starts at the first read of that
    // op or of a later op.  The ids come from the plan of the active region,
    // and each activation, each switch and each deactivation drops the list,
    // so a listed id is always an external slot of the active plan.  Two
    // passes, one to count and one to fill, so the list takes one allocation.
    // Complexity: O(the inputs of the region).
    [[gnu::cold]] void list_external_reads(OpIndex resume_op, CompiledView const&) {
        const RegionNode& region = active_region_node_();
        const ::fixy::View<const TensorSlot> plan_slots = active_plan_().slots.view();
        const auto each_read = [&region, plan_slots](auto&& on_read) {
            for (uint32_t i = 0; i < region.num_ops(); i++) {
                const TraceEntry& te = region.op_at(i);
                const ::fixy::View<const OpIndex> producers = te.input_trace_indices();
                const ::fixy::View<const SlotId> slots = te.input_slot_ids();
                for (uint16_t input = 0; input < te.num_inputs(); input++) {
                    const SlotId slot = *slots.get(input).expect("the input arrays of an entry hold one count");
                    const bool has_producer =
                        producers.get(input).expect("the input arrays of an entry hold one count")->is_valid();
                    for (const ExternalSlotId external : external_slot_(plan_slots, slot)) {
                        if (!has_producer) on_read(ExternalRead{OpIndex{i}, external, InputPosition{input}});
                    }
                }
            }
        };
        uint32_t count = 0;
        each_read([&count](const ExternalRead&) noexcept { ++count; });
        uint32_t filled = 0;
        next_external_read_ = count;
        // The lease writes each read once, in order, so a read needs no
        // default value.  The second pass gives the reads that the first
        // pass counted.
        external_reads_ = ::fixy::mint_heap_array_written<ExternalRead>(
            allocation_, count, [&](::fixy::WriteLease<ExternalRead>& lease) noexcept {
                each_read([&](const ExternalRead& read) noexcept {
                    lease.put(read);
                    if (next_external_read_ == count && read.op.raw() >= resume_op.raw()) next_external_read_ = filled;
                    ++filled;
                });
            });
    }

    // Takes the cursor of bind_external_reads back to the first read, for
    // the first op of an iteration.
    CRUCIBLE_INLINE void rewind_external_reads() noexcept { next_external_read_ = 0; }

    // Binds each external slot that the op at matched reads to the storage of
    // that input of the live op.  The replay of each op calls the door once,
    // in the order of the ops.  An op that reads no external costs two
    // compares, and an op that reads one costs one call for all its reads.
    // The list holds only external slots of the active plan
    // (list_external_reads), so the door reads no plan, and it writes no
    // pooled slot.  Complexity: O(the reads of the op).
    CRUCIBLE_INLINE void bind_external_reads(OpIndex matched, const ValidatedOp& live, CompiledView const&) {
        const bool has_read =
            external_reads_.view().get(next_external_read_).is_some_and([matched](auto const& next) noexcept {
                return next->op.raw() <= matched.raw();
            });
        if (has_read) bind_reads_up_to_(matched, live);
    }

    // The bytes of an external slot as a View of the host
    // (PoolAllocator::register_external_view).  The View borrows its owner,
    // and the owner outlives the activation of the region.  The plan of the
    // active region is the one source of the windows, so the door checks
    // the View against the slot of the plan once, here.  The slot must be
    // external, the View must hold at least the nbytes of the slot, and its
    // first byte must be aligned for an element of the slot.  The View comes
    // from its owner, outside the program, so a refusal gives its
    // ViewRefusal, and the pool keeps the slot as it was.  The pool keeps the
    // first nbytes of a larger View, so a read of the slot gives the count of
    // elements of the plan.  A slot id outside the plan ends the process.
    // Complexity: O(1).
    [[nodiscard]] ::fixy::Result<::fixy::Unit, ViewRefusal>
    register_external_view(SlotId sid, ::fixy::View<uint8_t> bytes, CompiledView const&) {
        using Registered = ::fixy::Result<::fixy::Unit, ViewRefusal>;
        const TensorSlot& slot =
            *active_plan_().slots.view().get(sid.raw()).expect("a slot id of the plan is below the count of its slots");
        if (!slot.is_external) [[unlikely]]
            return ::fixy::err(ViewRefusal::PooledSlot);
        return bytes.window(0, slot.nbytes)
            .match(
                [this, sid, &slot](::fixy::View<uint8_t> planned) noexcept -> Registered {
                    if (!is_aligned_for_(slot.dtype, planned)) [[unlikely]]
                        return ::fixy::err(ViewRefusal::Misaligned);
                    auto pv = pool_.initialized_view_();
                    pool_.register_external_view(sid, planned, pv);
                    return ::fixy::Unit{};
                },
                []() noexcept -> Registered { return ::fixy::err(ViewRefusal::TooSmall); });
    }

    // The bytes of an input of the matched op as a read-only View of T, for a
    // kernel that reads them (PoolAllocator::slot_view).  No value for an
    // input with no slot, an external slot with no registered View, bytes of
    // the wrong size or alignment for T, and an input slot that is also an
    // output slot of the op: an op that writes in place reads that slot
    // through its ViewMut of with_output_views alone.  Complexity:
    // O(outputs of the op).
    template <class T>
        requires ::foundation::lifetime::ImplicitLifetimeThroughout<T>
    [[nodiscard]] ::fixy::Option<::fixy::View<T const>> input_view(InputPosition position, CompiledView const&) const& {
        const TraceEntry& op = engine_.current_entry();
        const ::fixy::View<const SlotId> inputs = op.input_slot_ids();
        CRUCIBLE_PRE(position.raw() < inputs.size());
        const SlotId sid = inputs.window<1>(position.raw())
                               .expect("the precondition bounds the input position")
                               .at(::fixy::index<0>());
        const bool is_written =
            op.output_slot_ids().walk().any_of([sid](SlotId const& output) noexcept { return output == sid; });
        if (is_written) return ::fixy::Option<::fixy::View<T const>>{::fixy::none};
        auto pv = pool_.initialized_view_();
        return pool_.slot_view<T>(sid, pv);
    }

    template <class T>
    ::fixy::Option<::fixy::View<T const>> input_view(InputPosition, CompiledView const&) const&& =
        delete("a temporary context releases its pool at the end of the statement. Use a context that has a name");

    // body gets a reference to a FixedList of one ViewMut<T> for each output
    // of the matched op, in the order of the outputs, and the door gives the
    // result of body, or Unit for a body that gives void.  The views come
    // from the frame door with_mut_windows of the bytes of the pool
    // (foundation/AlignedBytes.h), so each lives for the call of body alone.
    // The block of the pool moves into the frame and comes back after body, so
    // body reads the inputs through input_view, whose Views stay valid, and
    // the pool lends no block to a second call: with_output_views in body
    // gives OutsidePool for an output that holds an element.  The pool is lent
    // for the time of body, so a deactivation, an activation or a switch of
    // region through a second name of the context ends the process
    // (PoolAllocator, the lent pool).
    // The window of each output comes from the plan of the active region.
    // An external output gives ExternalOutput, an output with no slot
    // NoSlot, more outputs than max_block_windows TooManyOutputs, and a
    // window that the frame door refuses the refusal of the same name: the
    // windows of the outputs of one op must not overlap.  Complexity:
    // O(outputs) for the windows, O(outputs^2) for the check of overlap, and
    // the cost of body.
    template <class T, class Body>
        requires ::foundation::lifetime::ImplicitLifetimeThroughout<T>
    [[nodiscard]] auto with_output_views(CompiledView const&, Body&& body) & {
        return output_windows_().and_then([this, &body](OutputWindows windows) noexcept {
            [[maybe_unused]] auto const lend = pool_.lends_.lend();
            auto [written, block] =
                ::foundation::with_mut_windows<T>(std::move(pool_.pool_), windows, static_cast<Body&&>(body));
            pool_.pool_ = std::move(block);
            return std::move(written).map_err(
                [](::foundation::WindowRefusal refusal) noexcept { return output_refusal_of_(refusal); });
        });
    }

    template <class T, class Body>
    void with_output_views(CompiledView const&, Body&&) && =
        delete("a temporary context releases its pool at the end of the statement. Use a context that has a name");

    [[nodiscard]] uint64_t compiled_iterations() const { return compiled_iterations_.get(); }
    [[nodiscard]] uint64_t diverged_count() const { return diverged_count_.get(); }
    // The Vigil owns the region, and this context only points at it.  The
    // reference does not point into this context, and it has no twin.  It
    // is empty while the context records.
    [[nodiscard]] ::fixy::Option<DagRef<RegionNode const>> active_region() const { return active_region_.value(); }

    [[nodiscard]] const ReplayEngine& engine() const& { return engine_; }
    const ReplayEngine& engine() const&& =
        delete("a temporary context releases its engine at the end of the statement. Use a context that has a name");
    // The test door of the engine: a test or a bench of the engine alone drives
    // the engine of a context, which owns the pool that the engine reads.  The
    // capability of a test keeps product code out.  An advance or a reset
    // through the door counts no iteration and moves no cursor of the external
    // reads.
    [[nodiscard]] ReplayEngine& engine(::foundation::effects::Test const& /*test*/) & noexcept { return engine_; }
    [[nodiscard]] const PoolAllocator& pool() const& { return pool_; }
    const PoolAllocator& pool() const&& =
        delete("a temporary context releases its pool at the end of the statement. Use a context that has a name");

private:
    // The bitset below is sized at this bound, so a plan with more slots than
    // this would lose track of which it had already copied and copy some
    // twice. It is far tighter than the allocator's own slot ceiling, which is
    // why the migration checks it separately.
    static constexpr uint32_t MIGRATION_MAX_SLOTS = 1024;
    static constexpr uint32_t VISITED_WORDS = ::fixy::div_ceil(MIGRATION_MAX_SLOTS, uint32_t{64});

    // The windows of the outputs of the matched op in the bytes of the pool,
    // one for each output, in the order of the outputs.
    using OutputWindows = ::fixy::FixedList<::foundation::ByteWindow, ::foundation::max_block_windows>;

    // A slot that the plan of the active region marks external.  The door
    // external_slot_ is the one source, so a register door that takes one
    // writes no pooled slot.  The class is private to the context, and only
    // the list of the reads holds one, which each change of the active
    // region drops.  It holds one SlotId, so it costs what a SlotId costs.
    class ExternalSlotId final {
    public:
        [[nodiscard]] constexpr SlotId slot() const noexcept { return slot_; }

    private:
        friend struct CrucibleContext;

        constexpr explicit ExternalSlotId(SlotId slot) noexcept : slot_{slot} {}

        SlotId slot_{};
    };

    // The slot of the id, when the slot of the plan at the id is external,
    // or no value for an id outside the plan and for a pooled slot.
    // Complexity: O(1).
    [[nodiscard]] static constexpr ::fixy::Option<ExternalSlotId>
    external_slot_(::fixy::View<const TensorSlot> plan_slots, SlotId sid) noexcept {
        // The window of one slot holds the one bound test.
        const bool is_external =
            plan_slots.window<1>(sid.raw()).is_some_and([](::fixy::View<const TensorSlot, 1> const& planned) noexcept {
                return planned.at(::fixy::index<0>()).is_external;
            });
        if (!is_external) return ::fixy::none;
        return ::fixy::some(ExternalSlotId{sid});
    }

    // One read of an external slot by an op of the active region: the op,
    // the slot and the position of the input.  The slot and the position
    // have no default, so the read has none.
    struct ExternalRead {
        OpIndex op{};
        ExternalSlotId slot;
        InputPosition input;
        [[maybe_unused]] ::fixy::FixedArray<uint8_t, 2> pad{};
    };

    CRUCIBLE_INLINE void register_external_(ExternalSlotId external, TensorData data) {
        auto pv = pool_.initialized_view_();
        pool_.register_external(external.slot(), data, pv);
    }

    // Binds the listed reads of the op at matched to the live op, and passes
    // the reads of the earlier ops.  The cursor stops at the first read of a
    // later op.
    [[gnu::noinline]] void bind_reads_up_to_(OpIndex matched, const ValidatedOp& live) {
        const ::fixy::View<const ExternalRead> reads = external_reads_.view();
        while (next_external_read_ < reads.size()) {
            const ExternalRead& read = *reads.get(next_external_read_).expect("the test above bounds the cursor");
            if (read.op.raw() > matched.raw()) break;
            if (read.op.raw() == matched.raw()) bind_external_read_(live, read);
            ++next_external_read_;
        }
    }

    // Binds one external slot to the storage of one input of the live op.  A
    // slot holds one storage, so the slot takes the address of the first
    // byte of the storage, and two views at different offsets of one storage
    // bind the slot to one address.  The guard matched the shape hash of the
    // inputs, so the input is in the live op, and the test keeps each read in
    // bounds when a hash collides.
    void bind_external_read_(const ValidatedOp& live, const ExternalRead& read) {
        const uint16_t input = read.input.raw();
        if (input >= live.entry().num_inputs) return;
        storage_address(*live.metas().get(input).expect("the live op holds its inputs first"))
            .match([this, &read](TensorData data) noexcept { register_external_(read.slot, data); }, []() noexcept {});
    }

    void drop_external_reads_() noexcept {
        external_reads_ = ::fixy::HeapArray<ExternalRead>{};
        next_external_read_ = 0;
    }

    // The active region.  A compiled context has one.
    [[nodiscard]] const RegionNode& active_region_node_() const {
        return *::fixy::Option<DagRef<RegionNode const>>{active_region_.value()}.expect(
            "a compiled context has a region");
    }

    // The plan of the active region.  A compiled context has a region, and
    // the region of an activation has a plan.
    [[nodiscard]] const MemoryPlan& active_plan_() const {
        return *PlanLink{active_region_node_().plan}.expect("an active region has a memory plan");
    }

    // True when the first byte of the bytes is aligned for an element of the
    // type, or the bytes hold no byte.  A complex element is aligned for its
    // part, so its alignment is half its size.  An element of one byte and
    // the undefined type ask for no alignment.  Complexity: O(1).
    [[nodiscard]] static bool is_aligned_for_(ScalarType dtype, ::fixy::View<uint8_t> bytes) noexcept {
        const bool is_complex =
            dtype == ScalarType::ComplexHalf || dtype == ScalarType::ComplexFloat || dtype == ScalarType::ComplexDouble;
        const uint8_t size = element_size(dtype).raw();
        const uint8_t alignment = is_complex ? static_cast<uint8_t>(size / 2) : size;
        return ::fixy::mint_run_address(bytes, 0).is_none_or([alignment](::fixy::RunAddress const& first) noexcept {
            switch (alignment) {
                case 2:
                    return is_aligned<2>(first);
                case 4:
                    return is_aligned<4>(first);
                case 8:
                    return is_aligned<8>(first);
                default:
                    return true;
            }
        });
    }

    // The window of each output of the matched op, from the slot of the
    // plan of the active region: the offset of its first byte and its count
    // of bytes.  An external slot holds bytes of its owner, and no window of
    // the pool, so it gives ExternalOutput.  The column of bytes of the pool
    // holds the same window for each pooled slot, because register_external,
    // register_external_view and the list of the external reads refuse a
    // pooled slot of the active plan.  The check of each output stays in each
    // build: a pooled slot with another address or View ends the process, and
    // an input never reads other bytes than the output wrote.  Complexity:
    // O(outputs).
    [[nodiscard]] ::fixy::Result<OutputWindows, OutputRefusal> output_windows_() const {
        using ::fixy::side_as;
        using Start = ::foundation::core::side::WindowStart;
        using Length = ::foundation::core::side::WindowLength;
        const MemoryPlan& plan = active_plan_();
        OutputWindows windows{};
        for (const SlotId sid : engine_.current_entry().output_slot_ids()) {
            if (!sid.is_valid()) [[unlikely]]
                return ::fixy::err(OutputRefusal::NoSlot);
            const TensorSlot& slot =
                *plan.slots.view().get(sid.raw()).expect("init checked each slot id against the plan");
            if (slot.is_external) [[unlikely]]
                return ::fixy::err(OutputRefusal::ExternalOutput);
            CRUCIBLE_FATAL_INVARIANT(pool_.is_pool_window_(sid, slot));
            const ::foundation::ByteWindow window{side_as<Start>(::fixy::ByteCount{slot.offset_bytes}),
                                                  side_as<Length>(::fixy::ByteCount{slot.nbytes})};
            if (windows.append(window).is_err()) [[unlikely]]
                return ::fixy::err(OutputRefusal::TooManyOutputs);
        }
        return windows;
    }

    // The refusal of the outputs for a refusal of the frame door.
    [[nodiscard]] static constexpr OutputRefusal output_refusal_of_(::foundation::WindowRefusal refusal) noexcept {
        switch (refusal) {
            case ::foundation::WindowRefusal::outside_block:
                return OutputRefusal::OutsidePool;
            case ::foundation::WindowRefusal::misaligned:
                return OutputRefusal::Misaligned;
            case ::foundation::WindowRefusal::partial_element:
                return OutputRefusal::PartialElement;
            case ::foundation::WindowRefusal::overlap:
                return OutputRefusal::Overlap;
            default:
                ::fixy::unreachable();
        }
    }

    // The one body of both advance overloads.  A switch rather than a chain
    // of tests, so that a status added later has to be given a meaning here
    // instead of silently counting as a divergence.
    [[gnu::always_inline]] ReplayStep advance_(SchemaHash schema_hash, ShapeHash shape_hash) {
        const ReplayStep step = engine_.advance(schema_hash, shape_hash);
        switch (step.status()) {
            case ReplayStatus::MATCH:
                return step;
            case ReplayStatus::COMPLETE:
                // The advance that completes the region took the cursor back
                // to the first op and kept the current operation, so the
                // output and input pointers stay valid until the next advance.
                compiled_iterations_.bump();
                return step;
            case ReplayStatus::DIVERGED:
                diverged_count_.bump();
                return step;
            default:
                ::fixy::unreachable();
        }
    }

    void migrate_prefix_slots_(const RegionNode& old_region, const RegionNode& alt,
                               ::fixy::View<const uint8_t> old_pool, uint32_t div_pos) CRUCIBLE_NO_THREAD_SAFETY {
        // The two regions are compiled regions, so each has a plan.  A region
        // with no plan ends the process in each build.
        const MemoryPlan& old_plan = *PlanLink{old_region.plan}.expect("the region of a migration has a memory plan");
        const MemoryPlan& new_plan = *PlanLink{alt.plan}.expect("the region of a migration has a memory plan");
        const uint32_t old_num_slots = old_plan.num_slots();
        const uint32_t new_num_slots = new_plan.num_slots();
        // The bound of the bitset below.  The check runs in each build and
        // before any slot is copied, so a plan that the bitset cannot track
        // is refused and not half-migrated.  It is not a CRUCIBLE_PRE: under
        // the `ignore` semantic a CRUCIBLE_PRE gives its condition to the
        // optimizer as an assumption, and no check of that condition after
        // the assumption is certain to stay.
        CRUCIBLE_FATAL_INVARIANT(new_num_slots <= MIGRATION_MAX_SLOTS);
        using VisitedWord = ::fixy::Index<VISITED_WORDS>;
        ::fixy::FixedArray<uint64_t, VISITED_WORDS> visited{};

        for (uint32_t i = 0; i < div_pos; i++) {
            const auto& old_te = old_region.op_at(i);
            const auto& new_te = alt.op_at(i);

            // An op with no output slot ids has nothing to migrate.
            const ::fixy::View<const SlotId> old_ids = old_te.output_slot_ids();
            const ::fixy::View<const SlotId> new_ids = new_te.output_slot_ids();
            if (old_ids.size() == 0 || new_ids.size() == 0) continue;

            // Armed in a release build. The loop below pairs the output slot
            // ids of the two ops. The prefix ops carry identical schema and
            // shape hashes, which is exactly why unequal output counts mean
            // the region pair is corrupt.
            CRUCIBLE_FATAL_INVARIANT(old_te.num_outputs() == new_te.num_outputs());

            for (auto [old_id, new_id] : old_ids.walk().try_zip(new_ids.walk()).expect("the output counts are equal")) {
                const SlotId old_sid = *old_id;
                const SlotId new_sid = *new_id;

                if (!old_sid.is_valid() || !new_sid.is_valid()) continue;

                // Each id indexes the slot array of its own plan on the next
                // line, and the new id then indexes the bitset on the stack.
                // The check of the new id and the check of the plan bound
                // above keep the bitset index below MIGRATION_MAX_SLOTS.
                CRUCIBLE_FATAL_INVARIANT(old_sid.raw() < old_num_slots);
                CRUCIBLE_FATAL_INVARIANT(new_sid.raw() < new_num_slots);

                const TensorSlot& old_slot = *old_plan.slots.view()
                                                  .get(old_sid.raw())
                                                  .expect("the check above keeps the old slot id below its count");
                const TensorSlot& new_slot = *new_plan.slots.view()
                                                  .get(new_sid.raw())
                                                  .expect("the check above keeps the new slot id below its count");
                if (old_slot.is_external || new_slot.is_external) continue;

                const uint32_t ns = new_sid.raw();
                const VisitedWord word = VisitedWord::check(ns / 64).expect(
                    "the bound of the plan keeps a slot id of the migration below MIGRATION_MAX_SLOTS");
                const uint64_t bit = uint64_t{1} << (ns % 64);
                if ((visited.at(word) & bit) != 0) continue;
                visited.set(word, visited.at(word) | bit);

                // The copy takes the bytes of the old slot that the new slot
                // holds.  A new slot smaller than the old one gets its own
                // size, so the copy never writes past it into a neighbor or
                // past the end of the pool.  Each init checked its slots
                // against its pool, so each window fits.
                const uint64_t nbytes = ::fixy::min(old_slot.nbytes, new_slot.nbytes);
                if (nbytes == 0) continue;
                const ::fixy::View<const uint8_t> source =
                    old_pool.window(old_slot.offset_bytes, nbytes).expect("the old init kept each slot in its pool");
                pool_.copy_into_slot_(new_slot, source);
            }
        }
    }

    // The engine sits at offset zero, so its own hot cache line is this
    // object's hot cache line. The View of the ops of the engine, the mode,
    // the count of iterations, the active region and the list of the external
    // reads with its cursor share the next one, so the replay of an op that
    // reads no external touches no other line of this object. The pool
    // follows. The count of divergences is cold, and it goes after the pool.
    // The check file of this header pins the size.
    ReplayEngine engine_;  // 72 bytes, offset 0
    ContextMode mode_ = ContextMode::RECORD;  // offset 72
    [[maybe_unused]] ::fixy::FixedArray<uint8_t, 7> pad_{};
    // The count of completed iterations only goes up, and it is never reset.
    // Its type starts it at zero and steps it by one, so no bump can wrap it
    // (the wide counter of fixy/Mutation.h).
    ::fixy::WideMonotonic<1> compiled_iterations_ = ::fixy::mint_wide_monotonic<1>();  // offset 80
    // The tag records that the background worker of the owning foreground
    // published this region. A region allocated somewhere else has a
    // different lifetime regime, and without the tag the two would be
    // interchangeable here.  The default is the empty slot: no value under
    // the tag, which every reader tests before it reads the region.  The
    // empty Option is the null pointer, so the slot takes eight bytes.
    using ActiveRegionLink = ::fixy::Tagged<::fixy::Option<DagRef<RegionNode const>>, ::fixy::tags::source::Vigil>;

    // The slot of a region that the context replays.  The region outlives the
    // replay, and it lives in a DAG arena, so the slot holds it with the
    // DagArena brand.
    [[nodiscard]] static ActiveRegionLink link_active_region_(const RegionNode& region) noexcept {
        return ::fixy::mint_tagged<::fixy::tags::source::Vigil>(::fixy::some(::fixy::to(region)));
    }

    ActiveRegionLink active_region_{};  // offset 88
    // The reads of the external slots of the active region, in the order of
    // the ops (list_external_reads), and the next read of the iteration, at
    // most the count of the reads.  The list is empty while the context
    // records, and from a change of the active region to the next list.
    ::fixy::HeapArray<ExternalRead> external_reads_{};  // 16 bytes, offset 96
    uint32_t next_external_read_ = 0;  // offset 112
    [[maybe_unused]] ::fixy::FixedArray<uint8_t, 4> cursor_pad_{};
    PoolAllocator pool_;  // 80 bytes, offset 120
    // The count of divergences, with the type of the count of iterations.
    ::fixy::WideMonotonic<1> diverged_count_ = ::fixy::mint_wide_monotonic<1>();  // offset 200
    // The allocation capability that the constructor received.  The pool of
    // each activated region allocates only through it.  The tag is empty, so
    // it adds no storage.  It has no initializer, because the one
    // constructor sets it.
    [[no_unique_address]] ::foundation::effects::Alloc allocation_;
};

}  // namespace crucible
