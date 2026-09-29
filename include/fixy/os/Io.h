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
//     hand.  A check at the foot of this header pins the derived row to
//     IO and Block.
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

#include <fixy/Qtt.h>
#include <fixy/atoms/Os.h>
#include <fixy/os/AtomPack.h>
#include <fixy/os/Fs.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <meta>
#include <system_error>
#include <type_traits>
#include <utility>

// A kernel header older than the flag does not declare it, so each value
// is spelled here as well.  COOP_TASKRUN arrives in Linux 5.19,
// SINGLE_ISSUER in 6.0, DEFER_TASKRUN in 6.1.

#ifndef IORING_SETUP_COOP_TASKRUN
#define IORING_SETUP_COOP_TASKRUN (1U << 8)
#endif
#ifndef IORING_SETUP_SINGLE_ISSUER
#define IORING_SETUP_SINGLE_ISSUER (1U << 12)
#endif
#ifndef IORING_SETUP_DEFER_TASKRUN
#define IORING_SETUP_DEFER_TASKRUN (1U << 13)
#endif
#ifndef IORING_FEAT_SINGLE_MMAP
#define IORING_FEAT_SINGLE_MMAP (1U << 0)
#endif

namespace fixy::io {

class IoUringRing;

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
    {^^ring_flag::IoPoll, IORING_SETUP_IOPOLL},
    {^^ring_flag::SqPoll, IORING_SETUP_SQPOLL},
    {^^ring_flag::SingleIssuer, IORING_SETUP_SINGLE_ISSUER},
    {^^ring_flag::CoopTaskrun, IORING_SETUP_COOP_TASKRUN},
    {^^ring_flag::DeferTaskrun, IORING_SETUP_DEFER_TASKRUN},
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
concept CtxFitsCopyRangeTransfer = CtxFitsZerocopyTransfer<Ctx, Atoms...>
                                && std::is_same_v<detail::zerocopy_of_t<Atoms...>, zerocopy::CopyFileRange>;

// Declared before the class so the class can name it as its sole friend.
// The definition follows the class, because it builds one.
//
// §XXI carve-out: cx=alloc — setting up a ring is a kernel side effect.
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
    int ring_fd_ = -1;
    void* sq_ring_ = MAP_FAILED;
    std::size_t sq_size_ = 0;
    void* cq_ring_ = MAP_FAILED;
    std::size_t cq_size_ = 0;  // 0 marker = aliases sq_ring_ (SINGLE_MMAP)
    void* sqes_ = MAP_FAILED;
    std::size_t sqes_size_ = 0;
    std::uint32_t sq_entries_n_ = 0;
    std::uint32_t cq_entries_n_ = 0;

    IoUringRing(int fd, void* sq_ring, std::size_t sq_size, void* cq_ring, std::size_t cq_size, void* sqes,
                std::size_t sqes_size, std::uint32_t sq_entries_n, std::uint32_t cq_entries_n) noexcept
        : ring_fd_{fd},
          sq_ring_{sq_ring},
          sq_size_{sq_size},
          cq_ring_{cq_ring},
          cq_size_{cq_size},
          sqes_{sqes},
          sqes_size_{sqes_size},
          sq_entries_n_{sq_entries_n},
          cq_entries_n_{cq_entries_n} {}

    // The trailing return type is not a style choice: written the other
    // way the declaration ends `..., std::error_code> ::fixy::io::...`
    // and the parser takes the `>::` as a nested-name-specifier inside
    // the template-id.  The same shape is documented at
    // fixy/os/CpuPinned.h, where a friend crosses a header boundary.
    template <typename... FriendAtoms, ::foundation::effects::IsExecCtx FriendCtx>
        requires CtxFitsIoUringMint<FriendCtx, FriendAtoms...>
    friend auto mint_io_uring_ring(FriendCtx const&) noexcept
        -> std::expected<Linear<IoUringRing>, std::error_code>;

public:
    // Default construction is the empty handle: it owns nothing, closes
    // nothing, and unmaps nothing.  It claims no kernel resource, so it
    // is not a forgery.
    IoUringRing() noexcept = default;

