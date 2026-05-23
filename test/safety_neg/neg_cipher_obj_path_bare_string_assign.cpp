// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Cipher-3 #886, mismatch class #1 of 2:
// BARE std::string CANNOT MASQUERADE AS Tagged<std::string, CipherPath>.
//
// After the migration, Cipher::obj_path() returns
// Tagged<std::string, source::CipherPath>.  A bare std::string —
// however constructed — has no CipherPath provenance witness, so it
// must be statically rejected when assigned to a Tagged-typed slot.
// If this fixture starts compiling, the wrap silently degraded back
// to a bare std::string and every CipherPath guarantee disappeared
// (in particular, future downstream Sanitized-consumer admissions
// for CipherPath would silently match unrelated user-input strings).
//
// Distinct from the cross-source fixture, which fails because two
// DIFFERENT Tagged wrappers collide; here the failure is a bare
// std::string trying to land in a wrapped slot.
//
// Expected diagnostic: no match for 'operator=' / cannot convert /
// no viable / conversion from.

#include <crucible/safety/Tagged.h>
#include <crucible/safety/source/Path.h>

#include <string>

int main() {
    using CipherPathString = ::crucible::safety::Tagged<
        std::string, ::crucible::safety::source::CipherPath>;

    CipherPathString slot{std::string{"/cipher/objects/00/...zero-witness"}};

    // Should FAIL: a bare std::string has no implicit conversion to
    // Tagged<std::string, CipherPath>.  The explicit ctor exists but
    // the cross-Tagged-and-bare assignment operator does not.
    slot = std::string{"/some/external/path"};

    return 0;
}
