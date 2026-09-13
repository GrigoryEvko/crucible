#pragma once

// The bridge is a free function here rather than a method on the endpoint so
// that a translation unit which only builds endpoints never pulls in the
// stage machinery.

#include <crucible/Platform.h>
#include <crucible/concurrent/Endpoint.h>
#include <crucible/concurrent/Stage.h>
#include <crucible/safety/Decide.h>

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::concurrent {

namespace detail {

template <class T>
struct is_endpoint : std::false_type {};

template <class Substr, Direction Dir, ::crucible::effects::IsExecCtx Ctx>
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

// Without this check, handing a channel of one payload type to a body that
// expects another would only fail far below, where the parameters are bound.
// Checking it here turns that into one failed constraint at the call site.

template <auto FnPtr, class ConsumerEp, class ProducerEp>
concept StageHandlesMatchEndpoints =
    ::crucible::safety::extract::PipelineStage<FnPtr>
    && std::is_same_v<typename std::remove_cvref_t<ConsumerEp>::handle_type,
                      std::remove_reference_t<::crucible::safety::extract::param_type_t<FnPtr, 0>>>
    && std::is_same_v<typename std::remove_cvref_t<ProducerEp>::handle_type,
                      std::remove_reference_t<::crucible::safety::extract::param_type_t<FnPtr, 1>>>;

template <class... Endpoints>
struct EndpointPack {};

namespace detail {

template <auto FnPtr, class Inputs, class Outputs>
struct stage_handles_match_endpoints_extended : std::false_type {};

template <auto FnPtr, class... ConsumerEps, class... ProducerEps>
struct stage_handles_match_endpoints_extended<FnPtr, EndpointPack<ConsumerEps...>, EndpointPack<ProducerEps...>> {
private:
    using extract = ::crucible::safety::extract::StageArity<FnPtr>;
    using consumer_tuple = std::tuple<ConsumerEps...>;
    using producer_tuple = std::tuple<ProducerEps...>;

    template <class Endpoint, std::size_t I>
    static consteval bool consumer_endpoint_matches_param() noexcept {
        using endpoint = std::remove_cvref_t<Endpoint>;
        if constexpr (I >= ::crucible::safety::extract::arity_v<FnPtr>) {
            return false;
        } else if constexpr (!IsEndpoint<endpoint>) {
            return false;
        } else if constexpr (is_endpoint<endpoint>::direction != Direction::Consumer) {
            return false;
        } else {
            return std::is_same_v<typename endpoint::handle_type,
                                  std::remove_reference_t<::crucible::safety::extract::param_type_t<FnPtr, I>>>;
        }
    }

    template <class Endpoint, std::size_t I>
    static consteval bool producer_endpoint_matches_param() noexcept {
        using endpoint = std::remove_cvref_t<Endpoint>;
        if constexpr (I >= ::crucible::safety::extract::arity_v<FnPtr>) {
            return false;
        } else if constexpr (!IsEndpoint<endpoint>) {
            return false;
        } else if constexpr (is_endpoint<endpoint>::direction != Direction::Producer) {
            return false;
        } else {
            return std::is_same_v<typename endpoint::handle_type,
                                  std::remove_reference_t<::crucible::safety::extract::param_type_t<FnPtr, I>>>;
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
        if constexpr (!::crucible::safety::extract::VariadicPipelineStage<FnPtr>) {
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
    using arity = ::crucible::safety::extract::StageArity<FnPtr>;

    static consteval bool compute() noexcept {
        if constexpr (!::crucible::safety::extract::VariadicPipelineStage<FnPtr> || !::crucible::effects::IsExecCtx<Ctx>
                      || sizeof...(Endpoints) != ::crucible::safety::extract::arity_v<FnPtr>) {
            return false;
        } else {
            using inputs = endpoint_take_pack_t<arity::input_count, Endpoints...>;
            using outputs = endpoint_drop_pack_t<arity::input_count, Endpoints...>;
            return CtxFitsVariadicStage<FnPtr, Ctx> && StageHandlesMatchEndpointsExtended<FnPtr, inputs, outputs>;
        }
    }

public:
    static constexpr bool value = compute();
};

template <auto FnPtr, class Ctx, class Tuple, std::size_t... Is>
[[nodiscard]] constexpr auto move_input_handles_from_endpoint_tuple(Tuple& endpoints,
                                                                    std::index_sequence<Is...>) noexcept {
    return std::tuple{std::move(std::get<Is>(endpoints)).into_handle()...};
}

template <auto FnPtr, class Ctx, class Tuple, std::size_t... Is>
[[nodiscard]] constexpr auto move_output_handles_from_endpoint_tuple(Tuple& endpoints,
                                                                     std::index_sequence<Is...>) noexcept {
    constexpr std::size_t offset = ::crucible::safety::extract::StageArity<FnPtr>::input_count;
    return std::tuple{std::move(std::get<offset + Is>(endpoints)).into_handle()...};
}

template <auto FnPtr, class Ctx, class Tuple>
[[nodiscard]] constexpr auto make_mpmc_stage_from_endpoint_tuple(Ctx const& ctx, Tuple& endpoints) noexcept {
    using arity = ::crucible::safety::extract::StageArity<FnPtr>;
    auto inputs =
        move_input_handles_from_endpoint_tuple<FnPtr, Ctx>(endpoints, std::make_index_sequence<arity::input_count>{});
    auto outputs =
        move_output_handles_from_endpoint_tuple<FnPtr, Ctx>(endpoints, std::make_index_sequence<arity::output_count>{});
    using stage_type = MpmcStage<FnPtr, Ctx, decltype(inputs), decltype(outputs)>;
    return stage_type{ctx, std::move(inputs), std::move(outputs)};
}

// Unconstrained on purpose: the concept that gates it is declared below, so
// the friend declaration in the class could not have named it.  The gate runs
// in the factory that calls this.
template <auto FnPtr, class Ctx>
[[nodiscard]] constexpr auto
make_swmr_stage(Ctx const& ctx, std::remove_reference_t<::crucible::safety::extract::param_type_t<FnPtr, 0>>&& in,
                std::remove_reference_t<::crucible::safety::extract::param_type_t<FnPtr, 1>>&& writer) noexcept {
    return SwmrStage<FnPtr, Ctx>{ctx, std::move(in), std::move(writer)};
}

inline void mpmc_stage_row_admission_anchor_() noexcept {}
inline void swmr_stage_row_admission_anchor_() noexcept {}

template <auto FnPtr, class Ctx, class ConsumerEp, class Writer>
struct swmr_stage_from_endpoint_gate {
private:
    static consteval bool compute() noexcept {
        using consumer_ep = std::remove_cvref_t<ConsumerEp>;
        using writer = std::remove_cvref_t<Writer>;
        if constexpr (!CtxFitsSwmrPublishStage<FnPtr, Ctx> || !IsConsumerEndpoint<consumer_ep>
                      || !::crucible::safety::extract::is_swmr_writer_v<writer>) {
            return false;
        } else {
            return std::is_same_v<typename consumer_ep::handle_type,
                                  std::remove_reference_t<::crucible::safety::extract::param_type_t<FnPtr, 0>>>
                && std::is_same_v<writer, std::remove_reference_t<::crucible::safety::extract::param_type_t<FnPtr, 1>>>;
        }
    }

public:
    static constexpr bool value = compute();
};

}  // namespace detail

template <auto FnPtr, class Ctx, class... Endpoints>
concept CtxFitsMpmcStageFromEndpoints =
    detail::mpmc_stage_from_endpoints_gate<FnPtr, Ctx, std::remove_cvref_t<Endpoints>...>::value;

template <auto FnPtr, class Ctx, class ConsumerEp, class Writer>
concept CtxFitsSwmrStageFromEndpoint = detail::swmr_stage_from_endpoint_gate<FnPtr, Ctx, ConsumerEp, Writer>::value;

template <auto FnPtr, class Ctx, class ConsumerEp, class ProducerEp>
concept CtxFitsStageFromEndpoints =
    CtxFitsStage<FnPtr, Ctx> && IsConsumerEndpoint<ConsumerEp> && IsProducerEndpoint<ProducerEp>
    && StageHandlesMatchEndpoints<FnPtr, ConsumerEp, ProducerEp>;

template <auto FnPtr, ::crucible::effects::IsExecCtx Ctx, class ConsumerEp, class ProducerEp>
    requires CtxFitsStageFromEndpoints<FnPtr, Ctx, ConsumerEp, ProducerEp>
[[nodiscard]] constexpr auto mint_stage_from_endpoints(Ctx const& ctx, ConsumerEp&& in_ep,
                                                       ProducerEp&& out_ep) noexcept {
    return mint_stage<FnPtr>(ctx, std::move(in_ep).into_handle(), std::move(out_ep).into_handle());
}

template <auto FnPtr, ::crucible::effects::IsExecCtx Ctx, class... Endpoints>
    requires CtxFitsMpmcStageFromEndpoints<FnPtr, Ctx, Endpoints...>
[[nodiscard]] constexpr auto mint_mpmc_stage_from_endpoints(Ctx const& ctx, Endpoints&&... endpoints) noexcept {
    using ctx_row = typename Ctx::row_type;
    using required_row = variadic_stage_row_union_t<FnPtr>;
    using offending_row = ::crucible::effects::row_difference_t<required_row, ctx_row>;

    CRUCIBLE_ROW_MISMATCH_ASSERT((::crucible::decide::row_subset<required_row, ctx_row>()), EffectRowMismatch,
                                 &::crucible::concurrent::detail::mpmc_stage_row_admission_anchor_, ctx_row,
                                 required_row, offending_row);

    std::tuple<std::remove_cvref_t<Endpoints>...> endpoint_tuple{std::forward<Endpoints>(endpoints)...};
    return detail::make_mpmc_stage_from_endpoint_tuple<FnPtr>(ctx, endpoint_tuple);
}

template <auto FnPtr, ::crucible::effects::IsExecCtx Ctx, class ConsumerEp, class Writer>
    requires CtxFitsSwmrStageFromEndpoint<FnPtr, Ctx, ConsumerEp, Writer>
[[nodiscard]] constexpr auto mint_swmr_stage(Ctx const& ctx, ConsumerEp&& in_ep, Writer&& writer) noexcept {
    using ctx_row = typename Ctx::row_type;
    using required_row = swmr_stage_row_union_t<FnPtr>;
    using offending_row = ::crucible::effects::row_difference_t<required_row, ctx_row>;

    CRUCIBLE_ROW_MISMATCH_ASSERT((::crucible::decide::row_subset<required_row, ctx_row>()), EffectRowMismatch,
                                 &::crucible::concurrent::detail::swmr_stage_row_admission_anchor_, ctx_row,
                                 required_row, offending_row);

    return detail::make_swmr_stage<FnPtr>(ctx, std::move(in_ep).into_handle(), std::forward<Writer>(writer));
}

namespace detail::stage_endpoint_bridge_self_test {

namespace eff = ::crucible::effects;
namespace saf = ::crucible::safety;

struct UTag1 {};
struct UTag2 {};

using Ch1 = PermissionedSpscChannel<int, 64, UTag1>;
using Ch2 = PermissionedSpscChannel<int, 64, UTag2>;

using ConsEp = Endpoint<Ch1, Direction::Consumer, eff::HotFgCtx>;
using ProdEp = Endpoint<Ch2, Direction::Producer, eff::HotFgCtx>;

static_assert(IsEndpoint<ConsEp>);
static_assert(IsEndpoint<ProdEp>);
static_assert(!IsEndpoint<int>);

static_assert(IsConsumerEndpoint<ConsEp>);
static_assert(!IsConsumerEndpoint<ProdEp>);
static_assert(!IsConsumerEndpoint<int>);

static_assert(IsProducerEndpoint<ProdEp>);
static_assert(!IsProducerEndpoint<ConsEp>);
static_assert(!IsProducerEndpoint<int>);

inline void int_stage_body(typename Ch1::ConsumerHandle&&, typename Ch2::ProducerHandle&&) noexcept {}

static_assert(saf::extract::PipelineStage<&int_stage_body>);

static_assert(StageHandlesMatchEndpoints<&int_stage_body, ConsEp, ProdEp>);

static_assert(CtxFitsStageFromEndpoints<&int_stage_body, eff::HotFgCtx, ConsEp, ProdEp>);

static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, eff::HotFgCtx, int, ProdEp>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, eff::HotFgCtx, ConsEp, int>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, eff::HotFgCtx, ProdEp, ConsEp>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, int, ConsEp, ProdEp>);

// The payload axis is pinned here as well as in the negative-compile fixtures,
// so a refactor that loses payload-type discrimination breaks this header on
// its own rather than only the tests.

struct UTagFloat {};
using ChFloat = PermissionedSpscChannel<float, 64, UTagFloat>;
using FloatConsEp = Endpoint<ChFloat, Direction::Consumer, eff::HotFgCtx>;
using FloatProdEp = Endpoint<ChFloat, Direction::Producer, eff::HotFgCtx>;

// Direction, body shape and context all agree in the four rejections below.
// Only the payload type differs.
static_assert(IsConsumerEndpoint<FloatConsEp>);
static_assert(IsProducerEndpoint<FloatProdEp>);
static_assert(!StageHandlesMatchEndpoints<&int_stage_body, FloatConsEp, ProdEp>);
static_assert(!StageHandlesMatchEndpoints<&int_stage_body, ConsEp, FloatProdEp>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, eff::HotFgCtx, FloatConsEp, ProdEp>);
static_assert(!CtxFitsStageFromEndpoints<&int_stage_body, eff::HotFgCtx, ConsEp, FloatProdEp>);

// A positive control, so that a gate which rejects everything cannot pass.
static_assert(StageHandlesMatchEndpoints<&int_stage_body, ConsEp, ProdEp>);

inline void fan_in_body(typename Ch1::ConsumerHandle&&, typename Ch1::ConsumerHandle&&,
                        typename Ch2::ProducerHandle&&) noexcept {}
inline void fan_out_body(typename Ch1::ConsumerHandle&&, typename Ch2::ProducerHandle&&,
                         typename Ch2::ProducerHandle&&) noexcept {}

static_assert(saf::extract::VariadicPipelineStage<&fan_in_body>);
static_assert(!saf::extract::PipelineStage<&fan_in_body>);
static_assert(StageHandlesMatchEndpointsExtended<&fan_in_body, EndpointPack<ConsEp, ConsEp>, EndpointPack<ProdEp>>);
static_assert(!StageHandlesMatchEndpointsExtended<&fan_in_body, EndpointPack<ConsEp>, EndpointPack<ProdEp>>);
static_assert(
    !StageHandlesMatchEndpointsExtended<&fan_in_body, EndpointPack<ConsEp, ConsEp, ConsEp>, EndpointPack<ProdEp>>);
static_assert(!StageHandlesMatchEndpointsExtended<&fan_in_body, EndpointPack<ConsEp, int>, EndpointPack<ProdEp>>);

static_assert(saf::extract::VariadicPipelineStage<&fan_out_body>);
static_assert(!saf::extract::PipelineStage<&fan_out_body>);
static_assert(StageHandlesMatchEndpointsExtended<&fan_out_body, EndpointPack<ConsEp>, EndpointPack<ProdEp, ProdEp>>);
static_assert(!StageHandlesMatchEndpointsExtended<&fan_out_body, EndpointPack<ConsEp, ProdEp>, EndpointPack<ProdEp>>);
static_assert(
    !StageHandlesMatchEndpointsExtended<&fan_out_body, EndpointPack<ConsEp>, EndpointPack<ProdEp, ProdEp, ProdEp>>);

}  // namespace detail::stage_endpoint_bridge_self_test

}  // namespace crucible::concurrent
