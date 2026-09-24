// The compile-time, unbreakable half of the escape guard: no resource wrapper
// hands out a raw reference or pointer through a member the guard did
// not sanction, and the compiler reads the return type, so no spelling
// evades it.
//
// scripts/check-escape-doors.sh scans source text and is the tree-wide
// net; a text scan is coarse and a member written in a shape its regex
// does not match slips through, which is the dangerous direction.  This
// TU closes that gap for the wrappers where a raw escape is dangerous:
// it walks each one's public members through std::meta and asserts every
// reference- or pointer-returning member is a sanctioned accessor, a
// discouraged escape hatch, a within-object operator, or a mint.  A new
// member added to any wrapper below — a trailing-return `auto -> T&`, a
// macro-hidden type, a declaration split across lines — is seen exactly
// as the first was, because the compiler resolved its return type.
//
// This is a compile-only assertion: the TU builds iff every wrapper's
// escape surface is sanctioned, and main() only proves it linked.

#include <foundation/reflect/RawEscape.h>

#include <fixy/Borrowed.h>
#include <fixy/OwnedFile.h>
#include <fixy/OwnedMmap.h>
#include <fixy/OwnedRegion.h>
#include <fixy/Qtt.h>
#include <fixy/Secret.h>
#include <fixy/SharedRegion.h>
#include <fixy/Witnessed.h>
#include <fixy/os/Fs.h>
#include <foundation/permissions/Permission.h>

namespace pt = ::foundation::permissions::tag;

namespace {

// A concrete brand and tag the branded wrappers instantiate against.
struct probe_brand {};
struct probe_tag {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

// ── The owning wrappers ───────────────────────────────────────────────
// A raw reference or pointer that leaves one of these is a resource
// leaving its owner, so this is exactly where the guard must hold.
CRUCIBLE_NO_RAW_ESCAPE(fixy::OwnedFile);
CRUCIBLE_NO_RAW_ESCAPE(fixy::fs::OwnedFd);
CRUCIBLE_NO_RAW_ESCAPE(fixy::OwnedMmap<int, int, int>);
CRUCIBLE_NO_RAW_ESCAPE(fixy::OwnedRegion<int, pt::HugePageTag>);
CRUCIBLE_NO_RAW_ESCAPE(fixy::Linear<int>);
CRUCIBLE_NO_RAW_ESCAPE(fixy::Affine<int>);
CRUCIBLE_NO_RAW_ESCAPE(fixy::Secret<int>);

// ── The borrows and reads ─────────────────────────────────────────────
// A raw reference out of one of these is the borrow's own view; the
// guard admits the getters and refuses a new unnamed door.
CRUCIBLE_NO_RAW_ESCAPE(fixy::Borrowed<int, probe_tag, probe_brand>);
CRUCIBLE_NO_RAW_ESCAPE(fixy::BorrowedRef<int>);
CRUCIBLE_NO_RAW_ESCAPE(fixy::WeakRef<int>);
CRUCIBLE_NO_RAW_ESCAPE(fixy::SharedRegion<int, probe_tag, probe_brand>);
CRUCIBLE_NO_RAW_ESCAPE(fixy::SharedRead<int, probe_tag, probe_brand>);
CRUCIBLE_NO_RAW_ESCAPE(
    fixy::Witnessed<fixy::Borrowed<int, probe_tag, probe_brand>, fixy::witness::UnderRow<probe_tag>>);

int main() { return 0; }
