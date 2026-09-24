// A regression test for a defect of the GCC constant evaluator, which the
// patch series in toolchain/gcc/patches fixes.
//
// For __builtin_memchr, strchr, strrchr and strstr, an unpatched GCC 10.1
// through 16.2 rebases the folded result on the original first argument
// also when it did not replace that argument with its string literal.  The
// offset of an argument such as "0,1,2,3" + 2 then counts two times:
// memchr gives offset 3 where the correct offset is 1.  In C++26 the cast
// from const void* is a constant expression (P2738), so
// std::char_traits<char>::find, std::string_view::find, std::find and
// std::ranges::find reach __builtin_memchr when the front end folds a call
// with constant arguments at -O1 and above.  Code at run time then gets the
// wrong offset.
//
// The static_asserts check the manifest constant evaluation, so an
// unpatched compiler cannot compile this file.  main() checks the same
// searches through the library routes, against run-time answers that no
// fold can reach.

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string_view>

namespace {

// The offset of the first ',' in [text, text + count), or count.
constexpr std::size_t memchr_offset(const char* text, std::size_t count) noexcept {
    const void* hit = __builtin_memchr(text, ',', count);
    return hit ? static_cast<std::size_t>(static_cast<const char*>(hit) - text) : count;
}

// The offset of the first ',' in the NUL-terminated text, or -1.
constexpr long strchr_offset(const char* text) noexcept {
    const char* hit = __builtin_strchr(text, ',');
    return hit ? hit - text : -1;
}

// The offset of the last ',' in the NUL-terminated text, or -1.
constexpr long strrchr_offset(const char* text) noexcept {
    const char* hit = __builtin_strrchr(text, ',');
    return hit ? hit - text : -1;
}

// The offset of the first ",2" in the NUL-terminated text, or -1.
constexpr long strstr_offset(const char* text) noexcept {
    const char* hit = __builtin_strstr(text, ",2");
    return hit ? hit - text : -1;
}

// The same searches through the library routes that call __builtin_memchr.
constexpr std::size_t string_view_find(std::string_view text, std::size_t start) noexcept {
    return text.find(',', start);
}

constexpr std::size_t std_find_offset(const char* text, std::size_t count) noexcept {
    return static_cast<std::size_t>(std::find(text, text + count, ',') - text);
}

constexpr std::size_t ranges_find_offset(const char* text, std::size_t count) noexcept {
    return static_cast<std::size_t>(std::ranges::find(text, text + count, ',') - text);
}

// "0,1,2,3" + 2 is "1,2,3": the first ',' is at 1, the last at 3, ",2" at 1.
static_assert(memchr_offset("0,1,2,3" + 2, 5) == 1);
static_assert(strchr_offset("0,1,2,3" + 2) == 1);
static_assert(strrchr_offset("0,1,2,3" + 2) == 3);
static_assert(strstr_offset("0,1,2,3" + 2) == 1);
static_assert(string_view_find("0,1,2,3", 2) == 3);
static_assert(std_find_offset("0,1,2,3" + 2, 5) == 1);
static_assert(ranges_find_offset("0,1,2,3" + 2, 5) == 1);

// Compares one answer with the run-time answer.  Returns true when they agree.
bool agrees(const char* route, std::size_t folded, std::size_t runtime) {
    if (folded == runtime) return true;
    std::fprintf(stderr, "%s: the folded answer is %zu, and the run-time answer is %zu\n", route, folded,
                 runtime);
    return false;
}

}  // namespace

int main() {
    // A volatile pointer keeps the run-time calls out of every fold.  The
    // calls with literal arguments are the ones that the front end folds at
    // -O1 and above.
    const char* volatile runtime_text = "0,1,2,3";
    const char* const text = runtime_text;

    bool all_agree = true;
    all_agree &= agrees("__builtin_memchr", memchr_offset("0,1,2,3" + 2, 5), memchr_offset(text + 2, 5));
    all_agree &= agrees("std::string_view::find", string_view_find("0,1,2,3", 2),
                        string_view_find(std::string_view{text, 7}, 2));
    all_agree &= agrees("std::find", std_find_offset("0,1,2,3" + 2, 5), std_find_offset(text + 2, 5));
    all_agree &= agrees("std::ranges::find", ranges_find_offset("0,1,2,3" + 2, 5),
                        ranges_find_offset(text + 2, 5));
    if (!all_agree) return 1;
    std::printf("test_gcc_memchr_fold: every route gives the correct offset\n");
    return 0;
}
