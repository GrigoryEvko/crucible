#pragma once

// The allocation door of the Ref family: the one place of the base that
// takes storage from the heap and gives it back.
//
// Each owner of heap storage in the base, such as Box, the arenas and the
// owners of the Region family, gets its bytes from heap_allocate_ and gives
// them back through heap_release_.  The bodies are in src/foundation/os/Heap.cpp, and they call the allocator of
// the C library.  A change of the allocator is then a change of one file,
// and the rule artifact-symbols holds each call of the allocator in that one
// member of the archive.
//
// The door is in namespace detail, and the rule owners refuses a use of it
// outside the base and the check files.  Code outside the base gets heap
// storage only from an owner of the base, and each such owner takes the
// allocation capability (AllocationCapability) at its mint.
//
// The door does not fail.  A request of zero bytes or of an alignment that
// is not a power of two ends the process through fatal(), and so does a
// request that the allocator cannot give.  The tree does not run where an
// exhausted memory can recover.

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace foundation::core {

// The tag that shows an allocation at a call site.  foundation/effects is
// above this layer, so the gate reads the member that its allocation tag
// declares, and not the type.  The tag is empty and costs nothing.
template <class Marker>
concept AllocationCapability = std::is_empty_v<Marker> && std::is_trivially_copyable_v<Marker>
                            && std::same_as<typename Marker::grants_allocation, void>;

namespace detail {

// Gives back the storage that heap_allocate_ or heap_allocate_zeroed_ gave,
// with the same bytes and the same alignment.  storage must not be null.
void heap_release_(void* storage, std::size_t bytes, std::size_t alignment) noexcept;

// Gives the storage of bytes bytes at the alignment.  bytes is more than
// zero, and the alignment is a power of two.  The bytes hold no value until
// the caller writes them.  The result is never null.
[[nodiscard, gnu::malloc, gnu::malloc(heap_release_, 1), gnu::returns_nonnull, gnu::alloc_size(1),
  gnu::alloc_align(2)]] void*
heap_allocate_(std::size_t bytes, std::size_t alignment) noexcept;

}  // namespace detail

}  // namespace foundation::core
