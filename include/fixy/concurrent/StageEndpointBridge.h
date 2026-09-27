#pragma once

// The bridge is a free function here rather than a method on the endpoint
// so that a translation unit which only builds endpoints never pulls in the
// stage machinery.
//
// Old spelling: include/crucible/concurrent/_StageEndpointBridge.h.  Three
// things changed.
//   - Each mint takes its endpoints by move and refuses an lvalue.  The old
//     mints took forwarding references and moved from them, so an lvalue
//     endpoint was consumed without a std::move at the call site.
//   - MpmcStage and SwmrStage are built through StageEndpointDoor, a final
//     class with private static members and the two mints as its friends.
//     The old unconstrained helpers were callable from any code.
//   - The two mints check the row in the requires clause alone, as
//     mint_stage of fixy/concurrent/Stage.h does.  The old mints also
//     repeated the check in the body, where a refused row never arrives.

#include <fixy/concurrent/Endpoint.h>
#include <fixy/concurrent/HandleTraits.h>
#include <fixy/concurrent/Stage.h>
#include <fixy/concurrent/StageShape.h>

#include <foundation/Platform.h>
#include <foundation/contracts/Armed.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Signature.h>

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fixy::concurrent {

namespace detail {

template <class T>
struct is_endpoint : std::false_type {};

template <class Substr, Direction Dir, ::foundation::effects::IsExecCtx Ctx>
struct is_endpoint<Endpoint<Substr, Dir, Ctx>> : std::true_type {
    static constexpr Direction direction = Dir;
};

}  // namespace detail

template <class E>
concept IsEndpoint = detail::is_endpoint<std::remove_cvref_t<E>>::value;

template <class E>
concept IsConsumerEndpoint =
    IsEndpoint<E> && (detail::is_endpoint<std::remove_cvref_t<E>>::direction == Direction::Consumer);

template <class E>
concept IsProducerEndpoint =
    IsEndpoint<E> && (detail::is_endpoint<std::remove_cvref_t<E>>::direction == Direction::Producer);

// An endpoint that a mint consumes must come in as an rvalue.  E is the
// deduced type of a forwarding parameter, so an lvalue argument deduces an
// lvalue reference.
template <class E>
concept IsMovedEndpoint = IsEndpoint<E> && !std::is_lvalue_reference_v<E>;

// Without this check, handing a channel of one payload type to a body that
// expects another would only fail far below, where the parameters are bound.
// Checking it here turns that into one failed constraint at the call site.

template <auto FnPtr, class ConsumerEp, class ProducerEp>
concept StageHandlesMatchEndpoints =
    PipelineStage<FnPtr>
    && std::is_same_v<typename std::remove_cvref_t<ConsumerEp>::handle_type,
                      std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 0>>>
    && std::is_same_v<typename std::remove_cvref_t<ProducerEp>::handle_type,
                      std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 1>>>;

template <class... Endpoints>
struct EndpointPack {};

namespace detail {

template <auto FnPtr, class Inputs, class Outputs>
struct stage_handles_match_endpoints_extended : std::false_type {};

template <auto FnPtr, class... ConsumerEps, class... ProducerEps>
struct stage_handles_match_endpoints_extended<FnPtr, EndpointPack<ConsumerEps...>, EndpointPack<ProducerEps...>> {
private:
    using extract = StageArity<FnPtr>;
    using consumer_tuple = std::tuple<ConsumerEps...>;
    using producer_tuple = std::tuple<ProducerEps...>;

    template <class Endpoint, std::size_t I>
    static consteval bool consumer_endpoint_matches_param() noexcept {
        using endpoint = std::remove_cvref_t<Endpoint>;
        if constexpr (I >= ::foundation::reflect::arity_v<FnPtr>) {
            return false;
        } else if constexpr (!IsEndpoint<endpoint>) {
            return false;
        } else if constexpr (is_endpoint<endpoint>::direction != Direction::Consumer) {
            return false;
        } else {
            return std::is_same_v<typename endpoint::handle_type,
                                  std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, I>>>;
        }
    }

    template <class Endpoint, std::size_t I>
    static consteval bool producer_endpoint_matches_param() noexcept {
        using endpoint = std::remove_cvref_t<Endpoint>;
        if constexpr (I >= ::foundation::reflect::arity_v<FnPtr>) {
            return false;
        } else if constexpr (!IsEndpoint<endpoint>) {
            return false;
        } else if constexpr (is_endpoint<endpoint>::direction != Direction::Producer) {
            return false;
        } else {
            return std::is_same_v<typename endpoint::handle_type,
                                  std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, I>>>;
        }
    }

    template <std::size_t... Is>
    static consteval bool input_types_match(std::index_sequence<Is...>) noexcept {
        return (consumer_endpoint_matches_param<std::tuple_element_t<Is, consumer_tuple>, Is>() && ...);
    }

    template <std::size_t... Is>
    static consteval bool output_types_match(std::index_sequence<Is...>) noexcept {
        constexpr std::size_t offset = extract::input_count;
        return (producer_endpoint_matches_param<std::tuple_element_t<Is, producer_tuple>, offset + Is>() && ...);
    }

    static consteval bool compute() noexcept {
        if constexpr (!VariadicPipelineStage<FnPtr>) {
            return false;
        } else if constexpr (extract::input_count != sizeof...(ConsumerEps)
                             || extract::output_count != sizeof...(ProducerEps)) {
            return false;
        } else {
            return input_types_match(std::make_index_sequence<sizeof...(ConsumerEps)>{})
                && output_types_match(std::make_index_sequence<sizeof...(ProducerEps)>{});
        }
    }

public:
    static constexpr bool value = compute();
};

}  // namespace detail

template <auto FnPtr, class Inputs, class Outputs>
concept StageHandlesMatchEndpointsExtended =
    detail::stage_handles_match_endpoints_extended<FnPtr, Inputs, Outputs>::value;

namespace detail {

template <class Tuple, class Seq>
struct endpoint_pack_from_tuple_indices;

template <class Tuple, std::size_t... Is>
struct endpoint_pack_from_tuple_indices<Tuple, std::index_sequence<Is...>> {
    using type = EndpointPack<std::tuple_element_t<Is, Tuple>...>;
};

template <std::size_t Offset, class Tuple, class Seq>
struct endpoint_pack_from_tuple_offset_indices;

template <std::size_t Offset, class Tuple, std::size_t... Is>
struct endpoint_pack_from_tuple_offset_indices<Offset, Tuple, std::index_sequence<Is...>> {
    using type = EndpointPack<std::tuple_element_t<Offset + Is, Tuple>...>;
};

template <std::size_t N, class... Endpoints>
using endpoint_take_pack_t =
    typename endpoint_pack_from_tuple_indices<std::tuple<Endpoints...>, std::make_index_sequence<N>>::type;

template <std::size_t N, class... Endpoints>
using endpoint_drop_pack_t =
    typename endpoint_pack_from_tuple_offset_indices<N, std::tuple<Endpoints...>,
                                                     std::make_index_sequence<sizeof...(Endpoints) - N>>::type;

template <auto FnPtr, class Ctx, class... Endpoints>
struct mpmc_stage_from_endpoints_gate {
private:
    using arity = StageArity<FnPtr>;

    static consteval bool compute() noexcept {
        if constexpr (!VariadicPipelineStage<FnPtr> || !::foundation::effects::IsExecCtx<Ctx>
                      || sizeof...(Endpoints) != ::foundation::reflect::arity_v<FnPtr>) {
            return false;
        } else {
            using inputs = endpoint_take_pack_t<arity::input_count, std::remove_cvref_t<Endpoints>...>;
            using outputs = endpoint_drop_pack_t<arity::input_count, std::remove_cvref_t<Endpoints>...>;
            return (IsMovedEndpoint<Endpoints> && ...) && CtxFitsVariadicStage<FnPtr, Ctx>
                && StageHandlesMatchEndpointsExtended<FnPtr, inputs, outputs>;
        }
    }

public:
    static constexpr bool value = compute();
};

template <auto FnPtr, class Ctx, class ConsumerEp, class Writer>
struct swmr_stage_from_endpoint_gate {
private:
    static consteval bool compute() noexcept {
        using consumer_ep = std::remove_cvref_t<ConsumerEp>;
        using writer = std::remove_cvref_t<Writer>;
        if constexpr (!CtxFitsSwmrPublishStage<FnPtr, Ctx> || !IsConsumerEndpoint<consumer_ep>
                      || !IsMovedEndpoint<ConsumerEp> || std::is_lvalue_reference_v<Writer>
                      || !is_swmr_writer_v<writer>) {
            return false;
        } else {
            return std::is_same_v<typename consumer_ep::handle_type,
                                  std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 0>>>
                && std::is_same_v<writer, std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 1>>>;
        }
    }

public:
    static constexpr bool value = compute();
};

}  // namespace detail

template <auto FnPtr, class Ctx, class... Endpoints>
concept CtxFitsMpmcStageFromEndpoints = detail::mpmc_stage_from_endpoints_gate<FnPtr, Ctx, Endpoints...>::value;

template <auto FnPtr, class Ctx, class ConsumerEp, class Writer>
concept CtxFitsSwmrStageFromEndpoint = detail::swmr_stage_from_endpoint_gate<FnPtr, Ctx, ConsumerEp, Writer>::value;

template <auto FnPtr, class Ctx, class ConsumerEp, class ProducerEp>
concept CtxFitsStageFromEndpoints =
    CtxFitsStage<FnPtr, Ctx> && IsConsumerEndpoint<ConsumerEp> && IsProducerEndpoint<ProducerEp>
    && IsMovedEndpoint<ConsumerEp> && IsMovedEndpoint<ProducerEp>
    && StageHandlesMatchEndpoints<FnPtr, ConsumerEp, ProducerEp>;

template <auto FnPtr, ::foundation::effects::IsExecCtx Ctx, class ConsumerEp, class ProducerEp>
    requires CtxFitsStageFromEndpoints<FnPtr, Ctx, ConsumerEp, ProducerEp>
[[nodiscard]] constexpr auto mint_stage_from_endpoints(Ctx const& ctx, ConsumerEp&& in_ep,
                                                       ProducerEp&& out_ep) noexcept {
    return mint_stage<FnPtr>(ctx, std::move(in_ep).into_handle(), std::move(out_ep).into_handle());
}

template <auto FnPtr, ::foundation::effects::IsExecCtx Ctx, class... Endpoints>
    requires CtxFitsMpmcStageFromEndpoints<FnPtr, Ctx, Endpoints...>
[[nodiscard]] constexpr auto mint_mpmc_stage_from_endpoints(Ctx const& ctx, Endpoints&&... endpoints) noexcept {
    std::tuple<std::remove_cvref_t<Endpoints>...> endpoint_tuple{std::move(endpoints)...};
    return ::fixy::session::detail::late_door_t<StageEndpointDoor, Ctx>::template make_mpmc_<FnPtr>(ctx,
                                                                                                  endpoint_tuple);
}

template <auto FnPtr, ::foundation::effects::IsExecCtx Ctx, class ConsumerEp, class Writer>
    requires CtxFitsSwmrStageFromEndpoint<FnPtr, Ctx, ConsumerEp, Writer>
[[nodiscard]] constexpr auto mint_swmr_stage(Ctx const& ctx, ConsumerEp&& in_ep, Writer&& writer) noexcept {
    return ::fixy::session::detail::late_door_t<StageEndpointDoor, Ctx>::template make_swmr_<FnPtr>(
        ctx, std::move(in_ep).into_handle(), std::move(writer));
}

// ── The door of the stage mints ──────────────────────────────────────
//
// mint_mpmc_stage_from_endpoints and mint_swmr_stage get their stage
// through this class.  Its members are private and static.  Its friends are
// the two mints, which do the check of the body, the context and the
// endpoints before they call a member.  Each member does its check again
// and builds the stage.  The class is final, and no object of it exists.
class StageEndpointDoor final {
    StageEndpointDoor() = delete("the stage door holds static members only, and no object of it exists");
    StageEndpointDoor(const StageEndpointDoor&) = delete("the stage door holds static members only");
    StageEndpointDoor& operator=(const StageEndpointDoor&) = delete("the stage door holds static members only");
    StageEndpointDoor(StageEndpointDoor&&) = delete("the stage door holds static members only");
    StageEndpointDoor& operator=(StageEndpointDoor&&) = delete("the stage door holds static members only");
    constexpr ~StageEndpointDoor() noexcept {}

    template <auto FnPtr, ::foundation::effects::IsExecCtx Ctx, class... Endpoints>
        requires CtxFitsMpmcStageFromEndpoints<FnPtr, Ctx, Endpoints...>
    friend constexpr auto mint_mpmc_stage_from_endpoints(Ctx const& ctx, Endpoints&&... endpoints) noexcept;

    template <auto FnPtr, ::foundation::effects::IsExecCtx Ctx, class ConsumerEp, class Writer>
        requires CtxFitsSwmrStageFromEndpoint<FnPtr, Ctx, ConsumerEp, Writer>
    friend constexpr auto mint_swmr_stage(Ctx const& ctx, ConsumerEp&& in_ep, Writer&& writer) noexcept;

    template <auto FnPtr, class Tuple, std::size_t... Is>
    [[nodiscard]] static constexpr auto move_input_handles_(Tuple& endpoints, std::index_sequence<Is...>) noexcept {
        return std::tuple{std::move(std::get<Is>(endpoints)).into_handle()...};
    }

    template <auto FnPtr, class Tuple, std::size_t... Is>
    [[nodiscard]] static constexpr auto move_output_handles_(Tuple& endpoints, std::index_sequence<Is...>) noexcept {
        constexpr std::size_t offset = StageArity<FnPtr>::input_count;
        return std::tuple{std::move(std::get<offset + Is>(endpoints)).into_handle()...};
    }

    template <auto FnPtr, class Ctx, class... Endpoints>
    [[nodiscard]] static constexpr auto make_mpmc_(Ctx const& ctx, std::tuple<Endpoints...>& endpoints) noexcept {
        static_assert(CtxFitsMpmcStageFromEndpoints<FnPtr, Ctx, Endpoints...>,
                      "fixy::concurrent::diagnostic [StageDoor_Refused]: the stage door accepts only the body, "
                      "context and endpoints that mint_mpmc_stage_from_endpoints accepts.");
        using arity = StageArity<FnPtr>;
        auto inputs = move_input_handles_<FnPtr>(endpoints, std::make_index_sequence<arity::input_count>{});
        auto outputs = move_output_handles_<FnPtr>(endpoints, std::make_index_sequence<arity::output_count>{});
        using stage_type = MpmcStage<FnPtr, Ctx, decltype(inputs), decltype(outputs)>;
        return stage_type{ctx, std::move(inputs), std::move(outputs)};
    }

    template <auto FnPtr, class Ctx>
    [[nodiscard]] static constexpr auto
    make_swmr_(Ctx const& ctx, std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 0>>&& in,
               std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 1>>&& writer) noexcept {
        static_assert(CtxFitsSwmrPublishStage<FnPtr, Ctx>,
                      "fixy::concurrent::diagnostic [StageDoor_Refused]: the stage door accepts only the body and "
                      "context that mint_swmr_stage accepts.");
        return SwmrStage<FnPtr, Ctx>{ctx, std::move(in), std::move(writer)};
    }
};

namespace detail::stage_endpoint_bridge_self_test {

struct UTag1 {};
struct UTag2 {};

using FgCtx = ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::Fg, ::foundation::effects::Row<>>;
using Ch1 = PermissionedSpscChannel<int, 64, UTag1>;
using Ch2 = PermissionedSpscChannel<int, 64, UTag2>;

using ConsEp = Endpoint<Ch1, Direction::Consumer, FgCtx>;
using ProdEp = Endpoint<Ch2, Direction::Producer, FgCtx>;

static_assert(IsEndpoint<ConsEp>);
static_assert(IsEndpoint<ProdEp>);
static_assert(!IsEndpoint<int>);

static_assert(IsConsumerEndpoint<ConsEp>);
static_assert(!IsConsumerEndpoint<ProdEp>);
static_assert(!IsConsumerEndpoint<int>);

static_assert(IsProducerEndpoint<ProdEp>);
static_assert(!IsProducerEndpoint<ConsEp>);
static_assert(!IsProducerEndpoint<int>);

static_assert(IsMovedEndpoint<ConsEp>);
static_assert(IsMovedEndpoint<ConsEp&&>);
static_assert(!IsMovedEndpoint<ConsEp&>);

inline void int_stage_body(typename Ch1::ConsumerHandle&&, typename Ch2::ProducerHandle&&) noexcept {}

static_assert(PipelineStage<&int_stage_body>);

static_assert(StageHandlesMatchEndpoints<&int_stage_body, ConsEp, ProdEp>);

static_assert(CtxFitsStageFromEndpoints<&int_stage_body, FgCtx, ConsEp, ProdEp>);

static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, FgCtx, int, ProdEp>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, FgCtx, ConsEp, int>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, FgCtx, ProdEp, ConsEp>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, int, ConsEp, ProdEp>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, FgCtx, ConsEp&, ProdEp>);

// The payload axis is pinned here as well as in the negative-compile
// fixtures, so a refactor that loses payload-type discrimination breaks
// this header on its own rather than only the tests.

struct UTagFloat {};
using ChFloat = PermissionedSpscChannel<float, 64, UTagFloat>;
using FloatConsEp = Endpoint<ChFloat, Direction::Consumer, FgCtx>;
using FloatProdEp = Endpoint<ChFloat, Direction::Producer, FgCtx>;

// Direction, body shape and context all agree in the four rejections below.
// Only the payload type differs.
static_assert(IsConsumerEndpoint<FloatConsEp>);
static_assert(IsProducerEndpoint<FloatProdEp>);
static_assert(!StageHandlesMatchEndpoints<&int_stage_body, FloatConsEp, ProdEp>);
static_assert(!StageHandlesMatchEndpoints<&int_stage_body, ConsEp, FloatProdEp>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, FgCtx, FloatConsEp, ProdEp>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, FgCtx, ConsEp, FloatProdEp>);

// A positive control, so that a gate which rejects everything cannot pass.
static_assert(StageHandlesMatchEndpoints<&int_stage_body, ConsEp, ProdEp>);

// A stage can feed an MPSC channel.  Its producer handle takes the value
// by reference to const, which is the producer pole, so the body is a
// stage and the endpoint mint takes the MPSC producer endpoint.
struct UTagMpsc {};
using ChMpsc = PermissionedMpscChannel<int, 64, UTagMpsc>;
using MpscProdEp = Endpoint<ChMpsc, Direction::Producer, FgCtx>;

inline void feed_mpsc_body(typename Ch1::ConsumerHandle&&, typename ChMpsc::ProducerHandle&&) noexcept {}

static_assert(is_producer_handle_v<typename ChMpsc::ProducerHandle>);
static_assert(PipelineStage<&feed_mpsc_body>);
static_assert(CtxFitsStageFromEndpoints<&feed_mpsc_body, FgCtx, ConsEp, MpscProdEp>);

inline void fan_in_body(typename Ch1::ConsumerHandle&&, typename Ch1::ConsumerHandle&&,
                        typename Ch2::ProducerHandle&&) noexcept {}
inline void fan_out_body(typename Ch1::ConsumerHandle&&, typename Ch2::ProducerHandle&&,
                         typename Ch2::ProducerHandle&&) noexcept {}

static_assert(VariadicPipelineStage<&fan_in_body>);
static_assert(!PipelineStage<&fan_in_body>);
static_assert(StageHandlesMatchEndpointsExtended<&fan_in_body, EndpointPack<ConsEp, ConsEp>, EndpointPack<ProdEp>>);
static_assert(!StageHandlesMatchEndpointsExtended<&fan_in_body, EndpointPack<ConsEp>, EndpointPack<ProdEp>>);
static_assert(
    !StageHandlesMatchEndpointsExtended<&fan_in_body, EndpointPack<ConsEp, ConsEp, ConsEp>, EndpointPack<ProdEp>>);
static_assert(!StageHandlesMatchEndpointsExtended<&fan_in_body, EndpointPack<ConsEp, int>, EndpointPack<ProdEp>>);
static_assert(CtxFitsMpmcStageFromEndpoints<&fan_in_body, FgCtx, ConsEp, ConsEp, ProdEp>);
static_assert(!CtxFitsMpmcStageFromEndpoints<&fan_in_body, FgCtx, ConsEp&, ConsEp, ProdEp>);

static_assert(VariadicPipelineStage<&fan_out_body>);
static_assert(!PipelineStage<&fan_out_body>);
static_assert(StageHandlesMatchEndpointsExtended<&fan_out_body, EndpointPack<ConsEp>, EndpointPack<ProdEp, ProdEp>>);
static_assert(!StageHandlesMatchEndpointsExtended<&fan_out_body, EndpointPack<ConsEp, ProdEp>, EndpointPack<ProdEp>>);
static_assert(
    !StageHandlesMatchEndpointsExtended<&fan_out_body, EndpointPack<ConsEp>, EndpointPack<ProdEp, ProdEp, ProdEp>>);

}  // namespace detail::stage_endpoint_bridge_self_test

}  // namespace fixy::concurrent

// An endpoint of either direction is an endpoint.  The handle that it owns
// is not, and the trait sees a reference as no endpoint: IsEndpoint strips
// the reference before it asks.
template <>
struct foundation::contracts::armed_cell<::fixy::concurrent::detail::is_endpoint> {
    using accepts = witnesses<::fixy::concurrent::detail::stage_endpoint_bridge_self_test::ConsEp,
                              ::fixy::concurrent::detail::stage_endpoint_bridge_self_test::ProdEp>;
    using refuses = witnesses<int, ::fixy::concurrent::detail::stage_endpoint_bridge_self_test::Ch1::ConsumerHandle,
                              ::fixy::concurrent::detail::stage_endpoint_bridge_self_test::ConsEp&>;
};
