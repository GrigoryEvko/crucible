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

#include <foundation/Platform.h>
#include <foundation/Quarantine.h>

#include <cstdint>

extern "C" {

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: ctypes binds these entries by name, so they take and give only C types")

CRUCIBLE_API uint64_t crucible_dispatch_layout_digest();
CRUCIBLE_API void crucible_dispatch_set_tls_mode(uint8_t mode);
CRUCIBLE_API void crucible_dispatch_set_tls_context(void* ctx);
CRUCIBLE_API uint8_t crucible_dispatch_get_tls_mode();
CRUCIBLE_API void* crucible_dispatch_get_tls_context();
CRUCIBLE_API void crucible_dispatch_set_tls_scope(uint64_t scope_hash);
CRUCIBLE_API uint64_t crucible_dispatch_get_tls_scope();
CRUCIBLE_API void crucible_dispatch_backward_enter();
CRUCIBLE_API void crucible_dispatch_backward_exit();
CRUCIBLE_API uint32_t crucible_dispatch_backward_depth();
CRUCIBLE_API uint32_t crucible_dispatch_schema_count();
CRUCIBLE_API int crucible_dispatch_schema_entry(uint32_t i, uint64_t* out_hash, const char** out_name);
CRUCIBLE_API uint32_t crucible_dispatch_eager_numerics();
CRUCIBLE_API uint32_t crucible_dispatch_attach_allocator();
CRUCIBLE_API void crucible_dispatch_detach_allocator();
CRUCIBLE_API uint64_t crucible_dispatch_cpu_allocator();
CRUCIBLE_API uint64_t crucible_dispatch_generation_allocator();
CRUCIBLE_API uint64_t crucible_dispatch_inner_allocator();
CRUCIBLE_API uint32_t crucible_dispatch_storage_generation(void* storage);

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // extern "C"
