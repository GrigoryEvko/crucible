// Negative-compile test. This file must fail to compile.
//
// The hash zero is the sentinel of no schema, on which the content hash and
// the region cache key.  An internal name holds its hash, and the consteval
// constructor checks the hash at compile time, so a name on the hash zero
// stops the build.  The table then registers no internal name on the hash
// zero, and it has no check of the hash at run time.
//
// Expected diagnostic: the constructor calls
// crucible::detail::internal_schema_name_has_the_zero_hash, which no constant
// evaluation can call.

#include <crucible/SchemaTable.h>
#include <fixy/Core.h>

int main() {
    constexpr crucible::InternalSchemaName name{crucible::SchemaHash{0}, ::fixy::TextView{"aten::mm"}};
    return static_cast<int>(name.parts().name().size());
}
