// The compile-time checks of fixy/channels/SwmrSession.h.

#include <fixy/channels/SwmrSession.h>

// The second table of the unit, so the root of the pole tables holds two
// families.
#include <fixy/channels/SubstrateSessionBridge.h>

#include <foundation/Linear.h>

namespace fixy::channels::swmr_session {

namespace detail::swmr_session_self_test {

using swmr_session_witness::WriterTag;
using swmr_session_witness::ReaderTag;
using swmr_session_witness::WriterBrand;
struct OtherWriterBrand {};
using swmr_session_witness::ReaderBrand;
using SmallSession = SwmrSession<int, WriterTag, ReaderTag, ReaderBrand, WriterBrand>;
using WriterEnd = SmallSession::WriterEnd;
using ReaderEnd = SmallSession::ReaderEnd;

// A scope brand that the checks name.  No frame door gives a handle on it,
// because a frame door takes its scope brand from the call.
struct CheckScope {};
struct OtherScope {};
using Writer = WriterHandle<SmallSession, CheckScope>;
using Reader = ReaderHandle<SmallSession, CheckScope>;

static_assert(SwmrSessionSurface<SmallSession>);
// The session states synchronized{}, so the check of foundation/Transfer.h
// reads each door of it and of its ends.  The writer end holds the writer
// token by value, and a reader end binds a const session.
static_assert(::foundation::transfer::Synchronized<SmallSession>);
static_assert(!SwmrSessionSurface<int>, "an int is not a channel");

// The member alias templates name the handles of the frame doors, and two
// scope brands give two handle types.
static_assert(std::is_same_v<SmallSession::writer_handle<CheckScope>, Writer>);
static_assert(std::is_same_v<SmallSession::reader_handle<CheckScope>, Reader>);
static_assert(!std::is_same_v<SmallSession::writer_handle<CheckScope>, SmallSession::writer_handle<OtherScope>>);

// The session takes the writer permission of its own brand, and no other.
// Each claim door takes the key of the frame doors, and no code outside them
// builds one, so no code outside them opens an end.
template <typename Session, typename Brand>
concept TakesWriterOfBrand = requires(Session& session, SwmrFrameKey const& key) {
    session.writer(key, std::declval<::foundation::permissions::Permission<typename Session::writer_tag, Brand>&&>());
};
static_assert(TakesWriterOfBrand<SmallSession, WriterBrand>);
static_assert(!TakesWriterOfBrand<SmallSession, OtherWriterBrand>,
              "a writer permission of another brand would make a second writer");
template <typename Session>
concept OpensWriterWithNoKey = requires(Session& session) {
    session.writer(std::declval<::foundation::permissions::Permission<typename Session::writer_tag, WriterBrand>&&>());
};
static_assert(!OpensWriterWithNoKey<SmallSession>);
static_assert(!std::is_default_constructible_v<SwmrFrameKey> && !std::is_copy_constructible_v<SwmrFrameKey>);
static_assert(!std::is_trivially_copyable_v<SwmrFrameKey>);
static_assert(!std::is_constructible_v<Writer, WriterEnd&&> && !std::is_constructible_v<Reader, ReaderEnd&&>);

// No session names the erased brand as its writer brand.
template <typename Brand>
concept NamesSessionOverWriterBrand = requires { typename SwmrSession<int, WriterTag, ReaderTag, ReaderBrand, Brand>; };
static_assert(NamesSessionOverWriterBrand<WriterBrand>);
static_assert(!NamesSessionOverWriterBrand<::foundation::brand::DefaultBrand>);

static_assert(sizeof(WriterEnd) == sizeof(SmallSession*), "the writer end must EBO-collapse the writer Permission.");
static_assert(sizeof(ReaderEnd)
                  == sizeof(SmallSession*)
                         + sizeof(::foundation::permissions::SharedPermissionGuard<ReaderTag, ReaderBrand>),
              "the reader end must only store a session pointer plus guard.");
static_assert(sizeof(Writer) == sizeof(WriterEnd) && sizeof(Reader) == sizeof(ReaderEnd),
              "a handle adds no byte to its end");
static_assert(!std::is_copy_constructible_v<Writer>);
static_assert(!std::is_copy_constructible_v<Reader>);
static_assert(std::is_move_constructible_v<Writer>);
static_assert(std::is_move_constructible_v<Reader>);
// An assignment would drop the writer token of the target.
static_assert(!std::is_move_assignable_v<Writer> && !std::is_move_assignable_v<Reader>);
static_assert(!std::is_move_assignable_v<WriterEnd> && !std::is_move_assignable_v<ReaderEnd>);
// A handle names its session as its first argument, and rule 8 of
// foundation/Transfer.h reads it.  The session is Sendable, so each handle
// moves to another thread when its end does.
static_assert(::foundation::transfer::Sendable<SmallSession>);
static_assert(::foundation::transfer::Sendable<Writer> == ::foundation::transfer::Sendable<WriterEnd>
              && ::foundation::transfer::Sendable<Reader> == ::foundation::transfer::Sendable<ReaderEnd>);
static_assert(SwmrWriterHandleOf<SmallSession, Writer>);
static_assert(!SwmrWriterHandleOf<SmallSession, Writer&>, "a reference to a writer handle is not a handle");
static_assert(!SwmrWriterHandleOf<SmallSession, Reader>, "a reader handle is not a writer handle");
static_assert(SwmrReaderHandleOf<SmallSession, Reader> && !SwmrReaderHandleOf<SmallSession, Writer>);

// The writer end holds the linear writer token, so it is linear, and
// release() gives the token back.  The writer handle is linear too, and it
// marks its own end, so the drop door ends it.  A reader holds a share, which
// is not linear.
static_assert(::foundation::linear::is_marked_linear(^^WriterEnd) && ::foundation::linear::is_linear_v<WriterEnd>);
static_assert(::foundation::linear::is_marked_linear(^^Writer) && ::foundation::linear::is_linear_v<Writer>);
static_assert(::foundation::linear::DroppableLinear<Writer>);
static_assert(!::foundation::linear::is_linear_v<ReaderEnd> && !::foundation::linear::is_linear_v<Reader>);
static_assert(std::is_same_v<decltype(std::declval<Writer>().release()),
                             ::foundation::permissions::Permission<WriterTag, WriterBrand>>);

static_assert(std::is_same_v<WriterRuntimeProto<int>,
                             ::fixy::session::Loop<::fixy::session::Send<int, ::fixy::session::Continue>>>);
static_assert(std::is_same_v<ReaderRuntimeProto<int>,
                             ::fixy::session::Loop<::fixy::session::Recv<int, ::fixy::session::Continue>>>);
static_assert(std::is_same_v<ReaderRuntimeProto<int>, ::fixy::session::dual_of_t<WriterRuntimeProto<int>>>);

// Each handle is a pole of the session, and its table gives the writer the
// writer protocol and each reader the reader protocol.  A mint of one end
// refuses a pole, and the pole door admits it.
static_assert(::fixy::session::ChannelPole<Writer> && ::fixy::session::ChannelPole<Reader>);
static_assert(!::fixy::session::PlainSessionResource<Writer>);
static_assert(std::is_same_v<::fixy::session::pole_protocol_t<Writer>, WriterRuntimeProto<int>>);
static_assert(std::is_same_v<::fixy::session::pole_peer_protocol_t<Writer>, ReaderRuntimeProto<int>>);
static_assert(std::is_same_v<::fixy::session::pole_protocol_t<Reader>, ReaderRuntimeProto<int>>);
static_assert(std::is_same_v<::fixy::session::pole_peer_protocol_t<Reader>, WriterRuntimeProto<int>>);

// Each read of the schedule takes Nondet: the lend of a reader share, the
// withdraw of the reader root, the load, the read with no wait, and the read
// of a reader session.  A context of the drain owns no Nondet, and a context
// with Div and no Nondet loads nothing.
using NoNondetCtx = ::fixy::BgDrainCtx;
using DivNoNondetCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Bg,
    ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Div>>;
using NondetCtx = ::fixy::BgLoadCtx;
template <typename Session, typename Ctx>
concept LendsReaderIn =
    requires(Session& session, Ctx const& ctx) { with_reader(ctx, session, [](auto /*reader*/) noexcept {}); };
template <typename Session, typename Ctx>
concept WithdrawsReaderRootIn = requires(Session& session, Ctx const& ctx) { session.try_withdraw_reader_root(ctx); };
template <typename Handle, typename Ctx>
concept LoadsIn = requires(Handle const& handle, Ctx const& ctx) { handle.load(ctx); };
template <typename Handle>
concept TryLoadsWithNoTag = requires(Handle const& handle) { handle.try_load(); };
template <typename Handle>
concept TryLoadsWithTag =
    requires(Handle const& handle, ::foundation::effects::cap::Nondet nondet) { handle.try_load(nondet); };
static_assert(LendsReaderIn<SmallSession, NondetCtx> && !LendsReaderIn<SmallSession, NoNondetCtx>);
static_assert(WithdrawsReaderRootIn<SmallSession, NondetCtx> && !WithdrawsReaderRootIn<SmallSession, NoNondetCtx>);
static_assert(LoadsIn<Reader, NondetCtx> && !LoadsIn<Reader, DivNoNondetCtx>);
static_assert(TryLoadsWithTag<Reader> && !TryLoadsWithNoTag<Reader>);
static_assert(CtxFitsSwmrReaderSession<NondetCtx, SmallSession, Reader>
              && !CtxFitsSwmrReaderSession<NoNondetCtx, SmallSession, Reader>);
// The write reads no schedule, so a writer session needs no Nondet.
static_assert(CtxFitsSwmrWriterSession<NoNondetCtx, SmallSession, Writer>);

// The unit includes the bridge too, so the root of the pole tables holds two
// families, and each pole reads the one table that knows it.
struct BridgeUserTag {};
struct BridgeUserBrand {};
using BridgedWire = ::fixy::concurrent::PermissionedChannel<::fixy::concurrent::ChannelForm::wire, long, 8,
                                                            BridgeUserTag, BridgeUserBrand>;
static_assert(std::is_same_v<::fixy::session::pole_protocol_t<typename BridgedWire::ConsumerEnd>,
                             ::fixy::session::Loop<::fixy::session::Recv<long, ::fixy::session::Continue>>>);
static_assert(
    std::is_same_v<::fixy::session::pole_protocol_t<::fixy::concurrent::ConsumerHandle<BridgedWire, CheckScope>>,
                   ::fixy::session::Loop<::fixy::session::Recv<long, ::fixy::session::Continue>>>);
static_assert(std::is_same_v<::fixy::session::pole_protocol_t<Writer>, WriterRuntimeProto<int>>);

}  // namespace detail::swmr_session_self_test

}  // namespace fixy::channels::swmr_session
