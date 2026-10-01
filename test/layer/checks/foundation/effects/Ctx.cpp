// The compile-time checks of foundation/effects/Ctx.h.

#include <foundation/effects/Ctx.h>

namespace foundation::effects {

namespace detail::ctx_witnesses {

static_assert(std::is_same_v<HotFgCtx, ExecCtx<ctx_cap::Fg, Row<>>>);

static_assert(std::is_same_v<BgDrainCtx, ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc>>>);

static_assert(std::is_same_v<BgCompileCtx, ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc, Effect::IO>>>);

static_assert(std::is_same_v<BgLoadCtx, ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>>>);

static_assert(std::is_same_v<ColdInitCtx, ExecCtx<Init, Row<Effect::Init, Effect::Alloc, Effect::IO>>>);

static_assert(std::is_same_v<InitLoadCtx, ExecCtx<Init, Row<Effect::Init, Effect::Alloc, Effect::IO, Effect::Block>>>);

static_assert(
    std::is_same_v<TestRunnerCtx, ExecCtx<Test, Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>>>);

static_assert(sizeof(ExecCtx<>) == 1, "Both axes of ExecCtx are empty types, so the whole context must be 1 byte.");
static_assert(sizeof(FgWitness) == 1);
static_assert(sizeof(BgWitness) == 1);
static_assert(sizeof(BgIoWitness) == 1);
static_assert(sizeof(BgBlockWitness) == 1);
static_assert(sizeof(InitWitness) == 1);
static_assert(sizeof(InitBlockWitness) == 1);
static_assert(sizeof(TestWitnessCtx) == 1);

// A context that holds a capability source is evidence, so no route
// builds one without a constructor.  The background, init and test
// sources have no trivial constructor, so the copy of the context is not
// trivial either, and the one constructor that is not a copy takes a
// source.  std::bit_cast and std::start_lifetime_as then refuse the
// context.
template <class Ctx>
inline constexpr bool is_context_forgeable_v = std::is_trivially_copyable_v<Ctx> || std::is_implicit_lifetime_v<Ctx>;

static_assert(!is_context_forgeable_v<BgWitness> && !is_context_forgeable_v<BgBlockWitness>
                  && !is_context_forgeable_v<InitWitness> && !is_context_forgeable_v<InitBlockWitness>
                  && !is_context_forgeable_v<TestWitnessCtx>,
              "An execution context over Bg, Init or Test must have no trivial constructor, or std::bit_cast "
              "builds it from a byte and every ctx-bound gate admits the forged scope.");

// The foreground source keeps a trivial copy constructor, so that the
// hot path passes the context in no register.  That leaves it an
// implicit-lifetime type, and the annotation on the source is what the
// checked lifetime start refuses.  std::bit_cast is refused by the
// user-provided copy assignment, and the default constructor is gone.
static_assert(!std::is_trivially_copyable_v<FgWitness>,
              "The foreground context must not be trivially copyable, or std::bit_cast builds it from a byte.");
static_assert(std::is_trivially_copy_constructible_v<FgWitness>,
              "The foreground context must copy trivially, so that the hot path passes it for free.");
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<FgWitness>
                  && !::foundation::lifetime::ImplicitLifetimeThroughout<ctx_cap::Fg>,
              "The checked lifetime start must refuse the foreground context and its source.");
static_assert(!std::is_default_constructible_v<FgWitness> && !std::is_default_constructible_v<ctx_cap::Fg>
                  && !std::is_default_constructible_v<detail::ctx_mint::fg_key>,
              "A foreground context is built from the key of the producer claim, never from nothing.");

// A branded foreground context obeys every rule of the unbranded one, and
// its source is built only by the claim of its brand.  It passes a gate
// that asks for no brand, and no gate of another brand.
struct BrandWitness {};
struct OtherBrandWitness {};
using BrandedFgWitness = ExecCtx<ctx_cap::BrandedFg<BrandWitness>, Row<>>;
using OtherBrandedFgWitness = ExecCtx<ctx_cap::BrandedFg<OtherBrandWitness>, Row<>>;
static_assert(IsCapType<ctx_cap::BrandedFg<BrandWitness>> && IsBrandedForeground<ctx_cap::BrandedFg<BrandWitness>>);
static_assert(!IsBrandedForeground<ctx_cap::Fg> && !IsBrandedForeground<Bg> && !IsBrandedForeground<int>);
static_assert(sizeof(BrandedFgWitness) == 1);
static_assert(std::is_same_v<cap_permitted_row_t<ctx_cap::BrandedFg<BrandWitness>>, Row<>>);
static_assert(!std::is_copy_constructible_v<BrandedFgWitness> && !std::is_move_constructible_v<BrandedFgWitness>
                  && !std::is_copy_assignable_v<BrandedFgWitness> && !std::is_move_assignable_v<BrandedFgWitness>,
              "A branded foreground context must have no copy and no move, or a by-value capture or a thread "
              "argument takes it to a thread that holds no claim.");
static_assert(!std::is_trivially_copyable_v<BrandedFgWitness>
                  && !std::is_trivially_copyable_v<ctx_cap::BrandedFg<BrandWitness>>,
              "A branded foreground context must not be trivially copyable, or std::bit_cast builds it from a byte.");
static_assert(!std::is_constructible_v<BrandedFgWitness, detail::ctx_mint::fg_key>,
              "Only a producer claim and the test witness build a branded context from the key.");
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<BrandedFgWitness>
                  && !::foundation::lifetime::ImplicitLifetimeThroughout<ctx_cap::BrandedFg<BrandWitness>>,
              "The checked lifetime start must refuse the branded foreground context and its source.");
