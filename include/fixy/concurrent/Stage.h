#pragma once

// A stage is one node of a chain of N bodies joined by N-1 channels.  Its
// consumer end drains the channel behind it and its producer end fills the
// channel ahead of it, which is why every body has that same pair of
// parameters.
//
// Whether each channel suits the context it runs in is settled where the
// handles were bound to their contexts, and is deliberately not rechecked
// here: a handle does not carry its channel type back, so the check would
// have to be reconstructed from nothing.  Agreement between one stage's output
// and the next stage's input is a property of the chain, and belongs to
// whatever assembles the chain.
//
// Old spelling: include/crucible/concurrent/Stage.h.  The two bridge
// factories declared in detail below are defined by the endpoint bridge,
// which arrives with #190; until then MpmcStage and SwmrStage have a door
// and no caller.

#include <fixy/Ctx.h>
#include <fixy/concurrent/HandleTraits.h>
#include <fixy/concurrent/PayloadRow.h>
#include <fixy/concurrent/StageShape.h>
#include <fixy/concurrent/WorkingSet.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Decide.h>
#include <foundation/diag/RowHash.h>
#include <foundation/diag/RowMismatch.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/Signature.h>

#include <cstddef>
#include <optional>
#include <type_traits>
#include <tuple>
#include <utility>

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace fixy::row_discipline {
struct stage;
struct mpmc_stage;
struct swmr_stage;
}  // namespace fixy::row_discipline

namespace fixy::concurrent {

// Admission compares effect-only projections of the payload rows.  The
// unprojected row also carries grades that say nothing about what the context
// permits, such as a numerical tier, and matching on those would reject
// payloads the context can in fact run.

template <auto FnPtr, class Ctx>
concept StageInputRowAdmitted =
    PipelineStage<FnPtr> && ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::decide::row_subset<payload_effect_row_t<pipeline_stage_input_value_t<FnPtr>>,
                                        typename Ctx::row_type>();

template <auto FnPtr, class Ctx>
concept StageOutputRowAdmitted =
    PipelineStage<FnPtr> && ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::decide::row_subset<payload_effect_row_t<pipeline_stage_output_value_t<FnPtr>>,
                                        typename Ctx::row_type>();

template <auto FnPtr, class Ctx>
concept CtxFitsStage = PipelineStage<FnPtr> && ::foundation::effects::IsExecCtx<Ctx>
                    && StageInputRowAdmitted<FnPtr, Ctx> && StageOutputRowAdmitted<FnPtr, Ctx>;

namespace detail {

template <typename... Rows>
struct row_union_pack;

template <>
struct row_union_pack<> {
    using type = ::foundation::effects::Row<>;
};

template <typename Row0, typename... Rest>
struct row_union_pack<Row0, Rest...> {
    using rest = typename row_union_pack<Rest...>::type;
    using type = ::foundation::effects::row_union_t<Row0, rest>;
};

template <auto FnPtr, typename Is>
struct variadic_input_row_union;

template <auto FnPtr, std::size_t... Is>
struct variadic_input_row_union<FnPtr, std::index_sequence<Is...>> {
    using type = typename row_union_pack<payload_effect_row_t<pipeline_stage_input_value_at_t<FnPtr, Is>>...>::type;
};

template <auto FnPtr, typename Is>
struct variadic_output_row_union;

template <auto FnPtr, std::size_t... Is>
struct variadic_output_row_union<FnPtr, std::index_sequence<Is...>> {
    using type = typename row_union_pack<payload_effect_row_t<pipeline_stage_output_value_at_t<FnPtr, Is>>...>::type;
};

template <auto FnPtr>
struct variadic_stage_row_union {
private:
    using arity = StageArity<FnPtr>;
    using input_rows = typename variadic_input_row_union<FnPtr, std::make_index_sequence<arity::input_count>>::type;
    using output_rows = typename variadic_output_row_union<FnPtr, std::make_index_sequence<arity::output_count>>::type;

public:
    using type = ::foundation::effects::row_union_t<input_rows, output_rows>;
};

template <auto FnPtr, class Ctx>
consteval bool variadic_stage_rows_admitted() noexcept {
    if constexpr (!VariadicPipelineStage<FnPtr> || !::foundation::effects::IsExecCtx<Ctx>) {
        return false;
    } else {
        return ::foundation::decide::row_subset<typename variadic_stage_row_union<FnPtr>::type,
                                                typename Ctx::row_type>();
    }
}

template <auto FnPtr, class Inputs, class Outputs>
struct variadic_stage_handles_match : std::false_type {};

template <auto FnPtr, class... Inputs, class... Outputs>
struct variadic_stage_handles_match<FnPtr, std::tuple<Inputs...>, std::tuple<Outputs...>> {
private:
    using extract = StageArity<FnPtr>;

