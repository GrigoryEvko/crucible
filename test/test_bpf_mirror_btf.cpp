// The BTF of each embedded stub object agrees with the userspace mirror of
// each record that the object shares with userspace.
//
// A program of the dataplane writes a record of bpf/net_common.h or of its own
// source into a map, and userspace reads the same bytes through a mirror of
// cntp/dataplane/NetTimeline.h or cntp/dataplane/TcEbpf.h.  clang writes the
// layout of each record that a map or a program names into the section .BTF
// of the object (cmake/CrucibleBpf.cmake compiles with -g).  The test reads
// that section of each of the 27 stub objects, finds each struct that has a
// mirror, and compares the size of the struct, the count of its members, and
// the name, the byte offset and the byte size of each member with the mirror
// that reflection reads.  Each mirror must be in at least one object, so a
// rename of a C struct fails the test and does not skip it.
//
// The C structs name their padding _pad, and the mirrors name it pad, so a
// BTF name with a leading underscore compares without that underscore.  The
// test loads no program, so it needs no capability.

#include <crucible/cntp/dataplane/NetTimeline.h>
#include <crucible/cntp/dataplane/TcEbpf.h>
#include <crucible/perf/detail/BpfLoader.h>
#include <fixy/Core.h>

#include "test_assert.h"

#include <cstddef>
#include <cstdint>
#include <meta>

namespace dataplane = crucible::cntp::dataplane;
namespace perf = crucible::perf::detail;

namespace {

using Bytes = ::fixy::View<unsigned char const>;

// The kinds of a BTF type (include/uapi/linux/btf.h).
enum class BtfKind : std::uint8_t {
    integer = 1,
    pointer = 2,
    array = 3,
    structure = 4,
    union_kind = 5,
    enumeration = 6,
    forward = 7,
    type_alias = 8,
    volatile_kind = 9,
    const_kind = 10,
    restrict_kind = 11,
    function = 12,
    function_prototype = 13,
    variable = 14,
    data_section = 15,
    floating = 16,
    declaration_tag = 17,
    type_tag = 18,
    enumeration64 = 19,
};

// The bytes of a little-endian object, read at a byte offset.
class LittleEndian {
public:
    explicit constexpr LittleEndian(Bytes bytes) noexcept : bytes_{bytes} {}

    // The word of Width bytes at offset, or no value past the end.
    template <std::size_t Width>
    [[nodiscard]] ::fixy::Option<std::uint64_t> word_at(std::size_t offset) const noexcept {
        return bytes_.window<Width>(offset).map([](::fixy::View<unsigned char const, Width> word) noexcept {
            std::uint64_t value = 0;
            std::size_t shift = 0;
            for (unsigned char const byte : word) {
                value |= std::uint64_t{byte} << shift;
                shift += 8;
            }
            return value;
        });
    }

    // The word of Width bytes at offset.  The walk reads only inside the
    // parts that the headers bound, so a read past the end ends the test.
    template <std::size_t Width>
    [[nodiscard]] std::uint64_t word(std::size_t offset) const noexcept {
        return word_at<Width>(offset).expect("the walk reads inside the bytes of the object");
    }

    [[nodiscard]] constexpr Bytes bytes() const noexcept { return bytes_; }

    // True when the zero-terminated name at offset is name.  A name that
    // starts with an underscore compares without it (the note at the top).
    [[nodiscard]] bool is_named(std::size_t offset, ::fixy::TextView name) const noexcept {
        if (word<1>(offset) == '_') {
            ++offset;
        }
        std::size_t index = 0;
        for (char const character : name) {
            if (word<1>(offset + index) != static_cast<unsigned char>(character)) {
                return false;
            }
            ++index;
        }
        return word<1>(offset + index) == 0;
    }

private:
    Bytes bytes_;
};

// The bytes of the section .BTF of an ELF64 object of little-endian data, or
// no value when the object has no such section.  e_shoff is at byte 40,
// e_shentsize, e_shnum and e_shstrndx at bytes 58, 60 and 62.  sh_name of a
// section header is at its byte 0, sh_offset at byte 24 and sh_size at byte
// 32.  Complexity: linear in the sections of the object.
[[nodiscard]] ::fixy::Option<Bytes> btf_section_of(LittleEndian object) noexcept {
    std::uint64_t const headers = object.word<8>(40);
    std::uint64_t const header_size = object.word<2>(58);
    std::uint64_t const count = object.word<2>(60);
    std::uint64_t const names = object.word<8>(headers + header_size * object.word<2>(62) + 24);
    for (std::uint64_t section = 0; section < count; ++section) {
        std::uint64_t const header = headers + header_size * section;
        if (object.is_named(names + object.word<4>(header), ".BTF")) {
            return object.bytes().window(object.word<8>(header + 24), object.word<8>(header + 32));
        }
    }
    return ::fixy::none;
}

// The part of the section .BTF after its header that the words at bytes
// Field and Field + 4 of the header bound: the type section for 8 and the
// string section for 16.  The header holds the magic 0xEB9F at byte 0 and
// its own size at byte 4.
template <std::size_t Field>
[[nodiscard]] LittleEndian btf_part(Bytes section) noexcept {
    LittleEndian const header{section};
    assert(header.word<2>(0) == 0xEB9Fu);
    return LittleEndian{section.window(header.word<4>(4) + header.word<4>(Field), header.word<4>(Field + 4))
                            .expect("the part is inside the section .BTF")};
}

// The type section and the string section of the BTF of one object.
struct BtfParts {
    LittleEndian types;
    LittleEndian strings;
};

// One record of a struct: the words of its name, its type and its offset.
// With the kind flag of the struct, the low 24 bits of the offset hold the
// offset in bits, and the high 8 bits the size of a bit field.
struct BtfMember {
    BtfParts parts;
    std::size_t offset = 0;
    bool is_bit_field_layout = false;

