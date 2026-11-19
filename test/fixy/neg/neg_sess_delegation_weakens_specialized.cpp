// The payload order asks delegation_weakens whether a delegated endpoint
// weakens to another.  This file tries to send every delegated endpoint
// where any other is expected: it writes an explicit specialization of
// delegation_weakens.  delegation_weakens is a function at namespace scope
// that is not a template, so no specialization matches it.

#include <fixy/session/Delegate.h>

#include <meta>

template <>
consteval bool fixy::session::detail::delegation::delegation_weakens(std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
