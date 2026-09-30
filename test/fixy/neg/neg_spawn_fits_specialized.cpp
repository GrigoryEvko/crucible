// mint_spawn asks spawn_fits whether the fork admits the context and the
// children, and whether each body takes its own child.  This file tries to
// spawn any body: it writes an explicit specialization of spawn_fits.
// spawn_fits is a function at namespace scope that is not a template, so
// no specialization matches it.

#include <fixy/os/Spawn.h>

#include <meta>

template <>
consteval bool fixy::spawn::detail::spawn_fits(std::meta::info, std::meta::info, std::meta::info, std::meta::info,
                                               std::meta::info) {
    return true;
}

int main() { return 0; }