    IoUringRing(const IoUringRing&) = delete("io_uring fd + mmaps are unique; copy would double-close/unmap");
    IoUringRing&
    operator=(const IoUringRing&) = delete("io_uring fd + mmaps are unique; copy would double-close/unmap");

    IoUringRing(IoUringRing&& other) noexcept
        : ring_fd_{std::exchange(other.ring_fd_, -1)},
          sq_ring_{std::exchange(other.sq_ring_, MAP_FAILED)},
          sq_size_{std::exchange(other.sq_size_, 0)},
          cq_ring_{std::exchange(other.cq_ring_, MAP_FAILED)},
          cq_size_{std::exchange(other.cq_size_, 0)},
          sqes_{std::exchange(other.sqes_, MAP_FAILED)},
          sqes_size_{std::exchange(other.sqes_size_, 0)},
          sq_entries_n_{std::exchange(other.sq_entries_n_, 0)},
          cq_entries_n_{std::exchange(other.cq_entries_n_, 0)} {}

    IoUringRing& operator=(IoUringRing&& other) noexcept {
        if (this != &other) {
            release_();
            ring_fd_ = std::exchange(other.ring_fd_, -1);
            sq_ring_ = std::exchange(other.sq_ring_, MAP_FAILED);
            sq_size_ = std::exchange(other.sq_size_, 0);
            cq_ring_ = std::exchange(other.cq_ring_, MAP_FAILED);
            cq_size_ = std::exchange(other.cq_size_, 0);
            sqes_ = std::exchange(other.sqes_, MAP_FAILED);
            sqes_size_ = std::exchange(other.sqes_size_, 0);
            sq_entries_n_ = std::exchange(other.sq_entries_n_, 0);
            cq_entries_n_ = std::exchange(other.cq_entries_n_, 0);
        }
        return *this;
    }

    ~IoUringRing() noexcept { release_(); }

    [[nodiscard]] int ring_fd() const noexcept { return ring_fd_; }
    [[nodiscard]] void* sq_ring() const noexcept { return sq_ring_; }
    [[nodiscard]] std::size_t sq_ring_size() const noexcept { return sq_size_; }
    [[nodiscard]] void* cq_ring() const noexcept { return cq_ring_; }
    [[nodiscard]] std::size_t cq_ring_size() const noexcept { return cq_size_; }
    [[nodiscard]] void* sqes() const noexcept { return sqes_; }
    [[nodiscard]] std::size_t sqes_size() const noexcept { return sqes_size_; }
    [[nodiscard]] std::uint32_t sq_entries() const noexcept { return sq_entries_n_; }
    [[nodiscard]] std::uint32_t cq_entries() const noexcept { return cq_entries_n_; }
    [[nodiscard]] bool is_valid() const noexcept { return ring_fd_ >= 0; }

private:
    void release_() noexcept {
        if (sqes_ != MAP_FAILED && sqes_ != nullptr) {
            ::munmap(sqes_, sqes_size_);  // SYSCALL-CAP-OK: IoUringRing destructor, releases what the mint acquired
            sqes_ = MAP_FAILED;
            sqes_size_ = 0;
        }
        // cq_size_ == 0 marks IORING_FEAT_SINGLE_MMAP (cq aliases sq).
        if (cq_size_ != 0 && cq_ring_ != MAP_FAILED && cq_ring_ != nullptr) {
            ::munmap(cq_ring_, cq_size_);  // SYSCALL-CAP-OK: IoUringRing destructor, releases what the mint acquired
        }
        cq_ring_ = MAP_FAILED;
        cq_size_ = 0;
        if (sq_ring_ != MAP_FAILED && sq_ring_ != nullptr) {
            ::munmap(sq_ring_, sq_size_);  // SYSCALL-CAP-OK: IoUringRing destructor, releases what the mint acquired
            sq_ring_ = MAP_FAILED;
            sq_size_ = 0;
        }
        if (ring_fd_ >= 0) {
            ::close(ring_fd_);  // SYSCALL-CAP-OK: IoUringRing destructor, releases what the mint acquired
            ring_fd_ = -1;
        }
        sq_entries_n_ = 0;
        cq_entries_n_ = 0;
    }
};

