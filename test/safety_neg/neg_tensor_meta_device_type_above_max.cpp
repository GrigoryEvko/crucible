// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: constructing ValidDeviceType with int8_t{99} in constexpr
// context.  99 is far above DeviceType's highest enumerator
// (PrivateUse1 = 20), an out-of-range byte.
//
// Per the read_meta device_type gate (sibling of #534 ndim / #892
// kernel_id / the dtype gate), ValidDeviceType is
// safety::Refined<valid_device_type, int8_t>.  read_meta() in Serialize.h
// previously reconstructed `m.device_type = r.r<DeviceType>()` unguarded;
// device_type feeds the node content hash (Merkle identity), so a
// corrupt/version-skewed Cipher byte silently corrupted it.  The gate
// fail-closes instead.
//
// Companion fixture: neg_tensor_meta_device_type_gap_value.cpp
//   * This one is the ABOVE-MAX wide miss (99 > 20).  Catches "drop the
//     predicate entirely" regression where ValidDeviceType degrades into
//     a transparent wrapper.
//   * That one is the INTERIOR GAP (3).  Catches drift to a plain range
//     check that wrongly admits the sparse gaps.
//
// In constant evaluation a contract violation makes the expression
// non-constant per P1494R5 — using it where a constant is required is
// ill-formed.  Per HS14, >=2 fixtures per soundness gate, distinct class.

#include <crucible/TensorMeta.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidDeviceType bad{static_cast<std::int8_t>(99)};
    (void)bad;
    return 0;
}
