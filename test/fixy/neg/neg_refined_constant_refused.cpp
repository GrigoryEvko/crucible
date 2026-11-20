// A default member initializer is no constant evaluation, so mint_refined
// there refuses a wrong constant only at run time.  The constant mint runs
// the predicate on its template argument in its gate, so a warp size of 33
// does not compile.

#include <fixy/Refined.h>

#include <cstdint>

namespace {

inline constexpr auto valid_warp_size = ::fixy::all_of<::fixy::power_of_two, ::fixy::bounded_above<std::uint16_t{128}>>;

struct Profile {
    ::fixy::Refined<valid_warp_size, std::uint16_t> warp_size =
        ::fixy::mint_refined_constant<valid_warp_size, std::uint16_t{33}>();
};

}  // namespace

int main() {
    Profile const profile{};
    return profile.warp_size.value() == 33 ? 0 : 1;
}
