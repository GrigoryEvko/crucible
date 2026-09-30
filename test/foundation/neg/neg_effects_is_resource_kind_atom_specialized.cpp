// IsResourceKind asks is_resource_kind_atom whether a value of the
// ResourceKind type is one of the enumerators.  This file tries to admit
// a value that no enumerator holds: it writes an explicit specialization
// of is_resource_kind_atom.  is_resource_kind_atom is a function at
// namespace scope that is not a template, so no specialization matches
// it.

#include <foundation/effects/Resources.h>

template <>
consteval bool foundation::effects::is_resource_kind_atom(foundation::effects::ResourceKind) {
    return true;
}

int main() { return 0; }
