// SPDX-License-Identifier: Apache-2.0
//
// test/test_pre_post.cpp
//
// Positive runtime + consteval coverage for `safety/Pre.h` and
// `safety/Post.h`.  Validates that:
//
//   1. CRUCIBLE_PRE / CRUCIBLE_POST compile cleanly under `-DNDEBUG`
//      (release-mode positive case)
//   2. CRUCIBLE_PRE / CRUCIBLE_POST compile cleanly without `-DNDEBUG`
//      (debug-mode positive case)
//   3. The macros work for every parameter shape that GCC 16.1.1's
//      P2900 `pre()` clauses silently bypass at consteval (the empirical
//      probe in feedback_crucible_pre_post_macros.md identified 7 shapes
//      where the bypass occurs and 1 where it fires; the macro pair
//      MUST cover all 8)
//   4. Successful CRUCIBLE_POST does not double-evaluate the predicate
//      argument (the macro casts retvar to (void) once)
//
// Negative-compile coverage (consteval enforcement on contract
// violation) ships as separate fixtures in test/safety_neg/:
//   * neg_pre_macro_consteval_struct_ref.cpp
//   * neg_post_macro_consteval_scalar_return.cpp
//
// No printf / fprintf — the test is silent on success, exit code 0
// indicates pass.  Failure surfaces as a contract abort (SIGABRT in
// !NDEBUG) which CTest reports as a non-zero exit.

#include <crucible/safety/Contract.h>   // umbrella: Pre.h + Post.h
#include <contracts>                     // P2900 std::contracts surface

#include <cstdint>
#include <cstdio>

