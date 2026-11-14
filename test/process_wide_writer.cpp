// The shared library that writes each process-wide object.  See
// process_wide_libraries.h.

#include "process_wide_libraries.h"

#include <crucible/CKernel.h>
#include <crucible/ForegroundCtx.h>
#include <crucible/SchemaTable.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/core/Array.h>

namespace process_wide_libraries {
namespace {

inline constexpr ::foundation::core::FixedArray<crucible::CKernelEntry, 1> kPublishedRows{
    crucible::CKernelEntry{.schema_hash = crucible::SchemaHash{kPublishedHash}, .id = crucible::CKernelId::GEMM_MM}};

// Static storage duration, which publish_static_ckernel_table asks of the table.
inline constexpr crucible::StaticCKernelTable kPublishedTable{.entries = kPublishedRows.view()};

// A mutable view asks for the context of a Vigil's producer claim.  No Vigil
// runs here, so the context comes from the test door.
constexpr crucible::VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<crucible::Vigil>();

}  // namespace
}  // namespace process_wide_libraries

void process_wide_write() noexcept {
    using namespace process_wide_libraries;
    crucible::publish_static_ckernel_table(kPublishedTable);

    const bool was_kernel_registered = crucible::register_schema_hash(
        kVigilForeground, ::fixy::mint_tagged<::fixy::tags::source::External>(crucible::SchemaHash{kRegisteredHash}),
        crucible::CKernelId::EWISE_ADD);
    CRUCIBLE_FATAL_INVARIANT(was_kernel_registered);

    const auto view = crucible::global_schema_table()
                          .mint_mutable_view(kVigilForeground)
                          .expect("no code seals the global schema table before the writer runs");
    const bool was_name_registered = crucible::register_schema_name(
        view, crucible::SchemaHash{kNamedHash},
        ::fixy::mint_tagged<::fixy::tags::source::FromInternal>(::fixy::TextView{kSchemaName}));
    CRUCIBLE_FATAL_INVARIANT(was_name_registered);
}

// Value-initialization, not aggregate initialization: the implicit default
// constructor of the brand builds the claim, and only the brand has access.
void* process_wide_claim_brand() noexcept {
    auto* brand = new process_wide_libraries::SharedBrand();
    (void)brand->claim.mint_producer_context();
    return brand;
}

void process_wide_release_brand(void* brand) noexcept {
    delete static_cast<process_wide_libraries::SharedBrand*>(brand);
}
