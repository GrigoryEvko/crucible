// The compile-time checks of fixy/session/Network.h.

#include <fixy/session/Network.h>

static_assert(::fixy::session::network_refusal_v<::fixy::session::detail::network::witness::OneMessage,
                                                 ::fixy::session::Network::Local>
              == ::fixy::session::NetworkRefusal::PeerOnLocal);
