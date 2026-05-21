// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: invoking `Tagged<T, NeverFrom>::retag<NeverTo>()` for
// the V-022 sentinel-tag pair — guaranteed unspecialized forever
// per `detail::retag_policy_test::{NeverFrom, NeverTo}`.
//
// FIXY-V-024 wires `Tagged::retag()` with `requires RetagAllowed<
// Tag, NewTag>`.  The requires-clause consults the V-023 catalog;
// the sentinel pair is reserved by V-022 to remain unspecialized,
// so the fail-closed primary template witnesses the rejection at
// the CONSUMER level.
//
// Sister fixture to `neg_tagged_retag_cross_axis.cpp`.  Both
// exercise the same wired-in requires-clause on Tagged::retag(),
// but for DISTINCT mismatch classes:
//   - cross_axis: a semantically-meaningful transition the V-023
//     catalog never admits (source::* → trust::*).
//   - sentinel_pair (this file): a structurally-guaranteed-
//     unspecialized pair reserved by V-022, stays correct as the
//     V-023 catalog grows.
// Together they witness "Tagged::retag()'s requires-clause fires"
// across both kinds of catalog miss.

#include <crucible/safety/Tagged.h>

namespace ns = crucible::safety;

int main() {
    // Construct a Tagged with the V-022 sentinel `NeverFrom`.
    ns::Tagged<int, ns::detail::retag_policy_test::NeverFrom> witness{42};

    // Sentinel-pair retag — guaranteed unspecialized per V-022.
    // The wired-in requires-clause on Tagged::retag() consults
    // retag_policy<NeverFrom, NeverTo>::allowed == false (primary
    // template) and rejects the call.
    auto retagged = std::move(witness)
        .retag<ns::detail::retag_policy_test::NeverTo>();
    (void)retagged;
    return 0;
}
