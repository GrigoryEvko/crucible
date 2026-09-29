// dim_hash_*_det returns DimHashDet =
// DetSafe<Pure, Tagged<uint64_t, hash_family::FamilyB>>.
//
// This fixture provokes the provenance fence: a persistent Family-A
// hash must not substitute for a process-local Family-B dim hash even
// when both carry the same DetSafe<Pure> tier.

#include <crucible/DimHash.h>

#include <cstdint>

using FamilyAHash = ::fixy::Tagged<uint64_t, crucible::hash_family::FamilyA>;
using FamilyADetHash = ::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, FamilyAHash>;

static void consume_dim_hash(crucible::DimHashDet) {}

int main() {
    FamilyADetHash persistent{::fixy::mint_tagged<crucible::hash_family::FamilyA>(uint64_t{0x1234}), {}};
    consume_dim_hash(persistent);
}
