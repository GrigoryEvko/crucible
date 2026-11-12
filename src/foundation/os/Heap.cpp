// The bodies of the allocation door of the Ref family
// (foundation/core/Heap.h).
//
// The door calls the allocator of the C library through the builtins of
// GCC, so the file includes no system header.  Each builtin compiles to a
// call of the function of the C library with the same name, and this member
// of the archive of foundation is the one member that imports malloc,
// aligned_alloc and free (rule R5, THE DOORS of the rule artifact-symbols).
// The allocator of the C library owns the state of the
// heap, so the door issues no raw system call.

#include <foundation/core/Heap.h>

#include <foundation/core/Report.h>

#include <cstddef>

namespace foundation::core::detail {

namespace {

// The alignment of each block of malloc of glibc on x86_64 and aarch64
// (MALLOC_ALIGNMENT, two words).  A larger alignment goes to
// aligned_alloc.
constexpr std::size_t kMallocAlignment = 16;

// The size of one huge page on the two targets.  A request at that
// alignment that gets no storage names the catalog entry of the remediation.
constexpr std::size_t kHugePageBytes = std::size_t{2} << 20;

[[noreturn, gnu::cold, gnu::noinline]] void refuse_request_(std::size_t bytes, std::size_t alignment) noexcept {
    fatal("the allocation door got a request of {} bytes at the alignment {}: the bytes must be more than zero, and "
          "the alignment must be a power of two",
          bytes, alignment);
}

[[noreturn, gnu::cold, gnu::noinline]] void no_storage_(std::size_t bytes, std::size_t alignment) noexcept {
    if (alignment >= kHugePageBytes) {
        fatal("the allocator gave no storage for {} bytes at the alignment {}.  The catalog entry "
              "foundation::diag::HugePageAllocationFailed gives the remediation",
              bytes, alignment);
    }
    fatal("the allocator gave no storage for {} bytes at the alignment {}: the host has no memory left", bytes,
          alignment);
}

void check_request_(std::size_t bytes, std::size_t alignment) noexcept {
    bool const is_power_of_two = alignment != 0 && (alignment & (alignment - 1)) == 0;
    if (bytes == 0 || !is_power_of_two) [[unlikely]] {
        refuse_request_(bytes, alignment);
    }
}

}  // namespace

// Heap.h gives the declaration the attribute returns_nonnull.  GCC 16 at -O3
// still suggests the attribute for a function that has it, also when the
// definition spells it again, so the suggestion is off for this one body.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsuggest-attribute=returns_nonnull"
void* heap_allocate_(std::size_t bytes, std::size_t alignment) noexcept {
    check_request_(bytes, alignment);
    void* const storage =
        alignment <= kMallocAlignment ? __builtin_malloc(bytes) : __builtin_aligned_alloc(alignment, bytes);
    if (storage == nullptr) [[unlikely]] {
        no_storage_(bytes, alignment);
    }
    return storage;
}
#pragma GCC diagnostic pop

void heap_release_(void* storage, std::size_t /*bytes*/, std::size_t /*alignment*/) noexcept {
    __builtin_free(storage);
}

}  // namespace foundation::core::detail
