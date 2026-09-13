#pragma once

// Binding a channel handle to an execution context checks the fit between the
// two once, at construction.  Every send and receive afterwards runs with no
// further check.
//
// The channel outlives the endpoint by construction: the endpoint holds a
// borrowed handle and owns nothing that has to be released.

#include <crucible/Platform.h>
#include <crucible/concurrent/SubstrateCtxFit.h>
#include <crucible/concurrent/SubstrateSessionBridge.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/sessions/PermissionedSession.h>

#include <optional>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

namespace endpoint_detail {

template <class Workload>
struct workload_shape {
    static constexpr bool has_channel_shape = false;
    static constexpr bool has_bytes = false;
    static constexpr std::size_t bytes = 0;
    static constexpr std::size_t producers = 0;
    static constexpr std::size_t consumers = 0;
    static constexpr bool latest_only = false;
};

template <std::size_t Bytes>
struct workload_shape<::crucible::effects::ctx_workload::ByteBudget<Bytes>> {
    static constexpr bool has_channel_shape = false;
    static constexpr bool has_bytes = true;
    static constexpr std::size_t bytes = Bytes;
    static constexpr std::size_t producers = 0;
    static constexpr std::size_t consumers = 0;
    static constexpr bool latest_only = false;
};

template <std::size_t Bytes, std::size_t Producers, std::size_t Consumers, bool LatestOnly>
struct workload_shape<::crucible::effects::ctx_workload::ChannelBudget<Bytes, Producers, Consumers, LatestOnly>> {
    static constexpr bool has_channel_shape = true;
    static constexpr bool has_bytes = true;
    static constexpr std::size_t bytes = Bytes;
    static constexpr std::size_t producers = Producers;
    static constexpr std::size_t consumers = Consumers;
    static constexpr bool latest_only = LatestOnly;
};

// has_channel_shape stays false despite the name of the hint.  The
// recommendation below reads it to decide whether both cardinalities come from
// the hint, and a producer-only shape has no consumer count to offer, so the
// consumer side has to fall back to the substrate's own topology.  The
// producer count is still exposed for scheduling decisions.
template <std::size_t Bytes, std::size_t Producers, bool LatestOnly>
struct workload_shape<::crucible::effects::ctx_workload::ProducerOnlyChannel<Bytes, Producers, LatestOnly>> {
    static constexpr bool has_channel_shape = false;
    static constexpr bool has_bytes = true;
    static constexpr std::size_t bytes = Bytes;
    static constexpr std::size_t producers = Producers;
    static constexpr std::size_t consumers = 0;
    static constexpr bool latest_only = LatestOnly;
};

// Mirror of the producer-only shape above, with the producer side falling
// back to the substrate's own topology.
template <std::size_t Bytes, std::size_t Consumers>
struct workload_shape<::crucible::effects::ctx_workload::ConsumerOnlyChannel<Bytes, Consumers>> {
    static constexpr bool has_channel_shape = false;
    static constexpr bool has_bytes = true;
    static constexpr std::size_t bytes = Bytes;
    static constexpr std::size_t producers = 0;
    static constexpr std::size_t consumers = Consumers;
    static constexpr bool latest_only = false;
};

template <ChannelTopology Topology>
struct default_topology_shape {
    static constexpr std::size_t producers = 1;
    static constexpr std::size_t consumers = 1;
    static constexpr bool latest_only = false;
};

template <>
struct default_topology_shape<ChannelTopology::ManyToOne> {
    static constexpr std::size_t producers = 2;
    static constexpr std::size_t consumers = 1;
    static constexpr bool latest_only = false;
};

template <>
struct default_topology_shape<ChannelTopology::OneToMany_Latest> {
    static constexpr std::size_t producers = 1;
    static constexpr std::size_t consumers = 2;
    static constexpr bool latest_only = true;
};

template <>
struct default_topology_shape<ChannelTopology::ManyToMany> {
    static constexpr std::size_t producers = 2;
    static constexpr std::size_t consumers = 2;
    static constexpr bool latest_only = false;
};

template <>
struct default_topology_shape<ChannelTopology::WorkStealing> {
    static constexpr std::size_t producers = 1;
    static constexpr std::size_t consumers = 2;
    static constexpr bool latest_only = false;
};

template <IsSubstrate Substr, ::crucible::effects::IsExecCtx Ctx>
struct endpoint_recommendation {
    using shape = workload_shape<typename Ctx::workload_hint>;
    static constexpr ChannelTopology selected = substrate_topology_v<Substr>;
    using fallback = default_topology_shape<selected>;

