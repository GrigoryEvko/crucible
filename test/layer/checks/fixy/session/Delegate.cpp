// The compile-time checks of fixy/session/Delegate.h.

#include <fixy/session/Delegate.h>

namespace fixy::session {

static_assert(detail::is_sealed_passkey<DelegationKey>(),
              "a passkey is final, has a private user-provided constructor, no copy, no move and a user-provided "
              "destructor, so no route but its one friend makes it");

}  // namespace fixy::session

namespace fixy::session::detail::delegatable_armed_witness {
struct Wire {};
using Loose = SessionHandle<End, Wire, void, DefaultAbandonmentPolicy, ::foundation::permissions::EmptyPermSet>;
using Branded = SessionHandle<End, Wire, session_brand<Wire, void>, DefaultAbandonmentPolicy,
                              ::foundation::permissions::EmptyPermSet>;
using InLoop = SessionHandle<End, Wire, Loop<Send<int, Continue>>, DefaultAbandonmentPolicy,
                             ::foundation::permissions::EmptyPermSet>;
}  // namespace fixy::session::detail::delegatable_armed_witness

static_assert(::fixy::session::DelegatableHandle<::fixy::session::detail::delegatable_armed_witness::Loose>);

// A hand-off delegates, by the role of its payload rule.  The armed cell
// of the query in fixy/session/Flow.h cannot name the hand-off, because
// that header does not include this one.
static_assert(::fixy::session::payload_conveys_delegation_v<::fixy::session::DelegatedSession<
                  ::fixy::session::End, ::fixy::session::detail::delegatable_armed_witness::Wire,
                  ::fixy::session::DefaultAbandonmentPolicy, ::foundation::permissions::EmptyPermSet>>);
static_assert(!::fixy::session::DelegatableHandle<int>
              && !::fixy::session::DelegatableHandle<::fixy::session::detail::delegatable_armed_witness::Branded>
              && !::fixy::session::DelegatableHandle<::fixy::session::detail::delegatable_armed_witness::InLoop>);

// The two traits are aliases, and the roster walk of
// foundation/contracts/ArmedRoster.h finds class templates only.  These two
// assertions read the two cells.
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_delegate>);
static_assert(::foundation::contracts::armed_cell_holds_v<::fixy::session::is_accept>);