    [[nodiscard]] bool is_named(::fixy::TextView name) const noexcept {
        return parts.strings.is_named(parts.types.word<4>(offset), name);
    }
    [[nodiscard]] std::uint64_t type() const noexcept { return parts.types.word<4>(offset + 4); }
    [[nodiscard]] std::uint64_t bit_offset() const noexcept {
        std::uint64_t const word = parts.types.word<4>(offset + 8);
        return is_bit_field_layout ? (word & 0xFFFFFFu) : word;
    }
};

// One type: the 12 bytes of struct btf_type at its offset, and the records
// after them.
struct BtfType {
    BtfParts parts;
    std::size_t offset = 0;

    [[nodiscard]] std::uint64_t info() const noexcept { return parts.types.word<4>(offset + 4); }
    [[nodiscard]] BtfKind kind() const noexcept { return static_cast<BtfKind>((info() >> 24) & 0x1Fu); }
    [[nodiscard]] std::size_t record_count() const noexcept { return info() & 0xFFFFu; }
    [[nodiscard]] std::uint64_t size_or_type() const noexcept { return parts.types.word<4>(offset + 8); }
    [[nodiscard]] bool is_named(::fixy::TextView name) const noexcept {
        return parts.strings.is_named(parts.types.word<4>(offset), name);
    }

    [[nodiscard]] BtfMember member(std::size_t index) const noexcept {
        return BtfMember{
            .parts = parts, .offset = offset + 12 + index * 12, .is_bit_field_layout = (info() >> 31) != 0};
    }

    // struct btf_array after the type: the element type at byte 12 and the
    // count at byte 20.
    [[nodiscard]] std::uint64_t array_element() const noexcept { return parts.types.word<4>(offset + 12); }
    [[nodiscard]] std::uint64_t array_count() const noexcept { return parts.types.word<4>(offset + 20); }

    // The bytes after the 12 bytes of the type.
    [[nodiscard]] std::size_t extra_bytes() const noexcept {
        switch (kind()) {
            case BtfKind::integer:
            case BtfKind::variable:
            case BtfKind::declaration_tag:
                return 4;
            case BtfKind::array:
                return 12;
            case BtfKind::structure:
            case BtfKind::union_kind:
            case BtfKind::data_section:
            case BtfKind::enumeration64:
                return 12 * record_count();
            case BtfKind::enumeration:
            case BtfKind::function_prototype:
                return 8 * record_count();
            default:
                return 0;
        }
    }
};

// The largest count of the types of one stub object that the walk holds.
inline constexpr std::size_t kMaxTypes = 512;

// The types of the BTF of one object, by id from 1.
class BtfTypes {
public:
    explicit BtfTypes(Bytes section) noexcept
        : parts_{.types = btf_part<8>(section), .strings = btf_part<16>(section)} {
        std::size_t offset = 0;
        while (offset < parts_.types.bytes().size()) {
            static_cast<void>(offsets_.append(offset).expect("a stub object holds at most kMaxTypes types"));
            offset += 12 + BtfType{.parts = parts_, .offset = offset}.extra_bytes();
        }
        assert(offset == parts_.types.bytes().size());
    }

    [[nodiscard]] std::size_t count() const noexcept { return offsets_.size(); }

    // The type of id, from 1.
    [[nodiscard]] BtfType type(std::uint64_t id) const noexcept {
        std::size_t const offset =
            offsets_.view()
                .window<1>(id - 1)
                .map([](::fixy::View<std::size_t const, 1> one) noexcept { return one.at(::fixy::index<0>()); })
                .expect("the id names a type of the walk");
        return BtfType{.parts = parts_, .offset = offset};
    }