    static constexpr std::size_t workload_bytes = shape::has_bytes ? shape::bytes : channel_byte_footprint_v<Substr>;
    static constexpr std::size_t producers = shape::has_channel_shape ? shape::producers : fallback::producers;
    static constexpr std::size_t consumers = shape::has_channel_shape ? shape::consumers : fallback::consumers;
    static constexpr bool latest_only = shape::has_channel_shape ? shape::latest_only : fallback::latest_only;

    static constexpr ChannelTopology recommended =
        recommend_topology_for_workload(producers, consumers, workload_bytes, latest_only);

    static constexpr bool exact = selected == recommended;
    static constexpr bool explicit_work_stealing = selected == ChannelTopology::WorkStealing;
    static constexpr bool overprovisioned =
        selected == ChannelTopology::ManyToMany && recommended == ChannelTopology::OneToOne;
    static constexpr bool admissible = exact || explicit_work_stealing || overprovisioned;
};

}  // namespace endpoint_detail

template <class Substr, class Ctx>
concept SubstrateMatchesEndpointRecommendation = IsSubstrate<Substr> && ::crucible::effects::IsExecCtx<Ctx>
                                              && endpoint_detail::endpoint_recommendation<Substr, Ctx>::admissible;

// The class template, the friend declaration on the private constructor and
// the mint factory all spell this one gate.  Splitting it into separate
// conjunctions at each site would stop the friend declaration from matching
// the factory the moment the three drift apart.
template <class Substr, Direction Dir, class Ctx>
concept CtxFitsEndpointMint = IsBridgeableDirection<Substr, Dir> && SubstrateFitsCtxResidency<Substr, Ctx>
                           && SubstrateMatchesEndpointRecommendation<Substr, Ctx>;

template <class Substr, Direction Dir, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsEndpointMint<Substr, Dir, Ctx>
class [[nodiscard]] Endpoint {
public:
    using substrate_type = Substr;
    static constexpr Direction direction = Dir;
    using ctx_type = Ctx;
    using handle_type = handle_for_t<Substr, Dir>;
    using value_type = substrate_value_type_t<Substr>;
    using user_tag = substrate_user_tag_t<Substr>;
    using proto_type = default_proto_for_t<Substr, Dir>;

    static constexpr bool ctx_is_hot = ::crucible::effects::IsHotCtx<Ctx>;
    static constexpr bool ctx_is_warm = ::crucible::effects::IsWarmCtx<Ctx>;
    static constexpr bool ctx_is_cold = ::crucible::effects::IsColdCtx<Ctx>;
    static constexpr bool ctx_is_arena = ::crucible::effects::IsArenaCtx<Ctx>;
    static constexpr bool ctx_is_numa_local = IsNumaLocalCtx<Ctx>;
    static constexpr Tier residency_tier_v = ctx_residency_tier<Ctx>();

    // benefits_from_parallelism reports that total storage crosses the cache
    // cliff, so sharding the channel could pay.  It is advice for whoever
    // places the data, not a rejection: one producer and one consumer moving
    // that much through a single ring stays valid, because the hot path only
    // has to keep per_call_working_set resident and that does not grow with
    // capacity.
    static constexpr bool benefits_from_parallelism = SubstrateBenefitsFromParallelism<Substr>;
    static constexpr std::size_t per_call_working_set = per_call_working_set_v<Substr>;
    static constexpr std::size_t total_channel_bytes = channel_byte_footprint_v<Substr>;
    static constexpr std::size_t recommendation_workload_bytes =
        endpoint_detail::endpoint_recommendation<Substr, Ctx>::workload_bytes;
    static constexpr std::size_t recommendation_producers =
        endpoint_detail::endpoint_recommendation<Substr, Ctx>::producers;
    static constexpr std::size_t recommendation_consumers =
        endpoint_detail::endpoint_recommendation<Substr, Ctx>::consumers;
    static constexpr bool recommendation_latest_only =
        endpoint_detail::endpoint_recommendation<Substr, Ctx>::latest_only;
    static constexpr ChannelTopology recommended_topology =
        endpoint_detail::endpoint_recommendation<Substr, Ctx>::recommended;
    static constexpr bool topology_matches_recommendation =
        endpoint_detail::endpoint_recommendation<Substr, Ctx>::exact;
    static constexpr bool topology_overprovisioned =
        endpoint_detail::endpoint_recommendation<Substr, Ctx>::overprovisioned;

private:
    // A pointer, not a reference: the handle itself holds a reference to its
    // channel and so cannot be move-assigned.  Holding it indirectly lets the
    // endpoint move without disturbing the one-handle-per-channel invariant.
    handle_type* handle_;

