// The fallible door is gated on PredicateInvocableOn<Pred, T>, as the
// checked mint is.  non_null takes a pointer, so an int cannot be fed to
// it, and the gate refuses the pair at the call site.

#include <fixy/Refined.h>

#include <cstdint>

enum class Refusal : std::uint8_t {
    Null = 1,
};

int main() {
    auto refused = fixy::admit_refined<fixy::non_null>(42, Refusal::Null);
    return refused.has_value() ? 0 : 1;
}
