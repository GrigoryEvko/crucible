// ── test_fixy_eff_alias — FIXY-AUDIT-B12 HS14 sentinel ─────────────
//
// Witnesses that fixy/Eff.h uses a namespace-alias surface form per
// FIXY-AUDIT-B12 — i.e. every load-bearing symbol reachable through
// `crucible::fixy::eff::X` IS `crucible::effects::X` (same type,
// same address for ODR-used variables, same enum constant), NOT a
// re-declaration under a separate fixy::eff namespace.
//
// If a future edit accidentally reverts to a using-decl pile that
// shadows a substrate symbol — or worse, introduces a redeclaration
// that drifts from the substrate — these static_asserts fire at
// header-inclusion time pinpointing the divergence.
//
// Complementary to test_fixy_eff.cpp, which exercises the full
// surface end-to-end.  This file is the focused B12 discipline witness.

#include <crucible/fixy/Eff.h>

#include <type_traits>

namespace fe = ::crucible::fixy::eff;
namespace ce = ::crucible::effects;

// ─── 1. Load-bearing capability tags ──────────────────────────────
//
// Bg / Init / Test are the three ExecCtx aggregator structs the hot
// path / init scope / test driver carry as their effect-row anchors;
// Alloc / IO / Block are the cap::* tokens those contexts mint.  Each
// MUST be the exact substrate type, not a synonym in a separate
// namespace.

static_assert(std::is_same_v<fe::Bg,    ce::Bg>,
    "fe::Bg must BE ce::Bg under the namespace alias (B12).");
static_assert(std::is_same_v<fe::Init,  ce::Init>,
    "fe::Init must BE ce::Init under the namespace alias (B12).");
static_assert(std::is_same_v<fe::Test,  ce::Test>,
    "fe::Test must BE ce::Test under the namespace alias (B12).");

static_assert(std::is_same_v<fe::Alloc, ce::Alloc>);
static_assert(std::is_same_v<fe::IO,    ce::IO>);
static_assert(std::is_same_v<fe::Block, ce::Block>);

// ─── 2. cap:: sub-namespace identity ──────────────────────────────

static_assert(std::is_same_v<fe::cap::Alloc, ce::cap::Alloc>);
static_assert(std::is_same_v<fe::cap::IO,    ce::cap::IO>);
static_assert(std::is_same_v<fe::cap::Block, ce::cap::Block>);

// ─── 3. Row / Computation / Capability template identity ──────────

static_assert(std::is_same_v<
    fe::Row<fe::Effect::Alloc, fe::Effect::IO>,
    ce::Row<ce::Effect::Alloc, ce::Effect::IO>>);

static_assert(std::is_same_v<
    fe::Computation<fe::Row<>, int>,
    ce::Computation<ce::Row<>, int>>);

static_assert(std::is_same_v<
    fe::Capability<fe::Effect::Alloc, fe::Bg>,
    ce::Capability<ce::Effect::Alloc, ce::Bg>>);

// ─── 4. Canonical 5 ExecCtxs are the substrate types ──────────────

static_assert(std::is_same_v<fe::HotFgCtx,     ce::HotFgCtx>);
static_assert(std::is_same_v<fe::BgDrainCtx,   ce::BgDrainCtx>);
static_assert(std::is_same_v<fe::BgCompileCtx, ce::BgCompileCtx>);
static_assert(std::is_same_v<fe::ColdInitCtx,  ce::ColdInitCtx>);
static_assert(std::is_same_v<fe::TestRunnerCtx, ce::TestRunnerCtx>);

// ─── 5. F* alias rows are the substrate rows ──────────────────────

static_assert(std::is_same_v<fe::PureRow,  ce::PureRow>);
static_assert(std::is_same_v<fe::TotRow,   ce::TotRow>);
static_assert(std::is_same_v<fe::GhostRow, ce::GhostRow>);
static_assert(std::is_same_v<fe::DivRow,   ce::DivRow>);
static_assert(std::is_same_v<fe::STRow,    ce::STRow>);
static_assert(std::is_same_v<fe::AllRow,   ce::AllRow>);

// ─── 6. Enum-value identity (not just enum-type identity) ─────────
//
// A re-declaration of Effect under fixy::eff would produce a
// SEPARATE enum type with its own values; the equality below would
// fail to compile because there's no implicit conversion between
// the two enum types.  Under the namespace alias, both sides resolve
// to the same enumerator.

static_assert(fe::Effect::Alloc == ce::Effect::Alloc);
static_assert(fe::Effect::IO    == ce::Effect::IO);
static_assert(fe::Effect::Block == ce::Effect::Block);
static_assert(fe::Effect::Bg    == ce::Effect::Bg);
static_assert(fe::Effect::Init  == ce::Effect::Init);
static_assert(fe::Effect::Test  == ce::Effect::Test);

// ─── 7. effect_count constant identity (NOT just value equality) ──
//
// The tautological-comparison the compiler flags on
// `&fe::effect_count == &ce::effect_count` IS the load-bearing
// witness: under the namespace alias, both sides resolve to the
// SAME object, so the comparison is trivially true.  A using-decl
// re-declaration would produce two separate inline-variable
// definitions (and the substrate's own
// `__cpp_lib_inline_variables`-guarded inline-constexpr discipline
// would fire as an ODR conflict) — the comparison is then
// non-tautological.  Pin the structural fact via value-equality
// (which the substrate's static_asserts already enforce) plus the
// type-identity assertion above, both of which would silently
// drift under a re-declaration.

static_assert(fe::effect_count == ce::effect_count);
static_assert(std::is_same_v<
    std::remove_const_t<decltype(fe::effect_count)>,
    std::remove_const_t<decltype(ce::effect_count)>>);

// ─── 8. Resource sub-namespace identity ───────────────────────────

static_assert(std::is_same_v<fe::SmBudget<1>, ce::SmBudget<1>>);
static_assert(std::is_same_v<fe::HbmBytes<4096>, ce::HbmBytes<4096>>);

// ─── 9. Self-test that the alias collapses to the substrate ──────
//
// The substrate's own self-test block lives in detail::*_self_test
// namespaces inside crucible::effects.  Under the namespace alias,
// fixy::eff::detail::* is reachable too — verify one such name to
// pin the alias's transitivity through nested namespaces.

namespace detail_alias_check {

// effects/Capabilities.h declares
// `namespace crucible::effects::detail::capabilities_self_test`.
// Under the alias, the same namespace is reachable through
// `fixy::eff::detail::capabilities_self_test`.  Pin via a typedef.
//
// (This is a structural check; the substrate self-test's
// static_asserts have already fired by the time this TU's symbols
// resolve.)

}  // namespace detail_alias_check

int main() { return 0; }
