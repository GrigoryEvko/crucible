// The re-export surface must be a namespace alias and not a pile of
// using-declarations. Each assertion below pins a symbol reached through
// the alias to the substrate symbol itself. A redeclaration that drifts
// from the substrate fails here at header-inclusion time.

#include <crucible/fixy/Eff.h>

#include <type_traits>

namespace fe = ::crucible::fixy::eff;
namespace ce = ::crucible::effects;

static_assert(std::is_same_v<fe::Bg, ce::Bg>, "fe::Bg must be ce::Bg under the namespace alias.");
static_assert(std::is_same_v<fe::Init, ce::Init>, "fe::Init must be ce::Init under the namespace alias.");
static_assert(std::is_same_v<fe::Test, ce::Test>, "fe::Test must be ce::Test under the namespace alias.");

static_assert(std::is_same_v<fe::Alloc, ce::Alloc>);
static_assert(std::is_same_v<fe::IO, ce::IO>);
static_assert(std::is_same_v<fe::Block, ce::Block>);

static_assert(std::is_same_v<fe::cap::Alloc, ce::cap::Alloc>);
static_assert(std::is_same_v<fe::cap::IO, ce::cap::IO>);
static_assert(std::is_same_v<fe::cap::Block, ce::cap::Block>);

static_assert(std::is_same_v<fe::Row<fe::Effect::Alloc, fe::Effect::IO>, ce::Row<ce::Effect::Alloc, ce::Effect::IO>>);

static_assert(std::is_same_v<fe::Computation<fe::Row<>, int>, ce::Computation<ce::Row<>, int>>);

static_assert(std::is_same_v<fe::Capability<fe::Effect::Alloc, fe::Bg>, ce::Capability<ce::Effect::Alloc, ce::Bg>>);

static_assert(std::is_same_v<fe::HotFgCtx, ce::HotFgCtx>);
static_assert(std::is_same_v<fe::BgDrainCtx, ce::BgDrainCtx>);
static_assert(std::is_same_v<fe::BgCompileCtx, ce::BgCompileCtx>);
static_assert(std::is_same_v<fe::ColdInitCtx, ce::ColdInitCtx>);
static_assert(std::is_same_v<fe::TestRunnerCtx, ce::TestRunnerCtx>);

static_assert(std::is_same_v<fe::PureRow, ce::PureRow>);
static_assert(std::is_same_v<fe::TotRow, ce::TotRow>);
static_assert(std::is_same_v<fe::GhostRow, ce::GhostRow>);
static_assert(std::is_same_v<fe::DivRow, ce::DivRow>);
static_assert(std::is_same_v<fe::STRow, ce::STRow>);
static_assert(std::is_same_v<fe::AllRow, ce::AllRow>);

// A redeclared Effect would be a separate enum type with its own values,
// and the equalities below would not compile at all, because there is no
// implicit conversion between two enum types. Under the alias both sides
// name one enumerator.

static_assert(fe::Effect::Alloc == ce::Effect::Alloc);
static_assert(fe::Effect::IO == ce::Effect::IO);
static_assert(fe::Effect::Block == ce::Effect::Block);
static_assert(fe::Effect::Bg == ce::Effect::Bg);
static_assert(fe::Effect::Init == ce::Effect::Init);
static_assert(fe::Effect::Test == ce::Effect::Test);

// Comparing the two addresses is the direct witness, but under the alias
// both sides name one object and the compiler rejects the comparison as
// tautological. Value equality plus type identity is the checkable form,
// and a redeclaration would break one or the other.

static_assert(fe::effect_count == ce::effect_count);
static_assert(
    std::is_same_v<std::remove_const_t<decltype(fe::effect_count)>, std::remove_const_t<decltype(ce::effect_count)>>);

static_assert(std::is_same_v<fe::SmBudget<1>, ce::SmBudget<1>>);
static_assert(std::is_same_v<fe::HbmBytes<4096>, ce::HbmBytes<4096>>);

namespace detail_alias_check {}  // namespace detail_alias_check

int main() { return 0; }
