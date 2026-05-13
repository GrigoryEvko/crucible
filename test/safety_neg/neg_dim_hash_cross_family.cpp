// WRAP-DimHash-1 (#910): dim_hash_*_det returns DimHashDet =
// DetSafe<Pure, Tagged<uint64_t, hash_family::FamilyB>>.
//
// This fixture provokes the provenance fence: a persistent Family-A
// hash must not substitute for a process-local Family-B dim hash even
// when both carry the same DetSafe<Pure> tier.

#include <crucible/ir001/DimHash.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>

using FamilyAHash = crucible::safety::Tagged<
    uint64_t, crucible::hash_family::FamilyA>;
using FamilyADetHash = crucible::safety::DetSafe<
    crucible::safety::DetSafeTier_v::Pure, FamilyAHash>;

static void consume_dim_hash(crucible::DimHashDet) {}

int main() {
  FamilyADetHash persistent{FamilyAHash{0x1234ULL}};
  consume_dim_hash(persistent);
}
