// ── Shared scaffold for fixy_neg/* fixtures ────────────────────────
//
// Each fixture instantiates IsAccepted with a 22-engaged pack that
// omits exactly one axis.  The omitted axis name appears in both:
//   (a) a static_assert message naming `FixyNotEngaged_<Axis>` —
//       caught by the neg_compile_driver's grep, AND
//   (b) the first_missing_axis_v constexpr.
//
// The helper macro `FIXY_NEG_FIXTURE(AxisName)` expands to the
// boilerplate for one fixture; each .cpp drops in the macro and
// the fixture's main().

#pragma once

#include <crucible/fixy/Reject.h>
#include <tuple>

namespace fixy_neg_detail {

namespace fixy = crucible::fixy;
namespace gr   = crucible::fixy::grant;
using D        = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

// Compile-time per-axis omission helper: drop strict<Omit> from the
// otherwise-complete pack.
template <D Omit>
using PackWithout = std::tuple<
    std::conditional_t<Omit == D::Type,           void, strict<D::Type>>,
    std::conditional_t<Omit == D::Refinement,     void, strict<D::Refinement>>,
    std::conditional_t<Omit == D::Usage,          void, strict<D::Usage>>,
    std::conditional_t<Omit == D::Effect,         void, strict<D::Effect>>,
    std::conditional_t<Omit == D::Security,       void, strict<D::Security>>,
    std::conditional_t<Omit == D::Protocol,       void, strict<D::Protocol>>,
    std::conditional_t<Omit == D::Lifetime,       void, strict<D::Lifetime>>,
    std::conditional_t<Omit == D::Provenance,     void, strict<D::Provenance>>,
    std::conditional_t<Omit == D::Trust,          void, strict<D::Trust>>,
    std::conditional_t<Omit == D::Representation, void, strict<D::Representation>>,
    std::conditional_t<Omit == D::Observability,  void, strict<D::Observability>>,
    std::conditional_t<Omit == D::Complexity,     void, strict<D::Complexity>>,
    std::conditional_t<Omit == D::Precision,      void, strict<D::Precision>>,
    std::conditional_t<Omit == D::Space,          void, strict<D::Space>>,
    std::conditional_t<Omit == D::Overflow,       void, strict<D::Overflow>>,
    std::conditional_t<Omit == D::Mutation,       void, strict<D::Mutation>>,
    std::conditional_t<Omit == D::Reentrancy,     void, strict<D::Reentrancy>>,
    std::conditional_t<Omit == D::Size,           void, strict<D::Size>>,
    std::conditional_t<Omit == D::Version,        void, strict<D::Version>>,
    std::conditional_t<Omit == D::Staleness,      void, strict<D::Staleness>>,
    std::conditional_t<Omit == D::Synchronization, void, strict<D::Synchronization>>,
    std::conditional_t<Omit == D::Regime,         void, strict<D::Regime>>,
    std::conditional_t<Omit == D::FpMode,         void, strict<D::FpMode>>,
    std::conditional_t<Omit == D::SyscallSurface, void, strict<D::SyscallSurface>>>;

// Filter `void`s out of the pack; the resulting tuple is what we
// hand to IsAcceptedGrants.
template <typename... Ts> struct filter_void;
template <> struct filter_void<> { using type = std::tuple<>; };
template <typename T, typename... Rest>
struct filter_void<T, Rest...> {
    using rest = typename filter_void<Rest...>::type;
    template <typename Inner> struct prepend;
    template <typename... Xs> struct prepend<std::tuple<Xs...>> {
        using type = std::tuple<T, Xs...>;
    };
    using type = typename prepend<rest>::type;
};
template <typename... Rest>
struct filter_void<void, Rest...> {
    using type = typename filter_void<Rest...>::type;
};
template <typename Tuple> struct filter_void_tuple;
template <typename... Ts>
struct filter_void_tuple<std::tuple<Ts...>>
    : filter_void<Ts...> {};

template <D Omit>
using FilteredPack = typename filter_void_tuple<PackWithout<Omit>>::type;

// Drive IsAcceptedGrants on the filtered pack.  This must reject.
template <typename Tuple> struct rejects_via;
template <typename... Ts>
struct rejects_via<std::tuple<Ts...>> {
    static constexpr bool value = !fixy::IsAcceptedGrants<Ts...>;
    // fixy-H-08: first_missing_axis_v is now `std::optional<D>`.
    // optional<D>'s `operator==(const D&)` makes the per-axis
    // probe comparison below compile unchanged.
    static constexpr auto first_missing = fixy::first_missing_axis_v<Ts...>;
};

}  // namespace fixy_neg_detail

// One-line fixture: each .cpp #includes this header then writes
//
//     FIXY_NEG_FIXTURE(Usage);   // — for "Usage axis omitted"
//     int main() { return 0; }
//
// The macro fires a named static_assert containing "FixyNotEngaged_X"
// — the neg_compile_driver greps the compile output for that string.

#define FIXY_NEG_FIXTURE(AxisName)                                                  \
    using AxisName##Probe =                                                          \
        ::fixy_neg_detail::rejects_via<                                              \
            ::fixy_neg_detail::FilteredPack<                                         \
                ::fixy_neg_detail::D::AxisName>>;                                    \
    static_assert(AxisName##Probe::value,                                            \
        "IsAcceptedGrants must reject when " #AxisName " is unengaged.");            \
    static_assert(AxisName##Probe::first_missing                                     \
                  == ::fixy_neg_detail::D::AxisName,                                 \
        "first_missing_axis_v must point at " #AxisName);                            \
    using AxisName##Tag =                                                            \
        ::crucible::fixy::diag::tag_for_axis_t<                                      \
            ::fixy_neg_detail::D::AxisName>;                                         \
    /* Fire the named diagnostic — neg_compile_driver greps this string. */         \
    static_assert(sizeof(AxisName##Tag) > 0 && false,                                \
        "FixyNotEngaged_" #AxisName ": dim " #AxisName " is not engaged "            \
        "by any grant in the binding's Grants pack.  Add "                           \
        "grant::accept_default_strict_for<dim::DimensionAxis::"                      \
        #AxisName "> OR a per-axis relaxation tag.")
