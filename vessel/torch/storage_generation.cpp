// The generation allocator of storage_generation.h.

#include "storage_generation.h"

#include <fixy/Core.h>

#include <c10/core/CPUAllocator.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace crucible::vessel {
namespace {

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the CPU allocator of PyTorch is a class with virtual functions")

// The count of the allocations of the process.  Each allocation takes the
// next value, also on two threads at one time.  64 bits do not wrap in the
// life of a process.
::fixy::Atomic<uint64_t> g_allocations{};

// The generation of a new allocation: the low word of the count.  A low word
// of zero reads as 1, so zero stays the value of no generation.
[[nodiscard]] uint32_t next_generation() noexcept {
    const auto low = static_cast<uint32_t>(g_allocations.fetch_add_acq_rel(1) + 1);
    return low == 0 ? 1U : low;
}

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
    // The address of the inner allocator, or 0 before the first attach.
    ::fixy::Atomic<uint64_t> inner_{};
};

// The storage of the allocator.  A union does not end its member, so the
// allocator lives as long as the process: a storage that it made keeps its
// address as the allocator of the storage, also after the static
// destructors.
union GenerationAllocatorStore {
    GenerationAllocator allocator;

    GenerationAllocatorStore() noexcept : allocator{} {}
    ~GenerationAllocatorStore() {}

    GenerationAllocatorStore(const GenerationAllocatorStore&) = delete("one allocator lives in the process");
    GenerationAllocatorStore& operator=(const GenerationAllocatorStore&) = delete("one allocator lives in the process");
};

[[nodiscard]] GenerationAllocator& the_allocator() noexcept {
    static GenerationAllocatorStore store;
    return store.allocator;
}

// The state of the sessions.  The thread of a session writes it at the attach
// and at the detach, as storage_generation.h states.
uint32_t g_sessions = 0;
c10::Allocator* g_prior = nullptr;
uint8_t g_priority = 0;

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
// first priority that takes is the priority of the prior allocator, and the
// detach puts that allocator back at that priority.  O(256) sets at most.
bool attach_generation_allocator() noexcept {
    GenerationAllocator& allocator = the_allocator();
    if (g_sessions > 0) {
        ++g_sessions;
        return c10::GetCPUAllocator() == &allocator;
    }
    c10::Allocator* const prior = c10::GetCPUAllocator();
    if (prior == &allocator) {
        ++g_sessions;
        return true;
    }
    allocator.set_inner(prior);
    for (uint32_t priority = 0; priority <= UINT8_MAX; ++priority) {
        c10::SetCPUAllocator(&allocator, static_cast<uint8_t>(priority));
        if (c10::GetCPUAllocator() != &allocator) continue;
        g_prior = prior;
        g_priority = static_cast<uint8_t>(priority);
        ++g_sessions;
        return true;
    }
    return false;
}

void detach_generation_allocator() noexcept {
    if (g_sessions == 0) return;
    --g_sessions;
    if (g_sessions > 0) return;
    if (c10::GetCPUAllocator() == &the_allocator()) c10::SetCPUAllocator(g_prior, g_priority);
}

uint64_t generation_allocator_address() noexcept { return std::bit_cast<uint64_t>(&the_allocator()); }

uint64_t generation_inner_address() noexcept { return std::bit_cast<uint64_t>(the_allocator().inner()); }

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace crucible::vessel