    template <std::size_t... Is>
    static consteval bool inputs_match(std::index_sequence<Is...>) noexcept {
        return ((std::is_same_v<std::remove_cvref_t<Inputs>,
                                std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, Is>>>)
                && ...);
    }

    template <std::size_t... Is>
    static consteval bool outputs_match(std::index_sequence<Is...>) noexcept {
        constexpr std::size_t offset = extract::input_count;
        return ((std::is_same_v<std::remove_cvref_t<Outputs>,
                                std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, offset + Is>>>)
                && ...);
    }

    static consteval bool compute() noexcept {
        if constexpr (!VariadicPipelineStage<FnPtr>) {
            return false;
        } else if constexpr (extract::input_count != sizeof...(Inputs) || extract::output_count != sizeof...(Outputs)) {
            return false;
        } else {
            return inputs_match(std::make_index_sequence<sizeof...(Inputs)>{})
                && outputs_match(std::make_index_sequence<sizeof...(Outputs)>{});
        }
    }

public:
    static constexpr bool value = compute();
};

// Declared here only so the class below can befriend it.  Its definition lives
// in the bridge header that includes this one, and is the one place a stage of
// this kind is built.
template <auto FnPtr, class Ctx, class Tuple>
[[nodiscard]] constexpr auto make_mpmc_stage_from_endpoint_tuple(Ctx const& ctx, Tuple& endpoints) noexcept;

// Same arrangement for the single-writer stage.  This one carries no
// constraint of its own: the concept that gates it is declared in the bridge
// header, which is included after this one, so the friend declaration below
// could not name it.  The gate runs in the factory that calls this.
template <auto FnPtr, class Ctx>
[[nodiscard]] constexpr auto
make_swmr_stage(Ctx const& ctx, std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 0>>&& in,
                std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 1>>&& writer) noexcept;

}  // namespace detail

template <auto FnPtr>
    requires VariadicPipelineStage<FnPtr>
using variadic_stage_row_union_t = typename detail::variadic_stage_row_union<FnPtr>::type;

template <auto FnPtr, class Ctx>
concept CtxFitsVariadicStage = VariadicPipelineStage<FnPtr> && ::foundation::effects::IsExecCtx<Ctx>
                            && detail::variadic_stage_rows_admitted<FnPtr, Ctx>();

template <auto FnPtr, class Inputs, class Outputs>
concept VariadicStageHandlesMatch = detail::variadic_stage_handles_match<FnPtr, Inputs, Outputs>::value;

template <auto FnPtr>
concept SwmrPublishStageBody =
    ::foundation::reflect::arity_v<FnPtr> == 2 && std::is_void_v<::foundation::reflect::return_type_t<FnPtr>>
    && std::is_rvalue_reference_v<::foundation::reflect::param_type_t<FnPtr, 0>>
    && !std::is_const_v<std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 0>>>
    && is_consumer_handle_v<::foundation::reflect::param_type_t<FnPtr, 0>>
    && std::is_rvalue_reference_v<::foundation::reflect::param_type_t<FnPtr, 1>>
    && !std::is_const_v<std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 1>>>
    && is_swmr_writer_v<::foundation::reflect::param_type_t<FnPtr, 1>>;

template <auto FnPtr>
    requires SwmrPublishStageBody<FnPtr>
using swmr_stage_row_union_t = ::foundation::effects::row_union_t<
    payload_effect_row_t<consumer_handle_value_t<::foundation::reflect::param_type_t<FnPtr, 0>>>,
    payload_effect_row_t<swmr_writer_value_t<::foundation::reflect::param_type_t<FnPtr, 1>>>>;

template <auto FnPtr, class Ctx>
concept CtxFitsSwmrPublishStage =
    SwmrPublishStageBody<FnPtr> && ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::decide::row_subset<swmr_stage_row_union_t<FnPtr>, typename Ctx::row_type>();

template <auto FnPtr, class Ctx>
    requires CtxFitsStage<FnPtr, Ctx>
class Stage {
public:
    using ctx_type = Ctx;
    using consumer_handle_type = std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 0>>;
    using producer_handle_type = std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 1>>;
    using input_value_type = pipeline_stage_input_value_t<FnPtr>;
    using output_value_type = pipeline_stage_output_value_t<FnPtr>;
    using row_discipline = ::fixy::row_discipline::stage;
    using row_payload = ::foundation::diag::row_payloads<Ctx, consumer_handle_type, producer_handle_type>;

