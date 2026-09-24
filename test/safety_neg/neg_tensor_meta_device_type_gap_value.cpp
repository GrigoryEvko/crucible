// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: the checked mint of ValidDeviceType receives int8_t{3} in
// constant evaluation.  3 is an interior gap of the sparse DeviceType set:
// MKLDNN is 2 and HIP is 6, and 3 thru 5 are not enumerators.
//
// ValidDeviceType is ::fixy::Refined<valid_device_type, int8_t>.  read_meta
// in Serialize.h reads the device byte through this gate, because the
// device type feeds the node content hash.
//
// The companion fixture neg_tensor_meta_device_type_above_max.cpp is the
// wide miss (99).  This one catches a predicate relaxed to a plain range
// check, which admits the gaps.
//
// mint_refined checks its predicate with CRUCIBLE_PRE.  In constant
// evaluation a false predicate reaches __builtin_trap, which is not a
// constant expression, so the constexpr variable is ill-formed.

#include <crucible/TensorMeta.h>

#include <cstdint>

int main() {
    constexpr crucible::ValidDeviceType bad =
        ::fixy::mint_refined<crucible::valid_device_type>(static_cast<std::int8_t>(3));
    (void)bad;
    return 0;
}
