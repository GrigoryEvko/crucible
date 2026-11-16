#pragma once

// The generation allocator: the CPU allocator of PyTorch while a session of
// the vessel is attached.  It gives each storage that it makes a generation, a
// count of the allocations of the process (crucible::StorageGeneration).  The
// allocator of PyTorch can give the address of a dead storage to a new
// storage, and the key of a storage in the build of a region is its address
// and its generation (crucible::storage_key).  So a free and a new allocation
// at one address give two keys.
//
// THE CHAIN.  The allocator keeps the CPU allocator that was set before the
// first attach, and it allocates through it.  A profiler or another library
// that set its own CPU allocator keeps its allocations.  The last detach puts
// that allocator back, at the priority that it had, when the generation
// allocator is still the CPU allocator.  When a library set its own allocator
// over the generation allocator in the session, the detach keeps that one.
//
// THE ALLOCATION.  Each allocation of n > 0 bytes takes n + 64 bytes from the
// inner allocator.  The first 64 bytes hold the header: the generation and
// the DataPtr of the inner allocation.  The data starts after the header.  The
// default CPU allocator of PyTorch aligns each allocation to 64 bytes
// (c10/core/alignment.h), so the data keeps that alignment.  When
// THP_MEM_ALLOC_ENABLE asks for the alignment of a page for a large
// allocation, the data keeps 64 bytes and not the page.  An allocation of zero
// bytes goes to the inner allocator with no header and gives no generation, so
// an empty tensor has no data address, as before.
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
// that the allocator made can end after the detach, and also after the
// static destructors of the process.  The library must not be unloaded while
// such a storage lives.
//
// THE THREADS.  An allocation and a free come from any thread, and the count
// of the generations is an atomic.  The attach and the detach call
// c10::SetCPUAllocator, which writes an array of PyTorch with no lock.  So a
// session attaches and detaches on its own thread, as CrucibleNative does,
// and no other thread allocates on the CPU at that time.  This is the rule of
// each caller of SetCPUAllocator.

#include <crucible/Types.h>
#include <foundation/Quarantine.h>

#include <c10/core/Allocator.h>

#include <cstddef>
#include <cstdint>

namespace crucible::vessel {

// The bytes before the data of each allocation of the generation allocator.
inline constexpr std::size_t kGenerationHeaderBytes = 64;

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the generation allocator gives PyTorch a deleter and a header of c10 types")

// The header of one allocation of the generation allocator, at the address
// that the inner allocator gave.  The generation comes first, so the record
// path reads it at a fixed offset.
struct GenerationHeader {
    uint32_t generation = 0;
    uint32_t pad = 0;  // zero
    c10::DataPtr inner;
};

static_assert(sizeof(GenerationHeader) <= kGenerationHeaderBytes, "the header fits the bytes before the data");

// The deleter of each allocation of the generation allocator.  It takes the
// data pointer, ends the header 64 bytes before it, and the end of the inner
// DataPtr gives the bytes back to the inner allocator.  A null pointer is a
// no-op, as the free of zero bytes through raw_deallocate is.
void delete_generation_allocation(void* data) noexcept;

// The generation of the storage of data, or no generation when data is not an
// allocation of the generation allocator.  A DataPtr with that deleter always
// has data, so the read is one compare and one load of the header.
[[nodiscard]] inline crucible::StorageGeneration generation_of(const c10::DataPtr& data) noexcept {
    if (data.get_deleter() != &delete_generation_allocation) return crucible::StorageGeneration{};
    const void* const header_bytes = static_cast<const char*>(data.get()) - kGenerationHeaderBytes;
    return crucible::StorageGeneration{static_cast<const GenerationHeader*>(header_bytes)->generation};
}

// Sets the generation allocator as the CPU allocator of PyTorch at the first
// attach, and counts each session.  True when the generation allocator is the
// CPU allocator after the call.
[[nodiscard]] bool attach_generation_allocator() noexcept;

// Counts the end of one session.  The last one puts the allocator of the
// first attach back, when the generation allocator is still the CPU
// allocator.  A detach with no attached session does nothing.
void detach_generation_allocator() noexcept;

// The address of the generation allocator, and the address of the allocator
// that it allocates through (0 before the first attach).  For the tests of
// the chain.
[[nodiscard]] uint64_t generation_allocator_address() noexcept;
[[nodiscard]] uint64_t generation_inner_address() noexcept;

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace crucible::vessel
