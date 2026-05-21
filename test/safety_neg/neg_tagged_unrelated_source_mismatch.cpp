// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing `Tagged<T, source::External>` (raw untrusted
// input) to a function whose signature demands
// `Tagged<T, source::Sanitized>`.  Despite identical payload type
// T, the two Tagged specializations are UNRELATED types — phantom
// Tag is a template parameter, not a runtime field; the type
// system gives no implicit conversion.
//
// Discipline rationale (Tagged.h):
//   Tagged<T, Tag> exists to FORCE every trust-boundary crossing
//   through an explicit retag<>() call.  External input from FFI /
//   network / Python carries source::External until Vessel-side
//   validation actively retags it to source::Sanitized.  An API
//   that demands Sanitized refuses External at the call site — by
//   construction, no path exists to feed unvalidated bytes into a
//   sanitized-only consumer.  This is the "production type
//   discipline":
//
//     void hash_for_kernel(Tagged<uint64_t, source::Sanitized> seed);
//     // hash_for_kernel(raw_ffi_uint64);  // compile-time reject
//     auto validated = std::move(raw_ffi_uint64).retag<source::Sanitized>();
//     hash_for_kernel(std::move(validated));  // OK after validator
//
// HS14 — paired with neg_tagged_mint_scalar_tag for distinct
// mismatch classes:
//   * Class U (sibling):    concept-gate rejection at constraint-checking
//   * Class T (THIS file):  typed-overload rejection across distinct tag types
// Together the pair pins both soundness layers of Tagged's discipline:
//   (a) tag-shape gate (class-type required); and
//   (b) tag-identity gate (different tags = different types, no
//       implicit conversion across the trust boundary).
//
// U-142 — Class T fixture (closes Tagged slice of #146 A8-P2).

#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <utility>

namespace {
    // Sanitized-only consumer — refuses any other Tag at the type
    // system level.  In production this would be a Forge / Mimic /
    // KernelCache entry point that requires validated input.
    [[maybe_unused]] void hash_for_kernel(
        ::crucible::safety::Tagged<std::uint64_t,
                                   ::crucible::safety::source::Sanitized> /*seed*/)
    {
        // body irrelevant — the call-site type-check is the test.
    }
}

// Anchor a legitimate call so the file is self-contained — Sanitized
// input is what the API admits.  This call compiles.
[[maybe_unused]] static void anchor_sanitized_call() {
    auto seed = ::crucible::safety::mint_tagged<
        ::crucible::safety::source::Sanitized, std::uint64_t>(0xDEADBEEFULL);
    hash_for_kernel(std::move(seed));
}

// VIOLATION: source::External and source::Sanitized are unrelated
// template instantiations of Tagged — even though the payload type
// (uint64_t) matches.  The C++ overload resolver finds no implicit
// conversion; GCC rejects with "cannot convert ... source::External
// ... to ... source::Sanitized" or similar typed-argument mismatch.
[[maybe_unused]] static void offending_external_into_sanitized_slot() {
    auto raw = ::crucible::safety::mint_tagged<
        ::crucible::safety::source::External, std::uint64_t>(0xDEADBEEFULL);
    hash_for_kernel(std::move(raw));  // ERROR: External ≠ Sanitized
}

int main() { return 0; }
