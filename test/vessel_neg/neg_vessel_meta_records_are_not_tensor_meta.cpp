// Negative-compile test.  This file must fail to compile.
//
// Violation: reading a typed record of a C caller as a TensorMeta.
//
// A C caller writes CrucibleMeta records.  A read of such a record through a
// glvalue of TensorMeta breaks the rule of strict aliasing, and GCC at -O3
// then removes the stores of the caller and reads bytes that the caller did
// not write.  So the typed records hold the C type, and decode_ffi_meta is
// the one door from a record to a TensorMeta.  This fixture gives a typed
// record to a reader of a TensorMeta, and the compiler refuses the binding.
//
// [GCC-WRAPPER-TEXT] — reference binding rejection.

#include "../../vessel/torch/vessel_api_typed.h"

#include <crucible/TensorMeta.h>
#include <fixy/Core.h>

int main() {
    ::fixy::FixedArray<CrucibleMeta, 1> records{};
    const auto typed = crucible::vessel::as_meta_typed(&records.at(::fixy::index<0>()), 1).expect("one record");
    return crucible::raw_grad_fn_hash(*typed.value()) == 0 ? 0 : 1;
}
