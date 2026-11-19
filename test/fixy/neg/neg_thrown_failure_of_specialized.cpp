// The failure that a ControlFlow grade throws comes from thrown_failure_of.
// This file tries to hide the throw of each grade: it writes an explicit
// specialization of thrown_failure_of.  thrown_failure_of is a function at
// namespace scope that is not a template, so no specialization matches it.

#include <fixy/Collision.h>

#include <meta>

template <>
consteval fixy::collision::detail::ReturnedFailure fixy::collision::detail::thrown_failure_of(std::meta::info) {
    return fixy::collision::detail::ReturnedFailure{};
}

int main() { return 0; }
