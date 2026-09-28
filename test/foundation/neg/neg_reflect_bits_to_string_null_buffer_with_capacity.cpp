// bits_to_string admits a null buffer only with a capacity of 0.  A null
// buffer with a capacity of 100 breaks the precondition valid_span, and
// the constant evaluation stops at the contract.
#include <foundation/reflect/Enumerate.h>

#include <cstddef>

namespace {
enum class Signal : unsigned char {
    Alpha = 0x01,
};

constexpr std::size_t rendered =
    ::foundation::reflect::bits_to_string<Signal>(static_cast<unsigned char>(Signal::Alpha), nullptr, 100);
}  // namespace

int main() { return static_cast<int>(rendered); }
