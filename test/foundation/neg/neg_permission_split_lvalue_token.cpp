// Every mint consumes the tokens it is given, so a permission passed as
// an lvalue is not a split argument: the argument-shape concept on the
// one split template is not satisfied, and the note shows the shape
// check answering false for the lvalue.

#include <foundation/permissions/Permission.h>

#include <type_traits>

namespace {
struct Whole {};
struct Left {};
struct Right {};
}  // namespace

namespace foundation::permissions {
template <>
struct splits_into<Whole, Left, Right> : std::true_type {};
template <>
struct splits_into_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    auto whole = ::foundation::permissions::mint_permission_root<Whole>();
    [[maybe_unused]] auto halves = ::foundation::permissions::mint_permission_split<Left, Right>(whole);
    return 0;
}
