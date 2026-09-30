// Every mint of a session asks empty_choice_of whether a protocol holds a
// choice with no branch.  This file tries to hide each empty choice: it
// writes an explicit specialization of empty_choice_of.  empty_choice_of
// is a function at namespace scope that is not a template, so no
// specialization matches it.

#include <fixy/session/Protocol.h>

#include <meta>

template <>
consteval bool fixy::session::detail::empty_choice_of(std::meta::info) {
    return false;
}

int main() { return 0; }
