// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-CKernel-5 (#893): source::Singleton is distinct from other
// provenance lanes.  A value tagged as External must not satisfy a
// singleton-only CKernelTable consumer.

#include <crucible/CKernel.h>

static void needs_singleton(crucible::CKernelTableSingleton) {}

int main() {
  crucible::CKernelTable table{};
  crucible::safety::Tagged<
      crucible::CKernelTable*,
      crucible::safety::source::External> external{&table};
  needs_singleton(external);
  return 0;
}