    [[no_unique_address]] Ctx ctx_;

    template <class S, Direction D, ::crucible::effects::IsExecCtx C>
        requires CtxFitsEndpointMint<S, D, C>
    friend constexpr auto mint_endpoint(C const&, handle_for_t<S, D>&) noexcept;

    constexpr explicit Endpoint(handle_type& h) noexcept : handle_{&h}, ctx_{} {}

public:
    // The move is written out because the default one would copy the pointer
    // and leave the source usable.  Sending on a moved-from endpoint would
    // then quietly succeed against the same handle, giving two typed views of
    // one channel.  Nulling the source turns that into a null dereference
    // instead.

    Endpoint(Endpoint const&) = delete("Endpoint owns the typed view of a linear handle — copy would "
                                       "duplicate the producer/consumer Permission's typed projection.  "
                                       "Use std::move to transfer; or mint a second Endpoint if the "
                                       "underlying substrate is multi-producer/multi-consumer.");
    Endpoint& operator=(Endpoint const&) = delete("Endpoint owns the typed view of a linear handle.");

    constexpr Endpoint(Endpoint&& o) noexcept : handle_{o.handle_}, ctx_{} { o.handle_ = nullptr; }
    constexpr Endpoint& operator=(Endpoint&& o) noexcept {
        if (this != &o) [[likely]] {
            handle_ = o.handle_;
            o.handle_ = nullptr;
        }
        return *this;
    }
    ~Endpoint() = default;

    template <class T = value_type>
        requires(Dir == Direction::Producer || Dir == Direction::Owner)
             && std::same_as<std::remove_cvref_t<T>, value_type>
    [[nodiscard, gnu::hot]] bool try_send(T const& v) noexcept {
        return handle_->try_push(v);
    }

    template <class T = value_type>
        requires(Dir == Direction::SwmrWriter) && std::same_as<std::remove_cvref_t<T>, value_type>
    [[gnu::hot]] void publish(T const& v) noexcept {
        handle_->publish(v);
    }

    template <Direction D = Dir>
        requires(D == Direction::Consumer || D == Direction::Owner || D == Direction::Thief)
    [[nodiscard, gnu::hot]] std::optional<value_type> try_recv() noexcept {
        if constexpr (D == Direction::Thief) {
            return handle_->try_steal();
        } else {
            return handle_->try_pop();
        }
    }

    template <Direction D = Dir>
        requires(D == Direction::SwmrReader)
    [[nodiscard, gnu::hot]] value_type load() noexcept {
        return handle_->load();
    }

    [[nodiscard]] bool empty_approx() const noexcept
        requires requires(handle_type const& h) { h.empty_approx(); }
    {
        return handle_->empty_approx();
    }
    [[nodiscard]] std::size_t size_approx() const noexcept
        requires requires(handle_type const& h) { h.size_approx(); }
    {
        return handle_->size_approx();
    }

    // The session carries an empty permission set.  The handle's own
    // permission already fixes how many producers or consumers there may be,
    // so the protocol has nothing further to transfer per step.

    [[nodiscard]] constexpr auto into_session() && noexcept {
        return mint_substrate_session<Substr, Dir>(ctx_, *handle_);
    }

    // Dropping the empty permission set to hand out a bare session handle is
    // safe for the same reason: an empty set evolves at no step of the
    // protocol.