static_assert(!std::is_default_constructible_v<BrandedFgWitness>
                  && !std::is_default_constructible_v<ctx_cap::BrandedFg<BrandWitness>>
                  && !std::is_constructible_v<ctx_cap::BrandedFg<BrandWitness>, detail::ctx_mint::fg_key>,
              "A branded source is built only by the claim of its brand.");
static_assert(!std::is_constructible_v<FgWitness, BrandedFgWitness>,
              "No route forgets a brand.  Any translation unit can declare a brand and hold its claim, so a "
              "context with no brand names no single-producer state and is not evidence.");
static_assert(!std::is_constructible_v<BrandedFgWitness, FgWitness>, "No route adds a brand to a context.");
static_assert(!std::is_constructible_v<OtherBrandedFgWitness, BrandedFgWitness>,
              "A context of one brand passes no gate of another brand.");
static_assert(!std::is_default_constructible_v<host::ProducerClaim<BrandWitness>>,
              "Only the brand builds its producer claim.");
static_assert(!std::is_trivially_copyable_v<host::ProducerClaim<BrandWitness>>
                  && !std::is_implicit_lifetime_v<host::ProducerClaim<BrandWitness>>,
              "No producer claim is built from bytes or started over a buffer.");
static_assert(!std::is_constructible_v<ctx_cap::Fg, const ctx_cap::BrandedFg<BrandWitness>&>,
              "A branded source does not become the unbranded source.");

// Every axis defaults to the claim-nothing end of its range.
static_assert(std::is_same_v<typename ExecCtx<>::cap_type, ctx_cap::Fg>);
static_assert(std::is_same_v<typename ExecCtx<>::row_type, Row<>>);

using BgVariantA = ExecCtx<Bg, Row<Effect::Bg>>;
using BgVariantB = ExecCtx<Bg, Row<Effect::Bg, Effect::IO>>;
static_assert(!std::is_same_v<BgVariantA, BgVariantB>, "Two contexts that differ in row must be distinct types");

static_assert(is_cap_type_v<ctx_cap::Fg>);
static_assert(is_cap_type_v<Bg>);
static_assert(is_cap_type_v<Init>);
static_assert(is_cap_type_v<Test>);
static_assert(!is_cap_type_v<int>);
static_assert(!is_cap_type_v<void*>);

