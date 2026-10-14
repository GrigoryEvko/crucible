#pragma once

// The io_uring ring and the zero-copy transfers.
//
// An atom pack says which engine, how many submission and completion
// entries, which setup flags, or which zero-copy primitive, and the pack
// is read twice: once for the IORING_SETUP_* word the syscall takes, and
// once for the effect row the calling context has to admit.
//
// Design notes:
//
//  1. The atoms fixy::atom::io::{engine, zerocopy, ring_flag,
//     sq_entries, cq_entries} carry the axis and the row.  The context
//     gate reads the row off the pack and does not name IO and Block by
//     hand.  A check in the check file of this header pins the derived
//     row to IO and Block.
//
//  2. Only engine::IoUring reaches a mint, and only zerocopy::Sendfile
//     and zerocopy::CopyFileRange reach a transfer.  The predicates that
//     refuse a tag are concepts over closed lists: EngineIsIoUring and
//     SimpleTransfer answer false for every tag that they do not name, so
//     a tag added to either namespace later is refused on the day it
//     appears rather than on the day somebody remembers to extend a gate.
//
//     A pack that names no ring-flag atom sets up a ring with no setup
//     bits, so no tag stands for the default ring.
//
//  3. IoUringRing's constructor is private and mint_io_uring_ring is its
//     sole friend.  A handle that owns a descriptor and three mappings
//     is a claim about what the kernel gave you, and nothing but the
//     setup call can make that claim truthfully.
//
//  4. The ring flags are a closed table, as the mapping tags are.  A
//     flag with no row is refused, so the mint never sets up a ring
//     without a flag that the caller asked for.
//
//  5. The zero-copy transfer is zerocopy_transfer, not a mint, because it
//     synthesizes nothing.  It takes two OwnedFd handles, not two ints, so
//     it moves bytes only between descriptors that the caller owns.  It
//     asks again after a short transfer or EINTR, and the sendfile form
//     takes no destination offset, which sendfile does not read.
//
//  6. The door file src/fixy/os/Io.cpp holds the setup of the ring, its
//     maps and its release, and the two transfers.  It issues them with
//     the raw primitive of foundation/os/Syscall.h, and it is the one
//     includer of <linux/io_uring.h>.  This header includes no system
//     header.  The setup of a ring and copy_file_range are feature calls
//     of foundation/os/KernelFloor.h: an older kernel gives enosys.

#include <fixy/Qtt.h>
#include <fixy/atoms/Os.h>
#include <fixy/os/AtomPack.h>
#include <fixy/os/Fd.h>
#include <foundation/Platform.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>
#include <foundation/os/Errno.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <meta>
#include <system_error>
#include <type_traits>
#include <utility>

