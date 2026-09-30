// Each permission mint asks permission_mint_args_shaped whether its
// arguments are contexts and then consumed tokens.  This file tries to
// admit every argument list: it writes an explicit specialization of
// permission_mint_args_shaped.  That function is at namespace scope and
// is not a template, so no specialization matches it.

#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <initializer_list>
#include <meta>

template <>
consteval bool foundation::permissions::detail::permission_mint_args_shaped(std::size_t, std::size_t,
                                                                            std::initializer_list<std::meta::info>) {
    return true;
}

int main() { return 0; }
