// The asynchronous channel mints ask bounded whether one side refines the
// dual of the other at the capacity of the channel.  This file tries to
// admit every pair: it writes an explicit specialization of bounded.
// bounded is a function at namespace scope that is not a template, so no
// specialization matches it.

#include <fixy/session/Subtype.h>

#include <cstddef>
#include <meta>

template <>
consteval bool fixy::session::detail::async::bounded(std::meta::info, std::meta::info, std::size_t, bool) {
    return true;
}

int main() { return 0; }
