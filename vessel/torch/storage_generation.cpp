// The generation allocator of storage_generation.h.

#include "storage_generation.h"

#include <crucible/ForegroundCtx.h>
#include <fixy/Core.h>
#include <foundation/core/Immortal.h>
#include <foundation/effects/Ctx.h>

#include <c10/core/CPUAllocator.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace crucible::vessel {
namespace {

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the CPU allocator of PyTorch is a class with virtual functions")

class GenerationAllocator final : public c10::Allocator {
public:
    // The allocator of the first attach.  Each later allocation goes
    // through it, also an allocation of a storage after the detach, such as
    // a resize of a tensor that the session made.  The attach stores it with
    // release order before it sets this allocator in PyTorch, and an
    // allocation on another thread loads it with acquire order.
    void set_inner(c10::Allocator* inner) noexcept { inner_.store_release(std::bit_cast<uint64_t>(inner)); }
    [[nodiscard]] c10::Allocator* inner() const noexcept {
        return std::bit_cast<c10::Allocator*>(inner_.load_acquire());
    }

    // The header and the data, as storage_generation.h states.  O(1), and the
    // cost of the inner allocation.
    c10::DataPtr allocate(std::size_t bytes) override {
        c10::Allocator* const inner = this->inner();
        if (inner == nullptr) ::fixy::fatal("the generation allocator allocates only after its first attach");
        if (bytes == 0 || bytes > SIZE_MAX - kGenerationHeaderBytes) return inner->allocate(bytes);
        c10::DataPtr allocation = inner->allocate(bytes + kGenerationHeaderBytes);
        void* const base = allocation.get();
        if (base == nullptr) return allocation;
        const c10::Device device = allocation.device();
        std::construct_at(static_cast<GenerationHeader*>(base),
                          GenerationHeader{.generation = next_generation(), .pad = 0, .inner = std::move(allocation)});
        void* const data = static_cast<char*>(base) + kGenerationHeaderBytes;
        return c10::DataPtr{data, data, &delete_generation_allocation, device};
    }

    [[nodiscard]] c10::DeleterFnPtr raw_deleter() const override { return &delete_generation_allocation; }

    // False for an allocation with the header, so lazy_clone_storage gives
    // no copy-on-write storage of it.
    [[nodiscard]] bool is_simple_data_ptr(const c10::DataPtr& data) const override {
        return data.get_deleter() != &delete_generation_allocation && data.get() == data.get_context();
    }

    void copy_data(void* destination, const void* source, std::size_t count) const override {
        default_copy_data(destination, source, count);
    }

private:
    // The known generation of a new allocation, from the low word of the
    // count of the allocations (crucible::StorageGeneration::known).
    [[nodiscard]] crucible::StorageGeneration next_generation() noexcept {
        const auto low = static_cast<uint32_t>(allocations_.fetch_add_acq_rel(1) + 1);
        return crucible::StorageGeneration::known(crucible::AllocationCount{low});
    }

    // The count of the allocations.  Each allocation takes the next value,
    // also on two threads at one time.  64 bits do not wrap in the life of a
    // process.
    ::fixy::Atomic<uint64_t> allocations_{};
    // The address of the inner allocator, or 0 before the first attach.
    ::fixy::Atomic<uint64_t> inner_{};
};

// The allocator and the state of its sessions: one object for the process,
// because PyTorch has one CPU allocator for the process.  An allocation and a
// free reach the allocator from each thread.  The thread of the sessions
// (storage_generation.h) is the one reader and the one writer of the other
// fields, at the attach, at the detach and at each change.
//
// The brand of the state is the brand of its one object, in the Immortal of
// the store, so the store keeps the Ref of the fill.
struct GenerationStateBrand final {};

struct GenerationState {
    using brand_type = GenerationStateBrand;

    GenerationAllocator allocator{};
    // The count of the attached sessions.
    uint32_t sessions = 0;
    // The CPU allocator before the first attach, and the priority at which
    // its set took (attach_generation_allocator).  No allocator before an
    // attach that took.
    c10::Allocator* prior = nullptr;
    uint8_t priority = 0;
};

// The holder of the state.  Its constructor builds the state in an Immortal,
// and its destructor is trivial.  So the allocator lives as long as the
// process: a storage that it made keeps its address as the allocator of the
// storage, also after the static destructors.  The store keeps the Ref that
// the fill gives, so a read of the state needs no test of the fill.
class GenerationStore {
public:
    GenerationStore() noexcept : state_{held_.fill()} {}

    GenerationStore(const GenerationStore&) = delete("one allocator lives in the process");
    GenerationStore& operator=(const GenerationStore&) = delete("one allocator lives in the process");