    [[maybe_unused]] static constexpr auto fn_ptr = FnPtr;

    static constexpr bool is_value_preserving = pipeline_stage_is_value_preserving_v<FnPtr>;

    Stage(Stage const&) = delete("Stage holds linear Permission tokens via the consumer/producer handles");
    Stage& operator=(Stage const&) = delete("Stage holds linear Permission tokens via the consumer/producer handles");
    Stage(Stage&&) noexcept = default;
    Stage& operator=(Stage&&) noexcept = default;

    // The body owns the drain loop, the retry policy on a full or empty
    // channel, and the decision to stop once its consumer sees the upstream
    // end close.  What is here is only the binding.

    void run() && noexcept { FnPtr(std::move(in_), std::move(out_)); }

    [[nodiscard]] constexpr Ctx const& ctx() const noexcept { return ctx_; }

    [[nodiscard]] constexpr consumer_handle_type& in() & noexcept { return in_; }
    [[nodiscard]] constexpr consumer_handle_type const& in() const& noexcept { return in_; }

    [[nodiscard]] constexpr producer_handle_type& out() & noexcept { return out_; }
    [[nodiscard]] constexpr producer_handle_type const& out() const& noexcept { return out_; }

private:
    // Private because direct construction would skip the row admission and
    // produce a stage whose payload effects were never weighed against the
    // context it runs in.
    [[nodiscard]] explicit constexpr Stage(Ctx const& ctx, consumer_handle_type&& in,
                                           producer_handle_type&& out) noexcept
        : ctx_{ctx}, in_{std::move(in)}, out_{std::move(out)} {}

    // The constraint here has to be the factory's constraint written out
    // identically.  A constrained function template is befriended only when
    // the friend declaration carries the same associated constraints, so any
    // drift silently un-friends the factory.
    template <auto MintFnPtr, ::foundation::effects::IsExecCtx MintCtx>
        requires CtxFitsStage<MintFnPtr, MintCtx>
    friend constexpr auto
    mint_stage(MintCtx const&, std::remove_reference_t<::foundation::reflect::param_type_t<MintFnPtr, 0>>&&,
               std::remove_reference_t<::foundation::reflect::param_type_t<MintFnPtr, 1>>&&) noexcept;

    [[no_unique_address]] Ctx ctx_;
    consumer_handle_type in_;
    producer_handle_type out_;
};

// The body's parameters are all consumer handles first and all producer
// handles after, and the two tuples have to match those two runs exactly.

template <auto FnPtr, class Ctx, class Inputs, class Outputs>
    requires CtxFitsVariadicStage<FnPtr, Ctx> && VariadicStageHandlesMatch<FnPtr, Inputs, Outputs>
class MpmcStage;

template <auto FnPtr, class Ctx, class... Inputs, class... Outputs>
    requires CtxFitsVariadicStage<FnPtr, Ctx>
          && VariadicStageHandlesMatch<FnPtr, std::tuple<Inputs...>, std::tuple<Outputs...>>
class MpmcStage<FnPtr, Ctx, std::tuple<Inputs...>, std::tuple<Outputs...>> {
public:
    using ctx_type = Ctx;
    using input_tuple_type = std::tuple<Inputs...>;
    using output_tuple_type = std::tuple<Outputs...>;
    using row_discipline = ::fixy::row_discipline::mpmc_stage;
    using row_payload = ::foundation::diag::row_payloads<Ctx, Inputs..., Outputs...>;

