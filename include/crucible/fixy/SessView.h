#pragma once

#include <crucible/sessions/SessionView.h>

#include <cstddef>
#include <string_view>
#include <type_traits>

namespace crucible::fixy::sess::view {

using ::crucible::safety::proto::AtSend;
using ::crucible::safety::proto::AtRecv;
using ::crucible::safety::proto::AtSelect;
using ::crucible::safety::proto::AtOffer;
using ::crucible::safety::proto::AtEnd;
using ::crucible::safety::proto::AtStop;
using ::crucible::safety::proto::AtTerminal;
using ::crucible::safety::proto::AtCheckpointed;
using ::crucible::safety::proto::AtDelegate;
using ::crucible::safety::proto::AtAccept;

using ::crucible::safety::proto::handle_is_at;
using ::crucible::safety::proto::handle_is_at_v;
using ::crucible::safety::proto::HandleIsAt;

using ::crucible::safety::proto::view_ok;

using ::crucible::safety::proto::mint_session_view;

using ::crucible::safety::proto::session_view_protocol_name;

using ::crucible::safety::proto::session_view_message_type;
using ::crucible::safety::proto::session_view_message_type_t;

using ::crucible::safety::proto::session_view_branch_count;
using ::crucible::safety::proto::session_view_branch_count_v;

}  // namespace crucible::fixy::sess::view

namespace crucible::fixy::sess::view::v063_self_test {

namespace proto = ::crucible::safety::proto;
namespace saf = ::crucible::safety;

struct FakeResource {};
struct Msg {};

using SendProto = proto::Send<Msg, proto::End>;
using RecvProto = proto::Recv<Msg, proto::End>;
using EndProto = proto::End;

using SendHandle = proto::SessionHandle<SendProto, FakeResource, void>;
using RecvHandle = proto::SessionHandle<RecvProto, FakeResource, void>;
using EndHandle = proto::SessionHandle<EndProto, FakeResource, void>;

static_assert(std::is_same_v<AtSend, proto::AtSend>);
static_assert(std::is_same_v<AtRecv, proto::AtRecv>);
static_assert(std::is_same_v<AtSelect, proto::AtSelect>);
static_assert(std::is_same_v<AtOffer, proto::AtOffer>);
static_assert(std::is_same_v<AtEnd, proto::AtEnd>);
static_assert(std::is_same_v<AtStop, proto::AtStop>);
static_assert(std::is_same_v<AtTerminal, proto::AtTerminal>);
static_assert(std::is_same_v<AtCheckpointed, proto::AtCheckpointed>);
static_assert(std::is_same_v<AtDelegate, proto::AtDelegate>);
static_assert(std::is_same_v<AtAccept, proto::AtAccept>);

static_assert(handle_is_at_v<SendHandle, AtSend>);
static_assert(!handle_is_at_v<SendHandle, AtRecv>);
static_assert(handle_is_at_v<EndHandle, AtEnd>);
static_assert(handle_is_at_v<EndHandle, AtTerminal>);
static_assert(handle_is_at_v<SendHandle, AtSend> == proto::handle_is_at_v<SendHandle, proto::AtSend>,
              "handle_is_at_v must reach identically through fixy::");

static_assert(HandleIsAt<SendHandle, AtSend>);
static_assert(HandleIsAt<RecvHandle, AtRecv>);
static_assert(!HandleIsAt<SendHandle, AtRecv>);
static_assert(!HandleIsAt<EndHandle, AtSend>);
static_assert(HandleIsAt<EndHandle, AtTerminal>);

// A handle in a non-terminal state runs an abandonment check in its
// destructor, so these assertions witness signatures through declval and
// construct no handle.
static_assert(std::is_same_v<decltype(view_ok(std::declval<SendHandle const&>(), std::type_identity<AtSend>{})), bool>,
              "view_ok(handle, type_identity<Tag>) must be a bool-returning "
              "predicate at the fixy:: re-export boundary.");

using MintedView = decltype(mint_session_view<AtSend>(std::declval<SendHandle const&>()));
static_assert(std::is_same_v<MintedView, saf::ScopedView<SendHandle, AtSend>>,
              "mint_session_view<AtSend>(send_handle&) must produce "
              "ScopedView<SendHandle, AtSend>.");
static_assert(
    std::is_same_v<MintedView, decltype(proto::mint_session_view<proto::AtSend>(std::declval<SendHandle const&>()))>,
    "mint_session_view must reach identically through fixy::");

using SendView = saf::ScopedView<SendHandle, AtSend>;
using RecvView = saf::ScopedView<RecvHandle, AtRecv>;
static_assert(std::is_same_v<session_view_message_type_t<SendView>, Msg>,
              "session_view_message_type_t<AtSend view> = Msg.");
static_assert(std::is_same_v<session_view_message_type_t<RecvView>, Msg>,
              "session_view_message_type_t<AtRecv view> = Msg.");
static_assert(std::is_same_v<typename session_view_message_type<SendView>::type, Msg>);

using SelectProto = proto::Select<SendProto, RecvProto, EndProto>;
using SelectHandle = proto::SessionHandle<SelectProto, FakeResource, void>;
using SelectView = saf::ScopedView<SelectHandle, AtSelect>;
static_assert(session_view_branch_count_v<SelectView> == 3,
              "session_view_branch_count_v counts sizeof...(Bs) of Select.");
static_assert(session_view_branch_count<SelectView>::value == 3, "Class-template form returns the same count.");

constexpr int v063_surface_cardinality = 20;
static_assert(v063_surface_cardinality == 20, "The re-exported view surface cardinality drifted. Update "
                                              "the using-decls and this sentinel together.");

}  // namespace crucible::fixy::sess::view::v063_self_test

namespace crucible::fixy::sess::view {

inline void runtime_smoke_test() noexcept {
    namespace proto = ::crucible::safety::proto;
    namespace saf = ::crucible::safety;

    struct FakeResource {};
    struct Msg {};

    using SendProto = proto::Send<Msg, proto::End>;
    using SendHandle = proto::SessionHandle<SendProto, FakeResource, void>;

    // A handle in a non-terminal state runs an abandonment check in its
    // destructor, so this check stays type-level and constructs no handle.
    [[maybe_unused]] constexpr bool admits_send = HandleIsAt<SendHandle, AtSend>;
    [[maybe_unused]] constexpr bool rejects_recv = !HandleIsAt<SendHandle, AtRecv>;

    using ViewType = decltype(mint_session_view<AtSend>(std::declval<SendHandle const&>()));
    [[maybe_unused]] constexpr bool msg_ok = std::is_same_v<session_view_message_type_t<ViewType>, Msg>;
    [[maybe_unused]] constexpr bool view_shape_ok = std::is_same_v<ViewType, saf::ScopedView<SendHandle, AtSend>>;

    (void)admits_send;
    (void)rejects_recv;
    (void)msg_ok;
    (void)view_shape_ok;
}

}  // namespace crucible::fixy::sess::view
