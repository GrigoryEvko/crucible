// Harness probe through the fixy target: the same concept rejection as
// test/foundation/neg/neg_constraint_failure_names_the_concept.cpp,
// compiled with fixy's include path and linked against fixy.

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
