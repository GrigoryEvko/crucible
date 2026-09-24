// A capability probe of cmake/PatchedGccProbe.cmake.  Do not build it.
//
// held_value violates its precondition at each call.  The first call is in a
// manifestly constant evaluation, and so is the second, because held_value
// is consteval.  A compiler with the fix in
// toolchain/gcc/patches/0001-*.patch refuses the second call with "call to
// consteval function 'held_value(42)' is not a constant expression".  A
// compiler without the fix keeps the result of an earlier evaluation in its
// constexpr cache and accepts the file.  In that compiler a contract has no
// effect on a constant evaluation that repeats a call.
consteval int held_value(int value) pre(false) { return value; }

static_assert(held_value(42) == 42);

int main() {
    return held_value(42);
}
