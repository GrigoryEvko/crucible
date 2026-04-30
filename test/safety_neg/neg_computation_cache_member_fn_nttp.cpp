// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F09-AUDIT-2 fixture — pins IsCacheableFunction concept
// rejection on member-function-pointer NTTPs.  Member functions
// require an implicit `this` argument the cache cannot represent
// in its key — admitting them would silently produce slots that
// alias across distinct receiver objects.
//
// The concept requires `is_pointer_v<decltype(FnPtr)>` AND
// `is_function_v<remove_pointer_t<decltype(FnPtr)>>`.  Member-
// function pointers are NOT plain pointers (`is_pointer_v` is
// false on `R(C::*)(Args...)`); they're a distinct type
// category, `is_member_function_pointer_v`.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsCacheableFunction<&Receiver::member_fn>.

#include <crucible/cipher/ComputationCache.h>

struct Receiver {
    void member_fn(int) noexcept {}
};

int main() {
    // &Receiver::member_fn has type `void (Receiver::*)(int) noexcept`
    // — neither a function pointer nor a function reference.
    // IsCacheableFunction must reject.
    (void)crucible::cipher::lookup_computation_cache<
        &Receiver::member_fn, int>();
    return 0;
}
