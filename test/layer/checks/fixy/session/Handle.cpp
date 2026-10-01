// The compile-time checks of fixy/session/Handle.h.

#include <fixy/session/Handle.h>

namespace fixy::session {

static_assert(detail::is_sealed_passkey<HandleKey>() && detail::is_sealed_passkey<SessionOpenKey>(),
              "a passkey is final, has a private user-provided constructor, no copy, no move and a user-provided "
              "destructor, so no route but its one friend makes it");

static_assert(sizeof(std::size_t) == sizeof(std::uint64_t),
              "a label word has 64 bits, and the Transport carries it as std::size_t");

}  // namespace fixy::session
