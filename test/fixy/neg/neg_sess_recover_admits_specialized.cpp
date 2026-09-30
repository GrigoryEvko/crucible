// HandleFactory::recover asks recover_admits whether a handle can enter a
// branch of an Offer that no peer picks.  This file tries to admit every
// branch: it writes an explicit specialization of recover_admits.
// recover_admits is a function at namespace scope that is not a template,
// so no specialization matches it.

#include <fixy/session/Handle.h>

#include <cstddef>
#include <meta>

template <>
consteval bool fixy::session::detail::recover_admits(std::meta::info, std::meta::info, std::size_t, std::meta::info,
                                                     std::meta::info) {
    return true;
}

int main() { return 0; }
