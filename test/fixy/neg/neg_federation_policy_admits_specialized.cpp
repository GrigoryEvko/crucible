// The admission mint asks policy_admits whether the policy of a deployment
// names an organization.  This file tries to admit every organization
// under every policy: it writes an explicit specialization of
// policy_admits.  policy_admits is a function at namespace scope that is
// not a template, so no specialization matches it.

#include <fixy/Federation.h>

#include <meta>

template <>
consteval bool fixy::federation::policy_admits(std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
