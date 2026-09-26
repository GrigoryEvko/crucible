// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A registry handle is the proof that an init context was checked, so
// its constructor is private and mint_hot_region_registry_handle is the
// only way to hold one.  Building a handle directly must not compile.

#include <crucible/warden/Registry.h>

int main() {
    crucible::warden::HotRegionRegistryHandle handle{};
    (void)handle.size();
    return 0;
}
