// The compile-time checks of fixy/concurrent/Endpoint.h.

#include <fixy/concurrent/Endpoint.h>

namespace fixy::concurrent {

namespace detail::endpoint_self_test {

namespace proto = ::fixy::session;

using endpoint_witness::UserTag;
using endpoint_witness::UserBrand;
using SmallSpsc = PermissionedSpscChannel<int, 64, UserTag, UserBrand>;
using SmallMpsc = PermissionedMpscChannel<int, 64, UserTag, UserBrand>;
using endpoint_witness::FgCtx;
using ProdEp = Endpoint<SmallSpsc, Direction::Producer, FgCtx>;
using ConsEp = Endpoint<SmallSpsc, Direction::Consumer, FgCtx>;
using MpscProdEp = Endpoint<SmallMpsc, Direction::Producer, FgCtx>;

static_assert(std::is_same_v<typename ProdEp::handle_type, typename SmallSpsc::ProducerHandle>);
static_assert(std::is_same_v<typename ConsEp::handle_type, typename SmallSpsc::ConsumerHandle>);
static_assert(std::is_same_v<typename MpscProdEp::handle_type, typename SmallMpsc::ProducerHandle>);
static_assert(std::is_same_v<typename ProdEp::value_type, int>);
static_assert(std::is_same_v<typename ProdEp::user_tag, UserTag>);
static_assert(std::is_same_v<typename ProdEp::ctx_type, FgCtx>);
static_assert(std::is_same_v<typename ProdEp::proto_type, proto::Loop<proto::Send<int, proto::Continue>>>);

// Head line, tail line and the cell line, as the handle states.
static_assert(ProdEp::per_call_working_set == 192);

static_assert(!std::is_copy_constructible_v<ProdEp>);
static_assert(!std::is_copy_assignable_v<ProdEp>);
static_assert(std::is_move_constructible_v<ProdEp>);
static_assert(!std::is_move_assignable_v<ProdEp>);
static_assert(std::is_nothrow_move_constructible_v<ProdEp>);

static_assert(sizeof(ProdEp) == sizeof(void*), "Endpoint must collapse to pointer-size — Ctx EBO-collapse is "
                                               "load-bearing for the zero-runtime-cost claim.");
static_assert(sizeof(ConsEp) == sizeof(void*));

}  // namespace detail::endpoint_self_test

}  // namespace fixy::concurrent
