// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// source::Singleton is a provenance tag of its own.  A table pointer
// tagged source::External must not satisfy a consumer that requires the
// singleton CKernelTable.
//
// Expected diagnostic: no conversion from the External pointer to
// CKernelTableSingleton.

#include <crucible/CKernel.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

static void needs_singleton(crucible::CKernelTableSingleton) {}

int main() {
    crucible::CKernelTable table{};
    auto external = ::fixy::mint_tagged<::fixy::tags::source::External>(&table);
    needs_singleton(external);
    return 0;
}
