// Negative-compile test. This file must fail to compile.
//
// A Sanitized name comes only from sanitize_schema_name, the one validator of
// a schema name from outside the process.  A retag of an External name to
// Sanitized records a check that no code ran.  This fixture is product code
// that takes a name of a trace file, retags it, and registers it through
// register_schema_name.  On the old table the retagged name registered, so
// bytes that no check read became a name that each reader trusts.
//
// Expected diagnostic: no register_schema_name takes the retagged name,
// because it does not satisfy SchemaNameSource.

#include <crucible/SchemaTable.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

namespace crucible {

[[maybe_unused]] static SchemaRegistration
register_trace_name(VigilFgCtx const& fg, ::foundation::effects::Alloc allocation, SchemaHash hash,
                    ::fixy::Tagged<::fixy::TextView, ::fixy::tags::source::External> const& name) {
    auto const laundered =
        ::fixy::Tagged<::fixy::TextView, ::fixy::tags::source::External>{name}.retag<::fixy::tags::source::Sanitized>();
    return register_schema_name(fg, allocation, hash, laundered);
}

}  // namespace crucible

int main() { return 0; }
