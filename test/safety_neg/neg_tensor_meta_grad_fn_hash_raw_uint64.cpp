// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for #1040 WRAP-TensorMeta-7
// (TensorMeta::grad_fn_hash -> Tagged<uint64_t, hash_family::FamilyB>).
//
// Premise: grad_fn_hash is a process-local PyTorch autograd identity
// hash.  A raw uint64_t must not be assignable without explicitly
// entering the Family-B lane via grad_fn_hash(raw).
//
// Distinct mismatch class from neg_tensor_meta_grad_fn_hash_cross_family.cpp:
//   * This fixture: raw uint64_t rejected at the field write surface.
//   * Companion: wrong hash-family tag rejected at the same surface.

#include <crucible/ir001/TensorMeta.h>

int main() {
  crucible::TensorMeta meta{};

  // MUST fail: grad_fn_hash is GradFnHash, not a raw uint64_t.
  meta.grad_fn_hash = 0x1234ULL;
  return 0;
}
