#pragma once

// The generation allocator: the CPU allocator of PyTorch while the Vigil of a
// session of the vessel records.  It gives each storage that it makes a known
// generation from a count of its allocations (crucible::StorageGeneration).
// The allocator of PyTorch can give the address of a dead storage to a new
// storage, and the key of a storage in the build of a region is its address
// and its generation (crucible::storage_key).  So a free and a new allocation
// at one address give two keys.
//
// THE RECORD AND THE REPLAY.  The header of each allocation costs time, and a
// replay reads no generation.  So the CPU allocator follows the mode of the
// Vigil: the generation allocator while the Vigil records, and the prior
// allocator while it replays.  The record path compares the mode of the Vigil
// of each op with the mode at which the allocator changes, at the entry and at
// the exit of the op (AllocatorFollowScope).  The check at the entry follows a
// change of the mode outside an op, such as the load of a region.  The check
// at the exit follows a divergence of the op or of the close of its nest.  So
// the rest of a parent whose child diverged runs on the generation allocator.
//
// THE PERIODS.  fill_meta gives each storage that the generation allocator did
// not make an unknown generation with the period of the read: a storage of a
// replay, a storage that lived before the session, and a storage of another
// allocator.  The attach, the detach and each change to the prior allocator
// start a new period (crucible::AllocatorEpoch).  A change to the generation
// allocator keeps the period.  So a period is a replay and the record after
// it, or the record after the attach.
//
// In the ops that the Vigil records in one period, one address with an
// unknown generation names one storage, for three reasons:
//   - The Vigil records no op of a replay, and the build after a divergence
//     takes only the ops after it.  So no build holds two records of two
//     periods.
//   - While the generation allocator is set, each new storage of the CPU
//     allocator of PyTorch has a known generation.  So a free and a new
//     storage at one address with no generation occur only in the replay part
//     of a period.
//   - A recorded op of the record part reads only storages that live after
//     that replay part.  The parent of a child that diverged is a recorded op
//     that reads its inputs in the replay part.  Its inputs live until it
//     returns, which is after the exit check of the child.
// The period also keeps the edges of that parent.  The parent reads its
// inputs and gives its output in the replay part, and the ops after it read
// the same storages with the same key.
//
// A storage that the generation allocator made keeps its header and its
// deleter after a change, so its free stays correct and its generation stays
// known.
//
// THE CHAIN.  The allocator keeps the CPU allocator that was set before the
// first attach, and it allocates through it.  A profiler or another library
// that set its own CPU allocator keeps its allocations.  A change and the last
// detach put that allocator back, at the priority that it had, only when the
// generation allocator is the CPU allocator.  When a library set its own
// allocator over the generation allocator in the session, no change touches
// it, and each change to a replay still starts a new period.
//
// THE ALLOCATION.  Each allocation of n > 0 bytes takes n + 64 bytes from the
// inner allocator.  The first 64 bytes hold the header: the generation and
// the DataPtr of the inner allocation.  The data starts after the header.  The
// default CPU allocator of PyTorch aligns each allocation to 64 bytes
// (c10/core/alignment.h), so the data keeps that alignment.  When
// THP_MEM_ALLOC_ENABLE asks for the alignment of a page for a large
// allocation, the data keeps 64 bytes and not the page.  An allocation of zero
// bytes goes to the inner allocator with no header, so an empty tensor has no
// data address, as before.
//
// THE DATA POINTER.  The context of the DataPtr is the data pointer, and its
// deleter finds the header 64 bytes before the data.  raw_allocate of PyTorch
// needs a context equal to the data, and oneDNN allocates through it.
// raw_deleter() gives the same deleter.  is_simple_data_ptr() is false for an
// allocation of the generation allocator, so _lazy_clone refuses its storage
// (lazy_clone_storage gives no storage).  A copy-on-write deleter would hide
// the generation in the middle of a region, and the key of one live storage
// would change.
//
// THE LIFE.  The allocator object lives as long as the process: its storage
// never runs its destructor.  The deleter is a function of the dispatch
// library, and Python never unloads a library that it loaded.  So a storage
// that the allocator made can end in a replay, after the detach, and also
// after the static destructors of the process.  The library must not be
// unloaded while such a storage lives.
//
// THE THREADS.  An allocation and a free come from any thread, and the count
// of the generations is an atomic.  The attach, the detach and each change
// write the state of the sessions with no lock, and they call
// c10::SetCPUAllocator, which writes one pointer of an array of PyTorch with
// no lock.  So they have one writer, the thread of the sessions: the thread
// that holds the live claims of the Vigils of the process (ProducerClaim in
// foundation/effects/Ctx.h).  crucible_create claims the Vigil of a session on
// the thread of the session, before the attach.  Each change runs on the
// producer thread of a Vigil.  A call on another thread, while a claim of a
// Vigil lives, ends the process.  A reader on another thread gets the old
// allocator or the new one, and the two stay valid for the life of the
// process.

#include <crucible/CrucibleContext.h>
#include <crucible/Types.h>
#include <fixy/Core.h>
#include <foundation/Quarantine.h>

#include <c10/core/Allocator.h>

#include <cstddef>
#include <cstdint>

