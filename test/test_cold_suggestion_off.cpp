// The build takes no -Wsuggest-attribute=cold, and section 3.10 of the root
// CMakeLists.txt tells why.  GCC reads the paths of a function after the
// inlining and the constant propagation of the build.  So the warning
// depends on the target, the level and the profile, and not on the
// source alone.
//
// run_case below reaches a cold call on each path only after GCC inlines
// check_value with a value that is not zero.  At -O0 GCC gives no
// warning for it.  At -O1 and -O3 GCC suggests the cold attribute for
// run_case and for main.  With -Werror and the flag, this file does not
// compile.

#include <cstdio>

namespace cold_suggestion {

// The cold function returns, so -Wsuggest-attribute=noreturn has nothing
// to suggest for a caller.
[[gnu::cold, gnu::noinline]] void note_slow_path(int value);

void note_slow_path(int value) { std::printf("test_cold_suggestion_off: slow path for %d\n", value); }

inline void check_value(int value) {
    if (value != 0) note_slow_path(value);
}

int run_case(int value);

int run_case(int value) {
    check_value(value | 1);
    return value;
}

}  // namespace cold_suggestion

int main() {
    const int result = cold_suggestion::run_case(2);
    std::printf("test_cold_suggestion_off: %s\n", result == 2 ? "passed" : "FAILED");
    return result == 2 ? 0 : 1;
}
