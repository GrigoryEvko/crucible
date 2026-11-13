#include <crucible/cntp/IncastControl.h>

#include <linux/tcp.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace crucible::cntp {

::fixy::TextView incast_error_name(IncastError error) noexcept { return ::foundation::core::enum_name_text(error); }

::fixy::Result<::fixy::Unit, IncastError> detail::set_socket_rto_min_usec_keyed(SocketOptionKey const&, SocketFd fd,
                                                                                PositiveRtoMinUsec rto_min) noexcept {
#ifdef TCP_RTO_MIN_US
    const std::uint32_t value = rto_min.value();
    const int rc = ::setsockopt(fd.value(), IPPROTO_TCP, TCP_RTO_MIN_US, &value, static_cast<socklen_t>(sizeof(value)));
    if (rc != 0) {
        return ::fixy::err(IncastError::SetRtoMinFailed);
    }
    return ::fixy::Unit{};
#else
    static_cast<void>(fd);
    static_cast<void>(rto_min);
    return ::fixy::err(IncastError::UnsupportedRtoMinSockOpt);
#endif
}

}  // namespace crucible::cntp
