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
// allocation capability Alloc at its mint (AllocationCapability).
//
// The door does not fail.  A request of zero bytes or of an alignment that
// is not a power of two ends the process through fatal(), and so does a
// request that the allocator cannot give.  The tree does not run where an
// exhausted memory can recover.

#include <concepts>
#include <cstddef>

// The owner of the bare value atoms.  foundation/effects/Effect.h defines it,
// and the rule home admits its definition only in that header.
namespace foundation::effects::host {
struct AtomOwner;
}  // namespace foundation::effects::host

namespace foundation::core {

// The allocation capability: the tag that shows an allocation at a call
// site.  foundation/effects gives it the name cap::Alloc with an alias, and
// the contexts Bg, Init and Test hold it, so ::foundation::effects::Alloc
// and bg.alloc name this one type.  The tag is empty and costs nothing.
//
// The tag is an authority, so no code makes one from nothing.  The default
// constructor is private, and its one friend is the owner of the bare value
// atoms in foundation/effects/Effect.h.  That owner builds the tag into a
// context and into the trade of a spent Capability for its bare tag.  So a
// holder of a tag got it from a context, or from a capability that a context
// minted.  The copy stays public, so a holder can pass the tag along.
//
// Each constructor is user-provided, and not defaulted.  A tag with a
// trivial copy is trivially copyable, and std::bit_cast then builds one from
// any byte.  A tag with a trivial constructor is an implicit-lifetime type,
// and std::start_lifetime_as then builds one over a buffer.  Neither route
// meets the access check.  The explicit noexcept on each special member
// turns a future throwing body into a compile error.
class Alloc {
public:
    constexpr Alloc(Alloc const&) noexcept {}
    constexpr Alloc& operator=(Alloc const&) noexcept { return *this; }
    ~Alloc() = default;

private:
    constexpr Alloc() noexcept {}

    friend struct ::foundation::effects::host::AtomOwner;
};

// The gate of each allocation door of the base: the tag is Alloc and no
// other type.  A tag of a different name does not pass, so each allocation
// shows the word Alloc at its call site.
template <class Marker>
concept AllocationCapability = std::same_as<Marker, Alloc>;

namespace detail {

// Gives back the storage that heap_allocate_ gave, with the same bytes and
// the same alignment.  storage must not be null.
void heap_release_(void* storage, std::size_t bytes, std::size_t alignment) noexcept;

// Gives the storage of bytes bytes at the alignment.  bytes is more than
// zero, and the alignment is a power of two.  The bytes hold no value until
// the caller writes them.  The result is never null.
[[nodiscard, gnu::malloc, gnu::malloc(heap_release_, 1), gnu::returns_nonnull, gnu::alloc_size(1),
  gnu::alloc_align(2)]] void*
heap_allocate_(std::size_t bytes, std::size_t alignment) noexcept;

// Gives the storage of bytes bytes, and each byte reads zero.  bytes is more
// than zero.  The storage has the alignment of the allocator of the C
// library, and heap_release_ takes it back with the same bytes and an
// alignment of one.  The allocator of the C library, and that of
// AddressSanitizer, take a large block from fresh pages of the kernel, which
// hold zero, so they write no byte of such a block.  The allocator of
// ThreadSanitizer writes each byte.  The result is never null.
[[nodiscard, gnu::malloc, gnu::malloc(heap_release_, 1), gnu::returns_nonnull, gnu::alloc_size(1)]] void*
heap_allocate_zeroed_(std::size_t bytes) noexcept;

}  // namespace detail

}  // namespace foundation::core
