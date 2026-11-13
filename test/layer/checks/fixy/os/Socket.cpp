// The compile-time checks of fixy/os/Socket.h.

#include <fixy/os/Socket.h>
#include <foundation/core/Array.h>
#include <foundation/core/Scalar.h>

#include <linux/net_tstamp.h>
#include <linux/sockios.h>
#include <net/if.h>

#include <cstddef>
#include <ctime>

namespace fixy::net::detail::socket_surface_invariants {

static_assert(socket_triple_of(^^socket_kind::NetlinkRoute).domain == AF_NETLINK);
static_assert(socket_triple_of(^^socket_kind::NetlinkRoute).type == SOCK_RAW);
static_assert(socket_triple_of(^^socket_kind::NetlinkRoute).protocol == NETLINK_ROUTE);

struct NotASocketKind final {};
static_assert(MappedSocketKind<socket_kind::NetlinkRoute>);
static_assert(!MappedSocketKind<NotASocketKind>, "a kind with no row must have no triple.");
static_assert(!MappedSocketKind<void>);
static_assert(!MappedSocketKind<const socket_kind::NetlinkRoute>, "a kind with a cv-qualifier is another type.");

// The row that the catalog gives to socket, stated here as a pin.
static_assert(std::is_same_v<socket_row_t, eff::Row<eff::Effect::IO, eff::Effect::Block>>);

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using IoOnlyCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init, eff::Effect::IO>>;

static_assert(CtxFitsSocketMint<IoBlockCtx, socket_kind::NetlinkRoute>);
static_assert(!CtxFitsSocketMint<IoOnlyCtx, socket_kind::NetlinkRoute>,
              "a context without Block must not open a socket: the catalog puts socket in a family that can park.");
static_assert(!CtxFitsSocketMint<IoBlockCtx, NotASocketKind>, "a kind with no triple must be refused.");
static_assert(!std::is_default_constructible_v<SocketDoor> && !std::is_copy_constructible_v<SocketDoor>
                  && !std::is_move_constructible_v<SocketDoor>,
              "No object of the socket door exists.  Its private member is the only call to ::socket.");

// THE KERNEL WORDS of fixy/os/SocketCalls.h are the words of the system
// headers.
static_assert(socket_level == SOL_SOCKET);
static_assert(timestamping_option == SO_TIMESTAMPING && timestamping_option == SCM_TIMESTAMPING);
static_assert(receive_timestamping_flags
              == (SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE
                  | SOF_TIMESTAMPING_RAW_HARDWARE | SOF_TIMESTAMPING_OPT_CMSG | SOF_TIMESTAMPING_OPT_TSONLY));
static_assert(set_hardware_timestamping_request == SIOCSHWTSTAMP);
static_assert(hardware_transmit_on == HWTSTAMP_TX_ON && hardware_filter_all == HWTSTAMP_FILTER_ALL);
static_assert(interface_name_capacity == IFNAMSIZ && interface_name_bytes + 1 == IFNAMSIZ);

static_assert(sizeof(HardwareTimestampConfig) == sizeof(::hwtstamp_config));
static_assert(offsetof(HardwareTimestampConfig, flags) == offsetof(::hwtstamp_config, flags));
static_assert(offsetof(HardwareTimestampConfig, transmit) == offsetof(::hwtstamp_config, tx_type));
static_assert(offsetof(HardwareTimestampConfig, receive_filter) == offsetof(::hwtstamp_config, rx_filter));

static_assert(sizeof(InterfaceRequest) == sizeof(::ifreq));
static_assert(offsetof(InterfaceRequest, name) == offsetof(::ifreq, ifr_name));
static_assert(offsetof(InterfaceRequest, data) == offsetof(::ifreq, ifr_data));

static_assert(sizeof(IoVector) == sizeof(::iovec));
static_assert(offsetof(IoVector, base) == offsetof(::iovec, iov_base));
static_assert(offsetof(IoVector, length) == offsetof(::iovec, iov_len));

static_assert(sizeof(MessageHeader) == sizeof(::msghdr));
static_assert(offsetof(MessageHeader, name) == offsetof(::msghdr, msg_name));
static_assert(offsetof(MessageHeader, name_length) == offsetof(::msghdr, msg_namelen));
static_assert(offsetof(MessageHeader, vectors) == offsetof(::msghdr, msg_iov));
static_assert(offsetof(MessageHeader, vector_count) == offsetof(::msghdr, msg_iovlen));
static_assert(offsetof(MessageHeader, control) == offsetof(::msghdr, msg_control));
static_assert(offsetof(MessageHeader, control_length) == offsetof(::msghdr, msg_controllen));
static_assert(offsetof(MessageHeader, flags) == offsetof(::msghdr, msg_flags));

static_assert(sizeof(ControlHeader) == sizeof(::cmsghdr) && CMSG_LEN(0) == sizeof(ControlHeader));
static_assert(offsetof(ControlHeader, length) == offsetof(::cmsghdr, cmsg_len));
static_assert(offsetof(ControlHeader, level) == offsetof(::cmsghdr, cmsg_level));
static_assert(offsetof(ControlHeader, type) == offsetof(::cmsghdr, cmsg_type));
static_assert(CMSG_ALIGN(1) == control_alignment);

// The stamps of struct scm_timestamping are the struct timespec of the C
// library, and a 64-bit build gives it the layout of KernelTimespec.
static_assert(sizeof(::foundation::os::KernelTimespec) == sizeof(::timespec));
static_assert(offsetof(::foundation::os::KernelTimespec, seconds) == offsetof(::timespec, tv_sec));
static_assert(offsetof(::foundation::os::KernelTimespec, nanoseconds) == offsetof(::timespec, tv_nsec));
static_assert(timestamping_message_bytes == CMSG_LEN(3 * sizeof(::timespec)));

// THE WALK of the control data.  Each case is the bytes of control messages
// that a record of a header and its data gives.

// One message: its header and the three stamps of SCM_TIMESTAMPING.
struct OneMessage final {
    ControlHeader header{};
    ::foundation::os::KernelTimespec first{};
    ::foundation::os::KernelTimespec second{};
    ::foundation::os::KernelTimespec third{};
};

// A message of 20 bytes, padded to 24, and then a second message.
struct TwoMessages final {
    ControlHeader header{};
    std::uint32_t data = 0;
    std::uint32_t pad = 0;
    OneMessage next{};
};

// The walk over the first count bytes of record.  The bytes are a local
// array, because the build has -fno-delete-null-pointer-checks, and GCC then
// folds no compare of the address of a global with null.
template <class Record>
[[nodiscard]] consteval TimestampControl walk_of(Record record, std::size_t count = sizeof(Record)) noexcept {
    ::foundation::core::FixedArray<std::byte, sizeof(Record)> const bytes =
        ::foundation::core::bit_cast<::foundation::core::FixedArray<std::byte, sizeof(Record)>>(record);
    return read_timestamp_control_(bytes.view().window(0, count).expect("the count is at most the record"));
}

// The software stamp and the raw hardware stamp of the result, or -1 for a
// result with no stamps.
[[nodiscard]] consteval std::int64_t stamp_sum(TimestampControl control) noexcept {
    return static_cast<TimestampControl&&>(control).match(
        [](NoTimestampMessage) -> std::int64_t { return -1; }, [](ShortTimestampMessage) -> std::int64_t { return -1; },
        [](ReceiveStamps stamps) -> std::int64_t {
            std::int64_t const software = stamps.software.seconds * 1000 + stamps.software.nanoseconds * 100;
            return software + stamps.raw_hardware.seconds * 10 + stamps.raw_hardware.nanoseconds;
        });
}

inline constexpr OneMessage stamped{
    .header = {.length = timestamping_message_bytes, .level = socket_level, .type = timestamping_option},
    .first = {.seconds = 5, .nanoseconds = 6},
    .second = {.seconds = 0, .nanoseconds = 0},
    .third = {.seconds = 7, .nanoseconds = 8},
};

// A message of 20 bytes of a different type, and then the stamped message.
inline constexpr TwoMessages two_messages{
    .header = {.length = sizeof(ControlHeader) + 4, .level = socket_level, .type = timestamping_option - 1},
    .data = 0,
    .pad = 0,
    .next = stamped,
};

// The software stamp is index 0 and the raw hardware stamp is index 2.
static_assert(stamp_sum(walk_of(stamped)) == 5678);

// A message shorter than its three stamps is short.
static_assert(walk_of(OneMessage{
                          .header = {.length = timestamping_message_bytes - 8,
                                     .level = socket_level,
                                     .type = timestamping_option},
                      })
                  .is<ShortTimestampMessage>());

// A message of a different type or level is no stamp message.
static_assert(walk_of(OneMessage{
                          .header = {.length = timestamping_message_bytes,
                                     .level = socket_level,
                                     .type = timestamping_option - 1},
                      })
                  .is<NoTimestampMessage>());
static_assert(walk_of(OneMessage{
                          .header = {.length = timestamping_message_bytes,
                                     .level = socket_level + 1,
                                     .type = timestamping_option},
                      })
                  .is<NoTimestampMessage>());

// The walk steps over a message to its length rounded up to 8 bytes.
static_assert(stamp_sum(walk_of(two_messages)) == 5678);

// A step past the bytes ends the walk: the first message has 20 bytes, and
// its step of 24 bytes leaves the first 20 bytes.
static_assert(walk_of(two_messages, sizeof(ControlHeader) + 4).is<NoTimestampMessage>());

// A length below the header ends the walk, so a zero length does not loop.
static_assert(walk_of(TwoMessages{.next = stamped}).is<NoTimestampMessage>());

// A length past the bytes ends the walk.
static_assert(
    walk_of(OneMessage{
                .header = {.length = sizeof(OneMessage) + 1, .level = socket_level, .type = timestamping_option},
            })
        .is<NoTimestampMessage>());

// Control data with no room for a header holds no message.
static_assert(read_timestamp_control_(::foundation::core::View<std::byte const>{}).is<NoTimestampMessage>());
static_assert(walk_of(stamped, sizeof(ControlHeader) - 1).is<NoTimestampMessage>());

}  // namespace fixy::net::detail::socket_surface_invariants