static_assert(is_effect_row(^^Row<>));
static_assert(is_effect_row(^^Row<Effect::Bg, Effect::Alloc>));
static_assert(!is_effect_row(^^int));
static_assert(is_effect_row(^^Row<> const));
static_assert(is_effect_row(^^Row<>&));
static_assert(is_effect_row(^^Row<Effect::Bg> const&));
static_assert(is_effect_row(^^Row<Effect::Bg>&&));
static_assert(!is_effect_row(^^int const&));
static_assert(IsEffectRow<Row<Effect::Bg> const&>);
static_assert(IsEffectRow<Row<Effect::Bg>&&>);

static_assert(std::is_same_v<cap_permitted_row_t<ctx_cap::Fg>, Row<>>);
static_assert(std::is_same_v<cap_permitted_row_t<Bg>, Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>>);
static_assert(std::is_same_v<cap_permitted_row_t<Init>, Row<Effect::Init, Effect::Alloc, Effect::IO, Effect::Block>>);
static_assert(std::is_same_v<cap_permitted_row_t<Test>, Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>>);

// The witnesses already satisfy this, or their own declarations would
// have failed.  Restating it here catches a later rewrite.
static_assert(Subrow<typename FgWitness::row_type, cap_permitted_row_t<typename FgWitness::cap_type>>);
static_assert(Subrow<typename BgWitness::row_type, cap_permitted_row_t<typename BgWitness::cap_type>>);
static_assert(Subrow<typename BgIoWitness::row_type, cap_permitted_row_t<typename BgIoWitness::cap_type>>);
static_assert(Subrow<typename InitWitness::row_type, cap_permitted_row_t<typename InitWitness::cap_type>>);
static_assert(Subrow<typename InitBlockWitness::row_type, cap_permitted_row_t<typename InitBlockWitness::cap_type>>);
static_assert(Subrow<typename TestWitnessCtx::row_type, cap_permitted_row_t<typename TestWitnessCtx::cap_type>>);

static_assert(!WellFormedExecCtx<ctx_cap::Fg, Row<Effect::Bg>>,
              "A foreground source permits the empty row only; a context claiming Bg on it is ill-formed.");
static_assert(!WellFormedExecCtx<ctx_cap::Fg, Row<Effect::Block>>,
              "A foreground source never permits Block, so the hot path cannot claim it.");
static_assert(WellFormedExecCtx<Init, Row<Effect::Init, Effect::Block>>,
              "An init source permits Block, because process startup waits in the kernel.");
static_assert(!WellFormedExecCtx<Init, Row<Effect::Bg>>, "An init source cannot stand in for a background one.");
static_assert(!WellFormedExecCtx<Test, Row<Effect::Bg>>, "A test source cannot stand in for a background one.");
static_assert(!WellFormedExecCtx<int, Row<>>);

static_assert(IsExecCtx<FgWitness>);
static_assert(IsExecCtx<BgWitness>);
static_assert(!IsExecCtx<int>);
static_assert(!IsExecCtx<Bg>);
static_assert(IsExecCtx<FgWitness const>);
static_assert(IsExecCtx<FgWitness&>);
static_assert(IsExecCtx<FgWitness const&>);
static_assert(IsExecCtx<FgWitness&&>);
static_assert(IsExecCtx<BgWitness const&>);
static_assert(!IsExecCtx<int const&>);
static_assert(!IsExecCtx<Bg const&>);
static_assert(is_exec_ctx(^^FgWitness) && !is_exec_ctx(^^int));

static_assert(std::is_same_v<cap_type_of_t<BgWitness>, Bg>);
static_assert(std::is_same_v<row_type_of_t<BgWitness>, Row<Effect::Bg, Effect::Alloc>>);
static_assert(std::is_same_v<row_type_of_t<HotFgCtx>, Row<>>);
static_assert(std::is_same_v<row_type_of_t<BgCompileCtx>, Row<Effect::Bg, Effect::Alloc, Effect::IO>>);
static_assert(std::is_same_v<cap_type_of_t<ColdInitCtx>, Init>);
static_assert(std::is_same_v<cap_type_of_t<TestRunnerCtx>, Test>);

static_assert(std::is_same_v<ctx_cap::Bg, Bg>);
static_assert(std::is_same_v<ctx_cap::Init, Init>);
static_assert(std::is_same_v<ctx_cap::Test, Test>);

