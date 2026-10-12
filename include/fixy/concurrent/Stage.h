#pragma once

// A stage is one node of a chain of N bodies joined by N-1 channels.  Its
// consumer end drains the channel behind it and its producer end fills the
// channel ahead of it, which is why every body has that same pair of
// parameters.
//
// Whether each channel suits the context it runs in is settled where the
// handles were bound to their contexts, and is deliberately not rechecked
// here.  Whether one stage feeds the next is a property of the chain, and
// fixy/concurrent/Pipeline.h checks it: the producer handle of one stage and
// the consumer handle of the next name the same channel, and at the mint
// they bind the same channel instance.
//
// Each stage has a private constructor.  The one friend of a Stage is
// mint_stage, the one friend of an MpmcStage is mint_mpmc_stage, and the one
// friend of a SwmrStage is mint_swmr_stage.  Each mint takes handles.  A
// mint that takes endpoints takes the handle out of each endpoint and calls
// the mint of this header.
//
// The body names the handles of a stage, so the type of a stage holds only
// the body and the context.

#include <fixy/Ctx.h>
#include <fixy/concurrent/HandleTraits.h>
#include <fixy/concurrent/PayloadRow.h>
#include <fixy/concurrent/StageShape.h>
#include <fixy/concurrent/WorkingSet.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Decide.h>
#include <foundation/diag/RowHash.h>
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
    && ::foundation::decide::row_subset(^^payload_effect_row_t<pipeline_stage_input_value_t<FnPtr>>,
                                        ^^typename Ctx::row_type);

template <auto FnPtr, class Ctx>
concept StageOutputRowAdmitted =
    PipelineStage<FnPtr> && ::foundation::effects::IsExecCtx<Ctx>
    && ::foundation::decide::row_subset(^^payload_effect_row_t<pipeline_stage_output_value_t<FnPtr>>,
                                        ^^typename Ctx::row_type);

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
        return ::foundation::decide::row_subset(^^typename variadic_stage_row_union<FnPtr>::type,
                                                ^^typename Ctx::row_type);
    }
}

// The handles of a variadic stage body, in the order of its parameters:
// the tuple that holds them, the payload of the row hash, and the working
// set of one call.
template <auto FnPtr, class Positions>
struct stage_handle_pack;

template <auto FnPtr, std::size_t... Is>
struct stage_handle_pack<FnPtr, std::index_sequence<Is...>> {
    template <std::size_t I>
    using handle_at = std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, I>>;

    using tuple_type = std::tuple<handle_at<Is>...>;

    template <class Ctx>
    using row_payload = ::foundation::diag::row_payloads<Ctx, handle_at<Is>...>;

    static constexpr bool working_set_known = (HasStaticPerCallWorkingSet<handle_at<Is>> && ...);

    static constexpr std::size_t per_call_working_set = [] consteval {
        if constexpr (working_set_known) {
            std::size_t total = 0;
            ((total = saturating_ws_add(total, per_call_working_set_of_v<handle_at<Is>>)), ...);
            return total;
        } else {
            return unknown_per_call_working_set;
        }
    }();
};

// The handles name the parameters of the body: the same count, and each
// handle the type of its parameter, in the order of the parameters.
template <auto FnPtr, class... Handles>
struct can_variadic_stage_take_handles {
private:
    template <std::size_t... Is>
    static consteval bool each_handle_matches(std::index_sequence<Is...>) noexcept {
        return ((std::is_same_v<std::remove_cvref_t<Handles>,
                                std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, Is>>>)
                && ...);
    }

    static consteval bool compute() noexcept {
        if constexpr (!VariadicPipelineStage<FnPtr>) {
            return false;
        } else if constexpr (sizeof...(Handles) != ::foundation::reflect::arity_v<FnPtr>) {
            return false;
        } else {
            return each_handle_matches(std::make_index_sequence<sizeof...(Handles)>{});
        }
    }

public:
    static constexpr bool value = compute();
};

}  // namespace detail

template <auto FnPtr>
    requires VariadicPipelineStage<FnPtr>
using variadic_stage_row_union_t = typename detail::variadic_stage_row_union<FnPtr>::type;

template <auto FnPtr, class Ctx>
concept CtxFitsVariadicStage = VariadicPipelineStage<FnPtr> && ::foundation::effects::IsExecCtx<Ctx>
                            && detail::variadic_stage_rows_admitted<FnPtr, Ctx>();

