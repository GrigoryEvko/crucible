#pragma once

#include <crucible/cntp/CongestionControl.h>
#include <crucible/effects/Capability.h>

namespace crucible::fixy::cap {

using ::crucible::effects::mint_cap;
using ::crucible::effects::mint_from_ctx;
using ::crucible::effects::Capability;

template <class Ctx, ::crucible::effects::Effect Cap>
concept CtxAdmitsCap = ::crucible::effects::IsExecCtx<Ctx>
                    && ::crucible::effects::row_contains_v<::crucible::effects::row_type_of_t<Ctx>, Cap>;

namespace detail {

template <class CapSource, ::crucible::effects::Effect E>
inline constexpr bool ctx_cap_source_has_nsdmi_v = false;

template <class CapSource>
inline constexpr bool ctx_cap_source_has_nsdmi_v<CapSource, ::crucible::effects::Effect::Alloc> =
    requires(CapSource const& src) { src.alloc; };

template <class CapSource>
inline constexpr bool ctx_cap_source_has_nsdmi_v<CapSource, ::crucible::effects::Effect::IO> =
    requires(CapSource const& src) { src.io; };

template <class CapSource>
inline constexpr bool ctx_cap_source_has_nsdmi_v<CapSource, ::crucible::effects::Effect::Block> =
    requires(CapSource const& src) { src.block; };

// A thread-effect tag has no field of its own. The cap source carries that
// authority by being the tag, so the check is type identity.

template <class CapSource>
inline constexpr bool ctx_cap_source_has_nsdmi_v<CapSource, ::crucible::effects::Effect::Bg> =
    std::is_same_v<CapSource, ::crucible::effects::Bg>;

template <class CapSource>
inline constexpr bool ctx_cap_source_has_nsdmi_v<CapSource, ::crucible::effects::Effect::Init> =
    std::is_same_v<CapSource, ::crucible::effects::Init>;

template <class CapSource>
inline constexpr bool ctx_cap_source_has_nsdmi_v<CapSource, ::crucible::effects::Effect::Test> =
    std::is_same_v<CapSource, ::crucible::effects::Test>;

}  // namespace detail

// The second leg is redundant for every well-formed context: such a context
// claims only effects its cap source permits, and the permitted set matches
// the cap source field layout. The leg stays as drift detection. It rejects a
// permitted-set change that misses a matching field change, and a synthetic
// context that satisfies the context concept without the well-formedness check.
template <class Ctx, ::crucible::effects::Effect Cap>
concept CtxAdmitsCapStrict =
    CtxAdmitsCap<Ctx, Cap> && detail::ctx_cap_source_has_nsdmi_v<::crucible::effects::cap_type_of_t<Ctx>, Cap>;

// Each of these trades a passkey for a fresh context. The passkey
// constructors are private and friend-restricted, so only their friends can
// call them. The re-exports exist to keep every authorization point on one
// grep target.
using ::crucible::effects::mint_bg_context;
using ::crucible::effects::mint_init_context;
using ::crucible::effects::mint_test_context;

}  // namespace crucible::fixy::cap

namespace crucible::fixy::cap::cntp {

using ::crucible::cntp::mint_cc_choice;
using ::crucible::cntp::mint_custom_cc_choice;

using ::crucible::cntp::CcCompatible;
using ::crucible::cntp::CustomCcModule;

using ::crucible::cntp::CcAlgorithm;
using ::crucible::cntp::CcSelection;
using ::crucible::cntp::DeclaredCcChoice;
using ::crucible::cntp::LinkClass;

}  // namespace crucible::fixy::cap::cntp

