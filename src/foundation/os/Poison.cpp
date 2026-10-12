// The bodies of the door of AddressSanitizer (foundation/os/Poison.h).
//
// Only a build with AddressSanitizer compiles code from this file.  The two
// functions of libasan that it calls take an address and a count of bytes.
// This file declares them, as src/foundation/os/Intercepted.cpp declares the
// hooks of ThreadSanitizer, so the door names no header outside the
// allowance of its layer.

#include <foundation/os/Poison.h>

#if defined(__SANITIZE_ADDRESS__)

extern "C" {
void __asan_poison_memory_region(void const volatile* address, std::size_t size);
void __asan_unpoison_memory_region(void const volatile* address, std::size_t size);
}

namespace foundation::os {

void poison_bytes(void const* first, std::size_t count) noexcept { __asan_poison_memory_region(first, count); }

void unpoison_bytes(void const* first, std::size_t count) noexcept { __asan_unpoison_memory_region(first, count); }

}  // namespace foundation::os

#endif
