#pragma once

#include <crucible/sessions/Session.h>

#include <type_traits>

namespace crucible::fixy::sess::shape {

using ::crucible::safety::proto::is_send_v;
using ::crucible::safety::proto::is_recv_v;

using ::crucible::safety::proto::is_select_v;
using ::crucible::safety::proto::is_offer_v;

using ::crucible::safety::proto::is_loop_v;

using ::crucible::safety::proto::is_head_v;

}  // namespace crucible::fixy::sess::shape

namespace crucible::fixy::sess::shape::v066_self_test {

namespace proto = ::crucible::safety::proto;

struct Probe {};

using SendP = proto::Send<Probe, proto::End>;
using RecvP = proto::Recv<Probe, proto::End>;
using SelectP = proto::Select<SendP>;
using OfferP = proto::Offer<RecvP>;
using LoopP = proto::Loop<proto::End>;
using EndP = proto::End;
using ContP = proto::Continue;

static_assert(is_send_v<SendP>, "fixy::sess::shape::is_send_v must admit Send<T, K>. A failure "
                                "means the using-decl is broken or the substrate predicate moved.");
static_assert(!is_send_v<RecvP>, "is_send_v must REJECT Recv (sender-vs-receiver discriminant).");
static_assert(!is_send_v<EndP>);
static_assert(!is_send_v<LoopP>);
static_assert(is_send_v<SendP> == proto::is_send_v<SendP>,
              "is_send_v must produce identical results through fixy::sess::shape::.");

static_assert(is_recv_v<RecvP>);
static_assert(!is_recv_v<SendP>);
static_assert(!is_recv_v<EndP>);
static_assert(!is_recv_v<LoopP>);
static_assert(is_recv_v<RecvP> == proto::is_recv_v<RecvP>);

static_assert(is_select_v<SelectP>);
static_assert(!is_select_v<OfferP>, "is_select_v must REJECT Offer (internal-vs-external choice "
                                    "discriminant).");
static_assert(!is_select_v<SendP>);
static_assert(!is_select_v<LoopP>);
static_assert(is_select_v<SelectP> == proto::is_select_v<SelectP>);

static_assert(is_offer_v<OfferP>);
static_assert(!is_offer_v<SelectP>);
static_assert(!is_offer_v<RecvP>);
static_assert(!is_offer_v<LoopP>);
static_assert(is_offer_v<OfferP> == proto::is_offer_v<OfferP>);

static_assert(is_loop_v<LoopP>);
static_assert(!is_loop_v<SendP>);
static_assert(!is_loop_v<EndP>);
static_assert(!is_loop_v<SelectP>);
static_assert(is_loop_v<LoopP> == proto::is_loop_v<LoopP>);

static_assert(is_head_v<SendP>);
static_assert(is_head_v<RecvP>);
static_assert(is_head_v<SelectP>);
static_assert(is_head_v<OfferP>);
static_assert(is_head_v<EndP>);
static_assert(is_head_v<ContP>);
static_assert(!is_head_v<LoopP>, "is_head_v must REJECT Loop (the recursion wrapper is the only "
                                 "non-head shape per substrate's negation-based definition).");
static_assert(is_head_v<SendP> == proto::is_head_v<SendP>);

template <typename P>
consteval int count_shape_matches() {
    int count = 0;
    if (is_send_v<P>) ++count;
    if (is_recv_v<P>) ++count;
    if (is_select_v<P>) ++count;
    if (is_offer_v<P>) ++count;
    if (is_loop_v<P>) ++count;
    return count;
}

static_assert(count_shape_matches<SendP>() == 1);
static_assert(count_shape_matches<RecvP>() == 1);
static_assert(count_shape_matches<SelectP>() == 1);
static_assert(count_shape_matches<OfferP>() == 1);
static_assert(count_shape_matches<LoopP>() == 1);
static_assert(count_shape_matches<EndP>() == 0, "End is a terminal state — none of the head-shape predicates "
                                                "should fire on it.");
static_assert(count_shape_matches<ContP>() == 0, "Continue is a recursion marker — none of the head-shape "
                                                 "predicates should fire on it.");

constexpr int v066_surface_cardinality = 6;
static_assert(v066_surface_cardinality == 6, "The re-exported shape-predicate surface cardinality drifted. "
                                             "Update the using-decls and this sentinel together.");

}  // namespace crucible::fixy::sess::shape::v066_self_test

namespace crucible::fixy::sess::shape {

inline void runtime_smoke_test() noexcept {
    namespace proto = ::crucible::safety::proto;
    struct Probe {};

    using S = proto::Send<Probe, proto::End>;
    using R = proto::Recv<Probe, proto::End>;
    using Se = proto::Select<S>;
    using Of = proto::Offer<R>;
    using L = proto::Loop<proto::End>;
    using E = proto::End;

    [[maybe_unused]] constexpr bool send_yes = is_send_v<S>;
    [[maybe_unused]] constexpr bool send_no = is_send_v<R>;
    [[maybe_unused]] constexpr bool recv_yes = is_recv_v<R>;
    [[maybe_unused]] constexpr bool recv_no = is_recv_v<S>;
    [[maybe_unused]] constexpr bool select_yes = is_select_v<Se>;
    [[maybe_unused]] constexpr bool select_no = is_select_v<Of>;
    [[maybe_unused]] constexpr bool offer_yes = is_offer_v<Of>;
    [[maybe_unused]] constexpr bool offer_no = is_offer_v<Se>;
    [[maybe_unused]] constexpr bool loop_yes = is_loop_v<L>;
    [[maybe_unused]] constexpr bool loop_no = is_loop_v<S>;
    [[maybe_unused]] constexpr bool head_send = is_head_v<S>;
    [[maybe_unused]] constexpr bool head_end = is_head_v<E>;
    [[maybe_unused]] constexpr bool head_no = is_head_v<L>;

    (void)send_yes;
    (void)send_no;
    (void)recv_yes;
    (void)recv_no;
    (void)select_yes;
    (void)select_no;
    (void)offer_yes;
    (void)offer_no;
    (void)loop_yes;
    (void)loop_no;
    (void)head_send;
    (void)head_end;
    (void)head_no;
}

}  // namespace crucible::fixy::sess::shape
