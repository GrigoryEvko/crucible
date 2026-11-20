// The address of each process-wide object, as the library that holds this
// file sees it.  See process_wide_libraries.h.  An object whose name sits in
// a detail namespace of foundation or fixy has no row here: the claim test
// reads the record of producer claims through its public check.  The probe
// settings of the ledger have no row, because their header needs the bench
// harness.

#include "process_wide_libraries.h"

#include <crucible/CKernel.h>
#include <crucible/SchemaTable.h>
#include <crucible/warden/Registry.h>
#include <foundation/alloc/ChunkMap.h>
#include <foundation/alloc/HeapState.h>
#include <foundation/alloc/Root.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/KernelFloor.h>

#include <array>

const void* process_wide_object_address(unsigned index) noexcept {
    const std::array<const void*, 7> addresses{
        &crucible::detail::static_ckernel_table_,
        crucible::global_ckernel_table().value(),
        &crucible::global_schema_table(),
        &crucible::late_schema_table(),
        &crucible::warden::HotRegionRegistry::instance(),
        &::foundation::alloc::alloc_root_,
        &::foundation::alloc::sentinel_heap_,
    };
    return index < addresses.size() ? addresses[index] : nullptr;
}

const void* process_wide_thread_object_address(unsigned index) noexcept {
    using namespace process_wide_libraries;
    const std::array<const void*, 3> addresses{&per_thread_value, &per_thread_local(), &PerThreadHolder::held};
    return index < addresses.size() ? addresses[index] : nullptr;
}

void process_wide_set_thread_objects(std::uint64_t value) noexcept {
    using namespace process_wide_libraries;
    per_thread_value = value;
    per_thread_local() = value;
    PerThreadHolder::held = &per_thread_value;
    ThreadCell{}.store(value);
}

const void* process_wide_control_address(unsigned index) noexcept {
    using namespace process_wide_libraries;
    const std::array<const void*, 2> addresses{&control_value, &control_thread_value};
    return index < addresses.size() ? addresses[index] : nullptr;
}

// The member holds an address, so its value is read as a comparison with
// the address of the first object on this thread: 1 when it points there.
// Index 3 is the ThreadLocalRef cell.
std::uint64_t process_wide_thread_object_value(unsigned index) noexcept {
    using namespace process_wide_libraries;
    if (index == 0) return per_thread_value;
    if (index == 1) return per_thread_local();
    if (index == 3) return ThreadCell{}.peek();
    return PerThreadHolder::held == &per_thread_value ? 1u : 0u;
}

void process_wide_report_floor_fallbacks() noexcept {
    ::foundation::os::report_kernel_fallbacks_once(::foundation::os::minimum_kernel());
}

void process_wide_open_init_door() noexcept {
    static_cast<void>(::foundation::effects::host::InitOwner::mint_init_context());
}

::fixy::AddressKey process_wide_heap_cell_key() noexcept {
    return ::fixy::mint_address_key(&::foundation::alloc::tls_heap_);
}

int process_wide_heap_is_sentinel() noexcept {
    return ::foundation::alloc::is_sentinel(::foundation::alloc::current_heap()) ? 1 : 0;
}

void process_wide_start_alloc_root() noexcept { ::foundation::alloc::ensure_root(); }

int process_wide_sentinel_holds_host_map() noexcept {
    namespace alloc = ::foundation::alloc;
    alloc::BiasedChunkMap const map = alloc::sentinel_heap_.chunk_map();
    bool const is_host_map = alloc::alloc_root_.is_ready() && !map.is_empty()
                          && map == alloc::alloc_root_.window(alloc::kHostWindow).chunk_map();
    return is_host_map && alloc::sentinel_heap_.self() == alloc::ChunkOwner::none ? 1 : 0;
}

void process_wide_claim_heap() noexcept { static_cast<void>(::foundation::alloc::claim_thread_heap()); }

std::uint16_t process_wide_heap_id() noexcept {
    return static_cast<std::uint16_t>(::foundation::alloc::current_heap().self());
}