namespace fixy::io {

class IoUringRing;

namespace detail {

// The IORING_SETUP_* bits of the ring flags, with the values of
// <linux/io_uring.h>.  The door file src/fixy/os/Io.cpp compares each value
// with that header.
inline constexpr std::uint32_t ring_setup_io_poll = 1U << 0;
inline constexpr std::uint32_t ring_setup_sq_poll = 1U << 1;
inline constexpr std::uint32_t ring_setup_coop_taskrun = 1U << 8;
inline constexpr std::uint32_t ring_setup_single_issuer = 1U << 12;
inline constexpr std::uint32_t ring_setup_defer_taskrun = 1U << 13;

// The transfers of the door file src/fixy/os/Io.cpp.  Each one moves at most
// LENGTH bytes, asks again after a short transfer or EINTR, and stops at the
// end of the source.  It gives the count that it moved, or the first other
// error of the kernel.

// sendfile from SOURCE_OFFSET of SOURCE to the file position of DESTINATION.
[[nodiscard]] ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
send_file_(int destination, int source, std::int64_t source_offset, std::size_t length) noexcept;

// copy_file_range from SOURCE_OFFSET of SOURCE to DESTINATION_OFFSET of
// DESTINATION.
[[nodiscard]] ::foundation::core::Result<std::size_t, ::foundation::os::Errno>
copy_file_range_(int source, std::int64_t source_offset, int destination, std::int64_t destination_offset,
                 std::size_t length) noexcept;

}  // namespace detail

// An atom pack engages exactly one engine.  Only IoUring reaches a mint
// here, so a tag added to fixy::io::engine later is refused rather than
// admitted.  A concept cannot be specialized, so no class of a caller can
// pass for io_uring.
template <typename E>
concept EngineIsIoUring = std::is_same_v<E, engine::IoUring>;

// Only the two primitives with a uniform fd-to-fd signature are
// reachable from a transfer here.  splice(2) needs a pipe as intermediary
// and send(MSG_ZEROCOPY) needs socket-message arguments, so neither has
// a shape this call can take.
inline constexpr std::meta::info simple_transfer_tags[] = {^^zerocopy::Sendfile, ^^zerocopy::CopyFileRange};

template <typename Z>
concept SimpleTransfer = ::fixy::atom_pack::names_tag(simple_transfer_tags, ^^Z);

// The IORING_SETUP_* bit of each ring flag.  These are bits of one setup
// word, so a pack may engage several flags and they fold together.  A
// tag reaches io_uring_setup only through a row of this table, and
// fixy/os/AtomPack.h says why a table is closed.  A map that answers
// zero for an unknown flag sets up a ring without the flag the caller
// asked for.
inline constexpr ::fixy::atom_pack::tag_row<std::uint32_t> ring_flag_table[] = {
    {^^ring_flag::IoPoll, detail::ring_setup_io_poll},
    {^^ring_flag::SqPoll, detail::ring_setup_sq_poll},
    {^^ring_flag::SingleIssuer, detail::ring_setup_single_issuer},
    {^^ring_flag::CoopTaskrun, detail::ring_setup_coop_taskrun},
    {^^ring_flag::DeferTaskrun, detail::ring_setup_defer_taskrun},
};

// A flag is known when the table has a row.  The walk at the foot of this
// header checks that each tag fixy::io::ring_flag declares has a row.
template <typename F>
concept MappedRingFlag = ::fixy::atom_pack::has_row(ring_flag_table, ^^F);

// The bit of a flag that has a row.  The lookup is a function and not a
// variable template, because a caller can specialize a variable template
// for one flag and give it a bit of its own.
[[nodiscard]] consteval std::uint32_t ring_flag_bits_of(std::meta::info flag_tag) noexcept {
    return ::fixy::atom_pack::value_for(ring_flag_table, flag_tag);
}

namespace detail {

namespace eff = ::foundation::effects;

template <typename A>
inline constexpr bool is_engine_atom_v = ::fixy::atom_pack::IsAtomOf<A, ^^::fixy::atom::io::engine>;
template <typename A>
inline constexpr bool is_zerocopy_atom_v = ::fixy::atom_pack::IsAtomOf<A, ^^::fixy::atom::io::zerocopy>;
template <typename A>
inline constexpr bool is_ring_flag_atom_v = ::fixy::atom_pack::IsAtomOf<A, ^^::fixy::atom::io::ring_flag>;
template <typename A>
inline constexpr bool is_sq_entries_atom_v = ::fixy::atom_pack::IsAtomOf<A, ^^::fixy::atom::io::sq_entries>;
template <typename A>
inline constexpr bool is_cq_entries_atom_v = ::fixy::atom_pack::IsAtomOf<A, ^^::fixy::atom::io::cq_entries>;

template <typename A>
struct extract_engine {
    using type = void;
};
template <typename E>
struct extract_engine<::fixy::atom::io::engine<E>> {
    using type = E;
};
template <typename A>
using extract_engine_t = typename extract_engine<A>::type;

template <typename A>
struct extract_zerocopy {
    using type = void;
};
template <typename Z>
struct extract_zerocopy<::fixy::atom::io::zerocopy<Z>> {
    using type = Z;
};
template <typename A>
using extract_zerocopy_t = typename extract_zerocopy<A>::type;

template <typename A>
struct extract_ring_flag {
    using type = void;
};
template <typename F>
struct extract_ring_flag<::fixy::atom::io::ring_flag<F>> {
    using type = F;
};
template <typename A>
using extract_ring_flag_t = typename extract_ring_flag<A>::type;

template <typename... Atoms>
inline constexpr bool has_sq_entries_atom_v = (is_sq_entries_atom_v<Atoms> || ...);
template <typename... Atoms>
inline constexpr bool has_cq_entries_atom_v = (is_cq_entries_atom_v<Atoms> || ...);

template <typename... Atoms>
struct engine_of {
    using type = void;
};
template <typename First, typename... Rest>
struct engine_of<First, Rest...> {
    using type =
        std::conditional_t<is_engine_atom_v<First>, extract_engine_t<First>, typename engine_of<Rest...>::type>;
};
template <typename... Atoms>
using engine_of_t = typename engine_of<Atoms...>::type;

template <typename... Atoms>
struct zerocopy_of {
    using type = void;
};
template <typename First, typename... Rest>
struct zerocopy_of<First, Rest...> {
    using type =
        std::conditional_t<is_zerocopy_atom_v<First>, extract_zerocopy_t<First>, typename zerocopy_of<Rest...>::type>;
};
template <typename... Atoms>
using zerocopy_of_t = typename zerocopy_of<Atoms...>::type;

// Exactly one entries atom of each kind is engaged, and the rest
// contribute zero, so the fold is a sum.
//
// A partial specialization rather than a ternary over is_*_atom_v: a
// ternary instantiates BOTH arms, so `A::value` would be looked up on
// an atom that has no such member and the read would be a hard error
// rather than a zero.
template <typename A>
struct atom_sq_entries : std::integral_constant<std::uint32_t, 0> {};
template <std::uint32_t N>
struct atom_sq_entries<::fixy::atom::io::sq_entries<N>> : std::integral_constant<std::uint32_t, N> {};
template <typename A>
inline constexpr std::uint32_t atom_sq_entries_v = atom_sq_entries<std::remove_cvref_t<A>>::value;

template <typename A>
struct atom_cq_entries : std::integral_constant<std::uint32_t, 0> {};
template <std::uint32_t N>
struct atom_cq_entries<::fixy::atom::io::cq_entries<N>> : std::integral_constant<std::uint32_t, N> {};
template <typename A>
inline constexpr std::uint32_t atom_cq_entries_v = atom_cq_entries<std::remove_cvref_t<A>>::value;

template <typename... Atoms>
inline constexpr std::uint32_t sq_entries_of_v = (atom_sq_entries_v<Atoms> + ... + 0u);

// Zero means "kernel default", which is twice the submission count.
template <typename... Atoms>
inline constexpr std::uint32_t cq_entries_of_v = (atom_cq_entries_v<Atoms> + ... + 0u);

[[nodiscard]] inline constexpr bool is_pow2_(std::uint32_t value) noexcept {
    return value > 0 && (value & (value - 1)) == 0;
}

template <typename... Atoms>
inline constexpr bool sq_entries_is_pow2_v =
    has_sq_entries_atom_v<Atoms...> && is_pow2_(sq_entries_of_v<Atoms...>) && sq_entries_of_v<Atoms...> <= 32768;

template <typename... Atoms>
inline constexpr bool cq_entries_is_pow2_or_default_v =
    !has_cq_entries_atom_v<Atoms...> || (is_pow2_(cq_entries_of_v<Atoms...>) && cq_entries_of_v<Atoms...> <= 65536);

template <typename A>
[[nodiscard]] consteval std::uint32_t atom_ring_flag_bits() noexcept {
    // A discarded statement, not a conditional expression: a conditional
    // instantiates the bits of the void tag a non-flag atom names.
    if constexpr (is_ring_flag_atom_v<A>) {
        return ::fixy::io::ring_flag_bits_of(^^extract_ring_flag_t<std::remove_cvref_t<A>>);
    } else {
        return 0u;
    }
}

template <typename A>
inline constexpr std::uint32_t atom_ring_flag_bits_v = atom_ring_flag_bits<A>();

template <typename... Atoms>
[[nodiscard]] consteval std::uint32_t fold_ring_flags() noexcept {
    std::uint32_t acc = 0;
    ((acc |= atom_ring_flag_bits_v<Atoms>), ...);
    return acc;
}

// An atom of another kind is not a ring-flag atom, so it answers true
// and the fold is a conjunction over the whole pack.
template <typename A>
inline constexpr bool ring_flag_atom_is_known_v =
    !is_ring_flag_atom_v<A> || ::fixy::io::MappedRingFlag<extract_ring_flag_t<std::remove_cvref_t<A>>>;

template <typename... Atoms>
inline constexpr bool all_ring_flags_known_v = (ring_flag_atom_is_known_v<Atoms> && ... && true);

}  // namespace detail

// A ring engages one engine, one submission count and at most one
// completion count.  A ring flag may appear once each, and two different
// flags fold together.  The repeat rule reads the pack, so a flag added
// to fixy::io::ring_flag later is covered with no list to extend.
template <typename Ctx, typename... Atoms>
concept CtxFitsIoUringMint = ::fixy::atom_pack::CtxAdmitsAtomRow<Ctx, Atoms...>
                          && ::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::io::engine, Atoms...>
                          && EngineIsIoUring<detail::engine_of_t<Atoms...>>
                          && ::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::io::sq_entries, Atoms...>
                          && detail::sq_entries_is_pow2_v<Atoms...>
                          && ::fixy::atom_pack::HasAtMostOneAtomOf<^^::fixy::atom::io::cq_entries, Atoms...>
                          && detail::cq_entries_is_pow2_or_default_v<Atoms...>
                          && !::fixy::atom_pack::RepeatsAnAtomOf<^^::fixy::atom::io::ring_flag, Atoms...>
                          && detail::all_ring_flags_known_v<Atoms...>;

template <typename Ctx, typename... Atoms>
concept CtxFitsZerocopyTransfer = ::fixy::atom_pack::CtxAdmitsAtomRow<Ctx, Atoms...>
                               && ::fixy::atom_pack::HasOneAtomOf<^^::fixy::atom::io::zerocopy, Atoms...>
                               && SimpleTransfer<detail::zerocopy_of_t<Atoms...>>;

// One gate for each form of the transfer below, because the two forms
// take different offsets.
template <typename Ctx, typename... Atoms>
concept CtxFitsSendfileTransfer =
    CtxFitsZerocopyTransfer<Ctx, Atoms...> && std::is_same_v<detail::zerocopy_of_t<Atoms...>, zerocopy::Sendfile>;

template <typename Ctx, typename... Atoms>
concept CtxFitsCopyRangeTransfer =
    CtxFitsZerocopyTransfer<Ctx, Atoms...> && std::is_same_v<detail::zerocopy_of_t<Atoms...>, zerocopy::CopyFileRange>;

namespace detail {

// The parts of a ring that the kernel gave: its descriptor, the address and
// the count of bytes of each of the three maps, and the two counts of
// entries.  A completion size of zero marks a completion ring that aliases
// the submission ring.
struct RingParts {
    int descriptor = -1;
    void* sq_ring = nullptr;
    std::size_t sq_size = 0;
    void* cq_ring = nullptr;
    std::size_t cq_size = 0;
    void* sqes = nullptr;
    std::size_t sqes_size = 0;
    std::uint32_t sq_entries = 0;
    std::uint32_t cq_entries = 0;
};

// The door of the setup, in src/fixy/os/Io.cpp.  It makes a ring with
// SQ_ENTRIES submission entries, CQ_ENTRIES completion entries or the
// default of the kernel for zero, and the IORING_SETUP_* bits FLAGS.  It maps
// the submission ring, the completion ring and the submission entries, and
// it gives only what the kernel gave.  On an error it releases each part that
// it made, and it gives the error.
[[nodiscard]] ::foundation::core::Result<RingParts, ::foundation::os::Errno>
set_up_ring_(std::uint32_t sq_entries, std::uint32_t cq_entries, std::uint32_t flags) noexcept;

}  // namespace detail

// Declared before the class so the class can name it as its sole friend.
// The definition follows the class, because it builds one.
template <typename... Atoms, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsIoUringMint<Ctx, Atoms...>
[[nodiscard]] std::expected<Linear<IoUringRing>, std::error_code> mint_io_uring_ring(Ctx const&) noexcept;

// One descriptor and three mappings, released in the reverse of the
// order the kernel handed them over.
//
// The constructor is private.  With a public one, a caller who never
// called io_uring_setup can hand it any nine numbers, and the destructor
// closes a descriptor and unmaps three addresses that the caller chose.
// Only the setup call knows what the
// kernel actually gave out, so only the setup call can build one.
class [[nodiscard]] IoUringRing {
    // The empty parts are the empty handle.  A completion size of zero marks
    // a completion ring that aliases the submission ring (SINGLE_MMAP).
    detail::RingParts parts_{};

