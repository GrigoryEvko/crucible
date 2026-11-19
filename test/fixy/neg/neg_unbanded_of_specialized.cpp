// The failure rules and R004 read the payload after its bands, through
// unbanded_of.  This file tries to make each payload read as int: it writes
// an explicit specialization of unbanded_of.  unbanded_of is a function at
// namespace scope that is not a template, so no specialization matches it.

#include <fixy/Collision.h>

#include <meta>

template <>
consteval std::meta::info fixy::collision::detail::unbanded_of(std::meta::info) {
    return ^^int;
}

int main() { return 0; }