namespace crucible::fixy::cap::self_test {

template <::crucible::effects::Effect E, class Source>
inline constexpr bool same_mint_cap_v = std::is_same_v<decltype(&::crucible::fixy::cap::mint_cap<E, Source>),
                                                       decltype(&::crucible::effects::mint_cap<E, Source>)>;

template <::crucible::effects::Effect E, class Ctx>
inline constexpr bool same_mint_from_ctx_v = std::is_same_v<decltype(&::crucible::fixy::cap::mint_from_ctx<E, Ctx>),
                                                            decltype(&::crucible::effects::mint_from_ctx<E, Ctx>)>;

static_assert(same_mint_cap_v<::crucible::effects::Effect::Alloc, ::crucible::effects::Bg>,
              "fixy::cap::mint_cap must alias the substrate function — the "
              "using-decl did not introduce a new overload (Alloc row).");
static_assert(same_mint_cap_v<::crucible::effects::Effect::IO, ::crucible::effects::Bg>,
              "fixy::cap::mint_cap must alias the substrate function (IO row).");

static_assert(same_mint_from_ctx_v<::crucible::effects::Effect::Alloc, ::crucible::effects::BgDrainCtx>,
              "fixy::cap::mint_from_ctx must alias the substrate function.");

static_assert(
    std::is_same_v<::crucible::fixy::cap::Capability<::crucible::effects::Effect::Alloc, ::crucible::effects::Bg>,
                   ::crucible::effects::Capability<::crucible::effects::Effect::Alloc, ::crucible::effects::Bg>>,
    "fixy::cap::Capability must alias effects::Capability.");

// Each cap-context mint is a function template gated on its passkey type, so
// identity is taken at the one instantiation whose key satisfies the gate.
static_assert(
    std::is_same_v<decltype(&::crucible::fixy::cap::mint_bg_context<::crucible::effects::detail::ctx_mint::bg_key>),
                   decltype(&::crucible::effects::mint_bg_context<::crucible::effects::detail::ctx_mint::bg_key>)>,
    "fixy::cap::mint_bg_context must alias effects::mint_bg_context.");
static_assert(
    std::is_same_v<decltype(&::crucible::fixy::cap::mint_init_context<::crucible::effects::detail::ctx_mint::init_key>),
                   decltype(&::crucible::effects::mint_init_context<::crucible::effects::detail::ctx_mint::init_key>)>,
    "fixy::cap::mint_init_context must alias effects::mint_init_context.");
static_assert(
    std::is_same_v<decltype(&::crucible::fixy::cap::mint_test_context<::crucible::effects::detail::ctx_mint::test_key>),
                   decltype(&::crucible::effects::mint_test_context<::crucible::effects::detail::ctx_mint::test_key>)>,
    "fixy::cap::mint_test_context must alias effects::mint_test_context.");

static_assert(
    std::is_same_v<decltype(&::crucible::fixy::cap::cntp::mint_cc_choice<::crucible::cntp::CcAlgorithm::Cubic,
                                                                         ::crucible::cntp::LinkClass::CrossDatacenter>),
                   decltype(&::crucible::cntp::mint_cc_choice<::crucible::cntp::CcAlgorithm::Cubic,
                                                              ::crucible::cntp::LinkClass::CrossDatacenter>)>,
    "fixy::cap::cntp::mint_cc_choice must alias cntp::mint_cc_choice.");

static_assert(std::is_same_v<::crucible::fixy::cap::cntp::DeclaredCcChoice, ::crucible::cntp::DeclaredCcChoice>,
              "fixy::cap::cntp::DeclaredCcChoice must alias cntp::DeclaredCcChoice.");

static_assert(::crucible::fixy::cap::CtxAdmitsCap<::crucible::effects::BgDrainCtx, ::crucible::effects::Effect::Bg>,
              "BgDrainCtx::row = Row<Bg, Alloc> — CtxAdmitsCap must "
              "accept Effect::Bg (atom present in the claimed row).");
static_assert(::crucible::fixy::cap::CtxAdmitsCap<::crucible::effects::BgDrainCtx, ::crucible::effects::Effect::Alloc>,
              "BgDrainCtx::row = Row<Bg, Alloc> — CtxAdmitsCap must "
              "accept Effect::Alloc (atom present in the claimed row).");
static_assert(!::crucible::fixy::cap::CtxAdmitsCap<::crucible::effects::BgDrainCtx, ::crucible::effects::Effect::IO>,
              "BgDrainCtx::row = Row<Bg, Alloc> — CtxAdmitsCap must "
              "REJECT Effect::IO (not claimed in row, even though Bg permits it; "
              "this is the row-axis check, not the permitted-row-axis check).");
static_assert(!::crucible::fixy::cap::CtxAdmitsCap<::crucible::effects::HotFgCtx, ::crucible::effects::Effect::Alloc>,
              "HotFgCtx::row = Row<> (empty) — CtxAdmitsCap must "
              "REJECT every Effect (no row claim ⇒ no row-axis authority).");
static_assert(!::crucible::fixy::cap::CtxAdmitsCap<int, ::crucible::effects::Effect::Alloc>,
              "int is not an ExecCtx — IsExecCtx<int> = false short-"
              "circuits the row-axis clause; the concept evaluates to false "
              "without hard-erroring on row_type_of_t<int>.");

static_assert(
    ::crucible::fixy::cap::CtxAdmitsCapStrict<::crucible::effects::BgDrainCtx, ::crucible::effects::Effect::Alloc>,
    "BgDrainCtx claims Alloc in row AND Bg cap source has "
    "alloc NSDMI ⇒ CtxAdmitsCapStrict accepts.");
static_assert(
    ::crucible::fixy::cap::CtxAdmitsCapStrict<::crucible::effects::BgCompileCtx, ::crucible::effects::Effect::IO>,
    "BgCompileCtx claims IO in row AND Bg cap source has "
    "io NSDMI ⇒ CtxAdmitsCapStrict accepts.");
static_assert(
    ::crucible::fixy::cap::CtxAdmitsCapStrict<::crucible::effects::BgDrainCtx, ::crucible::effects::Effect::Bg>,
    "BgDrainCtx claims Bg in row AND cap source IS Bg ⇒ "
    "CtxAdmitsCapStrict accepts (thread-effect tag path).");
static_assert(
    !::crucible::fixy::cap::CtxAdmitsCapStrict<::crucible::effects::HotFgCtx, ::crucible::effects::Effect::Alloc>,
    "HotFgCtx::row = Row<> ⇒ CtxAdmitsCap fails ⇒ strict "
    "variant fails by conjunction (no row claim).");
static_assert(
    !::crucible::fixy::cap::CtxAdmitsCapStrict<::crucible::effects::BgDrainCtx, ::crucible::effects::Effect::Init>,
    "BgDrainCtx cap source is Bg, not Init — even if a "
    "synthetic row claimed Init the cap-source-NSDMI leg would still "
    "fail (defense-in-depth against thread-effect tag confusion).");

static_assert(::crucible::fixy::cap::detail::ctx_cap_source_has_nsdmi_v<::crucible::effects::Bg,
                                                                        ::crucible::effects::Effect::Alloc>,
              "Bg cap source must expose `alloc` NSDMI field.");
static_assert(
    ::crucible::fixy::cap::detail::ctx_cap_source_has_nsdmi_v<::crucible::effects::Bg, ::crucible::effects::Effect::IO>,
    "Bg cap source must expose `io` NSDMI field.");
static_assert(::crucible::fixy::cap::detail::ctx_cap_source_has_nsdmi_v<::crucible::effects::Bg,
                                                                        ::crucible::effects::Effect::Block>,
              "Bg cap source must expose `block` NSDMI field.");
static_assert(::crucible::fixy::cap::detail::ctx_cap_source_has_nsdmi_v<::crucible::effects::Init,
                                                                        ::crucible::effects::Effect::Alloc>,
              "Init cap source must expose `alloc` NSDMI field.");
static_assert(::crucible::fixy::cap::detail::ctx_cap_source_has_nsdmi_v<::crucible::effects::Init,
                                                                        ::crucible::effects::Effect::IO>,
              "Init cap source must expose `io` NSDMI field.");
static_assert(!::crucible::fixy::cap::detail::ctx_cap_source_has_nsdmi_v<::crucible::effects::Init,
                                                                         ::crucible::effects::Effect::Block>,
              "Init cap source MUST NOT expose `block` NSDMI field — "
              "Init's permitted_row is {Init, Alloc, IO}, no Block; this pin "
              "catches NSDMI drift if Init ever gains a block field without a "
              "permitted_row update.");

}  // namespace crucible::fixy::cap::self_test
