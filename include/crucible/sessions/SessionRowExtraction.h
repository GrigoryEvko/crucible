#pragma once

// payload_row_t<T> projects a session payload to the effect row that
// payload carries.  A protocol walker uses it to decide whether the
// surrounding execution context admits every payload in the tree.
//
// A wrapper that carries no effect of its own unwraps transparently to
// its element type, so an arbitrarily deep stack of value-level
// wrappers still reports the row of whatever sits at the bottom.  A
// wrapper with no specialisation reports the empty row, which makes an
// omitted specialisation a soundness bug rather than a missing feature:
// the walker then undercounts the effects of anything the unrecognised
// wrapper hides.
//
// Two consumers want different things from the result.  A row-admission
// check wants only the effect row and uses payload_effect_row_t.  An
// audit of the full payload contract uses payload_row_t and keeps
// wrapper grades such as the numerical tolerance axis visible.

#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_Capability.h>
#include <crucible/effects/_Computation.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/safety/_AllocClass.h>
#include <crucible/safety/Budgeted.h>
#include <crucible/safety/_CipherTier.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/_DetSafe.h>
#include <crucible/safety/EpochVersioned.h>
#include <crucible/safety/_HotPath.h>
#include <crucible/safety/_Linear.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/_Mutation.h>
#include <crucible/safety/NumaPlacement.h>
#include <crucible/safety/_NumericalTier.h>
#include <crucible/safety/_OpaqueLifetime.h>
#include <crucible/safety/Progress.h>
#include <crucible/safety/_RecipeSpec.h>
#include <crucible/safety/_Refined.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/_SealedRefined.h>
#include <crucible/safety/_Secret.h>
#include <crucible/safety/_Stale.h>
#include <crucible/safety/_Tagged.h>
#include <crucible/safety/TimeOrdered.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/_Wait.h>
#include <crucible/sessions/SessionContentAddressed.h>
#include <crucible/sessions/SessionPermPayloads.h>

#include <type_traits>

