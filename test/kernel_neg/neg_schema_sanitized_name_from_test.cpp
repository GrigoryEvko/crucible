// Negative-compile test. This file must fail to compile.
//
// A test that needs a Sanitized name takes it from sanitize_schema_name.
// Only the test door that the name declares as a friend,
// crucible::testing::UncheckedSchemaNameDoor, plants a name that no check
// read, and only the test of the C entries defines it.  This fixture is a
// test outside the friends that builds a Sanitized name from a retag.  On the
// old table the name was the Tagged of the retag, so it compiled.
//
// Expected diagnostic: the constructor of SchemaTable::SanitizedName is
// private.

#include <crucible/SchemaTable.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

int main() {
    const crucible::SchemaTable::SanitizedName planted{
        ::fixy::mint_tagged<::fixy::tags::source::External>(crucible::SchemaNameParts{::fixy::TextView{"aten::mm"}})
            .retag<::fixy::tags::source::Sanitized>()};
    return planted.parts().full_size() == 8 ? 0 : 1;
}