template <auto FnPtr, class... Handles>
concept VariadicStageHandlesMatch = detail::can_variadic_stage_take_handles<FnPtr, Handles...>::value;

// Each handle comes in by move, with the exact type of its parameter.  A
// handle that is const or an lvalue deduces a type that the body does not
// name, and the mint refuses it.
template <auto FnPtr, class Ctx, class... Handles>
concept CtxFitsMpmcStage =
    CtxFitsVariadicStage<FnPtr, Ctx> && (std::is_same_v<Handles, std::remove_cvref_t<Handles>> && ...)
    && VariadicStageHandlesMatch<FnPtr, Handles...>;

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
    && ::foundation::decide::row_subset(^^swmr_stage_row_union_t<FnPtr>, ^^typename Ctx::row_type);

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
// handles after.  The stage holds one handle for each parameter, in the
// order of the parameters, so an input I is handle I and an output I is
// handle input_count + I.
template <auto FnPtr, class Ctx>
    requires CtxFitsVariadicStage<FnPtr, Ctx>
class MpmcStage {
    using handle_pack =
        detail::stage_handle_pack<FnPtr, std::make_index_sequence<::foundation::reflect::arity_v<FnPtr>>>;

public:
    using ctx_type = Ctx;
    using row_discipline = ::fixy::row_discipline::mpmc_stage;
    using row_payload = typename handle_pack::template row_payload<Ctx>;

    static constexpr std::size_t input_count = StageArity<FnPtr>::input_count;
    static constexpr std::size_t output_count = StageArity<FnPtr>::output_count;
    static constexpr bool is_value_preserving = false;
    [[maybe_unused]] static constexpr auto fn_ptr = FnPtr;

    template <std::size_t I>
        requires(I < input_count)
    using input_handle_type = typename handle_pack::template handle_at<I>;

    template <std::size_t I>
        requires(I < output_count)
    using output_handle_type = typename handle_pack::template handle_at<input_count + I>;

    template <std::size_t I>
        requires(I < input_count)
    using input_value_type = pipeline_stage_input_value_at_t<FnPtr, I>;

    template <std::size_t I>
        requires(I < output_count)
    using output_value_type = pipeline_stage_output_value_at_t<FnPtr, I>;

    static constexpr bool aggregate_working_set_known = handle_pack::working_set_known;

    static constexpr std::size_t aggregate_per_call_working_set = handle_pack::per_call_working_set;

    MpmcStage(MpmcStage const&) = delete("MpmcStage holds linear or fractional endpoint handles");
    MpmcStage& operator=(MpmcStage const&) = delete("MpmcStage holds linear or fractional endpoint handles");
    MpmcStage(MpmcStage&&) noexcept = default;
    MpmcStage& operator=(MpmcStage&&) noexcept = default;

    void run() && noexcept { std::move(*this).run_impl_(std::make_index_sequence<input_count + output_count>{}); }

    [[nodiscard]] constexpr Ctx const& ctx() const noexcept { return ctx_; }

    template <std::size_t I>
        requires(I < input_count)
    [[nodiscard]] constexpr auto& input() & noexcept {
        return std::get<I>(handles_);
    }

    template <std::size_t I>
        requires(I < output_count)
    [[nodiscard]] constexpr auto& output() & noexcept {
        return std::get<input_count + I>(handles_);
    }

private:
    // Private because direct construction would skip the row admission and
    // produce a stage whose payload effects were never weighed against the
    // context it runs in.
    template <class... Handles>
        requires VariadicStageHandlesMatch<FnPtr, Handles...>
    [[nodiscard]] explicit constexpr MpmcStage(Ctx const& ctx, Handles&&... handles) noexcept
        : ctx_{ctx}, handles_{std::forward<Handles>(handles)...} {}

    // The constraint here has to be the factory's constraint written out
    // identically, as for mint_stage above.
    template <auto MintFnPtr, ::foundation::effects::IsExecCtx MintCtx, class... MintHandles>
        requires CtxFitsMpmcStage<MintFnPtr, MintCtx, MintHandles...>
    friend constexpr auto mint_mpmc_stage(MintCtx const&, MintHandles&&...) noexcept;

    template <std::size_t... Is>
    void run_impl_(std::index_sequence<Is...>) && noexcept {
        FnPtr(std::move(std::get<Is>(handles_))...);
    }