    [[nodiscard]] constexpr auto into_bare_session() && noexcept {
        return ::crucible::safety::proto::mint_session_handle<proto_type>(handle_);
    }

    [[nodiscard]] constexpr handle_type& handle() & noexcept { return *handle_; }
    [[nodiscard]] constexpr handle_type const& handle() const& noexcept { return *handle_; }

    // This leaves two objects moved-from, not one.  The caller's own handle,
    // the lvalue this endpoint was minted over, gives up its permission to the
    // returned handle, but its reference to the channel cannot be unbound, so
    // sending on it afterwards would still reach the ring.  The caller must
    // treat that lvalue as dead from here on.  The returned handle is the only
    // one holding the permission, and it releases it when it goes out of
    // scope.

    [[nodiscard]] constexpr handle_type into_handle() && noexcept {
        handle_type extracted = std::move(*handle_);
        handle_ = nullptr;
        return extracted;
    }

    [[nodiscard]] constexpr Ctx ctx() const noexcept { return ctx_; }
};

template <class Substr, Direction Dir, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsEndpointMint<Substr, Dir, Ctx>
[[nodiscard]] constexpr auto mint_endpoint(Ctx const&, handle_for_t<Substr, Dir>& handle) noexcept {
    return Endpoint<Substr, Dir, Ctx>{handle};
}

