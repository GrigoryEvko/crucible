// SPDX-License-Identifier: Apache-2.0
//
// test/test_fixy_b.cpp
//
// FIXY-B integration test — proves Phase B's Resolve + Fn + Stance +
// Rules ship a coherent end-to-end pipeline: a fixy::fn<Type,
// Grants...> binding compiles down to a safety::fn::Fn<...> with the
// correct resolved parameters, the engagement gate fires, the
// underlying ValidComposition gate fires, and stance aliases produce
// identical types to fully-expanded equivalents.

#include <crucible/fixy/Fn.h>
#include <crucible/fixy/Resolve.h>
#include <crucible/fixy/Rules.h>
#include <crucible/fixy/Stance.h>

#include <cstdint>
#include <cstdio>
#include <tuple>
#include <type_traits>

namespace {

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace cs = crucible::fixy::stance;
namespace cr = crucible::fixy::rule;
namespace ce = crucible::effects;
namespace sfn = crucible::safety::fn;

// ─── 1. Resolve: AllStrictAcceptPack → Fn<int> with substrate defaults
//
// The canonical "everything strict" pack must produce
// safety::fn::Fn<int> with NO non-default parameters.
using R1 = cf::resolve::resolved_fn_t<int,
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

static_assert(std::is_same_v<R1, sfn::Fn<int>>,
    "Phase B: AllStrict on int resolves to substrate defaults.");

// ─── 2. Resolve: single relaxation flows through ──────────────────────
static_assert(R1::usage_v == sfn::UsageMode::Linear);

using R2 = cf::resolve::resolved_fn_t<int,
    cg::copy,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
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

static_assert(R2::usage_v == sfn::UsageMode::Copy);

// ─── 3. Resolve: Effect row composition ───────────────────────────────
using R3 = cf::resolve::resolved_fn_t<float,
    cg::with<ce::Effect::IO, ce::Effect::Bg>,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
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

static_assert(std::is_same_v<typename R3::effect_row_t,
                             ce::Row<ce::Effect::IO, ce::Effect::Bg>>);

// ─── 4. fixy::fn EBO zero-cost ────────────────────────────────────────
auto v_pure = cs::mint_fn_for<cs::PureLinear>(42);
auto v_copy = cs::mint_fn_for<cs::PureCopy>(43);
auto v_io   = cs::mint_fn_for<cs::IoFunction>(44);
auto v_bg   = cs::mint_fn_for<cs::BgWorker>(45);

static_assert(sizeof(v_pure) == sizeof(int));
static_assert(sizeof(v_copy) == sizeof(int));
static_assert(sizeof(v_io)   == sizeof(int));
static_assert(sizeof(v_bg)   == sizeof(int));

// ─── 5. Stance produces correct underlying Fn ─────────────────────────
//
// PureCopy: Usage = Copy.
static_assert(decltype(v_copy)::usage_v == sfn::UsageMode::Copy);
static_assert(decltype(v_pure)::usage_v == sfn::UsageMode::Linear);

// IoFunction: Effect row contains IO.
static_assert(std::is_same_v<typename decltype(v_io)::effect_row_t,
                             ce::Row<ce::Effect::IO>>);

// BgWorker: Effect row contains Bg + Alloc + Block.
static_assert(std::is_same_v<typename decltype(v_bg)::effect_row_t,
                             ce::Row<ce::Effect::Bg,
                                     ce::Effect::Alloc,
                                     ce::Effect::Block>>);

// ─── 6. Stance-bound mint produces identical type to hand-expanded ────
//
// `mint_fn_for<PureCopy>(int)` produces the same fn<...> type as
// manually passing the 20 tags through mint_fn.  Pin via std::is_same_v.

using HandExpandedPureCopy = cf::fn<int,
    cg::copy,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
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

// AllStrict.h orders accepts by enumerator value: Type=0, Refinement=1,
// Usage=2.  PureCopy replaces dim::Usage's accept with grant::copy.
// Result: positionally identical to the hand-expanded form because
// replace_accept_in_pack keeps positions stable; only the Usage slot
// content differs.  Both produce the same fn::Fn<...> underlying.
static_assert(std::is_same_v<typename decltype(v_copy)::underlying_fn_t,
                             typename HandExpandedPureCopy::underlying_fn_t>,
    "mint_fn_for<PureCopy>(int) must produce the same underlying Fn "
    "as the hand-expanded 20-tag pack.");

// ─── 7. Rules: 12 collision tags re-exported correctly ───────────────
static_assert(std::tuple_size_v<cr::Catalog> == 12);
static_assert(std::is_same_v<cr::M012,
                             sfn::collision::M012_MonotonicConcurrentNoAtomic>);
static_assert(std::is_same_v<cr::I002,
                             sfn::collision::I002_ClassifiedFailPayload>);

// ─── 8. SecretConsumer is structurally identical to PureLinear ───────
//
// SecretConsumer's documented intent is "consume Classified, never
// emit publicly".  No relaxation; same tuple as PureLinear.  Pin
// identity.
static_assert(std::is_same_v<cs::SecretConsumer, cs::PureLinear>);

// ─── 9. PublicEmit<Policy> relaxes Security ──────────────────────────
struct AuditedPolicy {};
auto v_emit = cs::mint_fn_for<cs::PublicEmit<AuditedPolicy>>(99);

// PublicEmit places grant::declassify<Policy> at dim::Security's slot;
// the resolver maps declassify → SecLevel::Public.
static_assert(decltype(v_emit)::security_v == sfn::SecLevel::Public);
static_assert(sizeof(v_emit) == sizeof(int));

}  // namespace

// ─── Runtime smoke driver ────────────────────────────────────────────
//
// Exercise the value() forwarder + sizeof under non-constant
// arguments; pin that the wrapper is a pure passthrough at runtime.
int main() {
    volatile int input = 123;
    auto x = cs::mint_fn_for<cs::PureCopy>(static_cast<int>(input));
    if (x.value() != 123) {
        std::fprintf(stderr, "test_fixy_b: value() forwarder failed\n");
        return 1;
    }

    int next = input + 1;
    auto y = cs::mint_fn_for<cs::BgWorker>(next);
    if (y.value() != 124) {
        std::fprintf(stderr, "test_fixy_b: BgWorker stance value() failed\n");
        return 1;
    }

    // Pin RuleCode bijection at runtime.
    volatile int code = static_cast<int>(cr::RuleCode::M012);
    if (code != static_cast<int>(sfn::collision::RuleCode::M012)) {
        std::fprintf(stderr, "test_fixy_b: RuleCode bijection broken\n");
        return 1;
    }
    return 0;
}
