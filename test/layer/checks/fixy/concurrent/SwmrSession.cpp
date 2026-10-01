// The compile-time checks of fixy/concurrent/SwmrSession.h.

#include <fixy/concurrent/SwmrSession.h>

namespace fixy::concurrent::swmr_session {

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
static_assert(SwmrWriterHandleOf<SmallSession, WriterHandle>);
static_assert(!SwmrWriterHandleOf<SmallSession, WriterHandle&>, "a reference to a writer handle is not a handle");
static_assert(!SwmrWriterHandleOf<SmallSession, ReaderHandle>, "a reader handle is not a writer handle");

static_assert(std::is_same_v<WriterRuntimeProto<int>,
                             ::fixy::session::Loop<::fixy::session::Send<int, ::fixy::session::Continue>>>);
static_assert(std::is_same_v<ReaderRuntimeProto<int>,
                             ::fixy::session::Loop<::fixy::session::Recv<int, ::fixy::session::Continue>>>);

}  // namespace detail::swmr_session_self_test

}  // namespace fixy::concurrent::swmr_session
