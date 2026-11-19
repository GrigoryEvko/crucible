// The content-addressed axiom strips every layer of the marker and
// compares what is left.  This file tries to strip a payload that has no
// marker, so that Forged and ContentAddressed<int> stand for one content:
// it specializes unwrap_content_addressed as a class template.  The strip
// is a function at namespace scope that is not a template, so the
// specialization does not compile, and the two payloads stay apart.

#include <fixy/session/Subtype.h>
#include <foundation/Quarantine.h>

struct Forged {};

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a trait of the base, and the compiler refuses it")
template <>
struct fixy::session::unwrap_content_addressed<Forged> {
    using type = int;
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

static_assert(fixy::session::is_payload_subsort_v<Forged, fixy::session::ContentAddressed<int>>);

int main() { return 0; }
