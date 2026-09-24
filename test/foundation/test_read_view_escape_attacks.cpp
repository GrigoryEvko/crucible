// Attacks on the callback-shaped ReadView.
//
// A view exists only for the call of the body that with_read_view runs.
// Each attack below is legal C++ that tries to carry the view out of the
// frame of the door.  An attack that the door refuses is pinned by a
// static assertion.  An attack that still succeeds is an entry of the
// ledger at the foot of this file, and the ledger only shrinks.

#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <any>
#include <coroutine>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string_view>
#include <utility>

namespace read_view_escape_attacks {

namespace fp = ::foundation::permissions;

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
using View = fp::ReadView<Region>;

struct HoldsUntyped {
    void const* address;
};
struct HoldsCall {
    void (*call)();
};
struct Plain {
    int count;
    double mean;
};

// ── Attacks that the door refuses ────────────────────────────────────

// Type erasure keeps a view behind an untyped address, so a result that
// holds one is refused: std::function, std::move_only_function, std::any,
// a coroutine handle, a pointer to void, and a class with such a member.
static_assert(!fp::ReadViewResultStaysInside<std::function<void()>>);
static_assert(!fp::ReadViewResultStaysInside<std::move_only_function<void()>>);
static_assert(!fp::ReadViewResultStaysInside<std::any>);
static_assert(!fp::ReadViewResultStaysInside<std::coroutine_handle<>>);
static_assert(!fp::ReadViewResultStaysInside<void const*>);
static_assert(!fp::ReadViewResultStaysInside<HoldsUntyped>);
static_assert(!fp::ReadViewResultStaysInside<HoldsCall>);

// A result that names the view, and a reference, are refused too.
static_assert(!fp::ReadViewResultStaysInside<View const*>);
static_assert(!fp::ReadViewResultStaysInside<View const&>);

// A plain value leaves the frame.
static_assert(fp::ReadViewResultStaysInside<int>);
static_assert(fp::ReadViewResultStaysInside<Plain>);
static_assert(fp::ReadViewResultStaysInside<std::pair<int, double>>);

// ── The ledger ───────────────────────────────────────────────────────

struct KnownLimit {
    std::string_view attack;
    std::string_view reason;
};

inline constexpr KnownLimit kLedger[] = {
    {"a body that captures an outer object by reference stores the address of the view in it",
     "GCC 16 reflects no capture of a lambda, and a body must be able to capture its outer state, so no check "
     "sees a store through a capture; every later use of that address reads the released frame of the door"},
};
static_assert(std::size(kLedger) <= 1, "the ledger only shrinks");

}  // namespace read_view_escape_attacks

int main() {
    using namespace read_view_escape_attacks;
    int failures = 0;
    const auto expect = [&failures](bool condition, char const* what) {
        if (!condition) {
            std::fprintf(stderr, "FAIL: %s\n", what);
            ++failures;
        }
    };

    // A plain value leaves the door, and the source comes back.
    auto [value, source] =
        fp::with_read_view(fp::mint_permission_root<Region>(), [](View const&) noexcept { return Plain{3, 1.5}; });
    expect(value.count == 3, "the body's value did not leave the door");

    // The ledger entry reproduces: the address escapes through a capture.
    // It is never used after the door returns.
    void const* leaked = nullptr;
    auto back = fp::with_read_view(std::move(source), [&leaked](View const& view) noexcept { leaked = &view; });
    expect(leaked != nullptr,
           "stale ledger entry: a body no longer stores the address of its view through a capture, delete it");
    (void)back;

    if (failures != 0) return EXIT_FAILURE;
    std::printf("test_read_view_escape_attacks: every refused carrier refused, %zu ledger entry reproduces\n",
                std::size(kLedger));
    return EXIT_SUCCESS;
}
