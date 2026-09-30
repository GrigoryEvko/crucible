// fixy/Collision.h reads the class of a Security grade through
// security_class_of before it lets a binding through.  This file tries to
// make each grade read as public: it writes an explicit specialization of
// security_class_of.  The reading is a function at namespace scope that is
// not a template, so no specialization matches it.

#include <fixy/Atom.h>

#include <meta>

template <>
consteval fixy::atom::SecurityClass fixy::atom::security_class_of(std::meta::info) {
    return fixy::atom::SecurityClass::Public;
}

int main() { return 0; }