    // The size in bytes of the type of id.  An alias, a qualifier and a tag
    // give the size of their type, and an array the size of its element times
    // its count.  Complexity: linear in the length of the chain of the type.
    [[nodiscard]] std::uint64_t size_of_type(std::uint64_t id) const noexcept {
        BtfType const found = type(id);
        switch (found.kind()) {
            case BtfKind::integer:
            case BtfKind::structure:
            case BtfKind::union_kind:
            case BtfKind::enumeration:
            case BtfKind::enumeration64:
            case BtfKind::floating:
                return found.size_or_type();
            case BtfKind::pointer:
                return 8;
            case BtfKind::array:
                return size_of_type(found.array_element()) * found.array_count();
            case BtfKind::type_alias:
            case BtfKind::volatile_kind:
            case BtfKind::const_kind:
            case BtfKind::restrict_kind:
            case BtfKind::type_tag:
                return size_of_type(found.size_or_type());
            default:
                return 0;
        }
    }

private:
    BtfParts parts_;
    ::fixy::FixedList<std::size_t, kMaxTypes> offsets_{};
};

// The data members of a mirror, as a constant array, so the walk reads no
// container of a query.  Each member of a mirror is public.
template <class Mirror>
inline constexpr std::meta::info kMembersOf = std::meta::reflect_constant_array(
    std::meta::nonstatic_data_members_of(^^Mirror, std::meta::access_context::unprivileged()));

// True when the struct has the layout of Mirror: its size, the count of its
// members, and the name, the byte offset and the byte size of each member in
// order.  Reports the first difference.
template <class Mirror>
[[nodiscard]] bool has_layout_of(BtfTypes const& types, BtfType found, ::fixy::TextView name) noexcept {
    if (found.size_or_type() != sizeof(Mirror)) {
        ::fixy::report(::fixy::Sink::Err, "  {}: the struct has {} bytes, and the mirror {}\n", name,
                       found.size_or_type(), sizeof(Mirror));
        return false;
    }
    std::size_t index = 0;
    bool is_same = true;
    template for (constexpr std::meta::info member : [:kMembersOf<Mirror>:]) {
        constexpr ::fixy::TextView member_name{[:std::meta::reflect_constant_string(
                                                     std::meta::identifier_of(member)):]};
        constexpr std::size_t member_offset = static_cast<std::size_t>(std::meta::offset_of(member).bytes);
        constexpr std::size_t member_size = std::meta::size_of(std::meta::type_of(member));
        bool const is_member_same = index < found.record_count() && found.member(index).is_named(member_name)
                                 && found.member(index).bit_offset() == member_offset * 8
                                 && types.size_of_type(found.member(index).type()) == member_size;
        if (is_same && !is_member_same) {
            ::fixy::report(::fixy::Sink::Err, "  {}: the member {} at byte {} of {} bytes differs\n", name, member_name,
                           member_offset, member_size);
            is_same = false;
        }
        ++index;
    }
    if (index != found.record_count()) {
        ::fixy::report(::fixy::Sink::Err, "  {}: the struct has {} members, and the mirror {}\n", name,
                       found.record_count(), index);
        return false;
    }
    return is_same;
}

// The count of the objects that hold the struct of each mirror.
struct MirrorCounts {
    std::size_t flow_key = 0;
    std::size_t flow_class = 0;
    std::size_t event = 0;
    std::size_t timeline_header = 0;
    std::size_t timeline = 0;
};

// The count of the objects that hold the struct of Mirror, after a compare of
// the struct of one object when the object holds it.  Complexity: linear in
// the types of the object.
template <class Mirror>
[[nodiscard]] std::size_t count_after_compare(BtfTypes const& types, ::fixy::TextView struct_name,
                                              std::size_t objects) noexcept {
    for (std::size_t id = 1; id <= types.count(); ++id) {
        BtfType const found = types.type(id);
        if (found.kind() == BtfKind::structure && found.is_named(struct_name)) {
            assert(has_layout_of<Mirror>(types, found, struct_name));
            return objects + 1;
        }
    }
    return objects;
}

// Compares each mirror in the BTF of the object of name.
void compare_object(::fixy::TextView name, perf::ForeignBytes bytes, MirrorCounts& counts) noexcept {
    LittleEndian const object{perf::view_bytes(bytes)};
    Bytes const section = btf_section_of(object).expect("each stub object holds a section .BTF");
    BtfTypes const types{section};
    counts.flow_key = count_after_compare<dataplane::NetFlowKey>(types, "crucible_net_flow_key", counts.flow_key);
    counts.flow_class =
        count_after_compare<dataplane::TcFlowClassRecord>(types, "crucible_tc_flow_class", counts.flow_class);
    counts.event = count_after_compare<dataplane::NetEvent>(types, "crucible_net_event", counts.event);
    counts.timeline_header =
        count_after_compare<dataplane::NetTimelineHeader>(types, "timeline_header", counts.timeline_header);
    counts.timeline = count_after_compare<dataplane::NetTimeline>(types, "crucible_net_timeline", counts.timeline);
    crucible::test::pass("  {}: {} types\n", name, types.count());
}

void test_each_stub_object_agrees_with_its_mirrors() {
    MirrorCounts counts{};
    compare_object("cntp_dp_tc_ingress_classify", perf::cntp_dp_tc_ingress_classify_bpf_object(), counts);
    compare_object("cntp_dp_tc_egress_classify", perf::cntp_dp_tc_egress_classify_bpf_object(), counts);
    compare_object("cntp_dp_pacing_budget", perf::cntp_dp_pacing_budget_bpf_object(), counts);
    compare_object("cntp_dp_queue_backlog", perf::cntp_dp_queue_backlog_bpf_object(), counts);
    compare_object("cntp_dp_fq_budget", perf::cntp_dp_fq_budget_bpf_object(), counts);
    compare_object("cntp_xdp_flow_filter", perf::cntp_xdp_flow_filter_bpf_object(), counts);
    compare_object("cntp_tc_egress_mark", perf::cntp_tc_egress_mark_bpf_object(), counts);
    compare_object("cntp_sk_msg_credit_gate", perf::cntp_sk_msg_credit_gate_bpf_object(), counts);
    compare_object("cntp_sock_ops_telemetry", perf::cntp_sock_ops_telemetry_bpf_object(), counts);
    compare_object("cntp_ecn_ce_mark_sample", perf::cntp_ecn_ce_mark_sample_bpf_object(), counts);
    compare_object("cntp_wip_fq_pacing_sample", perf::cntp_wip_fq_pacing_sample_bpf_object(), counts);
    compare_object("cntp_wip_bbr_model_sample", perf::cntp_wip_bbr_model_sample_bpf_object(), counts);
    compare_object("cntp_wip_ack_clock_sample", perf::cntp_wip_ack_clock_sample_bpf_object(), counts);
    compare_object("cntp_wip_loss_recovery_sample", perf::cntp_wip_loss_recovery_sample_bpf_object(), counts);
    compare_object("warden_socket_lifecycle", perf::warden_socket_lifecycle_bpf_object(), counts);
    compare_object("canopy_gossip_xdp_tx", perf::canopy_gossip_xdp_tx_bpf_object(), counts);
    compare_object("canopy_peer_liveness_probe", perf::canopy_peer_liveness_probe_bpf_object(), counts);
    compare_object("canopy_gossip_fanout_sample", perf::canopy_gossip_fanout_sample_bpf_object(), counts);
    compare_object("topology_xdp_rx_timestamp_probe", perf::topology_xdp_rx_timestamp_probe_bpf_object(), counts);
    compare_object("topology_napi_pressure_probe", perf::topology_napi_pressure_probe_bpf_object(), counts);
    compare_object("topology_qdisc_backlog_probe", perf::topology_qdisc_backlog_probe_bpf_object(), counts);
    compare_object("topology_lldp_rx_sample", perf::topology_lldp_rx_sample_bpf_object(), counts);
    compare_object("observe_tcp_flow_sample", perf::observe_tcp_flow_sample_bpf_object(), counts);
    compare_object("observe_packet_loss_sample", perf::observe_packet_loss_sample_bpf_object(), counts);
    compare_object("observe_ecn_sample", perf::observe_ecn_sample_bpf_object(), counts);
    compare_object("cog_nic_cog_probe", perf::cog_nic_cog_probe_bpf_object(), counts);
    compare_object("cog_offload_cap_probe", perf::cog_offload_cap_probe_bpf_object(), counts);
    assert(counts.flow_key >= 1);
    assert(counts.flow_class >= 1);
    assert(counts.event >= 1);
    assert(counts.timeline_header >= 1);
    assert(counts.timeline >= 1);
    crucible::test::pass("  test_each_stub_object_agrees_with_its_mirrors: {} {} {} {} {} objects\n", counts.flow_key,
                         counts.flow_class, counts.event, counts.timeline_header, counts.timeline);
}

}  // namespace

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_bpf_mirror_btf:\n");
    test_each_stub_object_agrees_with_its_mirrors();
    crucible::test::pass("test_bpf_mirror_btf: all PASSED\n");
    return 0;
}
