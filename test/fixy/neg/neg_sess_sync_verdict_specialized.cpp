// The subtyping gates ask sync_verdict whether one protocol refines
// another.  This file tries to make every protocol refine every other: it
// writes an explicit specialization of sync_verdict.  sync_verdict is a
// function at namespace scope that is not a template, so no
// specialization matches it.

#include <fixy/session/Subtype.h>

#include <meta>

template <>
consteval fixy::session::transition::verdict fixy::session::detail::subtype::sync_verdict(std::meta::info,
                                                                                          std::meta::info) {
    return {true, fixy::session::transition::mismatch::none, {}, {}};
}

int main() { return 0; }