namespace crucible::vessel {

// The bytes before the data of each allocation of the generation allocator.
inline constexpr std::size_t kGenerationHeaderBytes = 64;

// The value of AllocatorFollow::switch_mode when no Vigil of a session
// records or replays: no mode of a Vigil has it.
inline constexpr uint8_t kNoAllocatorSwitch = 0xFF;

// The state of the allocator that the record path reads at the entry and at
// the exit of each op, and at each read of a storage.  The thread of the
// sessions (THE THREADS) is its one writer, at the attach, at the detach and
// at each change.  The record path reads it on the same thread.
struct AllocatorFollow {
    // The period of the CPU allocator.  The attach, the detach and each change
    // to the prior allocator add one (THE PERIODS).
    ::fixy::Atomic<uint32_t> epoch{};
    // The mode of a Vigil (crucible::ContextMode) before whose op the CPU
    // allocator changes: COMPILED while the generation allocator is set for
    // a record, RECORD while the prior allocator is set for a replay, and
    // kNoAllocatorSwitch with no session.
    ::fixy::Atomic<uint8_t> switch_mode{kNoAllocatorSwitch};
};

// One state for the dispatch library, which holds each kernel and the
// fallback.
inline AllocatorFollow g_allocator_follow{};

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the generation allocator gives PyTorch a deleter and a header of c10 types")

// The header of one allocation of the generation allocator, at the address
// that the inner allocator gave.  The generation comes first, so the record
// path reads it at a fixed offset.
struct GenerationHeader {
    crucible::StorageGeneration generation{};
    uint32_t pad = 0;  // zero
    c10::DataPtr inner;
};

static_assert(sizeof(GenerationHeader) <= kGenerationHeaderBytes, "the header fits the bytes before the data");

// The deleter of each allocation of the generation allocator.  It takes the
// data pointer, ends the header 64 bytes before it, and the end of the inner
// DataPtr gives the bytes back to the inner allocator.  A null pointer is a
// no-op, as the free of zero bytes through raw_deallocate is.
void delete_generation_allocation(void* data) noexcept;

// The generation of the storage of data: the known generation of an
// allocation of the generation allocator, and else an unknown generation in
// the period of the read.  A DataPtr with that deleter always has data.  One
// compare, and one load of the header or of the period.
[[nodiscard]] inline crucible::StorageGeneration generation_of(const c10::DataPtr& data) noexcept {
    if (data.get_deleter() == &delete_generation_allocation) {
        const void* const header_bytes = static_cast<const char*>(data.get()) - kGenerationHeaderBytes;
        return static_cast<const GenerationHeader*>(header_bytes)->generation;
    }
    return crucible::StorageGeneration::unknown(crucible::AllocatorEpoch{g_allocator_follow.epoch.load_acquire()});
}

// Sets the CPU allocator of the mode of the Vigil: the generation allocator
// for RECORD and the prior allocator for COMPILED, when the CPU allocator is
// the other one of the two.  A change to the prior allocator starts a new
// period.  Cold: the mode of a Vigil changes at the entry of a region and at a
// divergence.
CRUCIBLE_COLD void change_cpu_allocator(crucible::ContextMode mode) noexcept;

// The CPU allocator follows the mode of the Vigil.  One load and one compare
// when the allocator already follows the mode.
CRUCIBLE_INLINE void follow_vigil_mode(crucible::ContextMode mode) noexcept {
    if (static_cast<uint8_t>(mode) == g_allocator_follow.switch_mode.load_acquire()) [[unlikely]]
        change_cpu_allocator(mode);
}

// Counts one more session.  The first session sets the generation allocator
// as the CPU allocator of PyTorch, because a new Vigil records.  True when the
// CPU allocator follows the Vigils of the sessions after the call.
[[nodiscard]] bool attach_generation_allocator() noexcept;

// Counts the end of one session.  The last one puts the allocator of the
// first attach back, when the generation allocator is the CPU allocator, and
// the CPU allocator then follows no Vigil.  A detach with no attached session
// does nothing.
void detach_generation_allocator() noexcept;

// The address of the generation allocator, and the address of the allocator
// that it allocates through (0 before the first attach).  For the tests of
// the chain.
[[nodiscard]] uint64_t generation_allocator_address() noexcept;
[[nodiscard]] uint64_t generation_inner_address() noexcept;

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The CPU allocator follows the mode of the Vigil of one op that the vessel
// records, at the entry and at the exit of the op (THE RECORD AND THE REPLAY).
// The record path makes the scope before the nest of the op, so the exit check
// comes after the close of the nest, also in an unwind.
class [[nodiscard]] AllocatorFollowScope {
public:
    explicit AllocatorFollowScope(const crucible::CrucibleContext& context) noexcept : context_{::fixy::to(context)} {
        follow_vigil_mode(context.mode());
    }

    ~AllocatorFollowScope() { follow_vigil_mode(context_->mode()); }

    AllocatorFollowScope(const AllocatorFollowScope&) = delete("the scope of one op checks at its entry and its exit");
    AllocatorFollowScope&
    operator=(const AllocatorFollowScope&) = delete("the scope of one op checks at its entry and its exit");
    AllocatorFollowScope(AllocatorFollowScope&&) = delete("the scope of one op checks at its entry and its exit");
    AllocatorFollowScope&
    operator=(AllocatorFollowScope&&) = delete("the scope of one op checks at its entry and its exit");

private:
    ::fixy::Ref<const crucible::CrucibleContext> context_;
};

}  // namespace crucible::vessel
