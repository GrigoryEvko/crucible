// fixy/Collision.h reads the class of a Security grade before it lets a
// binding through.  This file tries to make a declassified grade read as
// public.  It specializes the variable template that the class once was.
// The class comes from one function over reflections, so the
// specialization has nothing to name.

#include <fixy/Atom.h>
#include <fixy/Tags.h>

template <>
inline constexpr fixy::atom::SecurityClass
    fixy::atom::security_class_of_v<fixy::atom::declassify<fixy::tags::secret_policy::WireSerialize>> =
        fixy::atom::SecurityClass::Public;

int main() { return 0; }
