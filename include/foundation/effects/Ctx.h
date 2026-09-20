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

#include <concepts>
#include <cstddef>
#include <string_view>
#include <type_traits>

namespace foundation::effects {

namespace ctx_cap {
// The foreground thread holds no minted capability token.  A reach for
// one fails to compile, because this context has no member of any
// capability type.  It permits the empty row, spelled the way a context
// spells its own, so that one reader below serves every source.
struct Fg {
    template <template <Effect...> class R>
    using permitted_as = R<>;
};

// These name the same three types the enclosing namespace declares.
// Either spelling works; the aliases only give the axis a uniform look.
using Bg = ::foundation::effects::Bg;
using Init = ::foundation::effects::Init;
using Test = ::foundation::effects::Test;
}  // namespace ctx_cap

// A capability source is the foreground marker or a rostered context.
// This is a concept over the roster in Effect.h, so nothing a
// translation unit declares can add a source.  The value and the trait
// spellings are derived from it and read by nothing that gates.
template <class T>
concept IsCapType = std::same_as<T, ctx_cap::Fg> || IsContext<T>;
template <class T>
inline constexpr bool is_cap_type_v = IsCapType<T>;
template <class T>
struct is_cap_type : std::bool_constant<IsCapType<T>> {};

// The row recognition trait, is_effect_row, lives in Row.h beside the
// row it recognizes.

// The largest row each capability source can authorize, read off the
// source's own declaration: a context permits its own atom and the
// value atoms it holds, and the foreground marker permits nothing.  A
// context's own row must stay inside it, which is what stops a
// foreground context from claiming a background effect.
template <IsCapType Cap>
struct cap_permitted_row {
    using type = typename Cap::template permitted_as<Row>;
};

template <IsCapType Cap>
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
    // member access.
    [[no_unique_address]] Cap cap_{};
    [[no_unique_address]] Row row_{};

public:
    // A context that claims nothing is free to build, because there is
    // nothing in it to forge.
    //
    // This constructor was once public for EVERY specialization, and
    // that was the hole: `ExecCtx<Init, Row<Init, Alloc, IO>>{}`
    // default-built the Init member, which the capability types then
    // befriended this template to allow, and satisfied CtxCanMint for
    // every effect an init source permits.  The capability was
    // reachable by constructing the context that holds it, without
    // ever passing the passkey that guards Init's own constructor.
    // The capability types no longer befriend this template: nothing
    // here builds one, and the member's default initializer is reached
    // through this constructor alone, which exists for the foreground
    // marker only.
    constexpr ExecCtx() noexcept
        requires std::is_same_v<Cap, ctx_cap::Fg>
    = default;

    // Every other context is handed the capability it claims, and that
    // capability IS the evidence: Cap's own default constructor is
    // private, so a caller holding one obtained it from mint_context.
    constexpr explicit ExecCtx(Cap cap) noexcept : cap_{cap} {}

    // The only way to reach the capability, and it borrows rather than
    // copies.  Code that wants a copy has to write one, which a grep
    // for this accessor finds.
    [[nodiscard]] constexpr Cap const& cap() const noexcept { return cap_; }

    using cap_type = Cap;
    using row_type = Row;

    // Each builder returns a fresh context with one axis replaced.
    // Every link of a chain is a distinct type and every link is one
    // byte.
    // Promoting the capability axis takes the capability itself, not
    // merely its name.  This used to be `with_cap<NewCap>()`, naming
    // the type and handing back a context that owned one, which is how
    // `ExecCtx<>{}.with_cap<Init>()` climbed from a foreground context
    // to an init context in a single call with no evidence at all.
    template <class NewCap>
        requires IsCapType<NewCap> && Subrow<Row, cap_permitted_row_t<NewCap>>
    [[nodiscard]] constexpr auto with_cap(NewCap cap) const noexcept -> ExecCtx<NewCap, Row> {
        return ExecCtx<NewCap, Row>{cap};
    }

