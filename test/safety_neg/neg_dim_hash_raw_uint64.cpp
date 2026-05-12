// WRAP-DimHash-1 (#910): dim_hash_*_det returns DimHashDet =
// DetSafe<Pure, Tagged<uint64_t, hash_family::FamilyB>>.
//
// This fixture provokes the raw-value fence: a bare uint64_t must not
// enter APIs that require a deterministic, Family-B dim hash.

#include <crucible/DimHash.h>

#include <cstdint>

static void consume_dim_hash(crucible::DimHashDet) {}

int main() {
  const uint64_t raw = 0x1234ULL;
  consume_dim_hash(raw);
}
