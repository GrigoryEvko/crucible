// The constant mint asks first that the predicate takes the type of the
// constant.  A predicate of a length does not take an integer, so the gate
// refuses the call before it runs the predicate.

#include <fixy/Refined.h>

#include <cstdint>

int main() {
    auto const count = ::fixy::mint_refined_constant<::fixy::non_empty, std::uint32_t{4}>();
    return count.value() == 4 ? 0 : 1;
}
