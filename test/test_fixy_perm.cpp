// Sentinel TU: compiles the alias header under the project warning flags so its
// static_asserts run.

#include <crucible/fixy/Perm.h>

#include <type_traits>
#include <utility>

namespace test_fixy_perm {
struct Whole {};
struct Left {};
struct Right {};
}  // namespace test_fixy_perm

namespace crucible::safety {

template <>
struct splits_into<test_fixy_perm::Whole, test_fixy_perm::Left, test_fixy_perm::Right> : std::true_type {};

template <>
struct splits_into_authoring_witness<test_fixy_perm::Whole, test_fixy_perm::Left, test_fixy_perm::Right>
    : std::true_type {};

}  // namespace crucible::safety

namespace safe = ::crucible::safety;
namespace fperm = ::crucible::fixy::perm;
namespace tags = test_fixy_perm;

static_assert(std::is_same_v<fperm::Permission<tags::Whole>, safe::Permission<tags::Whole>>,
              "fixy::perm::Permission<T> must alias safety::Permission<T>.");

static_assert(noexcept(fperm::mint_permission_root<tags::Whole>()),
              "fixy::perm::mint_permission_root<T>() must be noexcept.");

int main() {
    {
        auto whole = fperm::mint_permission_root<tags::Whole>();
        auto [l, r] = fperm::mint_permission_split<tags::Left, tags::Right>(std::move(whole));
        auto recombined = fperm::mint_permission_combine<tags::Whole>(std::move(l), std::move(r));
        safe::permission_drop(std::move(recombined));
    }
    return 0;
}
