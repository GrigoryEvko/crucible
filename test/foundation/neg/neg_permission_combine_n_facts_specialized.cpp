// A combine of many children asks combine_n_facts whether a manifest
// declares the split and whether the children are distinct.  This file
// tries to call every combine declared: it writes an explicit
// specialization of combine_n_facts.  combine_n_facts is a function at
// namespace scope that is not a template, so no specialization matches it.

#include <foundation/permissions/Permission.h>

#include <meta>

template <>
consteval foundation::permissions::detail::combine_n_manifest
foundation::permissions::detail::combine_n_facts(std::meta::info, std::meta::info) {
    return {true, true, true, true, true};
}

int main() { return 0; }
