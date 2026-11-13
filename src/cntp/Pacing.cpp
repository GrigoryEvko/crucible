#include <crucible/cntp/Pacing.h>

#include <foundation/os/File.h>

#include <linux/pkt_sched.h>
#include <linux/rtnetlink.h>

namespace crucible::cntp {

// The records and the constants of Pacing.h have the bytes and the values
// of the kernel headers.
static_assert(sizeof(detail::NetlinkHeader) == NLMSG_HDRLEN);
static_assert(sizeof(detail::TrafficControlMessage) == sizeof(tcmsg));
static_assert(sizeof(detail::RouteAttribute) == RTA_LENGTH(0));
static_assert(detail::netlink_error == NLMSG_ERROR);
static_assert(detail::netlink_done == NLMSG_DONE);
static_assert(detail::route_new_qdisc == RTM_NEWQDISC);
static_assert(detail::route_get_qdisc == RTM_GETQDISC);
static_assert(detail::netlink_dump_request == (NLM_F_REQUEST | NLM_F_DUMP));
static_assert(detail::qdisc_kind_attribute == TCA_KIND);
static_assert(detail::root_parent == TC_H_ROOT);

namespace {

using Answer = ::fixy::Option<::fixy::Result<Qdisc, PacingError>>;

// The longest kind of a queueing discipline, with its zero (IFNAMSIZ).
constexpr std::size_t qdisc_kind_bytes = 16;

// A netlink message and a route attribute start at a multiple of four bytes.
[[nodiscard]] constexpr std::size_t align4(std::size_t bytes) noexcept { return (bytes + 3u) & ~std::size_t{3u}; }

// The record T at offset of bytes, or no value when the bytes end before
// the end of T.  T is a record of integers, so each bit pattern is a value.
// The loop copies one byte at a time: fixy::copy is a memmove, and a build
// with no inlining calls memmove of libc for a record of 16 bytes or more.
template <class T>
[[nodiscard]] ::fixy::Option<T> read_record(::fixy::View<std::byte const> bytes, std::size_t offset) noexcept {
    CRUCIBLE_TRY(part, bytes.window<sizeof(T)>(offset));
    ::foundation::core::FixedArray<std::byte, sizeof(T)> image{};
    for (auto [target, source] : ::fixy::zip(image.view(), part)) {
        *target = *source;
    }
    return ::fixy::some(::fixy::bit_cast<T>(image));
}

// The answer of a result.
[[nodiscard]] Answer answer_of(::fixy::Result<Qdisc, PacingError> result) noexcept {
    return Answer::some(std::move(result));
}

// The qdisc of the kind of an attribute: the characters before the first
// zero.  A kind longer than each kind of the kernel is no known qdisc.
[[nodiscard]] ::fixy::Result<Qdisc, PacingError> qdisc_of_kind(::fixy::View<std::byte const> kind_bytes) noexcept {
    ::fixy::FixedText<qdisc_kind_bytes> kind{};
    for (std::byte const byte : kind_bytes) {
        char const character = static_cast<char>(byte);
        if (character == '\0') {
            break;
        }
        if (kind.push(character).is_err()) {
            return Qdisc::Other;
        }
    }
    return qdisc_from_kernel_name(kind.view());
}

// The qdisc of the attribute TCA_KIND among the attributes of a message.
// Complexity: linear in the count of the attributes.
[[nodiscard]] ::fixy::Result<Qdisc, PacingError>
qdisc_of_attributes(::fixy::View<std::byte const> attributes) noexcept {
    std::size_t offset = 0;
    while (attributes.size() - offset >= sizeof(detail::RouteAttribute)) {
        detail::RouteAttribute const attribute =
            read_record<detail::RouteAttribute>(attributes, offset).expect("the loop test fits the attribute header");
        std::size_t const remaining = attributes.size() - offset;
        if (attribute.length < sizeof(detail::RouteAttribute) || attribute.length > remaining) {
            return ::fixy::err(PacingError::QdiscKindMissing);
        }
        if (attribute.type == detail::qdisc_kind_attribute) {
            return qdisc_of_kind(
                attributes
                    .window(offset + sizeof(detail::RouteAttribute), attribute.length - sizeof(detail::RouteAttribute))
                    .expect("the attribute lies inside the message"));
        }
        std::size_t const step = align4(attribute.length);
        if (step > remaining) {
            return ::fixy::err(PacingError::QdiscKindMissing);
        }
        offset += step;
    }
    return ::fixy::err(PacingError::QdiscKindMissing);
}

// The qdisc of a message RTM_NEWQDISC that names the root qdisc of the
// interface, or no value for the message of a different qdisc.
[[nodiscard]] Answer root_qdisc_of(::fixy::View<std::byte const> message, std::int32_t interface_index) noexcept {
    constexpr std::size_t attributes_offset = sizeof(detail::NetlinkHeader) + sizeof(detail::TrafficControlMessage);
    if (message.size() < attributes_offset) {
        return answer_of(::fixy::err(PacingError::NetlinkReceiveFailed));
    }
    detail::TrafficControlMessage const traffic =
        read_record<detail::TrafficControlMessage>(message, sizeof(detail::NetlinkHeader))
            .expect("the message holds its traffic control part");
    if (traffic.interface_index != interface_index || traffic.parent != detail::root_parent) {
        return Answer{::fixy::none};
    }
    return answer_of(qdisc_of_attributes(message.window(attributes_offset, message.size() - attributes_offset)
                                             .expect("the attributes lie inside the message")));
}

// The characters after the first "qdisc " of text.  Complexity: linear in
// the length of the text.
[[nodiscard]] ::fixy::Result<::fixy::TextView, PacingError> text_after_qdisc_word(::fixy::TextView text) noexcept {
    constexpr ::fixy::TextView word = "qdisc ";
    for (std::size_t start = 0; start + word.size() <= text.size(); ++start) {
        if (text.window(start, word.size()).expect("the window lies inside the text") == word) {
            std::size_t const after = start + word.size();
            return text.window(after, text.size() - after).expect("the rest lies inside the text");
        }
    }
    return ::fixy::err(PacingError::QdiscKindMissing);
}

}  // namespace

::fixy::TextView qdisc_name(Qdisc qdisc) noexcept {
    switch (qdisc) {
        case Qdisc::Fq:
            return "fq";
        case Qdisc::FqCodel:
            return "fq_codel";
        case Qdisc::Pfifo:
            return "pfifo_fast";
        case Qdisc::Mq:
            return "mq";
        case Qdisc::Noqueue:
            return "noqueue";
        case Qdisc::Other:
            return "other";
        default:
            return "unknown";
    }
}

::fixy::Result<Qdisc, PacingError> qdisc_from_kernel_name(::fixy::TextView name) noexcept {
    if (name == "fq") {
        return Qdisc::Fq;
    }
    if (name == "fq_codel") {
        return Qdisc::FqCodel;
    }
    if (name == "pfifo_fast" || name == "pfifo") {
        return Qdisc::Pfifo;
    }
    if (name == "mq") {
        return Qdisc::Mq;
    }
    if (name == "noqueue") {
        return Qdisc::Noqueue;
    }
    if (name.size() == 0) {
        return ::fixy::err(PacingError::QdiscKindMissing);
    }
    return Qdisc::Other;
}

// The kind is the word after the first "qdisc " and its spaces.  A word
// longer than each kind of the kernel is no known qdisc.
::fixy::Result<Qdisc, PacingError> parse_tc_qdisc_show(::fixy::TextView text) noexcept {
    CRUCIBLE_TRY(after_word, text_after_qdisc_word(text));
    ::fixy::FixedText<qdisc_kind_bytes> kind{};
    bool is_long_kind = false;
    for (char const character : after_word) {
        if (character == ' ' && kind.size() == 0) {
            continue;
        }
        if (character == ' ' || character == '\n') {
            break;
        }
        is_long_kind = is_long_kind || kind.push(character).is_err();
    }
    if (is_long_kind) {
        return Qdisc::Other;
    }
    return qdisc_from_kernel_name(kind.view());
}

// The file holds the decimal index and a newline.  The largest index has
// ten digits.
::fixy::Result<std::int32_t, PacingError> detail::read_interface_index(NicInterfaceName iface) noexcept {
    InterfacePath const path = interface_path_of(iface, "ifindex");
    ::fixy::FixedText<16> text{};
    if (::foundation::os::read_file(path.c_text(), text).is_err()) {
        return ::fixy::err(PacingError::InterfaceNotFound);
    }
    ::fixy::TextView digits = text.view();
    if (::fixy::ends_with(digits, "\n")) {
        digits = digits.window(0, digits.size() - 1).expect("a text that ends with a newline holds it");
    }
    using Index = ::fixy::Result<std::int32_t, PacingError>;
    return ::fixy::parse<std::int32_t>(digits).match(
        [](std::int32_t index) -> Index {
            if (index <= 0) {
                return ::fixy::err(PacingError::InterfaceNotFound);
            }
            return index;
        },
        [](::fixy::ParseError) -> Index { return ::fixy::err(PacingError::InterfaceNotFound); });
}

// A reply of a different request can reach the socket, so the scan reads
// only the messages of its own sequence number.  It skips each other
// message, so a control message of the kernel does not end the dump.
// Complexity: linear in the count of the messages of the part.
::fixy::Option<::fixy::Result<Qdisc, PacingError>> detail::scan_qdisc_reply(::fixy::View<std::byte const> part,
                                                                            std::int32_t interface_index) noexcept {
    std::size_t offset = 0;
    while (part.size() - offset >= sizeof(NetlinkHeader)) {
        NetlinkHeader const header = read_record<NetlinkHeader>(part, offset).expect("the loop test fits the header");
        std::size_t const remaining = part.size() - offset;
        if (header.length < sizeof(NetlinkHeader) || header.length > remaining) {
            return answer_of(::fixy::err(PacingError::NetlinkReceiveFailed));
        }
        if (header.sequence == qdisc_dump_sequence) {
            if (header.type == netlink_done) {
                return answer_of(::fixy::err(PacingError::QdiscKindMissing));
            }
            if (header.type == netlink_error) {
                return answer_of(::fixy::err(PacingError::NetlinkReceiveFailed));
            }
            if (header.type == route_new_qdisc) {
                Answer root = root_qdisc_of(
                    part.window(offset, header.length).expect("the message lies inside the part"), interface_index);
                if (root.is_some()) {
                    return root;
                }
            }
        }
        std::size_t const step = align4(header.length);
        if (step > remaining) {
            return answer_of(::fixy::err(PacingError::NetlinkReceiveFailed));
        }
        offset += step;
    }
    return Answer{::fixy::none};
}

}  // namespace crucible::cntp
