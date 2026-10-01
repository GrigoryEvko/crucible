#pragma once

#include <crucible/PoolAllocator.h>
#include <crucible/ReplayEngine.h>
#include <crucible/Types.h>
#include <fixy/Mutation.h>
#include <fixy/Refined.h>
#include <fixy/ScopedView.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

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
    uint8_t pad[2]{};
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
        return DispatchResult{.action = Action::COMPILED, .status = s, .pad = {}, .op_index = idx};
    }
};

struct CrucibleContext {
    CrucibleContext() = default;

    CrucibleContext(const CrucibleContext&) = delete("owns PoolAllocator with interior pointers");
    CrucibleContext& operator=(const CrucibleContext&) = delete("owns PoolAllocator with interior pointers");
    CrucibleContext(CrucibleContext&&) = delete("PoolAllocator has interior pointers into pool");
    CrucibleContext& operator=(CrucibleContext&&) = delete("PoolAllocator has interior pointers into pool");

    [[nodiscard]] bool activate(const RegionNode* region) CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(region != nullptr);
        if (!region->plan) [[unlikely]]
            return false;

        if (mode_ == ContextMode::COMPILED) deactivate();

        pool_.init(region->plan);
        engine_.init(region, ReplayEngine::PoolBorrow{pool_});
        active_region_ = ::fixy::mint_tagged<::fixy::tags::source::Vigil>(region);
        mode_ = ContextMode::COMPILED;

        // The mode and the active region move together. Every compiled-mode
        // method requires the first and dereferences the second, so publishing
        // one without the other leaves a pair that no method can safely act on.
        CRUCIBLE_POST(0, mode_ == ContextMode::COMPILED);
        CRUCIBLE_POST(0, active_region_.value() == region);
        return true;
    }

    void deactivate() {
        mode_ = ContextMode::RECORD;
        pool_.destroy();
        active_region_ = ActiveRegionPtr{};

        // The dual of the pair above: clearing the mode while leaving the
        // region pointer behind would let the next region switch replay a
        // prefix against a region that is gone.
        CRUCIBLE_POST(0, mode_ == ContextMode::RECORD);
        CRUCIBLE_POST(0, active_region_.value() == nullptr);
    }

    [[nodiscard, gnu::flatten]] CRUCIBLE_HOT ReplayStatus advance(SchemaHash schema_hash, ShapeHash shape_hash) {
        CRUCIBLE_PRE(mode_ == ContextMode::COMPILED);
        return advance_(schema_hash, shape_hash);
    }

    // Binds one of the operation's tensors to its slot in the memory plan.
    [[nodiscard]] CRUCIBLE_HOT void* output_ptr(uint16_t j) const CRUCIBLE_LIFETIMEBOUND {
        CRUCIBLE_PRE(mode_ == ContextMode::COMPILED);
        return engine_.output_ptr(j);
    }

    [[nodiscard]] CRUCIBLE_HOT void* input_ptr(uint16_t j) const CRUCIBLE_LIFETIMEBOUND {
        CRUCIBLE_PRE(mode_ == ContextMode::COMPILED);
        return engine_.input_ptr(j);
    }

    // Safe to call part-way through an iteration.
    [[nodiscard]] bool switch_region(const RegionNode* alt, uint32_t div_pos) CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(alt != nullptr);
        CRUCIBLE_PRE(mode_ == ContextMode::COMPILED);
        if (!alt->plan) [[unlikely]]
            return false;

        if (div_pos == 0) return activate(alt);

        const RegionNode* const old_region = active_region_.value();
        // Armed in a release build, unlike the two beliefs further down this
        // file. A null plan here does not fault: migrate_prefix_slots_ reads
        // old_plan->slots[sid] at whatever address a null base plus a slot
        // index lands on, and feeds the offsets it finds to a memcpy. That
        // writes outside the pool with no diagnostic. The path runs once per
        // shape change, so an always-on check costs nothing worth counting.
        CRUCIBLE_FATAL_INVARIANT(old_region != nullptr && old_region->plan != nullptr);

        // Detaching empties the pool, which makes the view stale, but it goes
        // out of scope before anything touches the pool again.
        auto pv = pool_.initialized_view_();
        auto old_pool = pool_.detach(pv);
        pool_.init(alt->plan);
        migrate_prefix_slots_(old_region, alt, old_pool.base, div_pos);

        engine_.init(alt, ReplayEngine::PoolBorrow{pool_});
        active_region_ = ::fixy::mint_tagged<::fixy::tags::source::Vigil>(alt);

        // Replay the prefix the two regions share, so the new region resumes
        // where the old one diverged.
        auto av = engine_.active_view_();
        for (uint32_t i = 0; i < div_pos; i++) {
            auto s = engine_.advance(alt->ops[i].schema_hash, alt->ops[i].shape_hash, av);
            // Debug-only: the engine was just pointed at alt, and these are
            // alt's own hashes read back in order, so anything but a match
            // means the engine's cursor is out of step rather than that the
            // data is bad. A release build carries no check because the next
            // real op diverges and the recovery path already handles that.
            CRUCIBLE_DEBUG_ASSERT(s == ReplayStatus::MATCH || s == ReplayStatus::COMPLETE);
            (void)s;
        }

        // Leaving the region pointer on the old region while the engine has
        // moved to the new one is the torn pair the activation contract
        // guards against. The early exit above reaches it through activate.
        CRUCIBLE_POST(0, active_region_.value() == alt);
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
    [[nodiscard]] CRUCIBLE_INLINE constexpr CompiledView mint_compiled_view(VigilFgCtx const&) const noexcept {
        CRUCIBLE_PRE(mode_ == ContextMode::COMPILED);
        return ::fixy::mint_view<ctx_mode::Compiled>(*this);
    }

    // These overloads take the proof in place of the mode check. A proof that
    // the context is compiled implies both the engine and the pool are live,
    // because activation initialises the two together, so the inner proofs
    // can be minted from the outer one.

    [[nodiscard, gnu::flatten]] CRUCIBLE_HOT ReplayStatus advance(SchemaHash schema_hash, ShapeHash shape_hash,
                                                                  CompiledView const&) {
        return advance_(schema_hash, shape_hash);
    }

    [[nodiscard]] CRUCIBLE_HOT void* output_ptr(uint16_t j, CompiledView const&) const CRUCIBLE_LIFETIMEBOUND {
        auto av = engine_.active_view_();
        return engine_.output_ptr(j, av);
    }

    [[nodiscard]] CRUCIBLE_HOT void* input_ptr(uint16_t j, CompiledView const&) const CRUCIBLE_LIFETIMEBOUND {
        auto av = engine_.active_view_();
        return engine_.input_ptr(j, av);
    }

    CRUCIBLE_INLINE void register_external(SlotId sid, ::fixy::NonNull<void*> ptr, CompiledView const&) {
        auto pv = pool_.initialized_view_();
        pool_.register_external(sid, ptr, pv);
    }

    [[nodiscard]] uint32_t compiled_iterations() const { return compiled_iterations_.get(); }
    [[nodiscard]] uint32_t diverged_count() const { return diverged_count_.get(); }
    [[nodiscard]] const RegionNode* active_region() const CRUCIBLE_LIFETIMEBOUND { return active_region_.value(); }

    [[nodiscard]] const ReplayEngine& engine() const CRUCIBLE_LIFETIMEBOUND { return engine_; }
    [[nodiscard]] const PoolAllocator& pool() const CRUCIBLE_LIFETIMEBOUND { return pool_; }

