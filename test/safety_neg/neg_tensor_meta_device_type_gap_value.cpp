// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidDeviceType with int8_t{3} in constexpr
// context.  3 is an INTERIOR GAP in DeviceType's sparse enumerator set
// (MKLDNN = 2, HIP = 6 — values 3..5 are not enumerators).
//
// Per the read_meta device_type gate (sibling of #534 ndim / #892
// kernel_id / the dtype gate), ValidDeviceType is
// safety::Refined<valid_device_type, int8_t>.  read_meta() in Serialize.h
// previously reconstructed `m.device_type = r.r<DeviceType>()` unguarded;
// device_type feeds the node content hash (Merkle identity for
// KernelCache dedup / diff / merge), so a corrupt/version-skewed Cipher
// byte silently corrupted that identity.  The gate fail-closes instead.
//
// Companion fixture: neg_tensor_meta_device_type_above_max.cpp
//   * This one is the INTERIOR GAP (3).  Catches drift to a plain range
//     check `[0, 20]` that would wrongly admit the 3..5 / 7..8 / 10..12 /
//     15..19 gaps.
//   * That one is the ABOVE-MAX wide miss (99 > 20).  Catches "drop the
//     predicate entirely" regression.
//
// In constant evaluation a contract violation makes the expression
// non-constant per P1494R5 — using it where a constant is required is
// ill-formed.  Per HS14, >=2 fixtures per soundness gate, distinct class.

#include <crucible/TensorMeta.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidDeviceType bad{static_cast<std::int8_t>(3)};
    (void)bad;
    return 0;
}
