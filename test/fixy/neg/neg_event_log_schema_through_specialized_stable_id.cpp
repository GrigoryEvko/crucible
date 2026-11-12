// A recorded session event carries the schema hash of its payload, and
// the schema hash is the stable id of the payload type.  A unit that could
// specialize the stable id would record one payload under the schema of
// another, and a replay would read the event as the other payload.  The
// stable id is a function that is not a template, so the specialization
// does not compile.

#include <fixy/session/EventLog.h>

#include <cstdint>

namespace forged_payload {
struct Payload {};
}  // namespace forged_payload

template <>
inline constexpr std::uint64_t foundation::reflect::stable_type_id<forged_payload::Payload> = 1;

[[maybe_unused]] inline constexpr fixy::session::SchemaHash read_hash =
    fixy::session::default_schema_hash<forged_payload::Payload>;

int main() { return 0; }
