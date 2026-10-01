// The compile-time checks of crucible/CKernel.h.

#include <crucible/CKernel.h>

namespace crucible {

static_assert(::fixy::no_scoped_view_field_check<CKernelTable>());

static_assert(sizeof(CKernelTableSingleton) == sizeof(CKernelTable*));

}  // namespace crucible
