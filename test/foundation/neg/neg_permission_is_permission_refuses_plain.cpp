// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// IsPermission holds for an exclusive token and for nothing else.  A
// template that asks for a token by this concept refuses a plain value,
// so a call site cannot hand it a count where a region was meant.
//
// Expected diagnostic: the IsPermission constraint is not satisfied.

#include <foundation/permissions/Permission.h>

namespace {
template <typename T>
    requires ::foundation::permissions::IsPermission<T>
constexpr int takes_a_token(T&&) noexcept {
    return 0;
}
}  // namespace

int main() { return takes_a_token(0); }