    explicit IoUringRing(detail::RingParts const& parts) noexcept : parts_{parts} {}

    // The trailing return type is not a style choice: written the other
    // way the declaration ends `..., std::error_code> ::fixy::io::...`
    // and the parser takes the `>::` as a nested-name-specifier inside
    // the template-id.  The same shape is documented at
    // fixy/os/CpuPinned.h, where a friend crosses a header boundary.
    template <typename... FriendAtoms, ::foundation::effects::IsExecCtx FriendCtx>
        requires CtxFitsIoUringMint<FriendCtx, FriendAtoms...>
    friend auto mint_io_uring_ring(FriendCtx const&) noexcept -> std::expected<Linear<IoUringRing>, std::error_code>;

public:
    // Default construction is the empty handle: it owns nothing, closes
    // nothing, and unmaps nothing.  It claims no kernel resource, so it
    // is not a forgery.
    IoUringRing() noexcept = default;

    IoUringRing(const IoUringRing&) = delete("io_uring fd + mmaps are unique; copy would double-close/unmap");
    IoUringRing&
    operator=(const IoUringRing&) = delete("io_uring fd + mmaps are unique; copy would double-close/unmap");

    // A move takes the parts of OTHER and leaves OTHER the empty handle.
    IoUringRing(IoUringRing&& other) noexcept : parts_{other.parts_} { other.parts_ = detail::RingParts{}; }

