// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: the checked mint of ValidDeviceType receives int8_t{99} in
// constant evaluation.  99 is far above the highest DeviceType enumerator,
// PrivateUse1 = 20.
//
// ValidDeviceType is ::fixy::Refined<valid_device_type, int8_t>.  read_meta
// in Serialize.h reads the device byte through this gate.  The device type
// feeds the node content hash, so an unchecked byte would change the
// identity of a node without any other symptom.
//
// The companion fixture neg_tensor_meta_device_type_gap_value.cpp is an
// interior gap (3).  This one catches a predicate that is dropped entirely.
//
// mint_refined checks its predicate with CRUCIBLE_PRE.  In constant
// evaluation a false predicate reaches __builtin_trap, which is not a
// constant expression, so the constexpr variable is ill-formed.

#include <crucible/TensorMeta.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidDeviceType bad =
        ::fixy::mint_refined<crucible::valid_device_type>(static_cast<std::int8_t>(99));
    (void)bad;
    return 0;
}