namespace crucible::safety::proto {

// Most payload rows are a plain effect row.  A numerical tolerance
// grade is the exception, because dropping it at the wire boundary
// loses the numerical contract the two ends agreed on.  This pairing
// keeps the grade while the effect-row projection stays available.

template <::crucible::safety::Tolerance Tier, class InnerRow>
struct NumericalPayloadRow {
    using effect_row = InnerRow;
    static constexpr ::crucible::safety::Tolerance tolerance = Tier;
};

template <class PayloadRow>
struct payload_row_effect {
    using type = PayloadRow;
};

template <::crucible::safety::Tolerance Tier, class InnerRow>
struct payload_row_effect<NumericalPayloadRow<Tier, InnerRow>> : payload_row_effect<InnerRow> {};

template <class PayloadRow>
using payload_row_effect_t = typename payload_row_effect<PayloadRow>::type;

template <class T>
struct payload_row {
    using type = ::crucible::effects::Row<>;
};

// Sending a computation says that the payload was produced under row R,
// and the receiver inherits the obligation that R was authorized.
template <class R, class T>
struct payload_row<::crucible::effects::Computation<R, T>> {
    using type = R;
};

// Sending a capability conveys the effect itself, because the receiver
// gains the authority to perform it.  The source is informational at
// the row level.
template <::crucible::effects::Effect E, class S>
struct payload_row<::crucible::effects::Capability<E, S>> {
    using type = ::crucible::effects::Row<E>;
};

template <auto Pred, class T>
struct payload_row<::crucible::safety::Refined<Pred, T>> : payload_row<T> {};

template <auto Pred, class T>
struct payload_row<::crucible::safety::SealedRefined<Pred, T>> : payload_row<T> {};

template <class T, class Tag>
struct payload_row<::crucible::safety::Tagged<T, Tag>> : payload_row<T> {};

template <class T>
struct payload_row<::crucible::safety::Linear<T>> : payload_row<T> {};

template <class T>
struct payload_row<::crucible::safety::Stale<T>> : payload_row<T> {};

template <class T>
struct payload_row<ContentAddressed<T>> : payload_row<T> {};

// A permission-flow marker moves or borrows authority over the value,
// while the value itself is still what the context must admit.
template <class T, class Tag>
struct payload_row<Transferable<T, Tag>> : payload_row<T> {};

template <class T, class Tag>
struct payload_row<Borrowed<T, Tag>> : payload_row<T> {};

template <class T, class Tag>
struct payload_row<Returned<T, Tag>> : payload_row<T> {};

// Each policy wrapper below carries a compile-time axis and no effect
// of its own, so the row lives entirely in the element type.

template <auto V, class T>
struct payload_row<::crucible::safety::HotPath<V, T>> : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::DetSafe<V, T>> : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::AllocClass<V, T>> : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::ResidencyHeat<V, T>> : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::CipherTier<V, T>> : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::MemOrder<V, T>> : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::Wait<V, T>> : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::Progress<V, T>> : payload_row<T> {};

template <::crucible::safety::Tolerance V, class T>
struct payload_row<::crucible::safety::NumericalTier<V, T>> {
    using type = NumericalPayloadRow<V, typename payload_row<T>::type>;
};

template <auto V, class T>
struct payload_row<::crucible::safety::Vendor<V, T>> : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::Crash<V, T>> : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::Consistency<V, T>> : payload_row<T> {};

template <auto V, class T>
struct payload_row<::crucible::safety::OpaqueLifetime<V, T>> : payload_row<T> {};

// These wrappers carry their grade per instance rather than as a
// type-level axis, and the row still lives in the element type.

template <class T>
struct payload_row<::crucible::safety::Secret<T>> : payload_row<T> {};

template <class T>
struct payload_row<::crucible::safety::Budgeted<T>> : payload_row<T> {};

template <class T>
struct payload_row<::crucible::safety::EpochVersioned<T>> : payload_row<T> {};

template <class T>
struct payload_row<::crucible::safety::NumaPlacement<T>> : payload_row<T> {};

template <class T>
struct payload_row<::crucible::safety::RecipeSpec<T>> : payload_row<T> {};

// A state-holder wraps a value or a container.  The container is
// opaque at the row level, and only the element type propagates
// effects.

template <class T, class Cmp>
struct payload_row<::crucible::safety::Monotonic<T, Cmp>> : payload_row<T> {};

template <class T, auto Max, class Cmp>
struct payload_row<::crucible::safety::BoundedMonotonic<T, Max, Cmp>> : payload_row<T> {};

template <class T>
struct payload_row<::crucible::safety::WriteOnce<T>> : payload_row<T> {};

template <class T, class Cmp>
    requires std::is_trivially_copyable_v<T>
struct payload_row<::crucible::safety::AtomicMonotonic<T, Cmp>> : payload_row<T> {};

template <class T, template <class...> class Storage>
struct payload_row<::crucible::safety::AppendOnly<T, Storage>> : payload_row<T> {};

template <class T, std::size_t N, class Tag>
struct payload_row<::crucible::safety::TimeOrdered<T, N, Tag>> : payload_row<T> {};

// This walker sums the effect rows of every payload a protocol moves.
// Its purpose is higher-order capability transfer: handing a peer an
// endpoint of protocol P grants that peer the authority to run P, so by
// the same rule that makes sending a capability convey its effect, the
// sender's own context must admit the union of P's per-step rows.
//
// Without the walk, transferring an endpoint of an effect-heavy
// protocol would report the empty row, and a context with no effects at
// all would silently admit it.

template <class Proto>
struct protocol_effect_row;

template <>
struct protocol_effect_row<End> {
    using type = ::crucible::effects::Row<>;
};

// Continue closes back to the enclosing loop, whose payloads were
// already counted when the loop body was walked.
template <>
struct protocol_effect_row<Continue> {
    using type = ::crucible::effects::Row<>;
};

template <CrashClass C>
struct protocol_effect_row<Stop_g<C>> {
    using type = ::crucible::effects::Row<>;
};

template <class T, class K>
struct protocol_effect_row<Send<T, K>> {
    using type = ::crucible::effects::row_union_t<payload_row_effect_t<typename payload_row<T>::type>,
                                                  typename protocol_effect_row<K>::type>;
};

template <class T, class K>
struct protocol_effect_row<Recv<T, K>> {
    using type = ::crucible::effects::row_union_t<payload_row_effect_t<typename payload_row<T>::type>,
                                                  typename protocol_effect_row<K>::type>;
};

template <class B>
struct protocol_effect_row<Loop<B>> : protocol_effect_row<B> {};

template <VendorBackend V, class P>
struct protocol_effect_row<VendorPinned<V, P>> : protocol_effect_row<P> {};

namespace detail::protocol_effect_row_fold {

template <class... Rows>
struct row_union_pack;

template <>
struct row_union_pack<> {
    using type = ::crucible::effects::Row<>;
};

template <class R>
struct row_union_pack<R> {
    using type = R;
};

template <class R1, class R2, class... Rest>
struct row_union_pack<R1, R2, Rest...> {
    using type = typename row_union_pack<::crucible::effects::row_union_t<R1, R2>, Rest...>::type;
};

template <class... Rows>
using row_union_pack_t = typename row_union_pack<Rows...>::type;

}  // namespace detail::protocol_effect_row_fold

// Either side of a choice may be taken at run time, so the context has
// to admit the worst branch.
template <class... Branches>
struct protocol_effect_row<Select<Branches...>> {
    using type = detail::protocol_effect_row_fold::row_union_pack_t<typename protocol_effect_row<Branches>::type...>;
};

template <class... Branches>
struct protocol_effect_row<Offer<Branches...>> {
    using type = detail::protocol_effect_row_fold::row_union_pack_t<typename protocol_effect_row<Branches>::type...>;
};

// A sender tag carries no payload, so the branch pack alone is walked.
template <class Role, class... Branches>
struct protocol_effect_row<Offer<Sender<Role>, Branches...>> {
    using type = detail::protocol_effect_row_fold::row_union_pack_t<typename protocol_effect_row<Branches>::type...>;
};

// A checkpointed session can reach both its base arm and its rollback
// arm, so both contribute.
template <class Base, class Rollback>
struct protocol_effect_row<CheckpointedSession<Base, Rollback>> {
    using type = ::crucible::effects::row_union_t<typename protocol_effect_row<Base>::type,
                                                  typename protocol_effect_row<Rollback>::type>;
};

// The party that delegates does not execute the delegated protocol.
// The recipient does, under its own context, so only the delegator's
// own continuation is walked here.
template <class T, class K>
struct protocol_effect_row<Delegate<T, K>> : protocol_effect_row<K> {};

// Delegating an already-crashed carrier makes the continuation
// unreachable, so the row is empty.  Composition collapses this whole
// protocol to Stop, whose row is also empty.  Walking the continuation
// instead would make the row shrink the moment anything is composed
// onto the protocol, and a caller would get weaker admission after
// composition than before it.
template <CrashClass C, class K>
struct protocol_effect_row<Delegate<Stop_g<C>, K>> {
    using type = ::crucible::effects::Row<>;
};

template <class T, class K>
struct protocol_effect_row<Accept<T, K>> : protocol_effect_row<K> {};

// Accepting an already-crashed endpoint is the mirror of delegating
// one, and it collapses the same way.
template <CrashClass C, class K>
struct protocol_effect_row<Accept<Stop_g<C>, K>> {
    using type = ::crucible::effects::Row<>;
};

template <class T, class K, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct protocol_effect_row<EpochedDelegate<T, K, MinEpoch, MinGeneration>> : protocol_effect_row<Delegate<T, K>> {};

template <class T, class K, std::uint64_t MinEpoch, std::uint64_t MinGeneration>
struct protocol_effect_row<EpochedAccept<T, K, MinEpoch, MinGeneration>> : protocol_effect_row<Accept<T, K>> {};

template <class Proto>
using protocol_effect_row_t = typename protocol_effect_row<Proto>::type;

// The permission-set axis is authority bookkeeping rather than a
// row-admission concern, so it contributes nothing here.

template <class InnerProto, class InnerPS>
struct payload_row<DelegatedSession<InnerProto, InnerPS>> {
    using type = protocol_effect_row_t<InnerProto>;
};

template <class T>
using payload_row_t = typename payload_row<T>::type;

template <class T>
using payload_effect_row_t = payload_row_effect_t<payload_row_t<T>>;

namespace detail::payload_row_self_test {

namespace eff = ::crucible::effects;
namespace saf = ::crucible::safety;

static_assert(std::is_same_v<payload_row_t<int>, eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<double>, eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<void*>, eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<char>, eff::Row<>>);

struct UserPod {
    int x;
    double y;
};
static_assert(std::is_same_v<payload_row_t<UserPod>, eff::Row<>>);

static_assert(std::is_same_v<payload_row_t<eff::Computation<eff::Row<>, int>>, eff::Row<>>);

static_assert(
    std::is_same_v<payload_row_t<eff::Computation<eff::Row<eff::Effect::Bg>, int>>, eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<payload_row_t<eff::Computation<eff::Row<eff::Effect::Bg, eff::Effect::Alloc>, double>>,
                             eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>);

static_assert(
    std::is_same_v<payload_row_t<eff::Capability<eff::Effect::Alloc, eff::Bg>>, eff::Row<eff::Effect::Alloc>>);

static_assert(std::is_same_v<payload_row_t<eff::Capability<eff::Effect::IO, eff::Init>>, eff::Row<eff::Effect::IO>>);

static_assert(
    std::is_same_v<payload_row_t<eff::Capability<eff::Effect::Block, eff::Test>>, eff::Row<eff::Effect::Block>>);

static_assert(std::is_same_v<payload_row_t<saf::Refined<saf::positive, int>>, eff::Row<>>);

static_assert(
    std::is_same_v<payload_row_t<saf::Refined<saf::positive, eff::Computation<eff::Row<eff::Effect::Bg>, int>>>,
                   eff::Row<eff::Effect::Bg>>);

static_assert(
    std::is_same_v<payload_row_t<saf::SealedRefined<saf::positive, eff::Computation<eff::Row<eff::Effect::IO>, int>>>,
                   eff::Row<eff::Effect::IO>>);

static_assert(std::is_same_v<payload_row_t<saf::Linear<eff::Computation<eff::Row<eff::Effect::IO>, int>>>,
                             eff::Row<eff::Effect::IO>>);

static_assert(std::is_same_v<payload_row_t<saf::Linear<int>>, eff::Row<>>);

struct ProvTag {};
static_assert(std::is_same_v<payload_row_t<saf::Tagged<eff::Computation<eff::Row<eff::Effect::Bg>, int>, ProvTag>>,
                             eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<payload_row_t<saf::Tagged<int, ProvTag>>, eff::Row<>>);

static_assert(std::is_same_v<payload_row_t<saf::Stale<eff::Computation<eff::Row<eff::Effect::Alloc>, int>>>,
                             eff::Row<eff::Effect::Alloc>>);

using ComposedT = saf::Refined<
    saf::positive,
    saf::Linear<saf::Tagged<eff::Computation<eff::Row<eff::Effect::Bg, eff::Effect::Alloc>, int>, ProvTag>>>;
static_assert(std::is_same_v<payload_row_t<ComposedT>, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>);

using FourLayerT = saf::Linear<
    saf::Refined<saf::positive, saf::Tagged<saf::Stale<eff::Computation<eff::Row<eff::Effect::IO>, int>>, ProvTag>>>;
static_assert(std::is_same_v<payload_row_t<FourLayerT>, eff::Row<eff::Effect::IO>>);

using BarelyComposedT = saf::Refined<saf::positive, saf::Linear<saf::Tagged<int, ProvTag>>>;
static_assert(std::is_same_v<payload_row_t<BarelyComposedT>, eff::Row<>>);

static_assert(std::is_same_v<payload_row_t<ContentAddressed<int>>, eff::Row<>>);

static_assert(std::is_same_v<payload_row_t<ContentAddressed<eff::Computation<eff::Row<eff::Effect::Bg>, int>>>,
                             eff::Row<eff::Effect::Bg>>);

using CaRefinedT = ContentAddressed<saf::Refined<saf::positive, eff::Computation<eff::Row<eff::Effect::IO>, int>>>;
static_assert(std::is_same_v<payload_row_t<CaRefinedT>, eff::Row<eff::Effect::IO>>);

struct WirePerm {};
using IoComp = eff::Computation<eff::Row<eff::Effect::IO>, int>;
static_assert(std::is_same_v<payload_row_t<Transferable<IoComp, WirePerm>>, eff::Row<eff::Effect::IO>>);
static_assert(std::is_same_v<payload_row_t<Borrowed<IoComp, WirePerm>>, eff::Row<eff::Effect::IO>>);
static_assert(std::is_same_v<payload_row_t<Returned<IoComp, WirePerm>>, eff::Row<eff::Effect::IO>>);

// Every graded wrapper is checked twice: once around a payload with no
// effects, and once around a payload that carries one.  A wrapper that
// drops the inner row would admit an effectful payload into a context
// that forbids the effect.

using BgComp = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
using BareInt = int;

static_assert(std::is_same_v<payload_row_t<saf::HotPath<saf::HotPathTier_v::Hot, BgComp>>, eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<saf::HotPath<saf::HotPathTier_v::Hot, BareInt>>, eff::Row<>>);

static_assert(std::is_same_v<payload_row_t<saf::DetSafe<saf::DetSafeTier_v::Pure, BgComp>>, eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<saf::DetSafe<saf::DetSafeTier_v::Pure, BareInt>>, eff::Row<>>);

static_assert(
    std::is_same_v<payload_row_t<saf::AllocClass<saf::AllocClassTag_v::Arena, BgComp>>, eff::Row<eff::Effect::Bg>>);

static_assert(
    std::is_same_v<payload_row_t<saf::ResidencyHeat<saf::ResidencyHeatTag_v::Hot, BgComp>>, eff::Row<eff::Effect::Bg>>);

static_assert(
    std::is_same_v<payload_row_t<saf::CipherTier<saf::CipherTierTag_v::Hot, BgComp>>, eff::Row<eff::Effect::Bg>>);

static_assert(
    std::is_same_v<payload_row_t<saf::MemOrder<saf::MemOrderTag_v::Acquire, BgComp>>, eff::Row<eff::Effect::Bg>>);

static_assert(
    std::is_same_v<payload_row_t<saf::Wait<saf::WaitStrategy_v::SpinPause, BgComp>>, eff::Row<eff::Effect::Bg>>);

static_assert(
    std::is_same_v<payload_row_t<saf::Progress<saf::ProgressClass_v::Terminating, BgComp>>, eff::Row<eff::Effect::Bg>>);

using BitexactBgRow = payload_row_t<saf::NumericalTier<::crucible::algebra::lattices::Tolerance::BITEXACT, BgComp>>;
static_assert(BitexactBgRow::tolerance == ::crucible::algebra::lattices::Tolerance::BITEXACT);
static_assert(std::is_same_v<typename BitexactBgRow::effect_row, eff::Row<eff::Effect::Bg>>);
static_assert(
    std::is_same_v<payload_effect_row_t<saf::NumericalTier<::crucible::algebra::lattices::Tolerance::BITEXACT, BgComp>>,
                   eff::Row<eff::Effect::Bg>>);
static_assert(!std::is_same_v<BitexactBgRow, eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<payload_row_t<saf::Vendor<saf::VendorBackend_v::CPU, BgComp>>, eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<payload_row_t<saf::Crash<saf::CrashClass_v::NoThrow, BgComp>>, eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<payload_row_t<saf::Secret<BgComp>>, eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<saf::Secret<BareInt>>, eff::Row<>>);

static_assert(std::is_same_v<payload_row_t<saf::Monotonic<BgComp>>, eff::Row<eff::Effect::Bg>>);
static_assert(std::is_same_v<payload_row_t<saf::WriteOnce<BgComp>>, eff::Row<eff::Effect::Bg>>);

struct TimeTag {};
static_assert(std::is_same_v<payload_row_t<saf::TimeOrdered<BgComp, 4, TimeTag>>, eff::Row<eff::Effect::Bg>>);

// A full wrapper stack in canonical nesting order still reports the row
// of the payload at the bottom.
using DeepStack = saf::HotPath<
    saf::HotPathTier_v::Hot,
    saf::DetSafe<
        saf::DetSafeTier_v::Pure,
        saf::NumericalTier<
            ::crucible::algebra::lattices::Tolerance::BITEXACT,
            saf::Refined<saf::positive, eff::Computation<eff::Row<eff::Effect::Bg, eff::Effect::Alloc>, int>>>>>;
static_assert(payload_row_t<DeepStack>::tolerance == ::crucible::algebra::lattices::Tolerance::BITEXACT);
static_assert(
    std::is_same_v<typename payload_row_t<DeepStack>::effect_row, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>);
static_assert(std::is_same_v<payload_effect_row_t<DeepStack>, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>);

struct DSPermTag {};

using IPS_empty = ::crucible::safety::proto::EmptyPermSet;
static_assert(std::is_same_v<payload_row_t<DelegatedSession<End, IPS_empty>>, eff::Row<>>);
static_assert(std::is_same_v<protocol_effect_row_t<End>, eff::Row<>>);

using InnerIo = Send<eff::Computation<eff::Row<eff::Effect::IO>, int>, End>;
static_assert(std::is_same_v<protocol_effect_row_t<InnerIo>, eff::Row<eff::Effect::IO>>);
static_assert(std::is_same_v<payload_row_t<DelegatedSession<InnerIo, IPS_empty>>, eff::Row<eff::Effect::IO>>);

using InnerBgAlloc = Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>,
                          Recv<eff::Computation<eff::Row<eff::Effect::Alloc>, int>, End>>;
static_assert(::crucible::effects::is_subrow_v<eff::Row<eff::Effect::Bg, eff::Effect::Alloc>,
                                               protocol_effect_row_t<InnerBgAlloc>>);
static_assert(::crucible::effects::is_subrow_v<protocol_effect_row_t<InnerBgAlloc>,
                                               eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>);
static_assert(::crucible::effects::is_subrow_v<eff::Row<eff::Effect::Bg, eff::Effect::Alloc>,
                                               payload_row_t<DelegatedSession<InnerBgAlloc, IPS_empty>>>);

using InnerLoopIo = Loop<Send<eff::Computation<eff::Row<eff::Effect::IO>, int>, Continue>>;
static_assert(std::is_same_v<payload_row_t<DelegatedSession<InnerLoopIo, IPS_empty>>, eff::Row<eff::Effect::IO>>);

using InnerSelectIo = Select<End, Send<eff::Computation<eff::Row<eff::Effect::IO>, int>, End>>;
static_assert(std::is_same_v<protocol_effect_row_t<InnerSelectIo>, eff::Row<eff::Effect::IO>>);

// The delegated arm is bypassed at the delegator's row gate, so only
// the continuation contributes.
using InnerDelegate = Delegate<Send<eff::Computation<eff::Row<eff::Effect::Block>, int>, End>,
                               Send<eff::Computation<eff::Row<eff::Effect::Bg>, int>, End>>;
static_assert(std::is_same_v<protocol_effect_row_t<InnerDelegate>, eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<protocol_effect_row_t<Stop_g<CrashClass::Abort>>, eff::Row<>>);
static_assert(std::is_same_v<protocol_effect_row_t<Stop>, eff::Row<>>);

// Both ends of the composition boundary are pinned, so the row cannot
// narrow when something is composed onto a delegation of a crashed
// carrier.
using InnerBgRecv = Recv<eff::Computation<eff::Row<eff::Effect::Bg>, int>, End>;
using DelegateStopWithBg = Delegate<Stop_g<CrashClass::Abort>, InnerBgRecv>;

static_assert(std::is_same_v<protocol_effect_row_t<DelegateStopWithBg>, eff::Row<>>,
              "delegating a crashed carrier must carry an empty row, because the continuation is unreachable");

using DelegateStopComposed = ::crucible::safety::proto::compose_t<DelegateStopWithBg, End>;
static_assert(std::is_same_v<DelegateStopComposed, Stop_g<CrashClass::Abort>>);
static_assert(std::is_same_v<protocol_effect_row_t<DelegateStopComposed>, eff::Row<>>,
              "composing onto a delegation of a crashed carrier yields Stop, whose row must match the row before "
              "composition");

static_assert(std::is_same_v<protocol_effect_row_t<DelegateStopWithBg>, protocol_effect_row_t<DelegateStopComposed>>,
              "the effect row must not narrow under composition");

using AcceptStopWithBg = Accept<Stop_g<CrashClass::Abort>, InnerBgRecv>;
static_assert(std::is_same_v<protocol_effect_row_t<AcceptStopWithBg>, eff::Row<>>);
using AcceptStopComposed = ::crucible::safety::proto::compose_t<AcceptStopWithBg, End>;
static_assert(std::is_same_v<AcceptStopComposed, Stop_g<CrashClass::Abort>>);
static_assert(std::is_same_v<protocol_effect_row_t<AcceptStopComposed>, eff::Row<>>);
static_assert(std::is_same_v<protocol_effect_row_t<AcceptStopWithBg>, protocol_effect_row_t<AcceptStopComposed>>);

using EpochedDelegateStopWithBg = EpochedDelegate<Stop_g<CrashClass::Abort>, InnerBgRecv, 1, 1>;
static_assert(std::is_same_v<protocol_effect_row_t<EpochedDelegateStopWithBg>, eff::Row<>>);
using EpochedDelegateStopComposed = ::crucible::safety::proto::compose_t<EpochedDelegateStopWithBg, End>;
static_assert(std::is_same_v<EpochedDelegateStopComposed, Stop_g<CrashClass::Abort>>);
static_assert(std::is_same_v<protocol_effect_row_t<EpochedDelegateStopComposed>, eff::Row<>>);

using EpochedAcceptStopWithBg = EpochedAccept<Stop_g<CrashClass::Abort>, InnerBgRecv, 1, 1>;
static_assert(std::is_same_v<protocol_effect_row_t<EpochedAcceptStopWithBg>, eff::Row<>>);
using EpochedAcceptStopComposed = ::crucible::safety::proto::compose_t<EpochedAcceptStopWithBg, End>;
static_assert(std::is_same_v<EpochedAcceptStopComposed, Stop_g<CrashClass::Abort>>);
static_assert(std::is_same_v<protocol_effect_row_t<EpochedAcceptStopComposed>, eff::Row<>>);

// The collapse is scoped to a crashed carrier alone, so a live
// delegation still reports its continuation's row.
using DelegateLiveWithBg = Delegate<End, InnerBgRecv>;
static_assert(std::is_same_v<protocol_effect_row_t<DelegateLiveWithBg>, eff::Row<eff::Effect::Bg>>,
              "delegating a live carrier must still walk the continuation");

using OuterSendCarryingDelegated = Send<DelegatedSession<InnerIo, IPS_empty>, End>;
static_assert(std::is_same_v<protocol_effect_row_t<OuterSendCarryingDelegated>, eff::Row<eff::Effect::IO>>);

}  // namespace detail::payload_row_self_test

[[gnu::cold]] inline void runtime_smoke_test_payload_row() noexcept {
    namespace eff = ::crucible::effects;
    namespace saf = ::crucible::safety;

    static_assert(std::is_same_v<payload_row_t<int>, eff::Row<>>);

    // The background context has no public default constructor, so this
    // routes through the test-only witness path.
    auto bg = eff::testing::bg();
    auto cap = eff::mint_cap<eff::Effect::Alloc>(bg);
    static_assert(std::is_same_v<payload_row_t<decltype(cap)>, eff::Row<eff::Effect::Alloc>>);
    static_cast<void>(cap);

    using ComposedT = saf::Refined<saf::positive, eff::Computation<eff::Row<eff::Effect::Bg>, int>>;
    static_assert(std::is_same_v<payload_row_t<ComposedT>, eff::Row<eff::Effect::Bg>>);
}

}  // namespace crucible::safety::proto
