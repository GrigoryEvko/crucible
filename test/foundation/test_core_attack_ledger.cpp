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
// reference member, a payload equal to the empty value of its niche, a
// View of a temporary mapping, and an arm of an rvalue match that takes its
// part by reference or gives a borrow of it.

#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>

#include <concepts>
#include <type_traits>
#include <utility>

namespace {

using ::foundation::core::Box;
using ::foundation::core::ErrorValue;
using ::foundation::core::Option;
using ::foundation::core::View;
using ::foundation::core::ViewCursor;

// ── Entry: a borrow outlives its owner ───────────────────────────────
//
// A View, a cursor of a View and a borrow of a Box carry no lifetime.  A
// copy of a View that the caller keeps after its mapping unmaps, or a Ref
// from Box::view() that the caller keeps after a move assignment to the
// Box, reads freed memory.  The deleted rvalue overloads refuse only a
// borrow of a temporary owner.  The fix needs a lifetime that the type
// carries, which C++ does not have.
static_assert(std::is_trivially_copy_constructible_v<View<int>>);
static_assert(std::is_copy_constructible_v<ViewCursor<int>>);
static_assert(std::is_move_assignable_v<Box<int>>);
template <class T, class Brand>
T target_of(::foundation::core::Ref<T, Brand>);
static_assert(std::is_same_v<decltype(target_of(std::declval<Box<int>&>().view())), int>);

// ── Entry: a match arm writes the sum that it reads ──────────────────
//
// The borrow match gives the arm a const reference into the sum.  The rule
// borrow-match of the quarantine plugin refuses an assignment, a move or a
// mutating call of the sum in the body of the arm, through a capture by
// reference, a capture of this or a local reference.  A write in a callee
// of the arm, through a pointer or a second reference parameter, or in a
// closure that the call gets by name still ends the life of the payload
// that the reference names.  The Option of a trivially copyable payload
// stays assignable, because the ABI gives it back in registers only then.
// The owning form has a move assignment too, and there such a write runs
// the destructor of the payload that the reference names.
static_assert(std::is_copy_assignable_v<Option<int>>);
static_assert(std::is_copy_assignable_v<::foundation::core::Result<int, ::foundation::core::LengthMismatch>>);
static_assert(std::is_move_assignable_v<Option<Box<int>>>);

// ── Entry: an arm of the rvalue match stores a borrow of its part ─────
//
// An arm of the rvalue match takes its part by value, and its result holds
// no borrow.  The gates read the parameter and the result of the arm, and
// not its body.  An arm can still write a view of its part through a capture
// by reference, and the view dangles when the arm returns.  The fix needs a
// lifetime that the type carries, as the first entry tells.
using Numbers = ::foundation::core::FixedArray<int, 4>;
[[maybe_unused]] void arm_stores_a_borrow(View<int, 4>& out) {
    auto const stores = [&out](Numbers numbers) noexcept {
        out = numbers.view();
        return 0;
    };
    static_assert(::foundation::core::TakesPayloadByValue<decltype(stores), Numbers>);
    static_assert(::foundation::core::HoldsNoBorrow<decltype(stores(Numbers{}))>);
}

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
