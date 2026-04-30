// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F09-AUDIT-3 fixture — sister to the member-function-pointer
// rejection.  Without this fence, `&Receiver::data_field` (a
// member-data-pointer with type `int Receiver::*`) would compile
// against `auto FnPtr` and produce a nonsense cache slot keyed
// off a non-callable offset.
//
// Closes the rejection-space symmetry:
//   * neg_computation_cache_integral_nttp     — `int` NTTPs.
//   * neg_computation_cache_data_ptr_nttp     — `int*` NTTPs.
//   * neg_computation_cache_member_fn_nttp    — `R(C::*)(...)` NTTPs.
//   * neg_computation_cache_member_data_ptr_nttp — `T C::*` NTTPs.  ← THIS
//
// `int Receiver::*` is NOT `is_pointer_v` (that trait only matches
// plain pointers; member-pointers are a distinct category captured
// by `is_member_pointer_v` / `is_member_object_pointer_v`).  The
// concept's first conjunct rejects.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsCacheableFunction<&Receiver::data_field>.

#include <crucible/cipher/ComputationCache.h>

struct Receiver {
    int data_field = 0;
};

int main() {
    // &Receiver::data_field has type `int Receiver::*` —
    // member-object pointer, NOT a function pointer.
    // IsCacheableFunction must reject.
    (void)crucible::cipher::lookup_computation_cache<
        &Receiver::data_field>();
    return 0;
}
