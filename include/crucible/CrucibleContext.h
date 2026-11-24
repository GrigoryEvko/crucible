#pragma once

#include <crucible/PoolAllocator.h>
#include <crucible/ReplayEngine.h>
#include <crucible/Types.h>
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

// The slots of a memory plan, and no other type, so the mint reads the flag
// of a slot of a plan.
template <class Slots>
concept PlanSlotsView = std::is_same_v<Slots, ::fixy::View<TensorSlot const>>;

// A slot id that the plan of a region marks external.  mint_external_slot
// is the one door, so a register door that takes one writes no pooled slot,
// and the window of each pooled slot stays the window of the plan.  No
// constructor takes a SlotId outside the mint, and the class has no default.
// The id names a slot of the plan that the mint read: the Vigil mints the
// ids of each activation from the plan of that activation.  The class holds
// one SlotId, so it costs what a SlotId costs.
class ExternalSlotId final {
public:
    [[nodiscard]] constexpr SlotId slot() const noexcept { return slot_; }

private:
    // The return type is deduced, so the declaration names no Option of
    // this class before the class is complete.
    template <PlanSlotsView Slots>
    friend constexpr auto mint_external_slot(Slots slots, SlotId sid) noexcept;

    constexpr explicit ExternalSlotId(SlotId slot) noexcept : slot_{slot} {}

    SlotId slot_{};
};

// The id sid, when the slot at sid of the plan slots is external, or no
// value for an id outside the plan and for a pooled slot: an
// Option<ExternalSlotId>.  The Vigil mints one at each test of an external
// slot, and the address form of register_external takes only this type.
// Complexity: O(1).
template <PlanSlotsView Slots>
[[nodiscard]] constexpr auto mint_external_slot(Slots slots, SlotId sid) noexcept {
    using Minted = ::fixy::Option<ExternalSlotId>;
    // The window of one slot holds the one bound test, which a loop over the
    // positions of the plan folds into its own test.
    const bool is_external =
        slots.template window<1>(sid.raw()).is_some_and([](::fixy::View<TensorSlot const, 1> const& planned) noexcept {
            return planned.at(::fixy::index<0>()).is_external;
        });
    if (!is_external) return Minted{::fixy::none};
    return Minted::some(ExternalSlotId{sid});
}

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
        engine_.init(region, ::fixy::to(pool_));
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

        // Detaching empties the pool, which makes the view stale, but it goes
        // out of scope before anything touches the pool again.
        auto pv = pool_.initialized_view_();
        const ::foundation::AlignedBytes old_pool = pool_.detach(pv);
        pool_.init(allocation_, *new_plan);
        migrate_prefix_slots_(old_region, alt, old_pool.view(), div_pos);

        engine_.init(alt, ::fixy::to(pool_));
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
    // (PoolAllocator::register_external).  The id comes from
    // mint_external_slot, so the plan marks the slot external, and the door
    // writes no pooled slot.  The door runs for each external read of the
    // replay, and the type carries the proof, so it reads no plan.
    CRUCIBLE_INLINE void register_external(ExternalSlotId external, TensorData data, CompiledView const&) {
        auto pv = pool_.initialized_view_();
        pool_.register_external(external.slot(), data, pv);
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
    // gives OutsidePool for an output that holds an element.
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

    // The plan of the active region.  A compiled context has a region, and
    // the region of an activation has a plan.
    [[nodiscard]] const MemoryPlan& active_plan_() const {
        const RegionNode& region =
            *::fixy::Option<DagRef<RegionNode const>>{active_region_.value()}.expect("a compiled context has a region");
        return *PlanLink{region.plan}.expect("an active region has a memory plan");
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
    // holds the same window for each pooled slot.  register_external_view
    // refuses a pooled slot, and register_external takes an ExternalSlotId,
    // but that id can come from the plan of an earlier activation.  So the
    // check of each output stays in each build: a pooled slot with another
    // address or View ends the process, and an input never reads other bytes
    // than the output wrote.  Complexity: O(outputs).
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
                visited.with_element(word, [bit](uint64_t& marks) noexcept { marks |= bit; });

                // The copy takes the bytes of the old slot that the new slot
                // holds.  A new slot smaller than the old one gets its own
                // size, so the copy never writes past it into a neighbor or
                // past the end of the pool.  Each init checked its slots
                // against its pool, so each window fits.
                const uint64_t nbytes = ::fixy::min(old_slot.nbytes, new_slot.nbytes);
                if (nbytes == 0) continue;
                const ::fixy::View<const uint8_t> source =
                    old_pool.window(old_slot.offset_bytes, nbytes).expect("the old init kept each slot in its pool");
                const ::fixy::View<uint8_t> target = pool_.bytes()
                                                         .window(new_slot.offset_bytes, nbytes)
                                                         .expect("the new init kept each slot in its pool");
                ::fixy::copy(target, source).expect("the two windows hold one count of bytes");
            }
        }
    }

    // The engine sits at offset zero, so its own hot cache line is this
    // object's hot cache line. The View of the ops of the engine, the mode,
    // the count of iterations and the active region share the next one, and
    // the pool follows. The count of divergences is cold, and it goes after
    // the pool. The check file of this header pins the size.
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
    PoolAllocator pool_;  // 80 bytes, offset 96
    // The count of divergences, with the type of the count of iterations.
    ::fixy::WideMonotonic<1> diverged_count_ = ::fixy::mint_wide_monotonic<1>();  // offset 176
    // The allocation capability that the constructor received.  The pool of
    // each activated region allocates only through it.  The tag is empty, so
    // it adds no storage.  It has no initializer, because the one
    // constructor sets it.
    [[no_unique_address]] ::foundation::effects::Alloc allocation_;
};

}  // namespace crucible
