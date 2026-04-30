// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F09-AUDIT-2 fixture — pins IsCacheableFunction concept
// rejection on data-pointer NTTPs.  This is the SUBTLE silent-
// failure case: `auto FnPtr = &some_global` looks superficially
// like a pointer NTTP and would be accepted by a naive
// `auto FnPtr` parameter, producing a cache slot keyed off a
// data address that has no callable identity.
//
// The concept requires `is_function_v<remove_pointer_t<decltype(FnPtr)>>`
// — `int*` strips to `int`, which is NOT a function type.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsCacheableFunction<&data_global>.

#include <crucible/cipher/ComputationCache.h>

inline int data_global = 42;  // Plain data, not a function.

int main() {
    // &data_global is `int*` — IsCacheableFunction must reject:
    // is_function_v<int> is false.
    crucible::cipher::insert_computation_cache<&data_global>(
        reinterpret_cast<crucible::cipher::CompiledBody*>(
            static_cast<std::uintptr_t>(0x1)));
    return 0;
}
