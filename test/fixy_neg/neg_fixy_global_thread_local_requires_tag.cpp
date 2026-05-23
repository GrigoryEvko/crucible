// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-246 HS14 fixture (Global 1/2) — thread_local_ TLSTag is mandatory.
//
// `grant::global::thread_local_<TLSTag>` carries NO default: every
// thread_local datum MUST be named by a unique phantom tag.  A bare
// `grant::global::thread_local_<>` is ill-formed — "wrong number of
// template arguments".  This is the grant-site mirror of the V-243 G001
// composition rule (an untagged thread_local is a process-wide aliasing
// hazard the type system must surface).
//
// Mismatch class: template-argument ARITY (missing non-defaulted param).
//
// Expected diagnostic: a GCC template-argument-count error.

#include <crucible/fixy/grant/Global.h>

namespace glb = crucible::fixy::grant::global;

using Bad = glb::thread_local_<>;  // missing the mandatory TLSTag

int main() {
    [[maybe_unused]] Bad bad{};
    return 0;
}
