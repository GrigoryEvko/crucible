// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-216 HS14 fixture #2 of 2 for fixy::bridge::EpochVersioned:
// cross-T composition rejection — `combine_max` requires both sides
// of the fan-in fold to share the same T parameter.
//
// Violation: the substrate EpochVersioned<T>::combine_max member is
// declared `[[nodiscard]] constexpr EpochVersioned combine_max(
// EpochVersioned const& other)` — its parameter is EXACTLY the same
// instantiation, NOT a wider EpochVersioned<U>.  Composing
// EpochVersioned<int> with EpochVersioned<double> is a substrate-
// level compile error (no implicit cross-T conversion); the fixy::
// bridge:: alias preserves the rejection without retag laundering.
// If the alias ever drifted, a Cipher cold-tier roll-forward
// admission could silently combine bridge checkpoint values of
// incompatible payload types, breaking the row_hash federation
// cache (FOUND-G68) AND TypeSafe at the same time.
//
// Distinct from fixture #1 (handle axis swap):
//   * Fixture #1 — Strong-typed AXIS rejection at ctor parameter
//     position.  EpochVersioned<int> with Generation in Epoch slot.
//   * Fixture #2 — Cross-T COMPOSITION rejection at combine_max call.
//     EpochVersioned<int>::combine_max(EpochVersioned<double>).
// Two distinct rejection axes ⇒ HS14 floor satisfied.
//
// Expected diagnostic: no matching function for call to
// EpochVersioned<int>::combine_max with argument of type
// EpochVersioned<double> (cannot convert; cross-T composition not
// permitted).

#include <crucible/fixy/Bridge.h>

int main() {
    namespace fb = ::crucible::fixy::bridge;

    // Two distinct EpochVersioned instantiations.  Both legal; the
    // substrate freely admits per-T construction.  What it REJECTS is
    // composing them — the cross-T fold has no canonical semantics
    // (which T does the resulting value carry?), so the substrate
    // refuses to compile the call below.
    fb::EpochVersioned<int>    int_value{
        42, fb::Epoch{5}, fb::Generation{2}};
    fb::EpochVersioned<double> dbl_value{
        3.14, fb::Epoch{7}, fb::Generation{3}};

    // THE BYPASS: combine_max requires same-T on both sides.  If this
    // ever compiled through the fixy::bridge:: alias, a Cipher cold-
    // tier roll-forward could silently merge an int-payload checkpoint
    // with a double-payload checkpoint and emit a "newest" view whose
    // value bytes don't match either input — TypeSafe + DetSafe
    // simultaneously violated.
    auto merged = int_value.combine_max(dbl_value);
    (void)merged;
    return 0;
}