    [[nodiscard]] GenerationState& state() noexcept { return *state_; }

private:
    ::foundation::core::Immortal<GenerationState> held_{};
    ::foundation::core::Ref<GenerationState, GenerationStateBrand> state_;
};

// The state of the process.  The first call builds it, one time, also when
// two threads call at one time: the build of a static local is thread-safe.
[[nodiscard]] GenerationState& the_generation_state() noexcept {
    static GenerationStore store;
    return store.state();
}

// Ends the process when the calling thread is not the thread of the sessions:
// a claim of a Vigil lives, and another thread holds it (the threads of
// storage_generation.h).  A linear scan of the records of the brands.
void require_session_thread() noexcept {
    CRUCIBLE_FATAL_INVARIANT(::foundation::effects::host::ProducerClaim<crucible::Vigil>::can_caller_use_brand());
}

// Starts a new period of the allocator (the periods of storage_generation.h).
void begin_period() noexcept { g_allocator_follow.epoch.store_release(g_allocator_follow.epoch.load_acquire() + 1); }

// The next change of the CPU allocator comes at an op of a Vigil in the mode
// next.
void change_at(crucible::ContextMode next) noexcept {
    g_allocator_follow.switch_mode.store_release(static_cast<uint8_t>(next));
}

// Starts a new period in which the CPU allocator follows no Vigil.
void end_follow() noexcept {
    begin_period();
    g_allocator_follow.switch_mode.store_release(kNoAllocatorSwitch);
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the deleter and the doors take and give the allocators and the data of PyTorch")

void delete_generation_allocation(void* data) noexcept {
    if (data == nullptr) return;
    auto* const header =
        static_cast<GenerationHeader*>(static_cast<void*>(static_cast<char*>(data) - kGenerationHeaderBytes));
    // The DataPtr of the inner allocation ends after the header ends, and it
    // gives the bytes of the header and of the data back.
    c10::DataPtr allocation = std::move(header->inner);
    std::destroy_at(header);
}

// The priority of the CPU allocator of PyTorch is private.  A set with a
// priority below it changes nothing, and a set at or above it takes.  So the
// first priority that takes is the priority of the prior allocator, and each
// change and the detach put that allocator back at that priority.  O(256) sets
// at most.  A later session finds the allocator that follows the Vigils, and
// the first op of its Vigil sets the allocator of its mode.
bool attach_generation_allocator() noexcept {
    require_session_thread();
    GenerationState& state = the_generation_state();
    GenerationAllocator& allocator = state.allocator;
    if (state.sessions > 0) {
        ++state.sessions;
        return g_allocator_follow.switch_mode.load_acquire() != kNoAllocatorSwitch;
    }
    c10::Allocator* const prior = c10::GetCPUAllocator();
    if (prior == &allocator) {
        // A library set the generation allocator again with no session, so
        // the allocator of an earlier attach is its inner allocator.  An
        // allocator that never attached has none, and it refuses.
        if (state.prior == nullptr) return false;
        ++state.sessions;
        begin_period();
        change_at(crucible::ContextMode::COMPILED);
        return true;
    }
    allocator.set_inner(prior);
    for (uint32_t priority = 0; priority <= UINT8_MAX; ++priority) {
        c10::SetCPUAllocator(&allocator, static_cast<uint8_t>(priority));
        if (c10::GetCPUAllocator() != &allocator) continue;
        state.prior = prior;
        state.priority = static_cast<uint8_t>(priority);
        ++state.sessions;
        begin_period();
        change_at(crucible::ContextMode::COMPILED);
        return true;
    }
    return false;
}

void detach_generation_allocator() noexcept {
    require_session_thread();
    GenerationState& state = the_generation_state();
    if (state.sessions == 0) return;
    --state.sessions;
    if (state.sessions > 0) return;
    if (c10::GetCPUAllocator() == &state.allocator) c10::SetCPUAllocator(state.prior, state.priority);
    end_follow();
}

// The two sets take, because each one is at the priority that the attach
// found, and no other set came between them.  A CPU allocator that is neither
// of the two belongs to a library, and it stays.  A record keeps the period of
// the replay before it, so the ops after a divergence read the storages of the
// parent of the divergent op with the key that the parent gave them.
void change_cpu_allocator(crucible::ContextMode mode) noexcept {
    require_session_thread();
    GenerationState& state = the_generation_state();
    if (state.sessions == 0) {
        end_follow();
        return;
    }
    GenerationAllocator& allocator = state.allocator;
    c10::Allocator* const current = c10::GetCPUAllocator();
    if (mode == crucible::ContextMode::RECORD) {
        if (current == state.prior) c10::SetCPUAllocator(&allocator, state.priority);
        change_at(crucible::ContextMode::COMPILED);
        return;
    }
    if (current == &allocator) c10::SetCPUAllocator(state.prior, state.priority);
    begin_period();
    change_at(crucible::ContextMode::RECORD);
}

uint64_t generation_allocator_address() noexcept { return std::bit_cast<uint64_t>(&the_generation_state().allocator); }

uint64_t generation_inner_address() noexcept {
    return std::bit_cast<uint64_t>(the_generation_state().allocator.inner());
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace crucible::vessel
