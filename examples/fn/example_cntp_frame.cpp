// example_cntp_frame: a wire frame from an untrusted source, and its
// retag after validation.
//
// CNTP (Crucible Network Transport Protocol) frames arrive over the wire
// as raw bytes.  Before the runtime can act on a frame, the validator of
// the Vessel must:
//
//   1. Read the bytes as a packed header.
//   2. Validate the magic, the version, the length and the checksum.
//   3. Retag the frame: the source External becomes Sanitized, and the
//      trust Unverified becomes Tested.
//
// Until step 3 succeeds, the frame must not reach an API that takes only
// sanitized input.  The binding carries the trust state of the frame in
// its type.  So a function that asks for from_source<Sanitized> refuses
// an unvalidated frame at the call site, and not at run time.  The two
// states are two types, and validate() below is the one function that
// mints the second from the first.
//
// The contrast with the other examples: they bind callables, which are
// function pointers.  This example binds a data struct.  fixy::fn is the
// same template, and the atoms state the facts of a data struct rather
// than those of a callable.

#include <fixy/Atom.h>
#include <fixy/Axis.h>
#include <fixy/Ctx.h>
#include <fixy/Fn.h>
#include <fixy/Tags.h>
#include <foundation/diag/RowHash.h>

#include <cstdint>
#include <cstdio>
#include <expected>
#include <type_traits>

// The region of the network buffer that holds the frame.  in_region names
// the region by a value, and the name of an atom is its key in the row
// hash.  So the tag needs a name that is the same in each translation
// unit, and it has a named namespace, not the anonymous namespace below.
namespace example_cntp_frame {
struct NetworkBufferTag final {};
}  // namespace example_cntp_frame

namespace {

namespace at = ::fixy::atom;
namespace source = ::fixy::tags::source;
using ::fixy::Axis;
using ::fixy::axis_traits;

// A stand-in for the CNTP packet header.  A real frame also carries a
// sequence number, the identity of the peer and session keys.  The
// example uses the smallest shape that shows the binding: a packed POD
// with a checksum.
//
// The field order does not align: a u8 sits between the u32 magic and
// the u16 version.  Without [[gnu::packed]] the natural layout puts 3
// bytes of padding after flags and 4 after length, so that checksum sits
// on an 8-byte boundary, and the struct grows from 17 bytes to 24.  The
// static_assert holds the wire format to no padding, and the atom
// repr<Packed> states the same layout at the type level.
struct [[gnu::packed]] CntpHeader {
    std::uint32_t magic;  // 'CNTP' = 0x434E5450
    std::uint8_t flags;  // bit0 = ack, bit1 = fragment
    std::uint16_t version;  // the version of the wire format
    std::uint16_t length;  // the length of the frame, with the header
    std::uint64_t checksum;  // FNV-1a over magic thru length, without the checksum itself
};

static_assert(sizeof(CntpHeader) == 17, "CntpHeader must be exactly 17 bytes.  The packed layout is what repr<Packed> "
                                        "states.  Without packing, natural alignment adds 7 bytes of padding (3 after "
                                        "flags, 4 after length) and the struct grows to 24 bytes.");

inline constexpr std::uint32_t cntp_magic = 0x434E5450;
inline constexpr std::uint16_t cntp_version = 1;

// The pack of a CNTP frame, named one time for both trust states.  Source
// and Trust are the two axes that the retag changes.
//
// The Usage is borrow.  The frame is a view over storage that the
// network buffer owns, and in_region names that buffer.  A borrow cannot
// cross into a background context, because the buffer can die first:
// rule L007 of fixy/Collision.h refuses a borrow with a Bg row.
//
// The axes that the pack does not name take their strict poles:
//
//   - No session protocol, because the handshake is not complete.
//   - No in-place mutation, because the frame is read-only after the
//     receive.
//   - No self-call, because one owner holds each frame.
//   - Trap on overflow.
//   - No stale read.
//
// The wire format has no floating point, so the pack names no precision.
template <class Header, class Source, class Trust>
using CntpFrame = ::fixy::fn<Header,
                             at::borrow,  // a view over the storage of the network buffer
                             at::as_public,  // the header is visible on the wire
                             at::in_region<example_cntp_frame::NetworkBufferTag{}>,  // the buffer owns the storage
                             at::from_source<Source>,  // the retag changes this axis
                             Trust,  // and this one
                             at::repr<::fixy::pole::ReprKind::Packed>,  // no padding, as [[gnu::packed]] says
                             at::cost_constant,  // a header of fixed size
                             at::space_bounded<sizeof(Header)>,  // exactly the bytes of the header
                             at::sized_at<sizeof(Header)>,  // a fixed observation depth
                             at::version<cntp_version>>;  // the version of the wire protocol

// The frame as it comes from the wire.  trust_unverified names the strict
// pole of Trust, and the two spellings take one cache slot.
template <class Header>
using UnvalidatedFrame = CntpFrame<Header, source::External, at::trust_unverified>;

// The frame after validation.  In production a refinement predicate also
// gates this state, with at::refined_with<ValidCntpFrame>, and its check
// runs the checksum and the structural test at construction.  The example
// leaves the predicate out, so that the contrast stays on two axes.
template <class Header>
using ValidatedFrame = CntpFrame<Header, source::Sanitized, at::trust_tested>;

// The two states are different types.  A function that asks for a
// validated frame cannot take an unvalidated one, and the type system
// checks that at the call site.
static_assert(!std::is_same_v<UnvalidatedFrame<CntpHeader>, ValidatedFrame<CntpHeader>>,
              "the validated and the unvalidated frame must be different types, or the retag is not enforceable");
static_assert(!std::is_convertible_v<UnvalidatedFrame<CntpHeader>, ValidatedFrame<CntpHeader>>);

// The atoms are empty types, so each binding is the 17-byte header.
static_assert(sizeof(UnvalidatedFrame<CntpHeader>) == sizeof(CntpHeader));
static_assert(sizeof(ValidatedFrame<CntpHeader>) == sizeof(CntpHeader));

// The Provenance axis tells the two states apart.
static_assert(
    std::is_same_v<UnvalidatedFrame<CntpHeader>::grade_on<Axis::Provenance>, at::from_source<source::External>>);
static_assert(
    std::is_same_v<ValidatedFrame<CntpHeader>::grade_on<Axis::Provenance>, at::from_source<source::Sanitized>>);

// So does the Trust axis.
static_assert(std::is_same_v<UnvalidatedFrame<CntpHeader>::grade_on<Axis::Trust>, at::trust_unverified>);
static_assert(std::is_same_v<ValidatedFrame<CntpHeader>::grade_on<Axis::Trust>, at::trust_tested>);

// The Representation axis agrees with [[gnu::packed]].
static_assert(std::is_same_v<UnvalidatedFrame<CntpHeader>::grade_on<Axis::Representation>,
                             at::repr<::fixy::pole::ReprKind::Packed>>);

// The Mutation axis takes the strict pole: no in-place mutation.
static_assert(
    std::is_same_v<UnvalidatedFrame<CntpHeader>::grade_on<Axis::Mutation>, axis_traits<Axis::Mutation>::strict>);

// The two states take two federation cache slots.
static_assert(::foundation::diag::row_hash_contribution_v<UnvalidatedFrame<CntpHeader>>
              != ::foundation::diag::row_hash_contribution_v<ValidatedFrame<CntpHeader>>);

// A frame is pure data, so each context admits it, the foreground
// context too.  The same borrow with a Bg row is refused.
static_assert(::fixy::CtxAdmitsBinding<::fixy::HotFgCtx, UnvalidatedFrame<CntpHeader>>);
static_assert(!::fixy::IsAccepted<CntpHeader, at::borrow, at::with_bg, at::as_public>,
              "rule L007: a borrow cannot cross into a background context");

// Why a frame fails validation.
enum class CntpError : std::uint8_t {
    BadMagic = 1,
    BadVersion = 2,
    BadLength = 3,
};

// The retag.  It mints a validated frame only after each check passes.
// A production validator also checks the FNV-1a checksum.
[[nodiscard]] std::expected<ValidatedFrame<CntpHeader>, CntpError>
validate(const UnvalidatedFrame<CntpHeader>& raw) noexcept {
    // The checks read one copy of the header.
    const CntpHeader header = raw.value();
    if (header.magic != cntp_magic) return std::unexpected(CntpError::BadMagic);
    if (header.version != cntp_version) return std::unexpected(CntpError::BadVersion);
    if (header.length < sizeof(CntpHeader)) return std::unexpected(CntpError::BadLength);
    return ::fixy::mint_fn_for<ValidatedFrame>(header);
}

}  // namespace

