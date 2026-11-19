// The failure that a payload returns comes from returned_failure_of.  This
// file tries to hide the failure of each payload: it writes an explicit
// specialization of returned_failure_of.  returned_failure_of is a function
// at namespace scope that is not a template, so no specialization matches
// it.

#include <fixy/Collision.h>

#include <meta>

template <>
consteval fixy::collision::detail::ReturnedFailure fixy::collision::detail::returned_failure_of(std::meta::info) {
    return fixy::collision::detail::ReturnedFailure{};
}

int main() { return 0; }