    IoUringRing& operator=(IoUringRing&& other) noexcept {
        if (this != &other) {
            release_();
            parts_ = other.parts_;
            other.parts_ = detail::RingParts{};
        }
        return *this;
    }

    ~IoUringRing() noexcept { release_(); }

    [[nodiscard]] int ring_fd() const noexcept { return parts_.descriptor; }
    [[nodiscard]] void* sq_ring() const noexcept { return parts_.sq_ring; }
    [[nodiscard]] std::size_t sq_ring_size() const noexcept { return parts_.sq_size; }
    [[nodiscard]] void* cq_ring() const noexcept { return parts_.cq_ring; }
    [[nodiscard]] std::size_t cq_ring_size() const noexcept { return parts_.cq_size; }
    [[nodiscard]] void* sqes() const noexcept { return parts_.sqes; }
    [[nodiscard]] std::size_t sqes_size() const noexcept { return parts_.sqes_size; }
    [[nodiscard]] std::uint32_t sq_entries() const noexcept { return parts_.sq_entries; }
    [[nodiscard]] std::uint32_t cq_entries() const noexcept { return parts_.cq_entries; }
    [[nodiscard]] bool is_valid() const noexcept { return parts_.descriptor >= 0; }

private:
    // Unmaps the three parts of the ring and closes its descriptor, in the
    // reverse of the order of the setup, in src/fixy/os/Io.cpp.  A
    // completion size of zero marks a completion ring that aliases the
    // submission ring, and the release unmaps it one time.
    void release_() noexcept;
};

template <typename... Atoms, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsIoUringMint<Ctx, Atoms...>
[[nodiscard]] std::expected<Linear<IoUringRing>, std::error_code> mint_io_uring_ring(Ctx const&) noexcept {
    constexpr std::uint32_t sq_n = detail::sq_entries_of_v<Atoms...>;
    constexpr std::uint32_t cq_n = detail::cq_entries_of_v<Atoms...>;
    constexpr std::uint32_t flags = detail::fold_ring_flags<Atoms...>();

    auto parts = detail::set_up_ring_(sq_n, cq_n, flags);
    if (parts.is_err()) {
        return std::unexpected{std::error_code{::fixy::fs::detail::error_number_of_(parts), std::system_category()}};
    }
    return mint_linear<IoUringRing>(IoUringRing{std::move(parts).expect("the setup door gave the parts of the ring")});
}

// A transfer moves the bytes of one descriptor to another inside the
// kernel.  It is not a mint: it acts on two descriptors that the caller
// owns, and it synthesizes nothing.
//
// One call can move fewer bytes than it asked for, and a signal can stop
// it before it moves any.  Each form asks again for the rest until the
// length is moved, the source ends, or an error other than EINTR occurs.
// It returns the count that it moved, and a count short of the length
// means that the source ended.  Another error returns its code and not
// the count, so after an error the destination can hold a part of the
// bytes.
namespace detail {

// The check and the result that the two forms share.  step makes the
// transfer through the door, and gives the count that it moved or the error
// of the kernel.
template <typename Step>
[[nodiscard]] std::expected<std::size_t, std::error_code>
transfer_all(::fixy::fs::OwnedFd const& source, ::fixy::fs::OwnedFd const& destination, Step step) noexcept {
    if (!source.is_open() || !destination.is_open()) {
        return std::unexpected{std::error_code{::fixy::fs::detail::closed_handle_error, std::system_category()}};
    }
    auto moved = step();
    if (moved.is_err()) {
        return std::unexpected{std::error_code{::fixy::fs::detail::error_number_of_(moved), std::system_category()}};
    }
    return std::move(moved).expect("the transfer door gave the count that it moved");
}

}  // namespace detail

// sendfile writes at the file position of the destination and moves it,
// so this form takes no destination offset.  It reads the source from
// source_offset and does not move the file position of the source.
template <typename... Atoms, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSendfileTransfer<Ctx, Atoms...>
[[nodiscard]] inline std::expected<std::size_t, std::error_code>
zerocopy_transfer(Ctx const&, ::fixy::fs::OwnedFd const& source, ::fixy::fs::OwnedFd const& destination,
                  std::size_t length, ::off_t source_offset = 0) noexcept {
    return detail::transfer_all(source, destination, [&] {
        return detail::send_file_(destination.get(), source.get(), source_offset, length);
    });
}

// copy_file_range reads from source_offset and writes at
// destination_offset, and it moves neither file position.
template <typename... Atoms, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsCopyRangeTransfer<Ctx, Atoms...>
[[nodiscard]] inline std::expected<std::size_t, std::error_code>
zerocopy_transfer(Ctx const&, ::fixy::fs::OwnedFd const& source, ::fixy::fs::OwnedFd const& destination,
                  std::size_t length, ::off_t source_offset = 0, ::off_t destination_offset = 0) noexcept {
    return detail::transfer_all(source, destination, [&] {
        return detail::copy_file_range_(source.get(), source_offset, destination.get(), destination_offset, length);
    });
}

}  // namespace fixy::io

namespace fixy::io::detail::io_surface_invariants {

// These two checks stay in this header, and the other checks are in its
// check file.  Each one calls the lookup of a closed list or a closed
// table in each translation unit that includes this header.
// fixy/os/AtomPack.h says why that call must come before the code of the
// includer.
static_assert(SimpleTransfer<zerocopy::Sendfile>);

// Every tag fixy::io::ring_flag declares has an IORING_SETUP_* row in the
// table.  The known-flag concept reads the table, so this walk is the
// check that no declared tag is missing from it.
static_assert(::fixy::atom_pack::every_tag_in_satisfies<^^::fixy::io::ring_flag,
                                                        [](std::meta::info flag_tag) consteval {
                                                          return ::fixy::atom_pack::has_row(ring_flag_table, flag_tag);
                                                        }>(),
              "fixy/os/Io.h: a tag declared in fixy::io::ring_flag has no row in ring_flag_table, so the gate "
              "refuses every ring that names it.");

}  // namespace fixy::io::detail::io_surface_invariants
