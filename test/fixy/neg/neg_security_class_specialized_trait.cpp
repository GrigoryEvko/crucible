// A rule asks the class of a Security grade before it lets a binding
// through, and a public grade is the answer that lets it through.  This
// file tries to give a class of its own the public class.  It specializes
// the class template that the relation once was.  The relation is one
// function over reflections, so the specialization has nothing to name.

#include <fixy/Atom.h>

#include <type_traits>

namespace {

struct Fake final : fixy::atom::atom_of<fixy::Axis::Security> {};

using Public = std::integral_constant<fixy::atom::SecurityClass, fixy::atom::SecurityClass::Public>;

}  // namespace

template <>
struct fixy::atom::detail::security_class_of_<Fake> : Public {};

int main() { return 0; }
