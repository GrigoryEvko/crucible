// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-246 HS14 fixture (Stdio 1/2) — write expects a stream TYPE.
//
// `grant::stdio::write<Stream>`'s parameter is a stream policy TYPE
// (streams::Stderr / Stdout / Debug).  Substituting a VALUE (here `42`)
// where a type is expected is rejected at template-id formation — the
// destination stream cannot be an integer.
//
// Mismatch class: value-where-TYPE-expected.
//
// Expected diagnostic: a GCC "expected a type" / "type/value mismatch"
// / "invalid" template-argument error.

#include <crucible/fixy/grant/Stdio.h>

namespace sio = crucible::fixy::grant::stdio;

using Bad = sio::write<42>;  // 42 is a value; Stream is a type

int main() {
    [[maybe_unused]] Bad bad{};
    return 0;
}