    static constexpr std::size_t input_count = sizeof...(Inputs);
    static constexpr std::size_t output_count = sizeof...(Outputs);
    static constexpr bool is_value_preserving = false;
    [[maybe_unused]] static constexpr auto fn_ptr = FnPtr;

    template <std::size_t I>
        requires(I < input_count)
    using input_handle_type = std::tuple_element_t<I, input_tuple_type>;

    template <std::size_t I>
        requires(I < output_count)
    using output_handle_type = std::tuple_element_t<I, output_tuple_type>;

    template <std::size_t I>
        requires(I < input_count)
    using input_value_type = pipeline_stage_input_value_at_t<FnPtr, I>;

    template <std::size_t I>
        requires(I < output_count)
    using output_value_type = pipeline_stage_output_value_at_t<FnPtr, I>;

    static constexpr bool aggregate_working_set_known =
        ((HasStaticPerCallWorkingSet<Inputs>) && ...) && ((HasStaticPerCallWorkingSet<Outputs>) && ...);

    static constexpr std::size_t aggregate_per_call_working_set = [] consteval {
        if constexpr (aggregate_working_set_known) {
            std::size_t total = 0;
            ((total = saturating_ws_add(total, per_call_working_set_of_v<Inputs>)), ...);
            ((total = saturating_ws_add(total, per_call_working_set_of_v<Outputs>)), ...);
            return total;
        } else {
            return unknown_per_call_working_set;
        }
    }();

    MpmcStage(MpmcStage const&) = delete("MpmcStage holds linear or fractional endpoint handles");
    MpmcStage& operator=(MpmcStage const&) = delete("MpmcStage holds linear or fractional endpoint handles");
    MpmcStage(MpmcStage&&) noexcept = default;
    MpmcStage& operator=(MpmcStage&&) noexcept = default;

    void run() && noexcept {
        std::move(*this).run_impl_(std::make_index_sequence<input_count>{}, std::make_index_sequence<output_count>{});
    }

    [[nodiscard]] constexpr Ctx const& ctx() const noexcept { return ctx_; }

    template <std::size_t I>
        requires(I < input_count)
    [[nodiscard]] constexpr auto& input() & noexcept {
        return std::get<I>(inputs_);
    }

    template <std::size_t I>
        requires(I < output_count)
    [[nodiscard]] constexpr auto& output() & noexcept {
        return std::get<I>(outputs_);
    }

private:
    // Private because direct construction would skip the row admission and
    // produce a stage whose payload effects were never weighed against the
    // context it runs in.
    [[nodiscard]] explicit constexpr MpmcStage(Ctx const& ctx, input_tuple_type&& inputs,
                                               output_tuple_type&& outputs) noexcept
        : ctx_{ctx}, inputs_{std::move(inputs)}, outputs_{std::move(outputs)} {}

    template <auto MintFnPtr, class MintCtx, class MintTuple>
    friend constexpr auto detail::make_mpmc_stage_from_endpoint_tuple(MintCtx const&, MintTuple&) noexcept;

    template <std::size_t... Is, std::size_t... Os>
    void run_impl_(std::index_sequence<Is...>, std::index_sequence<Os...>) && noexcept {
        FnPtr(std::move(std::get<Is>(inputs_))..., std::move(std::get<Os>(outputs_))...);
    }

    [[no_unique_address]] Ctx ctx_;
    input_tuple_type inputs_;
    output_tuple_type outputs_;
};

template <auto FnPtr, class Ctx>
    requires CtxFitsSwmrPublishStage<FnPtr, Ctx>
class SwmrStage {
public:
    using ctx_type = Ctx;
    using consumer_handle_type = std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 0>>;
    using writer_handle_type = std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 1>>;
    using input_value_type = consumer_handle_value_t<consumer_handle_type>;
    using output_value_type = swmr_writer_value_t<writer_handle_type>;
    using row_discipline = ::fixy::row_discipline::swmr_stage;
    using row_payload = ::foundation::diag::row_payloads<Ctx, consumer_handle_type, writer_handle_type>;

