// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-117b negative fixture #1 (HS14 ≥2 floor):
// `mint_atomic_session<Proto, Cell>(cell)` AtomicMachineCell gate via
// the `fixy::bridge::` re-export (Bridge.h:105, FIXY-U-117).
//
// `mint_atomic_session` is the §XXI canonical mint factory that
// observes a Cell's atomic state-machine through a Send/Recv session
// protocol.  Substrate at `bridges/MachineSessionBridge.h:281` in
// `namespace crucible::safety` — NOT `crucible::bridges::` despite
// the file path.  Concept gate (line 279-280):
//
//   template <typename Proto, typename Cell>
//       requires (AtomicMachineCell<Cell>
//                 && safety::proto::is_well_formed_v<Proto>)
//   [[nodiscard]] constexpr auto mint_atomic_session(Cell& cell) noexcept;
//
// `AtomicMachineCell<Cell>` requires (line 270-276):
//   - `typename Cell::state_type` typedef
//   - `cell.load(memory_order)` returning `state_type`
//
// This fixture exercises the §XXI mint factory call site through the
// `fixy::bridge::mint_atomic_session` re-export.  Routing through the
// fixy:: layer (not directly through `safety::`) witnesses that the
// using-decl preserves the requires-clause AND the AtomicMachineCell
// concept gate.  A regression that silently dropped the concept gate
// (or that broke the using-decl import) would leave substrate-side
// tests green while THIS fixture would unexpectedly compile — that
// gap is what the HS14 floor closes.
//
// Reject sequence: caller passes `NotACell` (an empty struct with no
// `state_type`, no `load()`) → `AtomicMachineCell<NotACell>` fails
// concept satisfaction (missing required member) → requires-clause
// false → overload removed from candidate set → "no matching function
// for call to 'mint_atomic_session'."
//
// Distinct from fixture #2 (ill_formed_proto): #1 exercises the Cell-
// side concept gate; #2 exercises the Proto-side well-formedness
// trait.  Different parameter slots, different failure mechanisms
// (concept-requires unsatisfied vs trait-template undefined).
//
// Expected diagnostic: "no matching function for call to
// 'mint_atomic_session'" / "constraints not satisfied" /
// "AtomicMachineCell" / "state_type".

#include <crucible/fixy/Bridge.h>
#include <crucible/sessions/Session.h>

namespace fbridge = ::crucible::fixy::bridge;
namespace proto   = ::crucible::safety::proto;

namespace test_fixy_bridge_atomic_non_cell {

// Probe Proto — valid Send<int, End> protocol, isolates Cell-side
// failure (not co-mingled with Proto-side).
using ProbeProto = proto::Send<int, proto::End>;

// NotACell — empty struct.  Missing BOTH `state_type` typedef AND
// `load(memory_order)` member → AtomicMachineCell<NotACell> false.
struct NotACell {};

}  // namespace test_fixy_bridge_atomic_non_cell

int main() {
    test_fixy_bridge_atomic_non_cell::NotACell cell{};

    // First template argument is the (correctly-formed) Proto; Cell is
    // deduced from `cell` as NotACell.  AtomicMachineCell<NotACell>
    // fails → requires-clause false → overload removed.  fixy::bridge::
    // re-export must reject identically — the using-decl preserves
    // the substrate concept gate.
    [[maybe_unused]] auto bad =
        fbridge::mint_atomic_session<
            test_fixy_bridge_atomic_non_cell::ProbeProto>(cell);

    return 0;
}