namespace detail::endpoint_self_test {

namespace eff = ::crucible::effects;
namespace proto = ::crucible::safety::proto;

struct UserTag {};
using SmallSpsc = PermissionedSpscChannel<int, 64, UserTag>;
using ProdEp = Endpoint<SmallSpsc, Direction::Producer, eff::HotFgCtx>;
using ConsEp = Endpoint<SmallSpsc, Direction::Consumer, eff::BgDrainCtx>;
using SmallDeque = PermissionedChaseLevDeque<int, 64, UserTag>;
using OwnerEp = Endpoint<SmallDeque, Direction::Owner, eff::HotFgCtx>;
using ThiefEp = Endpoint<SmallDeque, Direction::Thief, eff::HotFgCtx>;
using SmallOneToOneCtx =
    eff::ExecCtx<eff::ctx_cap::Fg, eff::ctx_numa::Local, eff::ctx_alloc::Stack, eff::ctx_heat::Hot, eff::ctx_resid::L1,
                 eff::Row<>, eff::ctx_workload::ChannelBudget<16 * 1024, 1, 1, false>>;
using HugeManyToManyCtx =
    eff::ExecCtx<eff::ctx_cap::Fg, eff::ctx_numa::Local, eff::ctx_alloc::Stack, eff::ctx_heat::Hot, eff::ctx_resid::L1,
                 eff::Row<>, eff::ctx_workload::ChannelBudget<16ULL * 1024ULL * 1024ULL * 1024ULL, 4, 4, false>>;
using SmallOneToOneOverprovisionedCtx =
    eff::ExecCtx<eff::ctx_cap::Fg, eff::ctx_numa::Local, eff::ctx_alloc::Stack, eff::ctx_heat::Hot, eff::ctx_resid::L1,
                 eff::Row<>, eff::ctx_workload::ChannelBudget<16 * 1024, 1, 1, false>>;

static_assert(std::is_same_v<typename ProdEp::handle_type, typename SmallSpsc::ProducerHandle>);
static_assert(std::is_same_v<typename ProdEp::value_type, int>);
static_assert(std::is_same_v<typename ProdEp::ctx_type, eff::HotFgCtx>);
static_assert(std::is_same_v<typename ProdEp::proto_type, proto::Loop<proto::Send<int, proto::Continue>>>);
static_assert(std::is_same_v<typename OwnerEp::handle_type, typename SmallDeque::OwnerHandle>);
static_assert(std::is_same_v<typename ThiefEp::handle_type, typename SmallDeque::ThiefHandle>);
static_assert(std::is_same_v<typename OwnerEp::proto_type, proto::chaselev_session::OwnerProto<int>>);
static_assert(
    std::is_same_v<typename ThiefEp::proto_type, proto::chaselev_session::ThiefProto<int, SmallDeque::thief_tag>>);

static_assert(ProdEp::ctx_is_hot);
static_assert(!ProdEp::ctx_is_warm);
static_assert(!ProdEp::ctx_is_cold);
static_assert(ProdEp::ctx_is_numa_local);
static_assert(ProdEp::residency_tier_v == Tier::L1Resident);

static_assert(ConsEp::ctx_is_warm);
static_assert(!ConsEp::ctx_is_hot);
static_assert(ConsEp::ctx_is_arena);
static_assert(ConsEp::residency_tier_v == Tier::L2Resident);

static_assert(!ProdEp::benefits_from_parallelism);
static_assert(ProdEp::per_call_working_set == 192);  // head line, tail line, cell line
static_assert(ProdEp::total_channel_bytes == sizeof(int) * 64);
static_assert(ProdEp::recommended_topology == ChannelTopology::OneToOne);
static_assert(ProdEp::topology_matches_recommendation);

using SmallBudgetSpscEp = Endpoint<SmallSpsc, Direction::Producer, SmallOneToOneCtx>;
static_assert(SmallBudgetSpscEp::recommended_topology == ChannelTopology::OneToOne);
static_assert(SmallBudgetSpscEp::topology_matches_recommendation);

using SmallMpmc = PermissionedMpmcChannel<int, 64, UserTag>;
using HugeBudgetMpmcEp = Endpoint<SmallMpmc, Direction::Producer, HugeManyToManyCtx>;
static_assert(HugeBudgetMpmcEp::recommended_topology == ChannelTopology::ManyToMany);
static_assert(HugeBudgetMpmcEp::topology_matches_recommendation);

using OverprovisionedMpmcEp = Endpoint<SmallMpmc, Direction::Producer, SmallOneToOneOverprovisionedCtx>;
static_assert(OverprovisionedMpmcEp::recommended_topology == ChannelTopology::OneToOne);
static_assert(!OverprovisionedMpmcEp::topology_matches_recommendation);
static_assert(OverprovisionedMpmcEp::topology_overprovisioned);

// A ring far above the cliff still pairs with a hot context: the gate reads
// the per-call footprint, not total storage.
using HugeSpsc = PermissionedSpscChannel<int, 1024 * 1024, UserTag>;
using HugeProdEp = Endpoint<HugeSpsc, Direction::Producer, eff::HotFgCtx>;
static_assert(HugeProdEp::ctx_is_hot);
static_assert(HugeProdEp::residency_tier_v == Tier::L1Resident);
static_assert(HugeProdEp::per_call_working_set == 192);
static_assert(HugeProdEp::total_channel_bytes == 4 * 1024 * 1024);
static_assert(HugeProdEp::benefits_from_parallelism);

static_assert(!std::is_copy_constructible_v<ProdEp>);
static_assert(!std::is_copy_assignable_v<ProdEp>);
static_assert(std::is_move_constructible_v<ProdEp>);
static_assert(std::is_move_assignable_v<ProdEp>);

static_assert(sizeof(ProdEp) == sizeof(void*), "Endpoint must collapse to pointer-size — Ctx EBO-collapse is "
                                               "load-bearing for the zero-runtime-cost claim.");
static_assert(sizeof(ConsEp) == sizeof(void*));
static_assert(sizeof(OwnerEp) == sizeof(void*));
static_assert(sizeof(ThiefEp) == sizeof(void*));

struct SnapTag {};
using SmallSnap = PermissionedSnapshot<int, SnapTag>;
using SnapWriter = Endpoint<SmallSnap, Direction::SwmrWriter, eff::HotFgCtx>;
using SnapReader = Endpoint<SmallSnap, Direction::SwmrReader, eff::BgDrainCtx>;

static_assert(std::is_same_v<typename SnapWriter::handle_type, typename SmallSnap::WriterHandle>);
static_assert(std::is_same_v<typename SnapReader::handle_type, typename SmallSnap::ReaderHandle>);
static_assert(SnapWriter::ctx_is_hot);

static_assert(std::is_nothrow_move_constructible_v<ProdEp>);
static_assert(std::is_nothrow_move_assignable_v<ProdEp>);

}  // namespace detail::endpoint_self_test

}  // namespace crucible::concurrent
