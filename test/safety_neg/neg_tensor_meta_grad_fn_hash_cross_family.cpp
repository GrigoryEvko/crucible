// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// TensorMeta::grad_fn_hash is GradFnHash, ::fixy::Tagged<uint64_t,
// hash_family::FamilyB>.  A Family-A hash is stable across processes, and a
// Family-B hash is local to one process.  The two families must not stand
// in for each other.
//
// Distinct mismatch class from neg_tensor_meta_grad_fn_hash_raw_uint64.cpp:
//   * Companion: a raw uint64_t is refused at the field write.
//   * This fixture: Tagged<uint64_t, FamilyA> is not GradFnHash.

#include <crucible/TensorMeta.h>

#include <cstdint>

int main() {
    auto persistent = ::fixy::mint_tagged<crucible::hash_family::FamilyA>(std::uint64_t{0x1234});

    crucible::TensorMeta meta{};

    // MUST fail: a Family-A hash cannot occupy a Family-B slot.
    meta.grad_fn_hash = persistent;
    return 0;
}