template <typename... Atoms, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsIoUringMint<Ctx, Atoms...>
[[nodiscard]] std::expected<Linear<IoUringRing>, std::error_code> mint_io_uring_ring(Ctx const&) noexcept {
    constexpr std::uint32_t sq_n = detail::sq_entries_of_v<Atoms...>;
    constexpr std::uint32_t cq_n = detail::cq_entries_of_v<Atoms...>;
    constexpr std::uint32_t flags = detail::fold_ring_flags<Atoms...>();

    ::io_uring_params params{};
    params.flags = flags;
    if constexpr (cq_n != 0) {
        params.cq_entries = cq_n;
        params.flags |= IORING_SETUP_CQSIZE;
    }

    const long setup =
        ::syscall(__NR_io_uring_setup,  // SYSCALL-CAP-OK: mint_io_uring_ring body (CtxFitsIoUringMint, IO+Block)
                  static_cast<unsigned int>(sq_n), &params);
    if (setup < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    const int fd = static_cast<int>(setup);

    const std::size_t sq_ring_size = static_cast<std::size_t>(params.sq_off.array)
                                   + static_cast<std::size_t>(params.sq_entries) * sizeof(std::uint32_t);

    const std::size_t cq_ring_size = static_cast<std::size_t>(params.cq_off.cqes)
                                   + static_cast<std::size_t>(params.cq_entries) * sizeof(::io_uring_cqe);

    const std::size_t sqes_array_size = static_cast<std::size_t>(params.sq_entries) * sizeof(::io_uring_sqe);

    void* const sq_ring = ::mmap(  // SYSCALL-CAP-OK: mint_io_uring_ring body (CtxFitsIoUringMint, IO+Block)
        nullptr, sq_ring_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
    if (sq_ring == MAP_FAILED) {
        const int failure = errno;
        ::close(fd);  // SYSCALL-CAP-OK: mint_io_uring_ring body (CtxFitsIoUringMint, IO+Block)
        return std::unexpected{std::error_code{failure, std::system_category()}};
    }

    void* cq_ring = MAP_FAILED;
    std::size_t cq_size_for_dtor = 0;
    if ((params.features & IORING_FEAT_SINGLE_MMAP) != 0) {
        // Linux 5.4 and later hand back one allocation that both rings
        // live in, and aliasing them is the documented contract.  A zero
        // size is what tells the destructor not to unmap the second one.
        cq_ring = sq_ring;
    } else {
        cq_ring = ::mmap(  // SYSCALL-CAP-OK: mint_io_uring_ring body (CtxFitsIoUringMint, IO+Block)
            nullptr, cq_ring_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
        if (cq_ring == MAP_FAILED) {
            const int failure = errno;
            ::munmap(sq_ring, sq_ring_size);  // SYSCALL-CAP-OK: mint_io_uring_ring body (CtxFitsIoUringMint, IO+Block)
            ::close(fd);  // SYSCALL-CAP-OK: mint_io_uring_ring body (CtxFitsIoUringMint, IO+Block)
            return std::unexpected{std::error_code{failure, std::system_category()}};
        }
        cq_size_for_dtor = cq_ring_size;
    }

    void* const sqes = ::mmap(  // SYSCALL-CAP-OK: mint_io_uring_ring body (CtxFitsIoUringMint, IO+Block)
        nullptr, sqes_array_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
    if (sqes == MAP_FAILED) {
        const int failure = errno;
        if (cq_size_for_dtor != 0) {
            ::munmap(cq_ring, cq_ring_size);  // SYSCALL-CAP-OK: mint_io_uring_ring body (CtxFitsIoUringMint, IO+Block)
        }
        ::munmap(sq_ring, sq_ring_size);  // SYSCALL-CAP-OK: mint_io_uring_ring body (CtxFitsIoUringMint, IO+Block)
        ::close(fd);  // SYSCALL-CAP-OK: mint_io_uring_ring body (CtxFitsIoUringMint, IO+Block)
        return std::unexpected{std::error_code{failure, std::system_category()}};
    }

    return mint_linear<IoUringRing>(IoUringRing{fd, sq_ring, sq_ring_size, cq_ring, cq_size_for_dtor, sqes,
                                                sqes_array_size, params.sq_entries, params.cq_entries});
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

// The loop that the two forms share.  step moves at most the count that
// it takes, and returns the count that it moved, 0 at the end of the
// source, or -1 with errno set.
template <typename Step>
[[nodiscard]] std::expected<std::size_t, std::error_code> transfer_all(::fixy::fs::OwnedFd const& source,
                                                                       ::fixy::fs::OwnedFd const& destination,
                                                                       std::size_t length, Step step) noexcept {
    if (!source.is_open() || !destination.is_open()) {
        return std::unexpected{std::error_code{EBADF, std::system_category()}};
    }
    std::size_t moved = 0;
    while (moved < length) {
        const ::ssize_t count = step(length - moved);
        if (count == 0) break;
        if (count < 0) {
            if (errno == EINTR) continue;
            return std::unexpected{std::error_code{errno, std::system_category()}};
        }
        moved += static_cast<std::size_t>(count);
    }
    return moved;
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
    return detail::transfer_all(source, destination, length, [&, offset = source_offset](std::size_t rest) mutable {
        return ::sendfile(destination.get(), source.get(), &offset, rest);  // SYSCALL-CAP-OK: zerocopy_transfer ctx-gate (CtxFitsSendfileTransfer, IO+Block)
    });
}

// copy_file_range reads from source_offset and writes at
// destination_offset, and it moves neither file position.
template <typename... Atoms, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsCopyRangeTransfer<Ctx, Atoms...>
[[nodiscard]] inline std::expected<std::size_t, std::error_code>
zerocopy_transfer(Ctx const&, ::fixy::fs::OwnedFd const& source, ::fixy::fs::OwnedFd const& destination,
                  std::size_t length, ::off_t source_offset = 0, ::off_t destination_offset = 0) noexcept {
    return detail::transfer_all(
        source, destination, length,
        [&, in_offset = source_offset, out_offset = destination_offset](std::size_t rest) mutable {
            return ::copy_file_range(  // SYSCALL-CAP-OK: zerocopy_transfer ctx-gate (CtxFitsCopyRangeTransfer, IO+Block)
                source.get(), &in_offset, destination.get(), &out_offset, rest, 0);
        });
}

}  // namespace fixy::io

namespace fixy::io::detail::io_surface_invariants {

static_assert(ring_flag_bits_of(^^ring_flag::IoPoll) == IORING_SETUP_IOPOLL);
static_assert(ring_flag_bits_of(^^ring_flag::SqPoll) == IORING_SETUP_SQPOLL);
static_assert(ring_flag_bits_of(^^ring_flag::SingleIssuer) == IORING_SETUP_SINGLE_ISSUER);
static_assert(ring_flag_bits_of(^^ring_flag::CoopTaskrun) == IORING_SETUP_COOP_TASKRUN);
static_assert(ring_flag_bits_of(^^ring_flag::DeferTaskrun) == IORING_SETUP_DEFER_TASKRUN);

static_assert(EngineIsIoUring<engine::IoUring>);
static_assert(SimpleTransfer<zerocopy::Sendfile>);
static_assert(SimpleTransfer<zerocopy::CopyFileRange>);

// The three predicates are fail-closed: a tag they were never told about
// answers false.  These stand-ins are tags that fixy::io does not
// declare, and they witness that shape.
struct FutureEngine final {};
struct FutureZerocopy final {};
struct FutureRingFlag final {};
static_assert(!EngineIsIoUring<FutureEngine>,
              "an engine tag this surface was not told about must be refused, not admitted.");
static_assert(!SimpleTransfer<FutureZerocopy>);
static_assert(!MappedRingFlag<FutureRingFlag>, "a ring flag with no row must have no bits, not zero.");
static_assert(!MappedRingFlag<void>);
static_assert(MappedRingFlag<ring_flag::IoPoll>);
static_assert(!EngineIsIoUring<void>, "an empty pack names no engine, and void must not pass for one.");

static_assert(!is_pow2_(0));
static_assert(is_pow2_(1));
static_assert(is_pow2_(8));
static_assert(!is_pow2_(3));
static_assert(is_pow2_(32768));
static_assert(!is_pow2_(32769));

using A_Engine = ::fixy::atom::io::engine<engine::IoUring>;
using A_Sq8 = ::fixy::atom::io::sq_entries<8>;
using A_Sq7 = ::fixy::atom::io::sq_entries<7>;
using A_Cq16 = ::fixy::atom::io::cq_entries<16>;
using A_Sendfile = ::fixy::atom::io::zerocopy<zerocopy::Sendfile>;
using A_IoPoll = ::fixy::atom::io::ring_flag<ring_flag::IoPoll>;
using A_SqPoll = ::fixy::atom::io::ring_flag<ring_flag::SqPoll>;
using A_FutureFlag = ::fixy::atom::io::ring_flag<FutureRingFlag>;
using A_FutureEngine = ::fixy::atom::io::engine<FutureEngine>;

static_assert(is_engine_atom_v<A_Engine>);
static_assert(!is_engine_atom_v<A_Sendfile>);
static_assert(is_sq_entries_atom_v<A_Sq8>);
static_assert(atom_sq_entries_v<A_Sq8> == 8);
static_assert(is_zerocopy_atom_v<A_Sendfile>);
static_assert(is_ring_flag_atom_v<A_IoPoll>);

static_assert(std::is_same_v<engine_of_t<A_Engine, A_Sq8>, engine::IoUring>);
static_assert(std::is_same_v<engine_of_t<A_FutureEngine, A_Sq8>, FutureEngine>);
static_assert(sq_entries_of_v<A_Engine, A_Sq8> == 8);
static_assert(sq_entries_is_pow2_v<A_Engine, A_Sq8>);
static_assert(!sq_entries_is_pow2_v<A_Engine, A_Sq7>);
static_assert(cq_entries_is_pow2_or_default_v<A_Engine, A_Sq8>);
static_assert(cq_entries_of_v<A_Engine, A_Sq8> == 0, "no cq atom means the kernel default, which is a zero here.");
static_assert(cq_entries_of_v<A_Engine, A_Sq8, A_Cq16> == 16);

// The repeat rule reads the pack.  One flag named two times is a repeat,
// and two different flags fold together.
static_assert(::fixy::atom_pack::RepeatsAnAtomOf<^^::fixy::atom::io::ring_flag, A_IoPoll, A_IoPoll>);
static_assert(!::fixy::atom_pack::RepeatsAnAtomOf<^^::fixy::atom::io::ring_flag, A_IoPoll, A_SqPoll>);
static_assert(fold_ring_flags<A_IoPoll, A_SqPoll>() == (IORING_SETUP_IOPOLL | IORING_SETUP_SQPOLL));
static_assert(all_ring_flags_known_v<A_Engine, A_Sq8, A_IoPoll>);
static_assert(!all_ring_flags_known_v<A_Engine, A_Sq8, A_FutureFlag>);

static_assert(SimpleTransfer<zerocopy_of_t<A_Sendfile>>);
static_assert(!SimpleTransfer<zerocopy_of_t<A_Engine>>, "a pack with no zerocopy atom names no transfer.");

// The row derived from the pack is IO and Block.  This is the pin on
// that equality.
using ExpectedIoRow = eff::Row<eff::Effect::IO, eff::Effect::Block>;
static_assert(std::is_same_v<::fixy::atom_pack::atoms_row_t<A_Engine, A_Sq8, A_IoPoll>, ExpectedIoRow>);
static_assert(std::is_same_v<::fixy::atom_pack::atoms_row_t<A_Sendfile>, ExpectedIoRow>);

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using IoOnlyCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO>>;

static_assert(CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq8>);
static_assert(CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq8, A_Cq16, A_IoPoll>);
static_assert(CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq8, A_IoPoll, A_SqPoll>);
static_assert(!CtxFitsIoUringMint<IoOnlyCtx, A_Engine, A_Sq8>,
              "a context without Block must not set up a ring: the call can park.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Sq8>, "a pack with no engine atom names nothing to set up.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Engine>, "a ring needs a submission-queue size.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq7>, "the submission count must be a power of two.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Engine, A_Sq8>, "two engine atoms in one pack.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq8, A_Sq8>, "two submission counts in one pack.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq8, A_Cq16, A_Cq16>, "two completion counts in one pack.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq8, A_IoPoll, A_IoPoll>);
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq8, A_FutureFlag>,
              "a ring flag with no IORING_SETUP_* row must be refused, not folded to no bits.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_FutureEngine, A_Sq8>);
static_assert(!CtxFitsIoUringMint<IoBlockCtx>);

static_assert(CtxFitsZerocopyTransfer<IoBlockCtx, A_Sendfile>);
static_assert(!CtxFitsZerocopyTransfer<IoOnlyCtx, A_Sendfile>);
static_assert(!CtxFitsZerocopyTransfer<IoBlockCtx, A_Engine>);
static_assert(!CtxFitsZerocopyTransfer<IoBlockCtx, A_Sendfile, A_Sendfile>, "two transfers in one pack.");
static_assert(CtxFitsSendfileTransfer<IoBlockCtx, A_Sendfile> && !CtxFitsCopyRangeTransfer<IoBlockCtx, A_Sendfile>,
              "each form admits its own primitive only, so an offset that one form ignores is never taken.");

// The handle owns a descriptor and three mappings, so it is move-only,
// and the constructor that claims them is private with the mint as its
// sole friend.  The default handle owns nothing, which is why it stays
// public.
static_assert(std::is_default_constructible_v<IoUringRing>);
static_assert(std::is_nothrow_move_constructible_v<IoUringRing>);
static_assert(std::is_nothrow_move_assignable_v<IoUringRing>);
static_assert(!std::is_copy_constructible_v<IoUringRing>);
static_assert(!std::is_copy_assignable_v<IoUringRing>);
static_assert(!std::is_constructible_v<IoUringRing, int, void*, std::size_t, void*, std::size_t, void*, std::size_t,
                                       std::uint32_t, std::uint32_t>,
              "the constructor that claims a descriptor and three mappings must not be public: a caller who never "
              "called io_uring_setup could hand it numbers and the destructor would close and unmap them.");

// Every tag fixy::io::ring_flag declares has an IORING_SETUP_* row in the
// table.  The known-flag concept reads the table, so this walk is the
// check that no declared tag is missing from it.
static_assert(::fixy::atom_pack::every_tag_in_satisfies<^^::fixy::io::ring_flag, [](std::meta::info flag_tag) consteval {
                  return ::fixy::atom_pack::has_row(ring_flag_table, flag_tag);
              }>(),
              "fixy/os/Io.h: a tag declared in fixy::io::ring_flag has no row in ring_flag_table, so the gate "
              "refuses every ring that names it.");

}  // namespace fixy::io::detail::io_surface_invariants
