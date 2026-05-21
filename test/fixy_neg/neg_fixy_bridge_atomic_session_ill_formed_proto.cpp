// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-117b negative fixture #2 (HS14 ≥2 floor):
// `mint_atomic_session<Proto, Cell>(cell)` is_well_formed_v gate via
// the `fixy::bridge::` re-export (Bridge.h:105, FIXY-U-117).
//
// `safety::proto::is_well_formed_v<Proto>` is a trait template
// forward-declared at `sessions/Session.h:1060` with NO primary
// definition — every valid session-protocol carrier (End, Continue,
// Send<T,R>, Recv<T,R>, Select<Bs...>, Offer<Bs...>, Loop<B>,
// VendorPinned<V,P>) ships a partial specialization yielding
// `std::true_type`.  An unspecialized type produces an incomplete-
// type error inside the requires-clause, which fails substitution →
// requires-clause unsatisfied → overload removed.
//
// This fixture exercises the §XXI mint factory call site through the
// `fixy::bridge::mint_atomic_session` re-export.  Routing through the
// fixy:: layer witnesses that the using-decl preserves the second
// constraint conjunct `safety::proto::is_well_formed_v<Proto>` — a
// regression that silently dropped this gate (e.g., by reordering
// the conjunction or rewriting it to a weaker check) would leave
// substrate-side tests green while THIS fixture unexpectedly compiles.
//
// Reject sequence: caller passes `NotAProto` (an empty struct that
// matches NO specialization of `is_well_formed`) → access to
// `is_well_formed<NotAProto>::value` triggers undefined-trait
// instantiation → substitution into the requires-clause fails →
// overload removed from candidate set → "no matching function for
// call to 'mint_atomic_session'."
//
// Distinct from fixture #1 (non_cell): #1 exercises the Cell-side
// AtomicMachineCell concept; #2 exercises the Proto-side
// is_well_formed_v trait.  Different parameter slots, different
// failure mechanisms (concept-requires unsatisfied vs trait-template
// undefined).
//
// Expected diagnostic: "no matching function for call to
// 'mint_atomic_session'" / "constraints not satisfied" /
// "is_well_formed" / "incomplete type" / "no type named 'value'".

#include <crucible/fixy/Bridge.h>
#include <crucible/sessions/Session.h>

#include <atomic>

namespace fbridge = ::crucible::fixy::bridge;
namespace proto   = ::crucible::safety::proto;

namespace test_fixy_bridge_atomic_ill_formed_proto {

// Probe Cell — VALID AtomicMachineCell (state_type + load present),
// isolates Proto-side failure (not co-mingled with Cell-side).
struct AtomicProbeCell {
    using state_type = int;
    constexpr state_type load(std::memory_order) const noexcept { return 0; }
};

// NotAProto — empty struct.  Matches NO specialization of
// safety::proto::is_well_formed → is_well_formed<NotAProto> is the
// undefined primary template → access to ::value is ill-formed.
struct NotAProto {};

}  // namespace test_fixy_bridge_atomic_ill_formed_proto

int main() {
    test_fixy_bridge_atomic_ill_formed_proto::AtomicProbeCell cell{};

    // First template argument is the (malformed) Proto; Cell is
    // deduced from `cell` as the valid AtomicProbeCell.
    // is_well_formed_v<NotAProto> evaluates to an incomplete-type
    // error → substitution fails → overload removed.  fixy::bridge::
    // re-export must reject identically — the using-decl preserves
    // the substrate trait gate.
    [[maybe_unused]] auto bad =
        fbridge::mint_atomic_session<
            test_fixy_bridge_atomic_ill_formed_proto::NotAProto>(cell);

    return 0;
}
