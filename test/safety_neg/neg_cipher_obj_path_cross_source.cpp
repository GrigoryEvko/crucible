// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Cipher-3 #886, mismatch class #2 of 2:
// FromUserPath CANNOT MASQUERADE AS CipherPath.
//
// Each path-provenance tag pins a DISTINCT lane in the retag-policy
// matrix, and Cipher's CipherPath lane carries the invariant that
// the bytes were assembled INSIDE Cipher from a sanitized root plus
// a hex-formatted content hash — they never traversed an untrusted
// boundary.  A Tagged<std::string, source::FromUserPath> value came
// from CLI / argv input — it MUST NOT be silently relabelled as a
// Cipher-emitted path, otherwise user-supplied paths could land in
// Cipher's openat() helpers via a future Sanitized retag admission.
//
// The fail-closed retag_policy primary template (V-022, Tagged.h)
// rejects cross-lane assignment between distinct provenance NTTPs;
// V-232's catalog ships ONLY the laundering FORWARD into Sanitized
// for the three external lanes.  CipherPath is intentionally NOT in
// that forward catalog — Cipher must mint CipherPath via its
// internal helpers, not by retagging from FromUserPath.
//
// Distinct from the bare-string fixture which fails because the
// SOURCE side has no wrap at all; here both sides ARE wrapped, but
// the source-lattice tags disagree.
//
// Expected diagnostic: no match for 'operator=' / cannot convert /
// no viable / conversion from.

#include <crucible/safety/Tagged.h>
#include <crucible/safety/source/Path.h>

#include <string>

int main() {
    using UserPathString   = ::crucible::safety::Tagged<
        std::string, ::crucible::safety::source::FromUserPath>;
    using CipherPathString = ::crucible::safety::Tagged<
        std::string, ::crucible::safety::source::CipherPath>;

    UserPathString user_path{std::string{"/etc/passwd"}};
    CipherPathString cipher_slot{std::string{"/cipher/objects/00/zero"}};

    // Should FAIL: FromUserPath and CipherPath are distinct lanes;
    // no implicit conversion / no retag_policy admission exists
    // between them.
    cipher_slot = user_path;

    return 0;
}