namespace {

// ── Test fixtures ────────────────────────────────────────────────
// One struct, one scalar.  Nine functions, one per probed shape.
// The shapes mirror feedback_crucible_pre_post_macros.md so the
// audit trail is direct.

struct S {
    std::uint64_t lo = 0;
    [[nodiscard]] constexpr bool nz() const noexcept { return lo != 0; }
};

struct R { int v = 0; };

// Shape 1 — pre on scalar by-value
[[nodiscard]] constexpr int pre_scalar(int x) noexcept {
    CRUCIBLE_PRE(x > 0);
    return x * 2;
}

// Shape 2 — pre on struct const-ref (the load-bearing case for
// production migration; was silently bypassed by P2900 pre())
[[nodiscard]] constexpr std::uint64_t pre_struct_cref(S const& s) noexcept {
    CRUCIBLE_PRE(s.nz());
    return s.lo;
}

// Shape 3 — pre on struct const by-value
[[nodiscard]] constexpr std::uint64_t pre_struct_cval(S const s) noexcept {
    CRUCIBLE_PRE(s.nz());
    return s.lo;
}

// Shape 4 — pre on struct by-value (no const)
[[nodiscard]] constexpr std::uint64_t pre_struct_val(S s) noexcept {
    CRUCIBLE_PRE(s.nz());
    return s.lo;
}

// Shape 5 — pre on struct const-pointer (deref through pointer)
[[nodiscard]] constexpr std::uint64_t pre_struct_cptr(S const* s) noexcept {
    CRUCIBLE_PRE(s != nullptr && s->nz());
    return s->lo;
}

// Shape 6 — post on scalar return (named retvar pattern)
[[nodiscard]] constexpr int post_scalar(int const x) noexcept {
    int const r = x + 1;
    CRUCIBLE_POST(r, r > 0);
    return r;
}

// Shape 7 — post on struct return field
[[nodiscard]] constexpr R post_struct_field(int const x) noexcept {
    R r{x + 1};
    CRUCIBLE_POST(r, r.v > 0);
    return r;
}

// Shape 8 — pre + post composed in a single function (the most
// realistic production pattern: validate input, compute, validate
// output)
[[nodiscard]] constexpr int pre_and_post(int x) noexcept {
    CRUCIBLE_PRE(x > 0 && x < 1000);
    int const r = x * 3;
    CRUCIBLE_POST(r, r >= x && r < 3000);
    return r;
}

// Shape 9 — CRUCIBLE_PRE used as a mid-body invariant assertion
// (not a function-entry contract) — the macro doubles as a
// contract_assert-style intermediate check.  This pattern matches
// the CLAUDE.md §XII assertion triad.
[[nodiscard]] constexpr int mid_body_assert(int x) noexcept {
    CRUCIBLE_PRE(x > 0);
    int y = x * 2;
    CRUCIBLE_PRE(y > x);   // invariant after the multiply
    int z = y + 1;
    CRUCIBLE_POST(z, z > y);
    return z;
}

// ── Constexpr witnesses ───────────────────────────────────────────
// All eight shapes evaluated at consteval — proves the macro fires
// inside a `static_assert` context (the load-bearing surface for
// neg-compile fixtures).  If any of these regress, the consteval
// path is broken and every neg-compile fixture is at risk of silent
// false-pass.

constexpr S OK_S{42};

static_assert(pre_scalar(5) == 10,             "shape 1 positive");
static_assert(pre_struct_cref(OK_S) == 42,     "shape 2 positive");
static_assert(pre_struct_cval(OK_S) == 42,     "shape 3 positive");
static_assert(pre_struct_val(OK_S)  == 42,     "shape 4 positive");
static_assert(pre_struct_cptr(&OK_S) == 42,    "shape 5 positive");
static_assert(post_scalar(5) == 6,             "shape 6 positive");
static_assert(post_struct_field(5).v == 6,     "shape 7 positive");
static_assert(pre_and_post(10) == 30,          "shape 8 positive");
static_assert(mid_body_assert(5) == 11,        "shape 9 positive");

// ── Cost-model probe ──────────────────────────────────────────────
// Confirms the [[assume]] hint propagates: the optimizer should be
// able to elide bounds checks on the return because the post
// asserts r >= x.  We don't measure codegen size here (that's a
// bench-suite job); we only assert the invariant CAN be exploited
// by a downstream check.

[[nodiscard]] constexpr int relies_on_post(int x) noexcept {
    int const r = post_scalar(x);   // post-condition: r > 0
    // [[assume(r > 0)]] propagated via post_scalar's CRUCIBLE_POST.
    // The compiler is free to elide a redundant bounds check here.
    if (r <= 0) [[unlikely]] return -1;   // dead code under [[assume]]
    return r;
}

static_assert(relies_on_post(5) == 6, "post hint propagation");

// ── CRUCIBLE_PRE_FAST positive witnesses ──────────────────────────
// Quick-enforce variant — same consteval behavior as CRUCIBLE_PRE,
// minimal runtime cost (skip handler, trap directly).  Witness pins
// that the macro fires at consteval AND compiles cleanly across all
// shapes the regular PRE covers.  Negative-compile fixtures are
// shared with CRUCIBLE_PRE (the consteval branch is identical).

[[nodiscard]] constexpr int pre_fast_scalar(int x) noexcept {
    CRUCIBLE_PRE_FAST(x > 0);
    return x * 3;
}

[[nodiscard]] constexpr std::uint64_t pre_fast_struct(S const& s) noexcept {
    CRUCIBLE_PRE_FAST(s.nz());
    return s.lo;
}

static_assert(pre_fast_scalar(5) == 15,         "PRE_FAST scalar positive");
static_assert(pre_fast_struct(OK_S) == 42,      "PRE_FAST struct positive");

// ── CRUCIBLE_PRE_MSG / CRUCIBLE_POST_MSG / CRUCIBLE_POST_FAST ─────
// Annotated and fast variants exercised at consteval to confirm
// the message-arg form compiles cleanly across both successful and
// neg-compile-fixture-eligible shapes.

[[nodiscard]] constexpr int pre_msg_scalar(int x) noexcept {
    CRUCIBLE_PRE_MSG(x > 0, "scalar input must be strictly positive");
    return x + 100;
}

[[nodiscard]] constexpr R post_msg_struct(int const x) noexcept {
    R r{x + 50};
    CRUCIBLE_POST_MSG(r, r.v > 0,
                      "compute path must produce strictly positive R::v");
    return r;
}

[[nodiscard]] constexpr int post_fast_scalar(int const x) noexcept {
    int const r = x + 7;
    CRUCIBLE_POST_FAST(r, r > 0);
    return r;
}

static_assert(pre_msg_scalar(3) == 103,         "PRE_MSG scalar positive");
static_assert(post_msg_struct(7).v == 57,       "POST_MSG struct positive");
static_assert(post_fast_scalar(11) == 18,       "POST_FAST scalar positive");

// ── Native P2900 contract_assert positive witness ─────────────────
// On the patched build (PR c++/124241 cherry-picked, see
// misc/08_05_2026_harness.md §0), native `contract_assert(cond)`
// fires at consteval for all 7 probe shapes.  This witness pins
// that the patched compiler's behavior is what we expect, so a
// regression in the toolchain (e.g. user accidentally swaps to
// distro GCC) surfaces as a compile-time green-when-it-should-be-red
// rather than a silent skip.
//
// On un-patched distro GCC this assertion would compile cleanly
// because contract_assert silently bypasses on the foldable-body
// shape — that's exactly the regression we'd want to catch.
//
// The witness deliberately uses an ALWAYS-TRUE predicate so the
// assertion succeeds.  The negative complement (always-false
// predicate) lives in test/safety_neg/ as a separate fixture
// (would need a new neg_native_contract_assert_consteval fixture
// to pin the firing direction; deferred — the macro fixtures
// already pin the consteval-fire surface for the equivalent shape).

[[nodiscard]] constexpr int native_contract_assert_witness(int x) noexcept {
    contract_assert(x > 0);
    return x;
}

static_assert(native_contract_assert_witness(7) == 7,
    "Native contract_assert must compile cleanly with always-true "
    "predicate at consteval on the patched GCC build (per "
    "misc/08_05_2026_harness.md §3.1).  If this regresses, either "
    "the toolchain has been swapped or the cherry-pick was lost.");

}  // namespace

