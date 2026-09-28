// The Category enum is closed to the tags of the Catalog.  A tag that
// derives from tag_base and is not in the Catalog has no Category, so
// category_of_v reaches the static_assert of the closed enum.
#include <foundation/diag/Catalog.h>

#include <string_view>

namespace {
struct user_local_tag : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "UserLocalTag";
    static constexpr std::string_view description = "user-defined";
    static constexpr std::string_view remediation = "see project docs";
};
}  // namespace

constexpr auto bogus_category = ::foundation::diag::category_of_v<user_local_tag>;

int main() { return static_cast<int>(bogus_category); }
