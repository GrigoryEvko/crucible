// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-CKernel-5 (#893): the global CKernelTable accessor returns
// CKernelTableSingleton, not a raw CKernelTable*.  A raw pointer cannot
// enter a singleton-only consumer without explicit provenance tagging.

#include <crucible/CKernel.h>

static void needs_singleton(crucible::CKernelTableSingleton) {}

int main() {
  crucible::CKernelTable table{};
  needs_singleton(&table);
  return 0;
}