static_assert(CtxAdmits<FgWitness, Row<>>);
static_assert(!CtxAdmits<FgWitness, Row<Effect::Bg>>);
static_assert(CtxAdmits<BgWitness, Row<Effect::Bg>>);
static_assert(CtxAdmits<BgWitness, Row<Effect::Bg, Effect::Alloc>>);
static_assert(!CtxAdmits<BgWitness, Row<Effect::IO>>);
static_assert(CtxAdmits<BgIoWitness, Row<Effect::IO>>);
static_assert(CtxAdmits<TestWitnessCtx, Row<Effect::Block>>);

static_assert(CtxOwnsCapability<BgWitness, Effect::Bg>);
static_assert(CtxOwnsCapability<BgWitness, Effect::Alloc>);
static_assert(!CtxOwnsCapability<BgWitness, Effect::IO>);
static_assert(CtxOwnsCapability<BgIoWitness, Effect::IO>);
static_assert(!CtxOwnsCapability<FgWitness, Effect::Bg>);
static_assert(!CtxOwnsCapability<InitWitness, Effect::Block>);
static_assert(CtxOwnsCapability<InitLoadCtx, Effect::Block>);

static_assert(CtxOwnsAnyOf<BgWitness, Effect::Bg, Effect::IO>,
              "The disjunctive lift must accept a row that carries one of the named atoms.");
static_assert(CtxOwnsAnyOf<InitWitness, Effect::Bg, Effect::Init>);
static_assert(!CtxOwnsAnyOf<FgWitness, Effect::Bg, Effect::IO, Effect::Init>,
              "The disjunctive lift must reject an empty row.");
static_assert(!CtxOwnsAnyOf<BgWitness>, "The disjunctive lift over no atoms is false.");

static_assert(CtxOwnsAllOf<BgWitness, Effect::Bg, Effect::Alloc>,
              "The conjunctive lift must accept a row that carries every named atom.");
static_assert(!CtxOwnsAllOf<BgWitness, Effect::Bg, Effect::IO>,
              "The conjunctive lift must reject a row that is missing one of the named atoms.  Widening "
              "the row first is the way through.");
static_assert(CtxOwnsAllOf<BgWitness>, "The conjunctive lift over no atoms is true.");
static_assert(!CtxOwnsAllOf<FgWitness, Effect::Bg>,
              "The conjunctive lift must reject any non-empty pack against an empty row.");

// A one-atom lift must answer exactly what the single-atom concept
// answers, so that the two surfaces stay interchangeable.
static_assert(CtxOwnsAnyOf<BgWitness, Effect::Bg> == CtxOwnsCapability<BgWitness, Effect::Bg>);
static_assert(CtxOwnsAllOf<BgWitness, Effect::Bg> == CtxOwnsCapability<BgWitness, Effect::Bg>);

// The background witness claims two atoms and its source permits four.
// The row is the bound: the witness keeps its source, and it narrows its
// row but does not widen it.  A context whose row covers its source lends
// the source.
template <class Ctx>
concept LendsItsSource = requires(Ctx const& ctx) { ctx.cap(); };
template <class Ctx, class Row>
concept NarrowsTo = requires(Ctx const& ctx) { ctx.template in_row<Row>(); };

static_assert(!LendsItsSource<BgWitness> && !LendsItsSource<BgCompileCtx> && !LendsItsSource<InitWitness>);
static_assert(LendsItsSource<BgLoadCtx> && LendsItsSource<InitLoadCtx> && LendsItsSource<TestWitnessCtx>
              && LendsItsSource<FgWitness>);
static_assert(NarrowsTo<BgCompileCtx, Row<Effect::Bg, Effect::Alloc>> && NarrowsTo<BgWitness, Row<>>);
static_assert(!NarrowsTo<BgWitness, Row<Effect::Bg, Effect::Alloc, Effect::IO>>,
              "A context does not widen its row.  The atom it would add needs the source as evidence.");
static_assert(!NarrowsTo<InitWitness, Row<Effect::Init, Effect::Alloc, Effect::IO, Effect::Block>>);

}  // namespace detail::ctx_witnesses

}  // namespace foundation::effects
