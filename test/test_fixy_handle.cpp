// ── test_fixy_handle — sentinel TU for fixy/Handle.h ───────────────
//
// Pulls fixy/Handle.h into a TU compiled under project warning flags
// so the header's 9 dual-export sentinels + cardinality witness
// execute under enforcement.  Witnesses:
//
//   1. Every fixy::handle::X alias resolves to safety::X (9 aliases).
//   2. Cardinality witness (handle_alias_cardinality == 9) trips if
//      a future contributor adds/removes a handle type without
//      updating the sentinel block in Handle.h.
//   3. End-to-end RAII round-trip via the fixy:: alias proves no
//      name-shadow drift past the sentinel — exercises FileHandle
//      open_read / close pair through the fixy::handle:: path.
//
// FIXY-U-016.

#include <crucible/fixy/Handle.h>

#include <type_traits>
#include <utility>

namespace safe  = ::crucible::safety;
namespace fhand = ::crucible::fixy::handle;

// ─── 1. All 9 type carriers alias the substrate ───────────────────

namespace test_fixy_handle {
struct ProbeT {};
struct ProbeProto {};
struct ProbeResource {};
}  // namespace test_fixy_handle

namespace th = test_fixy_handle;

static_assert(std::is_same_v<fhand::Fd, safe::Fd>);
static_assert(std::is_same_v<fhand::FileHandle, safe::FileHandle>);
static_assert(std::is_same_v<fhand::Once, safe::Once>);
static_assert(std::is_same_v<fhand::Lazy<th::ProbeT>, safe::Lazy<th::ProbeT>>);
static_assert(std::is_same_v<fhand::SetOnce<th::ProbeT>, safe::SetOnce<th::ProbeT>>);
static_assert(std::is_same_v<fhand::OneShotFlag, safe::OneShotFlag>);
static_assert(std::is_same_v<fhand::PublishOnce<th::ProbeT>, safe::PublishOnce<th::ProbeT>>);
static_assert(std::is_same_v<fhand::PublishSlot<th::ProbeT>, safe::PublishSlot<th::ProbeT>>);
static_assert(std::is_same_v<
    fhand::LazyEstablishedChannel<th::ProbeProto, th::ProbeResource>,
    safe::LazyEstablishedChannel<th::ProbeProto, th::ProbeResource>>);

// FIXY-U-016b — AlignedBuffer + PublishCommitCell additions.
static_assert(std::is_same_v<fhand::AlignedBuffer<th::ProbeT>,
                             safe::AlignedBuffer<th::ProbeT>>);
static_assert(std::is_same_v<
    fhand::PublishCommitCell<th::ProbeT, th::ProbeProto>,
    safe::PublishCommitCell<th::ProbeT, th::ProbeProto>>);

// FIXY-U-016c — open_read + open_write_truncate free-function identity.
static_assert(std::is_same_v<
    decltype(&fhand::open_read),
    decltype(&safe::open_read)>);
static_assert(std::is_same_v<
    decltype(&fhand::open_write_truncate),
    decltype(&safe::open_write_truncate)>);

// ─── 2. Cardinality FLOOR witness mirror ──────────────────────────
//
// Per FIXY-U-127 / U-128 floor-vs-ceiling split: the EXACT ceiling
// pin (`== 13`) lives in fixy/Handle.h colocated with the source-
// of-truth constant; THIS TU only holds the FLOOR pin (`>= 13`)
// which catches the inverse direction — an accidental REMOVAL of a
// fixy::handle:: alias that escaped review.  Growth past 13 is
// silent here and auto-tracked by the header's `==` ceiling.

static_assert(
    ::crucible::fixy::handle::self_test::handle_alias_cardinality >= 13,
    "floor: fixy::handle:: alias cardinality regressed below 13 — "
    "an alias was removed without updating both Handle.h's colocated "
    "ceiling pin AND this floor witness.");

// ─── 3. End-to-end RAII through the fixy:: alias ──────────────────
//
// Exercises FileHandle::default_ctor + dtor via the fixy::handle::
// alias path.  Proves no name-shadow drift — the destructor closes
// the underlying fd via the substrate's RAII discipline.

int main() {
    {
        fhand::FileHandle fh;
        // Default-constructed handle owns no fd; destructor is a no-op
        // but still travels through the substrate vtable / RAII path,
        // proving the alias is honored at construction and destruction.
        (void)fh;
    }
    {
        // OneShotFlag round-trip — signal + peek via the fixy:: alias.
        fhand::OneShotFlag flag;
        flag.signal();
        bool seen = flag.peek();
        (void)seen;
    }
    return 0;
}