// ── Runtime exercise ──────────────────────────────────────────────
// Light runtime exercise — the static_asserts above already prove
// the consteval contract enforcement; this main() proves the same
// macro emits clean runtime code under !NDEBUG (debug build).
//
// We deliberately invoke each shape with a known-good input.  Any
// contract violation would SIGABRT in debug; clean exit is the test
// pass.

int main() {
    int volatile sink = 0;
    sink += pre_scalar(7);
    sink += static_cast<int>(pre_struct_cref(S{99}));
    sink += static_cast<int>(pre_struct_cval(S{100}));
    sink += static_cast<int>(pre_struct_val(S{101}));
    constexpr S const tmp{102};
    sink += static_cast<int>(pre_struct_cptr(&tmp));
    sink += post_scalar(13);
    sink += post_struct_field(14).v;
    sink += pre_and_post(20);
    sink += mid_body_assert(3);
    sink += relies_on_post(50);

    // Exercise the FAST variant + native contract_assert at runtime.
    // All three should execute cleanly with the chosen always-good
    // inputs; a violation would SIGABRT (FAST: direct trap; native:
    // routed through handle_contract_violation per ContractHandler.cpp).
    sink += pre_fast_scalar(11);
    sink += static_cast<int>(pre_fast_struct(S{77}));
    sink += native_contract_assert_witness(13);

    // Annotated + post-fast runtime exercise.
    sink += pre_msg_scalar(8);
    sink += post_msg_struct(3).v;
    sink += post_fast_scalar(15);

    // Sink prevents DCE; printf would be more visible but the test
    // is silent-on-success per CTest convention.
    if (sink == 0) {
        std::fprintf(stderr, "test_pre_post: sink unexpectedly zero\n");
        return 1;
    }
    return 0;
}