    // Widening the row carries the capability already held rather than
    // minting one, which is why it needs no evidence beyond the context
    // it is called on.  The row may not grow past what the capability
    // source permits, so a foreground context — whose source permits
    // nothing — cannot widen at all.
    template <class NewRow>
        requires IsEffectRow<NewRow> && Subrow<Row, NewRow> && Subrow<NewRow, cap_permitted_row_t<Cap>>
    [[nodiscard]] constexpr auto in_row() const noexcept -> ExecCtx<Cap, NewRow> {
        return ExecCtx<Cap, NewRow>{cap_};
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

namespace detail::ctx_witnesses {

// Witnesses in the shape of the five named contexts the layer above
// defines (fixy/Ctx.h, A11.3).  They are scaffolding, not a second
// spelling of those contexts.
//
// They live in the header rather than in a test because Capability.h,
// Computation.h, Permission.h and the fixtures of all three name them.
// The assertions below are invariants of the shipped concepts read
// against these shapes.  The scenarios that once sat here — building a
// promotion chain, probing with_cap, driving every operation at run
// time — moved to test/foundation/test_ctx.cpp, which is why this
// namespace no longer says self_test.
using FgWitness = ExecCtx<ctx_cap::Fg, Row<>>;
using BgWitness = ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc>>;
using BgIoWitness = ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc, Effect::IO>>;
using InitWitness = ExecCtx<Init, Row<Effect::Init, Effect::Alloc, Effect::IO>>;
using TestWitnessCtx = ExecCtx<Test, Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>>;

// The five named contexts of include/crucible/effects/_ExecCtx.h, with
// the six policy axes this layer dropped removed and the two that
// survive — the capability source and the row — kept exactly.  The
// port handed these to A11.3 by name and recorded their rows nowhere
// that deleting the old tree would not erase; this is that record.
// Each is one witness above, and the assertion after each restates the
// row the old tree declared, so the layer that promotes them to
// production contexts starts from a declaration rather than from
// memory.  The comment on each is the old tree's.

// The context of the foreground thread that runs dispatch.
using HotFgCtx = FgWitness;
static_assert(std::is_same_v<HotFgCtx, ExecCtx<ctx_cap::Fg, Row<>>>);

using BgDrainCtx = BgWitness;
static_assert(std::is_same_v<BgDrainCtx, ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc>>>);

// The compile context claims IO on top of the drain row, because
// compiling writes kernel artifacts.
using BgCompileCtx = BgIoWitness;
static_assert(std::is_same_v<BgCompileCtx, ExecCtx<Bg, Row<Effect::Bg, Effect::Alloc, Effect::IO>>>);

// The context of process startup, before the threads are pinned.
using ColdInitCtx = InitWitness;
static_assert(std::is_same_v<ColdInitCtx, ExecCtx<Init, Row<Effect::Init, Effect::Alloc, Effect::IO>>>);

// A fixture may claim any effect this row names, and no others.  In
// particular it cannot claim the background or initialization effects,
// so it cannot stand in for either of those contexts.
using TestRunnerCtx = TestWitnessCtx;
static_assert(
    std::is_same_v<TestRunnerCtx, ExecCtx<Test, Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>>>);

static_assert(sizeof(ExecCtx<>) == 1, "Both axes of ExecCtx are empty types, so the whole context must be 1 byte.");
static_assert(sizeof(FgWitness) == 1);
static_assert(sizeof(BgWitness) == 1);
static_assert(sizeof(BgIoWitness) == 1);
static_assert(sizeof(InitWitness) == 1);
static_assert(sizeof(TestWitnessCtx) == 1);

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
static_assert(CtxCanMint<BgCompileCtx, Effect::Block>);
static_assert(CtxCanMint<InitWitness, Effect::Alloc>);
static_assert(CtxCanMint<InitWitness, Effect::IO>);
static_assert(!CtxCanMint<InitWitness, Effect::Block>);
static_assert(!CtxCanMint<FgWitness, Effect::Alloc>);
static_assert(!CtxCanMint<FgWitness, Effect::Bg>);
static_assert(CtxCanMint<TestWitnessCtx, Effect::Block>);

}  // namespace detail::ctx_witnesses

}  // namespace foundation::effects