    static constexpr std::size_t input_count = 1;
    static constexpr std::size_t output_count = 1;
    static constexpr bool is_value_preserving = std::is_same_v<input_value_type, output_value_type>;
    [[maybe_unused]] static constexpr auto fn_ptr = FnPtr;

    template <std::size_t I>
        requires(I == 0)
    using input_value_type_at = input_value_type;

    template <std::size_t I>
        requires(I == 0)
    using output_value_type_at = output_value_type;

    static constexpr bool aggregate_working_set_known =
        HasStaticPerCallWorkingSet<consumer_handle_type> && HasStaticPerCallWorkingSet<writer_handle_type>;

    static constexpr std::size_t aggregate_per_call_working_set = [] consteval {
        if constexpr (aggregate_working_set_known) {
            return saturating_ws_add(per_call_working_set_of_v<consumer_handle_type>,
                                     per_call_working_set_of_v<writer_handle_type>);
        } else {
            return unknown_per_call_working_set;
        }
    }();

    SwmrStage(SwmrStage const&) = delete("SwmrStage owns endpoint handles");
    SwmrStage& operator=(SwmrStage const&) = delete("SwmrStage owns endpoint handles");
    SwmrStage(SwmrStage&&) noexcept = default;
    SwmrStage& operator=(SwmrStage&&) noexcept = default;

    void run() && noexcept { FnPtr(std::move(in_), std::move(writer_)); }

    [[nodiscard]] constexpr Ctx const& ctx() const noexcept { return ctx_; }

private:
    // Private because direct construction would skip the row admission and
    // produce a stage whose payload effects were never weighed against the
    // context it runs in.
    [[nodiscard]] explicit constexpr SwmrStage(Ctx const& ctx, consumer_handle_type&& in,
                                               writer_handle_type&& writer) noexcept
        : ctx_{ctx}, in_{std::move(in)}, writer_{std::move(writer)} {}

    // The befriended factory carries no constraint of its own.  The concept
    // that gates it runs in the caller above it.
    template <auto MintFnPtr, class MintCtx>
    friend constexpr auto detail::make_swmr_stage(
        MintCtx const&, std::remove_reference_t<::foundation::reflect::param_type_t<MintFnPtr, 0>>&&,
        std::remove_reference_t<::foundation::reflect::param_type_t<MintFnPtr, 1>>&&) noexcept;

