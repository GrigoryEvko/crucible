// SPDX-License-Identifier: Apache-2.0
//
// test/test_fixy_profile.cpp
//
// FIXY-D Phase D sentinel — pins the sketch/release profile toggle
// in include/crucible/fixy/Profile.h.  Acceptance per
// misc/16_05_2026_fixy.md §4 Phase D: the toggle must compile clean
// in both strict (default) and sketch modes.
//
// This test runs in DEFAULT (strict) mode.  A sister build under
// CRUCIBLE_FIXY_SKETCH would exercise the sketch path; we pin the
// strict resolution here since that's the production default.

#include <crucible/fixy/Profile.h>

#include <cstdio>
#include <type_traits>

namespace {

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;

// ─── 1. IsAcceptedStrict — identical to IsAccepted ────────────────────

static_assert(cf::IsAcceptedStrict<> == cf::IsAccepted<>);

// Empty pack: rejected under strict.
static_assert(!cf::IsAcceptedStrict<>);

// Full AllStrict pack: accepted under strict.
static_assert(cf::IsAcceptedStrict<
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>>);

// ─── 2. IsAcceptedSketch — accepts any pack ───────────────────────────
//
// Sketch profile is permissive — even an empty pack satisfies.
// The discipline-surface promise: same code that fails strict
// fails-as-warning under sketch but still type-checks.

static_assert(cf::IsAcceptedSketch<>);
static_assert(cf::IsAcceptedSketch<cg::copy>);
static_assert(cf::IsAcceptedSketch<int, float, double>);

// ─── 3. IsAcceptedSelected — preprocessor-toggled ─────────────────────
//
// Default (no CRUCIBLE_FIXY_SKETCH) → IsAcceptedStrict.  Empty pack
// is rejected.

#ifndef CRUCIBLE_FIXY_SKETCH
static_assert(!cf::IsAcceptedSelected<>);
#else
static_assert(cf::IsAcceptedSelected<>);
#endif

// ─── 4. strict_fn profile-pin — identical to fn<> ─────────────────────
//
// `strict_fn` is the production hot-path entry point — it always uses
// the strict gate even under CRUCIBLE_FIXY_SKETCH, so security-critical
// bindings can never be silenced by a sketch build.

using StrictInt = cf::strict_fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>>;

using FnInt = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>>;

static_assert(std::is_same_v<StrictInt, FnInt>,
    "strict_fn must be an exact alias of fn — pinning the profile must "
    "not introduce a structural difference.");
static_assert(sizeof(StrictInt) == sizeof(int));

// ─── 5. sketch_fn — always compiles, marks strict-rejection shapes ────
//
// `sketch_fn<int>` with NO grants compiles (sketch is permissive); a
// `value()` call on this binding emits a deprecation warning at the
// call site.  We pin the structural shape here without invoking
// `value()` (so the test TU itself stays warning-free under
// -Wdeprecated-declarations).

using SketchEmpty = cf::sketch_fn<int>;
static_assert(!SketchEmpty::strict_would_accept,
    "sketch_fn<int> with empty grants must report strict_would_accept=false.");

using SketchAllStrict = cf::sketch_fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>>;

static_assert(SketchAllStrict::strict_would_accept,
    "sketch_fn with AllStrict pack must report strict_would_accept=true; "
    "value() must NOT emit the deprecation warning.");

// EBO collapse: clean sketch binding adds no bytes.
static_assert(sizeof(SketchAllStrict) == sizeof(int),
    "sketch_fn must EBO-collapse for clean (strict-accepted) bindings.");

}  // namespace

int main() {
    std::printf("fixy profile sentinel: strict=%d sketch=%d\n",
                cf::IsAcceptedStrict<cf::accept_default_strict_for<cd::Type>,
                    cf::accept_default_strict_for<cd::Refinement>,
                    cf::accept_default_strict_for<cd::Usage>,
                    cf::accept_default_strict_for<cd::Effect>,
                    cf::accept_default_strict_for<cd::Security>,
                    cf::accept_default_strict_for<cd::Protocol>,
                    cf::accept_default_strict_for<cd::Lifetime>,
                    cf::accept_default_strict_for<cd::Provenance>,
                    cf::accept_default_strict_for<cd::Trust>,
                    cf::accept_default_strict_for<cd::Representation>,
                    cf::accept_default_strict_for<cd::Observability>,
                    cf::accept_default_strict_for<cd::Complexity>,
                    cf::accept_default_strict_for<cd::Precision>,
                    cf::accept_default_strict_for<cd::Space>,
                    cf::accept_default_strict_for<cd::Overflow>,
                    cf::accept_default_strict_for<cd::Mutation>,
                    cf::accept_default_strict_for<cd::Reentrancy>,
                    cf::accept_default_strict_for<cd::Size>,
                    cf::accept_default_strict_for<cd::Version>,
                    cf::accept_default_strict_for<cd::Staleness>> ? 1 : 0,
                cf::IsAcceptedSketch<> ? 1 : 0);
    return 0;
}