    [[no_unique_address]] Ctx ctx_;
    typename handle_pack::tuple_type handles_;
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

    [[nodiscard]] constexpr consumer_handle_type const& in() const& noexcept { return in_; }
    [[nodiscard]] constexpr writer_handle_type const& out() const& noexcept { return writer_; }

private:
    // Private because direct construction would skip the row admission and
    // produce a stage whose payload effects were never weighed against the
    // context it runs in.
    [[nodiscard]] explicit constexpr SwmrStage(Ctx const& ctx, consumer_handle_type&& in,
                                               writer_handle_type&& writer) noexcept
        : ctx_{ctx}, in_{std::move(in)}, writer_{std::move(writer)} {}

    // The constraint here has to be the factory's constraint written out
    // identically, as for mint_stage above.
    template <auto MintFnPtr, ::foundation::effects::IsExecCtx MintCtx>
        requires CtxFitsSwmrPublishStage<MintFnPtr, MintCtx>
    friend constexpr auto
    mint_swmr_stage(MintCtx const&, std::remove_reference_t<::foundation::reflect::param_type_t<MintFnPtr, 0>>&&,
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
    // The requires clause checks the two rows.  A refused row never reaches
    // this body, and the compiler names the conjunct that refused it:
    // StageInputRowAdmitted or StageOutputRowAdmitted.
    return Stage<FnPtr, Ctx>{ctx, std::move(in), std::move(out)};
}

// The stage of a body with more than one input or output, such as a
// fan-out that gives each value to two channels, or a fan-in that joins
// two channels.  The handles come in the order of the parameters of the
// body.
template <auto FnPtr, ::foundation::effects::IsExecCtx Ctx, class... Handles>
    requires CtxFitsMpmcStage<FnPtr, Ctx, Handles...>
[[nodiscard]] constexpr auto mint_mpmc_stage(Ctx const& ctx, Handles&&... handles) noexcept {
    // As for mint_stage, the requires clause checks the rows and the
    // handles, and a refused call never reaches this body.
    return MpmcStage<FnPtr, Ctx>{ctx, std::forward<Handles>(handles)...};
}

// The stage of a body that drains one channel and publishes each value
// into the writer of a single-writer cell.  The consumer handle and the
// writer come in by move, in the order of the parameters of the body.
template <auto FnPtr, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSwmrPublishStage<FnPtr, Ctx>
[[nodiscard]] constexpr auto
mint_swmr_stage(Ctx const& ctx, std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 0>>&& in,
                std::remove_reference_t<::foundation::reflect::param_type_t<FnPtr, 1>>&& writer) noexcept {
    // As for mint_stage, the requires clause checks the body and the rows,
    // and a refused call never reaches this body.
    return SwmrStage<FnPtr, Ctx>{ctx, std::move(in), std::move(writer)};
}

// The fake handles and the stage bodies that only checks name.  MpmcStage
// and SwmrStage have a private constructor, so the witness roster
// (the witness rows of utils/rule-table.txt) forges each stage through these
// names.  The check files of this header and of
// fixy/concurrent/Pipeline.h, test/fixy/test_armed_roster.cpp and
// test/fixy/row_hash_census.h name them too.  No code runs them.
namespace detail::stage_witness {

// A pipeline joins two stages only when their handles name one channel, so
// each fake names one.  The writer publishes into a cell that no stage
// drains, so it names a cell of its own.
template <typename T>
struct FakeChannel {};

template <typename T>
struct FakeCell {};

template <typename T>
struct FakeConsumer {
    using channel_type = FakeChannel<T>;
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    using channel_type = FakeChannel<T>;
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

// The single-writer pole of a SwmrStage.
template <typename T>
struct FakeWriter {
    using channel_type = FakeCell<T>;
    static constexpr std::size_t per_call_working_set = 64;
    void publish(T const&) noexcept {}
};

inline void stage_pass_through(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

inline void stage_transform_int_to_float(FakeConsumer<int>&&, FakeProducer<float>&&) noexcept {}

// A fan-in body with two consumer handles and one producer handle, and a
// body that publishes into a single-writer cell.
inline void stage_fan_in_two(FakeConsumer<int>&&, FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

inline void stage_swmr_publish(FakeConsumer<int>&&, FakeWriter<int>&&) noexcept {}

}  // namespace detail::stage_witness

}  // namespace fixy::concurrent
