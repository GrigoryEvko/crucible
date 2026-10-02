// The known-limitation ledger of the attacks on the primitive families of
// foundation/core.
//
// Each entry is an attack that type-checks and that a family cannot refuse
// in C++, with the condition that it breaks.  A pin after each entry
// asserts the fact that lets the attack compile, so a repair fails the
// pin, and the entry and its pin go together.  The ledger only shrinks.
//
// The attacks that the families refuse are not entries.  Their fixtures
// and checks are in test/foundation/neg/ and test/layer/checks/foundation/
// core/: a Fmt over a run-time pointer or a local array, a read or a step
// at the end of a cursor, a borrow of a moved-from Box, an error with a
// reference member, a payload equal to the empty value of its niche, and a
// View of a temporary mapping.

#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>

#include <concepts>
#include <type_traits>

namespace {

using ::foundation::core::Box;
using ::foundation::core::ErrorValue;
using ::foundation::core::Option;
using ::foundation::core::View;
using ::foundation::core::ViewCursor;

// ── Entry: a borrow outlives its owner ───────────────────────────────
//
// A View, a cursor of a View and a borrow of a Box carry no lifetime.  A
// copy of a View that the caller keeps after its mapping unmaps, or a
// reference from Box::get() that the caller keeps after a move assignment
// to the Box, reads freed memory.  The deleted rvalue overloads refuse
// only a borrow of a temporary owner.  The fix needs a lifetime that the
// type carries, which C++ does not have.
static_assert(std::is_trivially_copy_constructible_v<View<int>>);
static_assert(std::is_copy_constructible_v<ViewCursor<int>>);
static_assert(std::is_move_assignable_v<Box<int>>);
static_assert(requires(Box<int>& box) {
    { box.get() } -> std::same_as<int&>;
});

// ── Entry: a match arm assigns the Option that it reads ──────────────
//
// The borrow match gives the arm a const reference into the Option.  An
// arm that captures the Option and assigns it, for example `holder = none`,
// ends the life of the payload that its reference names.  The Option of a
// trivially copyable payload stays assignable, because the ABI gives it
// back in registers only then.
static_assert(std::is_copy_assignable_v<Option<int>>);
static_assert(std::is_copy_assignable_v<::foundation::core::Result<int, ::foundation::core::LengthMismatch>>);

// ── Entry: an error holds an address ─────────────────────────────────
//
// An error with a pointer member is a value of 8 bytes, and it can point
// at an object that ends before the error is read.  The compare-and-swap
// of a pointer cell gives the pointer that it read as its error, so the
// gate admits a pointer member.  The quarantine plugin refuses a raw
// pointer object outside the base.
static_assert(ErrorValue<::foundation::core::CasRefusal<int const*>>);

}  // namespace

int main() { return 0; }
