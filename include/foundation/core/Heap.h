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
// allocation capability Alloc of foundation/core/AllocTag.h at its mint
// (AllocationCapability).
//
// The door takes one Layout of foundation/alloc/Layout.h, so a size and an
// alignment cannot swap at a call, and each request has bytes above zero at
// a power of two.  The door does not fail: a request that the allocator
// cannot give ends the process through fatal().  The tree does not run where
// an exhausted memory can recover.

#include <foundation/ByteSeal.h>
#include <foundation/alloc/Layout.h>
#include <foundation/core/AllocTag.h>

#include <cstddef>

namespace foundation::core {

namespace detail {

// Gives back the storage that heap_allocate_ or heap_allocate_zeroed_ gave,
// with the same layout.  storage must not be null.  A Layout passes in two
// registers (the layout in foundation/alloc/Layout.h), so each door takes it
// by value.  Each object in the storage ended before the call, and the door
// writes no byte of the storage, so it carries the mark free_door of
// foundation/ByteSeal.h: the quarantine plugin reads the pointer to void of
// a call as the pointer of an operator delete, and no view of bytes.
[[= ::foundation::core::free_door{}]] void heap_release_(void* storage, ::foundation::alloc::Layout layout) noexcept;

// Words of a layout.  The two bodies below take the two words of a
// Layout as two numbers, because the attributes alloc_size and alloc_align
// name parameters of an integer type.  With them GCC and _FORTIFY_SOURCE know
// the size and the alignment of each block that the door gives.  Only the two
// doors that take a Layout call them, so each request still comes from a
// Layout.  The bodies are in src/foundation/os/Heap.cpp.
[[nodiscard, gnu::malloc, gnu::malloc(heap_release_, 1), gnu::returns_nonnull, gnu::alloc_size(1),
  gnu::alloc_align(2)]] void*
heap_take_words_(std::size_t bytes, std::size_t alignment) noexcept;

[[nodiscard, gnu::malloc, gnu::malloc(heap_release_, 1), gnu::returns_nonnull, gnu::alloc_size(1),
  gnu::alloc_align(2)]] void*
heap_take_zeroed_words_(std::size_t bytes, std::size_t alignment) noexcept;

// Gives the storage of the layout.  The bytes hold no value until the caller
// writes them.  The result is never null.
[[nodiscard, gnu::always_inline]] inline void* heap_allocate_(::foundation::alloc::Layout layout) noexcept {
    return heap_take_words_(layout.bytes().raw(), static_cast<std::size_t>(layout.alignment()));
}

// The layout of count slots of T, for an owner that holds a run of slots,
// such as a GrowList.  count is above zero, and the bytes of the slots fit
// in kMaxLayoutBytes, or the process ends.  Complexity: O(1).
template <class T>
[[nodiscard]] constexpr ::foundation::alloc::Layout slots_layout_(std::size_t count) noexcept {
    return ::foundation::alloc::Layout::of_array<T>(ElementCount{count})
        .expect("the slots of an owner are a layout: at least one slot, and bytes that fit");
}

// Gives the storage of the layout, and each byte reads zero.  The alignment of
// the layout is at most the alignment of the allocator of the C library, 16
// bytes, or the process ends.  heap_release_ takes the storage back with the
// same layout.  The allocator of the C library, and that of AddressSanitizer,
// take a large block from fresh pages of the kernel, which hold zero, so they
// write no byte of such a block.  The allocator of ThreadSanitizer writes each
// byte.  The result is never null.
[[nodiscard, gnu::always_inline]] inline void* heap_allocate_zeroed_(::foundation::alloc::Layout layout) noexcept {
    return heap_take_zeroed_words_(layout.bytes().raw(), static_cast<std::size_t>(layout.alignment()));
}

// Spare storage.  An owner that holds storage past its elements, such as
// a GrowList between its count and its capacity, tells AddressSanitizer which
// slots hold no element.  heap_poison_ marks a run of bytes as not
// addressable, and heap_unpoison_ marks it as addressable again before the
// owner writes it.  A load or a store of a poisoned byte then ends the
// process with a report of AddressSanitizer, as the vectors of the C++
// library report a read past their size.  Only a build with AddressSanitizer
// has a body of the two functions, in src/foundation/os/Heap.cpp, through
// the door of foundation/os/Poison.h.  In each other build they are empty
// inline functions, so a call costs nothing.  The owner gives each run back
// to the allocation door unpoisoned.
#if defined(__SANITIZE_ADDRESS__)
void heap_poison_(void const* first, std::size_t bytes) noexcept;
void heap_unpoison_(void const* first, std::size_t bytes) noexcept;
#else
inline void heap_poison_(void const* /*first*/, std::size_t /*bytes*/) noexcept {}
inline void heap_unpoison_(void const* /*first*/, std::size_t /*bytes*/) noexcept {}
#endif

// Poisons the count slots from first (the spare storage).  A constant
// evaluation has no sanitizer, and a count of zero names no slot, so neither
// calls the door.
template <class T>
constexpr void poison_slots_(T const* first, std::size_t count) noexcept {
    if !consteval {
        if (count != 0) heap_poison_(static_cast<void const*>(first), count * sizeof(T));
    }
}

// Unpoisons the count slots from first, before the owner writes them or
// gives them back (the spare storage).
template <class T>
constexpr void unpoison_slots_(T const* first, std::size_t count) noexcept {
    if !consteval {
        if (count != 0) heap_unpoison_(static_cast<void const*>(first), count * sizeof(T));
    }
}

}  // namespace detail

}  // namespace foundation::core
