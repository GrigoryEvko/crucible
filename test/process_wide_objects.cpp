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

#include <array>

const void* process_wide_object_address(unsigned index) noexcept {
    const std::array<const void*, 5> addresses{
        &crucible::detail::static_ckernel_table_,
        crucible::global_ckernel_table().value(),
        &crucible::global_schema_table(),
        &crucible::late_schema_table(),
        &crucible::warden::HotRegionRegistry::instance(),
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
}

const void* process_wide_control_address(unsigned index) noexcept {
    using namespace process_wide_libraries;
    const std::array<const void*, 2> addresses{&control_value, &control_thread_value};
    return index < addresses.size() ? addresses[index] : nullptr;
}

// The member holds an address, so its value is read as a comparison with
// the address of the first object on this thread: 1 when it points there.
std::uint64_t process_wide_thread_object_value(unsigned index) noexcept {
    using namespace process_wide_libraries;
    if (index == 0) return per_thread_value;
    if (index == 1) return per_thread_local();
    return PerThreadHolder::held == &per_thread_value ? 1u : 0u;
}
