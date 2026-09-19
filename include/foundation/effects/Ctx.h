#pragma once

// An execution context is the scope an operation runs in: which
// capability source the caller holds, and which effects the scope has
// claimed.  A body that claims row R may run in a context whose row
// covers R, and a context may claim no more than its capability source
// permits.  That pair is the whole of reject-by-default for effects: a
// context minted with the empty row admits nothing effectful, and every
// relaxation is a wider row named in the type.
//
// The old context (include/crucible/effects/_ExecCtx.h) carried six
// further axes: NUMA policy, allocator class, heat tier, cache
// residency, workload hint, progress class.  Outside the old substrate
// no production site read them; the three that spelled the tags
// (topology/TopologyGraph.h, ledger/LedgerStore.h, mimic/CogMimic.h)
// were naming a context, not reading an axis.  Inside the old substrate
// they had readers, and those readers are what the later port tasks
// decide about: concurrent/ExecCtxBridge.h and SubstrateCtxFit.h
// project the residency and workload axes into the cache-tier gates on
// the channel and session mints, WorkloadBudgetCoherent.h and
// AutoSplit.h read the workload hint, Endpoint.h reads the heat tier
// and the allocator class, permissions/PermissionFork.h reads the
// workload budget to choose between running the children inline and
// spawning them, and sessions/SessionMint.h re-exports every axis.
// This layer carries none of that policy.  Where a caller wants to say
// how hot a path is or which allocator it reaches for, it grades the
// value with the band wrapper for that axis; a channel or a fork that
// wants a budget takes it as its own parameter.  A context describes
// the surrounding scope, not a value.

#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <string_view>
#include <type_traits>

namespace foundation::effects {

namespace ctx_cap {
// The foreground thread holds no minted capability token.  A reach for
// one fails to compile, because this context has no member of any
// capability type.
struct Fg {};

// These name the same three types the enclosing namespace declares.
// Either spelling works; the aliases only give the axis a uniform look.
using Bg = ::foundation::effects::Bg;
using Init = ::foundation::effects::Init;
using Test = ::foundation::effects::Test;
}  // namespace ctx_cap

template <class T>
struct is_cap_type : std::false_type {};
template <>
struct is_cap_type<ctx_cap::Fg> : std::true_type {};
template <>
struct is_cap_type<Bg> : std::true_type {};
template <>
struct is_cap_type<Init> : std::true_type {};
template <>
struct is_cap_type<Test> : std::true_type {};
template <class T>
inline constexpr bool is_cap_type_v = is_cap_type<T>::value;
template <class T>
concept IsCapType = is_cap_type_v<T>;

// Top-level cv and reference are stripped before matching, so that a
// concept fed a forwarding-reference deduction still recognizes the
// row.  Every recognition trait in the project behaves this way.
template <class T>
struct is_effect_row : std::false_type {};
template <Effect... Es>
struct is_effect_row<Row<Es...>> : std::true_type {};
template <class T>
inline constexpr bool is_effect_row_v = is_effect_row<std::remove_cvref_t<T>>::value;
template <class T>
concept IsEffectRow = is_effect_row_v<T>;

// The largest row each capability source can authorize.  A context's
// own row must stay inside it, which is what stops a foreground
// context from claiming a background effect.
template <class Cap>
struct cap_permitted_row;

template <>
struct cap_permitted_row<ctx_cap::Fg> {
    using type = Row<>;
};
template <>
struct cap_permitted_row<Bg> {
    using type = Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>;
};
template <>
struct cap_permitted_row<Init> {
    using type = Row<Effect::Init, Effect::Alloc, Effect::IO>;
};
template <>
struct cap_permitted_row<Test> {
    using type = Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>;
};

template <class Cap>
using cap_permitted_row_t = typename cap_permitted_row<Cap>::type;

// The two checks are folded into one concept so that a bad argument
// reports one diagnostic.  Conjunction short-circuits at the first
// failing atom, which also stops the Subrow atom from being substituted
// and asking a non-capability type for its permitted row.
//
// The concept is checked by a static_assert in the class body rather
// than by a constraint on the template head.  The friend declaration
// that names this template sits in Effect.h, which cannot include the
// row algebra without a cycle, so it could not repeat a matching
// constraint.
template <class Cap, class Row>
concept WellFormedExecCtx = IsCapType<Cap> && IsEffectRow<Row> && Subrow<Row, cap_permitted_row_t<Cap>>;

template <class Cap = ctx_cap::Fg, class Row = ::foundation::effects::Row<>>
class [[nodiscard]] ExecCtx {
public:
    static_assert(WellFormedExecCtx<Cap, Row>,
                  "One argument to ExecCtx is not a member of its axis, or the row exceeds what the "
                  "capability source permits.  The axes are a capability source (ctx_cap::Fg, Bg, Init, "
                  "Test) and an effect Row that is a subrow of what the source permits.");

private:
    // The members are private, and the capability member is why.  It
    // holds a context whose own default constructor is private and
    // friended here, so exposing the member would let any translation
    // unit copy out a capability context that it could not have
    // constructed for itself.
    //
    // Making this a class rather than a struct changes only the default
    // member access, so the implicit default constructor stays public
    // and the aliases and builder methods below still work.
    [[no_unique_address]] Cap cap_{};
    [[no_unique_address]] Row row_{};

public:
    // The only way to reach the capability, and it borrows rather than
    // copies.  Code that wants a copy has to write one, which a grep
    // for this accessor finds.
    [[nodiscard]] constexpr Cap const& cap() const noexcept { return cap_; }

