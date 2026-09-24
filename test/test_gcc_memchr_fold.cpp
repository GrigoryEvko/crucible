// A pin on a defect of GCC 16.2.1: the constant evaluator gives a wrong
// answer for __builtin_memchr and __builtin_strchr on a pointer that is not
// at the start of a string literal.
//
// The evaluator adds the position of the match, measured from the start of
// the literal, to the pointer that the caller gave.  "0,1,2,3" + 2 has its
// first ',' at offset 1, and the evaluator gives 3.  The code is conforming,
// and at run time the answer is 1.  C++26 makes the fold reachable: the cast
// from const void* back to const char* is a constant expression only since
// P2738.
//
// The tree reaches the defect through std::string_view::find and
// std::char_traits<char>::find.  Outside a manifest constant evaluation,
// is_constant_evaluated() is false, and libstdc++ then calls
// __builtin_memchr.  At -O1 and above the front end tries to fold a call
// with constant arguments, so a constexpr function, or with
// -fimplicit-constexpr an inline one, that searches a literal with find
// gets a wrong constant.  A parser in the tree must search characters with
// a loop, not with find, strchr or memchr.
//
// This test passes while GCC gives the wrong answer.  It fails when a GCC
// update changes the answer, and the message then says what to re-check.

#include <cstddef>
#include <cstdio>

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

// Manifest constant evaluations from a pointer two bytes into the literal.
constexpr std::size_t kMemchrConstant = memchr_offset("0,1,2,3" + 2, 5);
constexpr long kStrchrConstant = strchr_offset("0,1,2,3" + 2);

// The offset that GCC 16.2.1 gives, and the offset that is correct.
constexpr std::size_t kKnownWrong = 3;
constexpr std::size_t kCorrect = 1;

// Compares one constant with the run-time answer and says which state the
// compiler is in.  Returns true while the known defect is still present.
bool defect_still_present(const char* builtin, long constant, long runtime) {
    if (runtime != static_cast<long>(kCorrect)) {
        std::fprintf(stderr, "%s: the run-time answer is %ld, not %zu, and the test itself is wrong\n",
                     builtin, runtime, kCorrect);
        return false;
    }
    if (constant == static_cast<long>(kKnownWrong)) return true;
    if (constant == static_cast<long>(kCorrect)) {
        std::fprintf(stderr,
                     "%s: GCC now evaluates the offset pointer correctly (constant %ld).  "
                     "Re-check the rule that a parser searches with a character loop, "
                     "then delete this pin.\n",
                     builtin, constant);
        return false;
    }
    std::fprintf(stderr, "%s: GCC gives a new wrong constant %ld (was %zu, correct %zu)\n", builtin,
                 constant, kKnownWrong, kCorrect);
    return false;
}

}  // namespace

int main() {
    // A volatile pointer keeps the run-time calls out of every fold.
    const char* volatile runtime_text = "0,1,2,3";
    const long memchr_runtime = static_cast<long>(memchr_offset(runtime_text + 2, 5));
    const long strchr_runtime = strchr_offset(runtime_text + 2);

    const bool memchr_pinned =
        defect_still_present("__builtin_memchr", static_cast<long>(kMemchrConstant), memchr_runtime);
    const bool strchr_pinned = defect_still_present("__builtin_strchr", kStrchrConstant, strchr_runtime);
    if (!memchr_pinned || !strchr_pinned) return 1;
    std::printf("test_gcc_memchr_fold: the known defect is present in both builtins\n");
    return 0;
}
