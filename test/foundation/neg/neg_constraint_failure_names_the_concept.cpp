// Harness probe: a concept that a type does not satisfy is rejected, and
// the compiler names the concept in its own prose.  The registered
// regexes are the boilerplate reason ("constraints not satisfied") and
// the concept's name as the compiler spells it in the satisfaction
// note; neither can be met by this file's echoed source, because
// test/neg_compile_driver.py strips the caret display before matching.
//
// A contract_assert violated during constant evaluation was the first
// candidate for this probe.  GCC 16.2 reports it as one line with no
// context ("contract predicate is false in constant expression"), so
// that shape cannot carry a second, project-specific regex.

#include <foundation/Platform.h>

#include <concepts>

namespace {

template <class T>
concept HalvesEvenly = requires(T const value) {
    { value.half() } -> std::same_as<int>;
};

struct OddCarrier {
    int value = 3;
};

template <HalvesEvenly T>
constexpr int use_half(T const&) {
    return 0;
}

}  // namespace

int main() { return use_half(OddCarrier{}); }
