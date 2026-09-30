// A rule asks the class of a Security grade before it lets a binding
// through, and a public grade is the answer that lets it through.  Every
// answer comes from one relation, security_class_answer_of_.  This file
// tries to give each type the public class: it writes an explicit
// specialization of that relation.  The relation is a function at
// namespace scope that is not a template, so no specialization matches it.

#include <fixy/Atom.h>

#include <meta>

template <>
consteval fixy::atom::detail::security_class_answer fixy::atom::detail::security_class_answer_of_(std::meta::info) {
    return {true, fixy::atom::SecurityClass::Public};
}

int main() { return 0; }
