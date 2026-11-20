// Attacks on the callback-shaped ReadView.
//
// A view exists only for the call of the body that with_read_view runs.
// Each attack below is legal C++ that tries to carry the view out of the
// frame of the door.  An attack that the door refuses is pinned by a
// static assertion.  An attack that still succeeds is an entry of the
// ledger at the foot of this file, and the ledger only shrinks.

#include <foundation/Quarantine.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <fixy/Core.h>

#include <any>
#include <coroutine>
#include <cstdlib>
#include <functional>
#include <utility>

namespace read_view_escape_attacks {

namespace fp = ::foundation::permissions;

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
using View = fp::ReadView<Region>;

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: an attack carries a view out of its door in a std type or a raw address")
struct HoldsUntyped {
    void const* address;
};
struct HoldsCall {
    void (*call)();
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
struct Plain {
    int count;
    double mean;
};

// ── Attacks that the door refuses ────────────────────────────────────

// Type erasure keeps a view behind an untyped address, so a result that
// holds one is refused: std::function, std::move_only_function, std::any,
// a coroutine handle, a pointer to void, and a class with such a member.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: an attack carries a view out of its door in a std type or a raw address")
static_assert(!fp::ReadViewResultStaysInside<std::function<void()>>);
static_assert(!fp::ReadViewResultStaysInside<std::move_only_function<void()>>);
static_assert(!fp::ReadViewResultStaysInside<std::any>);
static_assert(!fp::ReadViewResultStaysInside<std::coroutine_handle<>>);
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
static_assert(!fp::ReadViewResultStaysInside<void const*>);
static_assert(!fp::ReadViewResultStaysInside<HoldsUntyped>);
static_assert(!fp::ReadViewResultStaysInside<HoldsCall>);

// A result that names the view, and a reference, are refused too.
static_assert(!fp::ReadViewResultStaysInside<View const*>);
static_assert(!fp::ReadViewResultStaysInside<View const&>);

// A plain value leaves the frame.
static_assert(fp::ReadViewResultStaysInside<int>);
static_assert(fp::ReadViewResultStaysInside<Plain>);
static_assert(fp::ReadViewResultStaysInside<::fixy::Record<int, double>>);

// ── The ledger ───────────────────────────────────────────────────────

struct KnownLimit {
    ::fixy::TextView attack;
    ::fixy::TextView reason;
};

inline constexpr ::fixy::FixedArray<KnownLimit, 1> kLedger{KnownLimit{
    "a body that captures an outer object by reference stores the address of the view in it",
    "a body must be able to capture its outer state by reference, and the type of a capture shows the "
    "reference but not a store through it; every later use of that address reads the released frame of the door"}};
static_assert(kLedger.size() <= 1, "the ledger only shrinks");

}  // namespace read_view_escape_attacks

int main() {
    using namespace read_view_escape_attacks;
    int failures = 0;
    const auto expect = [&failures](bool condition, ::fixy::TextView what) {
        if (!condition) {
            ::fixy::report(::fixy::Sink::Err, "FAIL: {}\n", what);
            ++failures;
        }
    };

    // A plain value leaves the door, and the source comes back.
    auto [value, source] =
        fp::with_read_view(fp::mint_permission_root<Region>(), [](auto const&) noexcept { return Plain{3, 1.5}; });
    expect(value.count == 3, "the body's value did not leave the door");

    // The ledger entry reproduces: the address escapes through a capture.
    // It is never used after the door returns.
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: an attack carries a view out of its door in a std type or a raw address")
    void const* leaked = nullptr;
    auto back = fp::with_read_view(std::move(source), [&leaked](auto const& view) noexcept { leaked = &view; });
    expect(leaked != nullptr,
           "stale ledger entry: a body no longer stores the address of its view through a capture, delete it");
    (void)back;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

    if (failures != 0) return EXIT_FAILURE;
    ::fixy::report(::fixy::Sink::Out,
                   "test_read_view_escape_attacks: every refused carrier refused, {} ledger entry reproduces\n",
                   kLedger.size());
    return EXIT_SUCCESS;
}
