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
