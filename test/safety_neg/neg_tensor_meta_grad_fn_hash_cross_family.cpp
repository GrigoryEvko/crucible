// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for #1040 WRAP-TensorMeta-7
// (TensorMeta::grad_fn_hash -> Tagged<uint64_t, hash_family::FamilyB>).
//
// Premise: Family-A hashes are persistent / cross-process stable,
// while TensorMeta::grad_fn_hash is process-local Family-B.  The two
// hash families must not silently substitute for each other.
//
// Distinct mismatch class from neg_tensor_meta_grad_fn_hash_raw_uint64.cpp:
//   * Companion: raw uint64_t rejected at the field write surface.
//   * This fixture: Tagged<uint64_t, FamilyA> is not GradFnHash.

#include <crucible/ir001/TensorMeta.h>
#include <crucible/safety/Tagged.h>

int main() {
  using FamilyAHash = crucible::safety::Tagged<
      uint64_t, crucible::hash_family::FamilyA>;

  crucible::TensorMeta meta{};
  FamilyAHash persistent{0x1234ULL};

  // MUST fail: Family-A persistent hash cannot occupy a Family-B slot.
  meta.grad_fn_hash = persistent;
  return 0;
}