private:
    // The bitset below is sized at this bound, so a plan with more slots than
    // this would lose track of which it had already copied and copy some
    // twice. It is far tighter than the allocator's own slot ceiling, which is
    // why the migration checks it separately.
    static constexpr uint32_t MIGRATION_MAX_SLOTS = 1024;

    // The one body of both advance overloads.  A switch rather than a chain
    // of tests, so that a status added later has to be given a meaning here
    // instead of silently counting as a divergence.
    [[gnu::always_inline]] ReplayStatus advance_(SchemaHash schema_hash, ShapeHash shape_hash) {
        switch (engine_.advance(schema_hash, shape_hash)) {
            case ReplayStatus::MATCH:
                return ReplayStatus::MATCH;
            case ReplayStatus::COMPLETE:
                compiled_iterations_.bump();
                // The reset keeps the current operation, so the output and
                // input pointers stay valid until the next advance.
                engine_.reset();
                return ReplayStatus::COMPLETE;
            case ReplayStatus::DIVERGED:
                diverged_count_.bump();
                return ReplayStatus::DIVERGED;
            default:
                std::unreachable();
        }
    }

    void migrate_prefix_slots_(const RegionNode* old_region, const RegionNode* alt, const void* old_pool_base,
                               uint32_t div_pos) CRUCIBLE_NO_THREAD_SAFETY {
        CRUCIBLE_PRE(old_region != nullptr);
        CRUCIBLE_PRE(alt != nullptr);
        CRUCIBLE_PRE(old_region->plan != nullptr);
        CRUCIBLE_PRE(alt->plan != nullptr);
        const auto* old_plan = old_region->plan;
        const auto* new_plan = alt->plan;
        const uint32_t old_num_slots = old_plan->num_slots;
        const uint32_t new_num_slots = new_plan->num_slots;
        // The bound of the bitset below.  The check runs in each build and
        // before any slot is copied, so a plan that the bitset cannot track
        // is refused and not half-migrated.  It is not a CRUCIBLE_PRE: under
        // the `ignore` semantic a CRUCIBLE_PRE gives its condition to the
        // optimizer as an assumption, and no check of that condition after
        // the assumption is certain to stay.
        CRUCIBLE_FATAL_INVARIANT(new_num_slots <= MIGRATION_MAX_SLOTS);
        uint64_t visited[(MIGRATION_MAX_SLOTS + 63) / 64]{};

        for (uint32_t i = 0; i < div_pos; i++) {
            const auto& old_te = old_region->ops[i];
            const auto& new_te = alt->ops[i];

            if (!old_te.output_slot_ids || !new_te.output_slot_ids) continue;

            // Armed in a release build. The loop below runs to old_te's
            // output count and indexes new_te's array with the same j, so a
            // new entry with fewer outputs is read past its end. The slot id
            // that comes back is arbitrary, it selects an arbitrary entry of
            // new_plan->slots, and the offset there becomes the destination
            // of the memcpy at the bottom of this function. The prefix ops
            // carry identical schema and shape hashes, which is exactly why
            // unequal output counts mean the region pair is corrupt.
            CRUCIBLE_FATAL_INVARIANT(old_te.num_outputs == new_te.num_outputs);

            for (uint16_t j = 0; j < old_te.num_outputs; j++) {
                const SlotId old_sid = old_te.output_slot_ids[j];
                const SlotId new_sid = new_te.output_slot_ids[j];

                if (!old_sid.is_valid() || !new_sid.is_valid()) continue;

                // Each id indexes the slot array of its own plan on the next
                // line, and the new id then indexes the bitset on the stack.
                // The check of the new id and the check of the plan bound
                // above keep the bitset index below MIGRATION_MAX_SLOTS.
                CRUCIBLE_FATAL_INVARIANT(old_sid.raw() < old_num_slots);
                CRUCIBLE_FATAL_INVARIANT(new_sid.raw() < new_num_slots);

                if (old_plan->slots[old_sid.raw()].is_external || new_plan->slots[new_sid.raw()].is_external) continue;

                const uint32_t ns = new_sid.raw();
                if (visited[ns / 64] & (uint64_t{1} << (ns % 64))) continue;
                visited[ns / 64] |= uint64_t{1} << (ns % 64);

                const uint64_t old_offset = old_plan->slots[old_sid.raw()].offset_bytes;
                const uint64_t new_offset = new_plan->slots[ns].offset_bytes;
                const uint64_t nbytes = old_plan->slots[old_sid.raw()].nbytes;

                if (nbytes > 0 && old_pool_base) {
                    std::memcpy(static_cast<char*>(pool_.pool_base()) + new_offset,
                                static_cast<const char*>(old_pool_base) + old_offset, static_cast<size_t>(nbytes));
                }
            }
        }
    }

    // The engine sits at offset zero, so its own hot cache line is this
    // object's hot cache line. The mode and the counters share the next one,
    // and the pool follows. The size assertion below pins the whole layout.
    ReplayEngine engine_;  // 64 bytes, offset 0
    ContextMode mode_ = ContextMode::RECORD;  // offset 64
    [[maybe_unused]] uint8_t pad_[3]{};
    // Both counters only ever go up: one counts completed iterations, the
    // other divergences, and neither is reset. The type enforces that, and
    // catches the one way it could be broken, which is overflow.
    ::fixy::Monotonic<uint32_t> compiled_iterations_ = ::fixy::mint_monotonic<uint32_t>(0u);  // offset 68
    ::fixy::Monotonic<uint32_t> diverged_count_ = ::fixy::mint_monotonic<uint32_t>(0u);  // offset 72
    [[maybe_unused]] uint8_t pad2_[4]{};
    // The tag records that this pointer was published by the owning
    // foreground's background worker. A region allocated somewhere else has a
    // different lifetime regime, and without the tag the two would be
    // interchangeable here.  The default is the empty slot: a null pointer
    // under the tag, which every reader tests before it dereferences.
    using ActiveRegionPtr = ::fixy::Tagged<const RegionNode*, ::fixy::tags::source::Vigil>;
    // The tag must not add storage, or every offset in this class shifts.
    static_assert(sizeof(ActiveRegionPtr) == sizeof(const RegionNode*),
                  "a tagged region pointer must be the size of the pointer it wraps");
    static_assert(alignof(ActiveRegionPtr) == alignof(const RegionNode*),
                  "a tagged region pointer must be aligned as the pointer it wraps");
    ActiveRegionPtr active_region_{};  // offset 80
    PoolAllocator pool_;  // 32 bytes, offset 88
};

}  // namespace crucible
