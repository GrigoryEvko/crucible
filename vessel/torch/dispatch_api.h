#pragma once

// The C ABI of libcrucible_dispatch.so: the TLS accessors, the backward
// window, the schema accessors and the doors of the generation allocator,
// which crucible_native.py binds by name through ctypes.  crucible_fallback.cpp
// defines each one.
//
// The layout note of each vessel library folds the name and the type of each
// entry of this header and of vessel_api.h (shared_layout.h), so the loader
// refuses a library whose entries differ from the entries of the build.  The
// entries hold only C types, so the unit of the note reads them with no
// header of PyTorch.
//
// Each entry is noexcept, because ctypes has no C++ unwind, and the unit of
// the note checks that at compile time.  An entry that calls PyTorch code that
// can throw gives CRUCIBLE_HANDLE_FOREIGN_ERROR for an error of PyTorch, and
// crucible_dispatch_last_error then gives the text of that error.

#include <foundation/Platform.h>
#include <foundation/Quarantine.h>

#include <cstdint>

extern "C" {

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: ctypes binds these entries by name, so they take and give only C types")

CRUCIBLE_API uint64_t crucible_dispatch_layout_digest() noexcept;
CRUCIBLE_API uint32_t crucible_dispatch_mode_entry(uint32_t i, uint8_t* out_value, const char** out_name) noexcept;
CRUCIBLE_API uint32_t crucible_dispatch_set_tls_mode(uint8_t mode) noexcept;
// The handle of the TLS context is a CrucibleHandle of vessel_api.h, a word,
// and the set gives a CRUCIBLE_HANDLE_* status.
CRUCIBLE_API uint32_t crucible_dispatch_set_tls_context(uint64_t handle) noexcept;
CRUCIBLE_API uint8_t crucible_dispatch_get_tls_mode() noexcept;
CRUCIBLE_API uint64_t crucible_dispatch_get_tls_context() noexcept;
CRUCIBLE_API void crucible_dispatch_set_tls_scope(uint64_t scope_hash) noexcept;
CRUCIBLE_API uint64_t crucible_dispatch_get_tls_scope() noexcept;
CRUCIBLE_API void crucible_dispatch_backward_enter() noexcept;
CRUCIBLE_API void crucible_dispatch_backward_exit() noexcept;
CRUCIBLE_API uint32_t crucible_dispatch_backward_depth() noexcept;
CRUCIBLE_API uint32_t crucible_dispatch_schema_count() noexcept;
CRUCIBLE_API int crucible_dispatch_schema_entry(uint32_t i, uint64_t* out_hash, const char** out_name) noexcept;
// The eager word of the calling thread, and a CRUCIBLE_HANDLE_* status:
// FOREIGN_ERROR for an error of PyTorch, and REFUSED for a null out_bits or a
// precision that the vessel does not name.
CRUCIBLE_API uint32_t crucible_dispatch_eager_numerics(uint32_t* out_bits) noexcept;
// The text of the last error of PyTorch on the calling thread, which an entry
// gave as FOREIGN_ERROR.  The entry writes at most capacity - 1 bytes and a
// zero, and out_length gets the full length of the text of PyTorch, so a
// length above the bytes written shows a cut.  REFUSED for a null pointer or a
// capacity of 0.
CRUCIBLE_API uint32_t crucible_dispatch_last_error(char* out, uint32_t capacity, uint32_t* out_length) noexcept;
CRUCIBLE_API uint32_t crucible_dispatch_attach_allocator(uint32_t* out_follows) noexcept;
CRUCIBLE_API uint32_t crucible_dispatch_detach_allocator() noexcept;
CRUCIBLE_API uint64_t crucible_dispatch_cpu_allocator() noexcept;
CRUCIBLE_API uint64_t crucible_dispatch_generation_allocator() noexcept;
CRUCIBLE_API uint64_t crucible_dispatch_inner_allocator() noexcept;
CRUCIBLE_API uint32_t crucible_dispatch_storage_generation(void* storage) noexcept;

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // extern "C"