int main() {
    // A frame from the wire.  In production the bytes come from recv() or
    // from a ring buffer mapped with mmap().
    CntpHeader on_wire{};
    on_wire.magic = cntp_magic;
    on_wire.flags = 0;
    on_wire.version = cntp_version;
    on_wire.length = sizeof(CntpHeader);
    on_wire.checksum = 0;  // a real frame computes FNV-1a here

    const auto untrusted = ::fixy::mint_fn_for<UnvalidatedFrame>(on_wire);

    // A read of a packed field makes a local copy.  The address of a
    // member that is not aligned trips -Werror=address-of-packed-member.
    {
        const auto magic = untrusted.value().magic;
        const auto version = untrusted.value().version;
        const auto length = untrusted.value().length;
        std::printf("untrusted frame: magic=0x%08X version=%u length=%u (sizeof=%zu)\n", magic, version, length,
                    sizeof(CntpHeader));
    }

    const auto trusted = validate(untrusted);
    if (!trusted.has_value()) return 1;
    std::printf("validated frame: source retagged External->Sanitized, trust retagged Unverified->Tested\n");
    std::printf("ValidatedFrame sizeof = %zu (== sizeof(CntpHeader) %zu)\n", sizeof(ValidatedFrame<CntpHeader>),
                sizeof(CntpHeader));

    // A frame with a bad magic stays untrusted: validate() mints nothing.
    CntpHeader forged = on_wire;
    forged.magic = 0;
    const auto refused = validate(::fixy::mint_fn_for<UnvalidatedFrame>(forged));
    if (refused.has_value() || refused.error() != CntpError::BadMagic) return 2;
    std::printf("forged frame: refused with BadMagic\n");

    return 0;
}
