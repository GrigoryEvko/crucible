// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #7 of 7 for safety/Pre.h + safety/Post.h.
//
// Premise: CRUCIBLE_POST on a function returning a STRUCT, with the
// post-predicate reading through a field of the return value, must
// fire at consteval when the predicate is violated.  Probe Shape #7
// — distinct from #6 (scalar return) because the post-predicate
// projects into a struct field (`r.v > 0`), exercising both the
// post-binding and the field-access machinery.
//
// Distinct mismatch class: struct-return with field-projecting post.
// Production sites use this shape when the function returns a small
// PoD aggregate (TensorMeta, ContentHash, slot offset+size pairs)
// and the post-clause asserts an invariant on one specific field.
// The MerkleDag::compute_storage_nbytes invariant chain and saturated-
// arithmetic returns follow this exact pattern.
//
// Expected diagnostic: "non-constant condition for static assertion".

#include <crucible/safety/Post.h>

namespace {

struct R { int v = 0; };

[[nodiscard]] constexpr R compute_must_be_positive_field(int const x) noexcept {
    R r{x - 1};
    CRUCIBLE_POST(r, r.v > 0);
    return r;
}

// x = 1 → r.v = 0 → predicate (r.v > 0) is false → CRUCIBLE_POST
// must fire at consteval.
static_assert(compute_must_be_positive_field(1).v == 0,
    "CRUCIBLE_POST on a struct return with field-projecting predicate "
    "MUST fire at consteval when the predicate is violated.  If this "
    "static_assert ever evaluates successfully, Post.h's consteval "
    "enforcement is broken for struct-return shapes (Probe Shape #7) — "
    "the dominant pattern for invariant assertions on PoD aggregates "
    "(TensorMeta, ContentHash, MerkleDag::compute_storage_nbytes, etc.).");

}  // namespace

int main() { return 0; }
