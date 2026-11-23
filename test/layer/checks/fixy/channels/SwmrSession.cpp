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
using WriterHandle = SmallSession::WriterHandle;
using ReaderHandle = SmallSession::ReaderHandle;

static_assert(SwmrSessionSurface<SmallSession>);
// The session states synchronized{}, so the check of foundation/Transfer.h
// reads each door of it and of its handles.  The writer handle holds the
// writer token by value, and a reader handle binds a const session.
static_assert(::foundation::transfer::Synchronized<SmallSession>);
static_assert(!SwmrSessionSurface<int>, "an int is not a channel");

// The session takes the writer permission of its own brand, and no other.
template <typename Session, typename Brand>
concept TakesWriterOfBrand = requires(Session& session) {
    session.writer(std::declval<::foundation::permissions::Permission<typename Session::writer_tag, Brand>&&>());
};
static_assert(TakesWriterOfBrand<SmallSession, WriterBrand>);
static_assert(!TakesWriterOfBrand<SmallSession, OtherWriterBrand>,
              "a writer permission of another brand would make a second writer");

// No session names the erased brand as its writer brand.
template <typename Brand>
concept NamesSessionOverWriterBrand = requires { typename SwmrSession<int, WriterTag, ReaderTag, ReaderBrand, Brand>; };
static_assert(NamesSessionOverWriterBrand<WriterBrand>);
static_assert(!NamesSessionOverWriterBrand<::foundation::brand::DefaultBrand>);

static_assert(sizeof(WriterHandle) == sizeof(SmallSession*),
              "SwmrSession::WriterHandle must EBO-collapse the writer Permission.");
static_assert(sizeof(ReaderHandle)
                  == sizeof(SmallSession*)
                         + sizeof(::foundation::permissions::SharedPermissionGuard<ReaderTag, ReaderBrand>),
              "SwmrSession::ReaderHandle must only store a session pointer plus guard.");
static_assert(!std::is_copy_constructible_v<WriterHandle>);
static_assert(!std::is_copy_constructible_v<ReaderHandle>);
static_assert(std::is_move_constructible_v<WriterHandle>);
static_assert(std::is_move_constructible_v<ReaderHandle>);
// An assignment would drop the writer token of the target.
static_assert(!std::is_move_assignable_v<WriterHandle> && !std::is_move_assignable_v<ReaderHandle>);
static_assert(SwmrWriterHandleOf<SmallSession, WriterHandle>);
static_assert(!SwmrWriterHandleOf<SmallSession, WriterHandle&>, "a reference to a writer handle is not a handle");
static_assert(!SwmrWriterHandleOf<SmallSession, ReaderHandle>, "a reader handle is not a writer handle");

// The writer handle holds the linear writer token, so it is linear, and
// release() gives the token back.  A reader handle holds a share, which
// is not linear.
static_assert(::foundation::linear::is_marked_linear(^^WriterHandle)
              && ::foundation::linear::is_linear_v<WriterHandle>);
static_assert(!::foundation::linear::is_linear_v<ReaderHandle>);
static_assert(std::is_same_v<decltype(std::declval<WriterHandle>().release()),
                             ::foundation::permissions::Permission<WriterTag, WriterBrand>>);

static_assert(std::is_same_v<WriterRuntimeProto<int>,
                             ::fixy::session::Loop<::fixy::session::Send<int, ::fixy::session::Continue>>>);
static_assert(std::is_same_v<ReaderRuntimeProto<int>,
                             ::fixy::session::Loop<::fixy::session::Recv<int, ::fixy::session::Continue>>>);
static_assert(std::is_same_v<ReaderRuntimeProto<int>, ::fixy::session::dual_of_t<WriterRuntimeProto<int>>>);

// Each handle is a pole of the session, and its table gives the writer the
// writer protocol and each reader the reader protocol.  A mint of one end
// refuses a pole, and the pole door admits it.
static_assert(::fixy::session::ChannelPole<WriterHandle> && ::fixy::session::ChannelPole<ReaderHandle>);
static_assert(!::fixy::session::PlainSessionResource<WriterHandle>);
static_assert(std::is_same_v<::fixy::session::pole_protocol_t<WriterHandle>, WriterRuntimeProto<int>>);
static_assert(std::is_same_v<::fixy::session::pole_peer_protocol_t<WriterHandle>, ReaderRuntimeProto<int>>);
static_assert(std::is_same_v<::fixy::session::pole_protocol_t<ReaderHandle>, ReaderRuntimeProto<int>>);
static_assert(std::is_same_v<::fixy::session::pole_peer_protocol_t<ReaderHandle>, WriterRuntimeProto<int>>);

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
concept LendsReaderIn = requires(Session& session, Ctx const& ctx) { session.reader(ctx); };
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
static_assert(LoadsIn<ReaderHandle, NondetCtx> && !LoadsIn<ReaderHandle, DivNoNondetCtx>);
static_assert(TryLoadsWithTag<ReaderHandle> && !TryLoadsWithNoTag<ReaderHandle>);
static_assert(CtxFitsSwmrReaderMint<NondetCtx, SmallSession> && !CtxFitsSwmrReaderMint<NoNondetCtx, SmallSession>);
static_assert(CtxFitsSwmrReaderSession<NondetCtx, SmallSession, ReaderHandle>
              && !CtxFitsSwmrReaderSession<NoNondetCtx, SmallSession, ReaderHandle>);
// The write reads no schedule, so a writer session needs no Nondet.
static_assert(CtxFitsSwmrWriterSession<NoNondetCtx, SmallSession, WriterHandle>);

// The unit includes the bridge too, so the root of the pole tables holds two
// families, and each pole reads the one table that knows it.
struct BridgeUserTag {};
struct BridgeUserBrand {};
using BridgedWire = ::fixy::concurrent::PermissionedChannel<::fixy::concurrent::ChannelForm::wire, long, 8,
                                                            BridgeUserTag, BridgeUserBrand>;
static_assert(std::is_same_v<::fixy::session::pole_protocol_t<typename BridgedWire::ConsumerHandle>,
                             ::fixy::session::Loop<::fixy::session::Recv<long, ::fixy::session::Continue>>>);
static_assert(std::is_same_v<::fixy::session::pole_protocol_t<WriterHandle>, WriterRuntimeProto<int>>);

}  // namespace detail::swmr_session_self_test

}  // namespace fixy::channels::swmr_session