    using cap_type = Cap;
    using row_type = Row;

    // Each builder returns a fresh context with one axis replaced.
    // Every link of a chain is a distinct type and every link is one
    // byte.
    template <class NewCap>
        requires IsCapType<NewCap> && Subrow<Row, cap_permitted_row_t<NewCap>>
    [[nodiscard]] consteval auto with_cap() const noexcept -> ExecCtx<NewCap, Row> {
        return {};
    }

    // The row only grows.  It may not grow past what the capability
    // source permits, so no chain of calls turns a foreground context
    // into one that claims a background effect.
    template <class NewRow>
        requires IsEffectRow<NewRow> && Subrow<Row, NewRow> && Subrow<NewRow, cap_permitted_row_t<Cap>>
    [[nodiscard]] consteval auto in_row() const noexcept -> ExecCtx<Cap, NewRow> {
        return {};
    }

    [[nodiscard]] static consteval std::string_view kind_name() noexcept { return "ExecCtx"; }
};

// Top-level cv and reference are stripped before matching, so that a
// concept fed a forwarding-reference deduction still recognizes the
// context.
template <class T>
struct is_exec_ctx : std::false_type {};
template <class Cap, class Row>
struct is_exec_ctx<ExecCtx<Cap, Row>> : std::true_type {};
template <class T>
inline constexpr bool is_exec_ctx_v = is_exec_ctx<std::remove_cvref_t<T>>::value;
template <class T>
concept IsExecCtx = is_exec_ctx_v<T>;

template <IsExecCtx Ctx>
using cap_type_of_t = typename Ctx::cap_type;
template <IsExecCtx Ctx>
using row_type_of_t = typename Ctx::row_type;

// A body that claims row R may run in a context whose row covers R.
template <class Ctx, class R>
concept CtxAdmits = IsExecCtx<Ctx> && IsEffectRow<R> && Subrow<R, row_type_of_t<Ctx>>;

template <class Ctx, Effect Cap>
concept CtxOwnsCapability = IsExecCtx<Ctx> && row_contains_v<row_type_of_t<Ctx>, Cap>;

// The two named lifts below cost exactly what writing the fold by hand
// costs.  What they buy is that a reviewer recognizes the shape of an
// authorization at a glance, and that a rename of the row extractor
// reaches every site through one definition.
template <class Ctx, Effect... Es>
concept CtxOwnsAnyOf = IsExecCtx<Ctx> && (row_contains_v<row_type_of_t<Ctx>, Es> || ...);

template <class Ctx, Effect... Es>
concept CtxOwnsAllOf = IsExecCtx<Ctx> && (row_contains_v<row_type_of_t<Ctx>, Es> && ...);

// This asks what the capability source could authorize, not what the
// context currently claims.  The two differ: a background context may
// claim only two effects while its source permits four, and the
// context can widen into them.
template <class Ctx, Effect E>
concept CtxCanMint = IsExecCtx<Ctx> && row_contains_v<cap_permitted_row_t<cap_type_of_t<Ctx>>, E>;

namespace detail::exec_ctx_self_test {

// Local witnesses in the shape of the four named contexts the layer
// above defines (fixy/Ctx.h).  They are self-test scaffolding, not a
// second spelling of those contexts.
using FgWitness = ExecCtx<ctx_cap::Fg, Row<>>;
using BgWitness = ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc>>;
using BgIoWitness = ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc, Effect::IO>>;
using InitWitness = ExecCtx<Init, Row<Effect::Init, Effect::Alloc, Effect::IO>>;
using TestWitnessCtx = ExecCtx<Test, Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>>;

static_assert(sizeof(ExecCtx<>) == 1, "Both axes of ExecCtx are empty types, so the whole context must be 1 byte.");
static_assert(sizeof(FgWitness) == 1);
static_assert(sizeof(BgWitness) == 1);
static_assert(sizeof(BgIoWitness) == 1);
static_assert(sizeof(InitWitness) == 1);
static_assert(sizeof(TestWitnessCtx) == 1);

// Every axis defaults to the claim-nothing end of its range.
static_assert(std::is_same_v<typename ExecCtx<>::cap_type, ctx_cap::Fg>);
static_assert(std::is_same_v<typename ExecCtx<>::row_type, Row<>>);

constexpr auto ctx0 = ExecCtx<>{};
constexpr auto ctx1 = ctx0.with_cap<Bg>();
static_assert(std::is_same_v<typename decltype(ctx1)::cap_type, Bg>);
static_assert(std::is_same_v<typename decltype(ctx1)::row_type, Row<>>);

constexpr auto ctx2 = ctx1.in_row<Row<Effect::Bg, Effect::Alloc>>();
static_assert(std::is_same_v<typename decltype(ctx2)::row_type, Row<Effect::Bg, Effect::Alloc>>);

constexpr auto ctx3 = ctx2.in_row<Row<Effect::Bg, Effect::Alloc, Effect::IO>>();
static_assert(std::is_same_v<typename decltype(ctx3)::row_type, Row<Effect::Bg, Effect::Alloc, Effect::IO>>);
static_assert(std::is_same_v<typename decltype(ctx3)::cap_type, Bg>);

static_assert(sizeof(ctx0) == 1 && sizeof(ctx1) == 1 && sizeof(ctx2) == 1 && sizeof(ctx3) == 1);

using BgVariantA = ExecCtx<Bg, Row<Effect::Bg>>;
using BgVariantB = ExecCtx<Bg, Row<Effect::Bg, Effect::IO>>;
static_assert(!std::is_same_v<BgVariantA, BgVariantB>, "Two contexts that differ in row must be distinct types");

static_assert(is_cap_type_v<ctx_cap::Fg>);
static_assert(is_cap_type_v<Bg>);
static_assert(is_cap_type_v<Init>);
static_assert(is_cap_type_v<Test>);
static_assert(!is_cap_type_v<int>);
static_assert(!is_cap_type_v<void*>);

static_assert(is_effect_row_v<Row<>>);
static_assert(is_effect_row_v<Row<Effect::Bg, Effect::Alloc>>);
static_assert(!is_effect_row_v<int>);
static_assert(is_effect_row_v<Row<> const>);
static_assert(is_effect_row_v<Row<>&>);
static_assert(is_effect_row_v<Row<Effect::Bg> const&>);
static_assert(is_effect_row_v<Row<Effect::Bg>&&>);
static_assert(!is_effect_row_v<int const&>);
static_assert(IsEffectRow<Row<Effect::Bg> const&>);
static_assert(IsEffectRow<Row<Effect::Bg>&&>);

static_assert(std::is_same_v<cap_permitted_row_t<ctx_cap::Fg>, Row<>>);
static_assert(std::is_same_v<cap_permitted_row_t<Bg>, Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>>);
static_assert(std::is_same_v<cap_permitted_row_t<Init>, Row<Effect::Init, Effect::Alloc, Effect::IO>>);
static_assert(std::is_same_v<cap_permitted_row_t<Test>, Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>>);

// The witnesses already satisfy this, or their own declarations would
// have failed.  Restating it here catches a later rewrite.
static_assert(Subrow<typename FgWitness::row_type, cap_permitted_row_t<typename FgWitness::cap_type>>);
static_assert(Subrow<typename BgWitness::row_type, cap_permitted_row_t<typename BgWitness::cap_type>>);
static_assert(Subrow<typename BgIoWitness::row_type, cap_permitted_row_t<typename BgIoWitness::cap_type>>);
static_assert(Subrow<typename InitWitness::row_type, cap_permitted_row_t<typename InitWitness::cap_type>>);
static_assert(Subrow<typename TestWitnessCtx::row_type, cap_permitted_row_t<typename TestWitnessCtx::cap_type>>);

static_assert(!WellFormedExecCtx<ctx_cap::Fg, Row<Effect::Bg>>,
              "A foreground source permits the empty row only; a context claiming Bg on it is ill-formed.");
static_assert(!WellFormedExecCtx<Init, Row<Effect::Block>>, "An init source never permits Block.");
static_assert(!WellFormedExecCtx<Test, Row<Effect::Bg>>, "A test source cannot stand in for a background one.");
static_assert(!WellFormedExecCtx<int, Row<>>);

static_assert(is_exec_ctx_v<FgWitness>);
static_assert(is_exec_ctx_v<BgWitness>);
static_assert(!is_exec_ctx_v<int>);
static_assert(!is_exec_ctx_v<Bg>);
static_assert(is_exec_ctx_v<FgWitness const>);
static_assert(is_exec_ctx_v<FgWitness&>);
static_assert(is_exec_ctx_v<FgWitness const&>);
static_assert(is_exec_ctx_v<FgWitness&&>);
static_assert(is_exec_ctx_v<BgWitness const&>);
static_assert(!is_exec_ctx_v<int const&>);
static_assert(!is_exec_ctx_v<Bg const&>);
static_assert(IsExecCtx<FgWitness const&>);
static_assert(IsExecCtx<FgWitness&&>);

static_assert(std::is_same_v<cap_type_of_t<BgWitness>, Bg>);
static_assert(std::is_same_v<row_type_of_t<BgWitness>, Row<Effect::Bg, Effect::Alloc>>);

static_assert(std::is_same_v<ctx_cap::Bg, Bg>);
static_assert(std::is_same_v<ctx_cap::Init, Init>);
static_assert(std::is_same_v<ctx_cap::Test, Test>);

// Promoting the empty row to a background capability is admitted.
// Moving a background row to an initialization capability is not, and
// would have to narrow the row first.
constexpr auto bg_promoted = FgWitness{}.with_cap<Bg>();
static_assert(std::is_same_v<typename decltype(bg_promoted)::cap_type, Bg>);
static_assert(std::is_same_v<typename decltype(bg_promoted)::row_type, Row<>>);
template <class C>
concept CanTakeInitCap = requires(C const& c) { c.template with_cap<Init>(); };
static_assert(CanTakeInitCap<FgWitness>);
static_assert(!CanTakeInitCap<BgWitness>, "A row that names Bg cannot move under an Init source.");

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

// The background witness claims two effects but its source permits
// four, so the next assertions differ from the ownership ones above.
static_assert(CtxCanMint<BgWitness, Effect::Alloc>);
static_assert(CtxCanMint<BgWitness, Effect::IO>);
static_assert(CtxCanMint<BgWitness, Effect::Block>);
static_assert(CtxCanMint<BgWitness, Effect::Bg>);
static_assert(!CtxCanMint<BgWitness, Effect::Init>);
static_assert(CtxCanMint<InitWitness, Effect::Alloc>);
static_assert(CtxCanMint<InitWitness, Effect::IO>);
static_assert(!CtxCanMint<InitWitness, Effect::Block>);
static_assert(!CtxCanMint<FgWitness, Effect::Alloc>);
static_assert(!CtxCanMint<FgWitness, Effect::Bg>);
static_assert(CtxCanMint<TestWitnessCtx, Effect::Block>);

// Every operation is driven here with non-constant arguments.  The
// static_assert wall above only proves the constant-evaluated path.
inline void runtime_smoke_test() {
    [[maybe_unused]] FgWitness fg{};
    [[maybe_unused]] BgWitness bg{};
    [[maybe_unused]] BgIoWitness bg_io{};
    [[maybe_unused]] InitWitness init{};
    [[maybe_unused]] TestWitnessCtx test_ctx{};

    [[maybe_unused]] auto s1 = sizeof(fg);
    [[maybe_unused]] auto s2 = sizeof(bg);

    // The capability member is reachable through the borrowing
    // accessor and nowhere else.
    [[maybe_unused]] Bg const& held = bg.cap();
    [[maybe_unused]] cap::Alloc alloc_tag = held.alloc;

    auto widened = bg.template in_row<Row<Effect::Bg, Effect::Alloc, Effect::Block>>();
    static_assert(std::is_same_v<typename decltype(widened)::row_type, Row<Effect::Bg, Effect::Alloc, Effect::Block>>);
    [[maybe_unused]] auto s3 = sizeof(widened);
}

}  // namespace detail::exec_ctx_self_test

}  // namespace foundation::effects