    [[no_unique_address]] Ctx ctx_;
    consumer_handle_type in_;
    writer_handle_type writer_;
};

template <auto FnPtr, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsStage<FnPtr, Ctx>
[[nodiscard]] constexpr auto
mint_stage(Ctx const& ctx, std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 0>>&& in,
           std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 1>>&& out) noexcept {
    using ctx_row = typename Ctx::row_type;
    using input_row = payload_effect_row_t<pipeline_stage_input_value_t<FnPtr>>;
    using output_row = payload_effect_row_t<pipeline_stage_output_value_t<FnPtr>>;
    using input_offending_row = ::foundation::effects::row_difference_t<input_row, ctx_row>;
    using output_offending_row = ::foundation::effects::row_difference_t<output_row, ctx_row>;

    // These repeat the two subset checks the requires-clause already makes, so
    // a mismatch never reaches this body.  They stay because the concept
    // reports only that the constraint failed, while these name the offending
    // row.
    CRUCIBLE_ROW_MISMATCH_ASSERT((::foundation::decide::row_subset<input_row, ctx_row>()), EffectRowMismatch, FnPtr,
                                 ctx_row, input_row, input_offending_row);

    CRUCIBLE_ROW_MISMATCH_ASSERT((::foundation::decide::row_subset<output_row, ctx_row>()), EffectRowMismatch, FnPtr,
                                 ctx_row, output_row, output_offending_row);

    return Stage<FnPtr, Ctx>{ctx, std::move(in), std::move(out)};
}

namespace detail::stage_self_test {

namespace eff = ::foundation::effects;

template <typename T>
struct FakeConsumer {
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

// The single-writer pole, which the old self-test never stood a witness
// for.  SwmrStage below is instantiated over it.
template <typename T>
struct FakeWriter {
    static constexpr std::size_t per_call_working_set = 64;
    void publish(T const&) noexcept {}
};

static_assert(is_consumer_handle_v<FakeConsumer<int>>);
static_assert(is_producer_handle_v<FakeProducer<int>>);
static_assert(is_swmr_writer_v<FakeWriter<int>>);

inline void stage_pass_through(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}
static_assert(PipelineStage<&stage_pass_through>);

inline void stage_transform_int_to_float(FakeConsumer<int>&&, FakeProducer<float>&&) noexcept {}
static_assert(PipelineStage<&stage_transform_int_to_float>);

using BgPayload = eff::Computation<eff::Row<eff::Effect::Bg>, int>;
using IoPayload = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using AllocCapPayload = eff::Capability<eff::Effect::Alloc, eff::Bg>;

inline void stage_bg_input(FakeConsumer<BgPayload>&&, FakeProducer<int>&&) noexcept {}
inline void stage_io_output(FakeConsumer<int>&&, FakeProducer<IoPayload>&&) noexcept {}
inline void stage_alloc_cap_input(FakeConsumer<AllocCapPayload>&&, FakeProducer<int>&&) noexcept {}

static_assert(PipelineStage<&stage_bg_input>);
static_assert(PipelineStage<&stage_io_output>);
static_assert(PipelineStage<&stage_alloc_cap_input>);

inline void stage_wrong_arity(FakeConsumer<int>&&) noexcept {}
inline int stage_returns_int(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept { return 0; }
inline void stage_wrong_param_kind(FakeConsumer<int>&, FakeProducer<int>&&) noexcept {}

static_assert(!PipelineStage<&stage_wrong_arity>);
static_assert(!PipelineStage<&stage_returns_int>);
static_assert(!PipelineStage<&stage_wrong_param_kind>);

static_assert(CtxFitsStage<&stage_pass_through, HotFgCtx>);
static_assert(CtxFitsStage<&stage_pass_through, BgDrainCtx>);
static_assert(CtxFitsStage<&stage_transform_int_to_float, HotFgCtx>);
static_assert(CtxFitsStage<&stage_bg_input, BgDrainCtx>);
static_assert(CtxFitsStage<&stage_io_output, BgCompileCtx>);
static_assert(CtxFitsStage<&stage_alloc_cap_input, BgDrainCtx>);
static_assert(!CtxFitsStage<&stage_wrong_arity, HotFgCtx>);
static_assert(!CtxFitsStage<&stage_returns_int, HotFgCtx>);
static_assert(!CtxFitsStage<&stage_pass_through, int>);
static_assert(!CtxFitsStage<&stage_bg_input, HotFgCtx>);
static_assert(!CtxFitsStage<&stage_io_output, HotFgCtx>);
static_assert(!CtxFitsStage<&stage_io_output, BgDrainCtx>);
static_assert(!CtxFitsStage<&stage_alloc_cap_input, HotFgCtx>);

using S1 = Stage<&stage_pass_through, HotFgCtx>;

static_assert(std::is_same_v<typename S1::ctx_type, HotFgCtx>);
static_assert(std::is_same_v<typename S1::consumer_handle_type, FakeConsumer<int>>);
static_assert(std::is_same_v<typename S1::producer_handle_type, FakeProducer<int>>);
static_assert(std::is_same_v<typename S1::input_value_type, int>);
static_assert(std::is_same_v<typename S1::output_value_type, int>);
static_assert(S1::is_value_preserving);
static_assert(S1::fn_ptr == &stage_pass_through);

using S2 = Stage<&stage_transform_int_to_float, BgDrainCtx>;
static_assert(std::is_same_v<typename S2::input_value_type, int>);
static_assert(std::is_same_v<typename S2::output_value_type, float>);
static_assert(!S2::is_value_preserving);

static_assert(!std::is_copy_constructible_v<S1>);
static_assert(!std::is_copy_assignable_v<S1>);
static_assert(std::is_move_constructible_v<S1>);
static_assert(std::is_move_assignable_v<S1>);

// A row mismatch has to be visible to substitution.  Because the gate is in
// the requires-clause and not in the body, the probe below is well formed and
// false rather than a hard error, and these witnesses pin that.

template <auto FnPtr, class Ctx>
concept MintStageCallable =
    requires(Ctx const& probe_ctx, std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 0>>&& probe_in,
             std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 1>>&& probe_out) {
        { ::fixy::concurrent::mint_stage<FnPtr>(probe_ctx, std::move(probe_in), std::move(probe_out)) };
    };

static_assert(MintStageCallable<&stage_pass_through, HotFgCtx>);
static_assert(MintStageCallable<&stage_bg_input, BgDrainCtx>);
static_assert(!MintStageCallable<&stage_bg_input, HotFgCtx>);
static_assert(!MintStageCallable<&stage_io_output, HotFgCtx>);
static_assert(MintStageCallable<&stage_pass_through, HotFgCtx> == CtxFitsStage<&stage_pass_through, HotFgCtx>);
static_assert(MintStageCallable<&stage_bg_input, HotFgCtx> == CtxFitsStage<&stage_bg_input, HotFgCtx>);

// The two other stage shapes, which the old self-test stated the gates
// for and never instantiated.  A fan-in body with two consumer handles
// and one producer handle, and a body that publishes into a
// single-writer cell.  Both have a private door, so the witness roster
// forges them through these names.

inline void stage_fan_in_two(FakeConsumer<int>&&, FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}
static_assert(VariadicPipelineStage<&stage_fan_in_two>);
static_assert(!PipelineStage<&stage_fan_in_two>);
static_assert(CtxFitsVariadicStage<&stage_fan_in_two, HotFgCtx>);
static_assert(!CtxFitsVariadicStage<&stage_wrong_arity, HotFgCtx>);
static_assert(VariadicStageHandlesMatch<&stage_fan_in_two, std::tuple<FakeConsumer<int>, FakeConsumer<int>>,
                                        std::tuple<FakeProducer<int>>>);
static_assert(
    !VariadicStageHandlesMatch<&stage_fan_in_two, std::tuple<FakeConsumer<int>>, std::tuple<FakeProducer<int>>>);
static_assert(!VariadicStageHandlesMatch<&stage_fan_in_two, std::tuple<FakeConsumer<int>, FakeConsumer<float>>,
                                         std::tuple<FakeProducer<int>>>);

using M1 = MpmcStage<&stage_fan_in_two, HotFgCtx, std::tuple<FakeConsumer<int>, FakeConsumer<int>>,
                     std::tuple<FakeProducer<int>>>;
static_assert(M1::input_count == 2);
static_assert(M1::output_count == 1);
static_assert(std::is_same_v<typename M1::template input_value_type<1>, int>);
static_assert(std::is_same_v<typename M1::template output_value_type<0>, int>);
static_assert(M1::aggregate_working_set_known);
static_assert(M1::aggregate_per_call_working_set == 192);
static_assert(!std::is_copy_constructible_v<M1>);
static_assert(std::is_move_constructible_v<M1>);

inline void stage_swmr_publish(FakeConsumer<int>&&, FakeWriter<int>&&) noexcept {}
static_assert(SwmrPublishStageBody<&stage_swmr_publish>);
static_assert(!PipelineStage<&stage_swmr_publish>);
static_assert(!SwmrPublishStageBody<&stage_pass_through>);
static_assert(CtxFitsSwmrPublishStage<&stage_swmr_publish, HotFgCtx>);
static_assert(std::is_same_v<swmr_stage_row_union_t<&stage_swmr_publish>, eff::Row<>>);

using W1 = SwmrStage<&stage_swmr_publish, HotFgCtx>;
static_assert(std::is_same_v<typename W1::input_value_type, int>);
static_assert(std::is_same_v<typename W1::output_value_type, int>);
static_assert(W1::is_value_preserving);
static_assert(W1::aggregate_working_set_known);
static_assert(W1::aggregate_per_call_working_set == 128);
static_assert(!std::is_copy_constructible_v<W1>);
static_assert(std::is_move_constructible_v<W1>);

}  // namespace detail::stage_self_test

}  // namespace fixy::concurrent
