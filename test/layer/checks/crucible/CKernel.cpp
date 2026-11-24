// The compile-time checks of crucible/CKernel.h.

#include <crucible/CKernel.h>

namespace crucible {

CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(CKernelEntry);

static_assert(::fixy::no_scoped_view_field_check<CKernelTable>());

static_assert(sizeof(CKernelTableSingleton) == sizeof(CKernelTable*));

// Each byte below NUM_KERNELS names a kernel.  ValidCKernelIdRaw holds a
// byte below NUM_KERNELS, so the expect of make_ckernel_id of
// crucible/CKernelId.h has its proof in the type and never ends the process.
[[nodiscard]] consteval bool names_each_byte_below_the_count() {
    for (uint8_t raw = 0; raw < static_cast<uint8_t>(CKernelId::NUM_KERNELS); ++raw) {
        if (::fixy::enum_from<CKernelId>(raw).is_none()) return false;
    }
    return true;
}
static_assert(names_each_byte_below_the_count());

}  // namespace crucible
