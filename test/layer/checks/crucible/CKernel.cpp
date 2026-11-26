// The compile-time checks of crucible/CKernel.h.

#include <crucible/CKernel.h>

namespace crucible {

CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(CKernelEntry);

static_assert(::fixy::no_scoped_view_field_check<CKernelTable>());

static_assert(sizeof(CKernelTableSingleton) == sizeof(CKernelTable*));

// Each byte below ckernel_id_count names a kernel, and the count names none.
// ValidCKernelIdRaw holds a byte below the count, so the expect of
// make_ckernel_id of crucible/CKernelId.h has its proof in the type and never
// ends the process.
[[nodiscard]] consteval bool names_each_byte_below_the_count() {
    for (uint8_t raw = 0; std::size_t{raw} < ckernel_id_count; ++raw) {
        if (::fixy::enum_from<CKernelId>(raw).is_none()) return false;
    }
    return ::fixy::enum_from<CKernelId>(static_cast<uint8_t>(ckernel_id_count)).is_none();
}
static_assert(names_each_byte_below_the_count());

}  // namespace crucible
