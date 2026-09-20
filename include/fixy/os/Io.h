#pragma once

// The io_uring ring and the zero-copy transfers.
//
// An atom pack says which engine, how many submission and completion
// entries, which setup flags, or which zero-copy primitive, and the pack
// is read twice: once for the IORING_SETUP_* word the syscall takes, and
// once for the effect row the calling context has to admit.
//
// Old spelling: include/crucible/fixy/Io.h.
//
// Deviations from that header, each deliberate:
//
//  1. Grants became atoms.  fixy::atom::io::{engine, zerocopy,
//     ring_flag, sq_entries, cq_entries} carry the axis and the row, so
//     the which_dim specializations the old header wrote by hand are
//     gone with the grant system that needed them.
//
//  2. The context gate reads the row off the pack rather than naming IO
//     and Block by hand.  Same answer today, pinned below.
//
//  3. Six tags did not come across, because fixy/atoms/Os.h ported only
//     the tags a mint accepts: engine::{Synchronous, Aio},
//     zerocopy::{None, Splice, MsgZerocopy} and ring_flag::Default stay
//     in the old tree.  The predicates that refused them are ported
//     anyway, with false primaries: engine_is_io_uring_v and
//     zerocopy_is_simple_transfer_v answer false for every tag they were
//     not told about, so a tag added to either namespace later is
//     refused on the day it appears rather than on the day somebody
//     remembers to extend a gate.  Three old negative fixtures named
//     those tags and cannot be written here; the predicates are what
//     stands in their place.
//
//  4. IoUringRing's constructor is private and mint_io_uring_ring is its
//     sole friend.  The old one was public and took a descriptor and
//     three mapped addresses, so any caller could hand it numbers and
//     the destructor would close and unmap them.  A handle that owns a
//     descriptor and three mappings is a claim about what the kernel
//     gave you, and nothing but the setup call can make that claim
//     truthfully.
//
//  5. The ring-flag bit map gets the same fail-closed predicate the
//     mapping surface got: a flag tag the map has never heard of folds
//     to zero, which is a ring set up without the flag the caller asked
//     for rather than a refusal.
//
//  6. zerocopy_is_none_v and the gate conjunct that read it are gone
//     with the zerocopy::None tag they existed to refuse.  Nothing is
//     weakened: zerocopy_is_simple_transfer_v answers false for every
//     tag but the two, so a None reintroduced tomorrow is refused by
//     the surviving conjunct without anyone remembering to add a
//     sentinel check for it.

#include <fixy/Qtt.h>
#include <fixy/atoms/Os.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Lift.h>
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
// here, and the predicate is false for anything else, so a tag added to
// fixy::io::engine later is refused rather than admitted.

template <typename E>
struct engine_is_io_uring : std::false_type {};
template <>
struct engine_is_io_uring<engine::IoUring> : std::true_type {};
template <typename E>
inline constexpr bool engine_is_io_uring_v = engine_is_io_uring<E>::value;

// Only the two primitives with a uniform fd-to-fd signature are
// reachable from a mint here.  splice(2) needs a pipe as intermediary
// and send(MSG_ZEROCOPY) needs socket-message arguments, so neither has
// a shape this call can take.

template <typename Z>
struct zerocopy_is_simple_transfer : std::false_type {};
template <>
struct zerocopy_is_simple_transfer<zerocopy::Sendfile> : std::true_type {};
template <>
struct zerocopy_is_simple_transfer<zerocopy::CopyFileRange> : std::true_type {};
template <typename Z>
inline constexpr bool zerocopy_is_simple_transfer_v = zerocopy_is_simple_transfer<Z>::value;

// These are bits of one setup word, so a pack may engage several of
// different kinds and they fold together.

template <typename F>
struct ring_flag_bits : std::integral_constant<std::uint32_t, 0> {};
template <>
struct ring_flag_bits<ring_flag::IoPoll> : std::integral_constant<std::uint32_t, IORING_SETUP_IOPOLL> {};
template <>
struct ring_flag_bits<ring_flag::SqPoll> : std::integral_constant<std::uint32_t, IORING_SETUP_SQPOLL> {};
template <>
struct ring_flag_bits<ring_flag::SingleIssuer> : std::integral_constant<std::uint32_t, IORING_SETUP_SINGLE_ISSUER> {};
template <>
struct ring_flag_bits<ring_flag::CoopTaskrun> : std::integral_constant<std::uint32_t, IORING_SETUP_COOP_TASKRUN> {};
template <>
struct ring_flag_bits<ring_flag::DeferTaskrun> : std::integral_constant<std::uint32_t, IORING_SETUP_DEFER_TASKRUN> {};
template <typename F>
inline constexpr std::uint32_t ring_flag_bits_v = ring_flag_bits<F>::value;

// The bit map answers zero for a flag it does not know, and a ring set
// up without the flag the caller asked for is worse than one refused.
// The gate reads this, and the walk at the foot of this header checks
// that every tag fixy::io::ring_flag declares is on the list.
template <typename F>
inline constexpr bool is_known_ring_flag_v =
    std::is_same_v<F, ring_flag::IoPoll> || std::is_same_v<F, ring_flag::SqPoll>
    || std::is_same_v<F, ring_flag::SingleIssuer> || std::is_same_v<F, ring_flag::CoopTaskrun>
    || std::is_same_v<F, ring_flag::DeferTaskrun>;

namespace detail {

namespace eff = ::foundation::effects;

template <typename A>
inline constexpr bool is_engine_atom_v = ::foundation::reflect::is_instance_of_v<A, ^^::fixy::atom::io::engine>;
template <typename A>
inline constexpr bool is_zerocopy_atom_v = ::foundation::reflect::is_instance_of_v<A, ^^::fixy::atom::io::zerocopy>;
template <typename A>
inline constexpr bool is_ring_flag_atom_v = ::foundation::reflect::is_instance_of_v<A, ^^::fixy::atom::io::ring_flag>;
template <typename A>
inline constexpr bool is_sq_entries_atom_v = ::foundation::reflect::is_instance_of_v<A, ^^::fixy::atom::io::sq_entries>;
template <typename A>
inline constexpr bool is_cq_entries_atom_v = ::foundation::reflect::is_instance_of_v<A, ^^::fixy::atom::io::cq_entries>;

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
inline constexpr bool has_engine_atom_v = (is_engine_atom_v<Atoms> || ...);
template <typename... Atoms>
inline constexpr bool has_zerocopy_atom_v = (is_zerocopy_atom_v<Atoms> || ...);
template <typename... Atoms>
inline constexpr bool has_sq_entries_atom_v = (is_sq_entries_atom_v<Atoms> || ...);
template <typename... Atoms>
inline constexpr bool has_cq_entries_atom_v = (is_cq_entries_atom_v<Atoms> || ...);

template <typename... Atoms>
inline constexpr bool has_duplicate_engine_v = (static_cast<int>(is_engine_atom_v<Atoms>) + ... + 0) > 1;
template <typename... Atoms>
inline constexpr bool has_duplicate_zerocopy_v = (static_cast<int>(is_zerocopy_atom_v<Atoms>) + ... + 0) > 1;
template <typename... Atoms>
inline constexpr bool has_duplicate_sq_entries_v = (static_cast<int>(is_sq_entries_atom_v<Atoms>) + ... + 0) > 1;
template <typename... Atoms>
inline constexpr bool has_duplicate_cq_entries_v = (static_cast<int>(is_cq_entries_atom_v<Atoms>) + ... + 0) > 1;

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
inline constexpr bool pack_engine_is_io_uring_v = ::fixy::io::engine_is_io_uring_v<engine_of_t<Atoms...>>;

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

template <typename... Atoms>
inline constexpr bool pack_zerocopy_is_simple_transfer_v =
    ::fixy::io::zerocopy_is_simple_transfer_v<zerocopy_of_t<Atoms...>>;

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
inline constexpr std::uint32_t atom_ring_flag_bits_v =
    is_ring_flag_atom_v<A> ? ::fixy::io::ring_flag_bits_v<extract_ring_flag_t<std::remove_cvref_t<A>>> : 0u;

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
    !is_ring_flag_atom_v<A> || ::fixy::io::is_known_ring_flag_v<extract_ring_flag_t<std::remove_cvref_t<A>>>;

template <typename... Atoms>
inline constexpr bool all_ring_flags_known_v = (ring_flag_atom_is_known_v<Atoms> && ... && true);

// A repeat of one kind is a duplicate.  Two different kinds are not,
// because their bits fold together, so the count has to be per kind.
template <typename Target, typename A>
inline constexpr bool is_specific_ring_flag_v =
    std::is_same_v<std::remove_cvref_t<A>, ::fixy::atom::io::ring_flag<Target>>;

template <typename Target, typename... Atoms>
inline constexpr bool has_duplicate_specific_ring_flag_v =
    (static_cast<int>(is_specific_ring_flag_v<Target, Atoms>) + ... + 0) > 1;

template <typename... Atoms>
inline constexpr bool has_duplicate_ring_flag_v =
    has_duplicate_specific_ring_flag_v<::fixy::io::ring_flag::IoPoll, Atoms...>
    || has_duplicate_specific_ring_flag_v<::fixy::io::ring_flag::SqPoll, Atoms...>
    || has_duplicate_specific_ring_flag_v<::fixy::io::ring_flag::SingleIssuer, Atoms...>
    || has_duplicate_specific_ring_flag_v<::fixy::io::ring_flag::CoopTaskrun, Atoms...>
    || has_duplicate_specific_ring_flag_v<::fixy::io::ring_flag::DeferTaskrun, Atoms...>;

// The row a pack exercises is the union of the rows its atoms lift to.
template <typename... Atoms>
struct atoms_row {
    using type = eff::Row<>;
};
template <typename First, typename... Rest>
struct atoms_row<First, Rest...> {
    using type = eff::row_union_t<eff::lift_row_t<std::remove_cvref_t<First>>, typename atoms_row<Rest...>::type>;
};
template <typename... Atoms>
using atoms_row_t = typename atoms_row<Atoms...>::type;

}  // namespace detail

template <typename Ctx, typename... Atoms>
concept CtxAdmitsAtomRow = ::foundation::effects::IsExecCtx<Ctx>
                        && (::foundation::effects::LiftsToRow<std::remove_cvref_t<Atoms>> && ...)
                        && ::foundation::effects::CtxAdmits<Ctx, detail::atoms_row_t<Atoms...>>;

template <typename Ctx, typename... Atoms>
concept CtxFitsIoUringMint =
    CtxAdmitsAtomRow<Ctx, Atoms...> && detail::has_engine_atom_v<Atoms...>
    && !detail::has_duplicate_engine_v<Atoms...> && detail::pack_engine_is_io_uring_v<Atoms...>
    && detail::has_sq_entries_atom_v<Atoms...> && !detail::has_duplicate_sq_entries_v<Atoms...>
    && detail::sq_entries_is_pow2_v<Atoms...> && !detail::has_duplicate_cq_entries_v<Atoms...>
    && detail::cq_entries_is_pow2_or_default_v<Atoms...> && !detail::has_duplicate_ring_flag_v<Atoms...>
    && detail::all_ring_flags_known_v<Atoms...>;

template <typename Ctx, typename... Atoms>
concept CtxFitsZerocopyMint = CtxAdmitsAtomRow<Ctx, Atoms...> && detail::has_zerocopy_atom_v<Atoms...>
                           && !detail::has_duplicate_zerocopy_v<Atoms...>
                           && detail::pack_zerocopy_is_simple_transfer_v<Atoms...>;

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
// The constructor is private.  A public one took nine numbers and
// returned a handle whose destructor closes a descriptor and unmaps
// three addresses, so a caller who had never called io_uring_setup could
// hand it any numbers it liked.  Only the setup call knows what the
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

// §XXI carve-out: cx=alloc — a zero-copy transfer is a kernel side effect.
template <typename... Atoms, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsZerocopyMint<Ctx, Atoms...>
[[nodiscard]] inline std::expected<std::size_t, std::error_code>
mint_zerocopy_transfer(Ctx const&, int src_fd, int dst_fd, std::size_t length, ::off_t off_in = 0,
                       ::off_t off_out = 0) noexcept {
    using Z = detail::zerocopy_of_t<Atoms...>;
    ::ssize_t moved = -1;
    if constexpr (std::is_same_v<Z, zerocopy::Sendfile>) {
        ::off_t offset = off_in;
        moved = ::sendfile(dst_fd, src_fd, &offset,
                           length);  // SYSCALL-CAP-OK: mint_zerocopy_transfer body (CtxFitsZerocopyMint, IO+Block)
        (void)off_out;  // sendfile writes to dst_fd's current position.
    } else if constexpr (std::is_same_v<Z, zerocopy::CopyFileRange>) {
        ::off_t in_offset = off_in;
        ::off_t out_offset = off_out;
        moved = ::copy_file_range(src_fd, &in_offset, dst_fd, &out_offset, length,
                                  0);  // SYSCALL-CAP-OK: mint_zerocopy_transfer body (CtxFitsZerocopyMint, IO+Block)
    } else {
        static_assert(sizeof(Z) == 0, "mint_zerocopy_transfer: unsupported zerocopy tag "
                                      "reached body — concept gate should have rejected.");
    }
    if (moved < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return static_cast<std::size_t>(moved);
}

}  // namespace fixy::io

namespace fixy::io::detail::io_surface_invariants {

static_assert(ring_flag_bits_v<ring_flag::IoPoll> == IORING_SETUP_IOPOLL);
static_assert(ring_flag_bits_v<ring_flag::SqPoll> == IORING_SETUP_SQPOLL);
static_assert(ring_flag_bits_v<ring_flag::SingleIssuer> == IORING_SETUP_SINGLE_ISSUER);
static_assert(ring_flag_bits_v<ring_flag::CoopTaskrun> == IORING_SETUP_COOP_TASKRUN);
static_assert(ring_flag_bits_v<ring_flag::DeferTaskrun> == IORING_SETUP_DEFER_TASKRUN);

static_assert(engine_is_io_uring_v<engine::IoUring>);
static_assert(zerocopy_is_simple_transfer_v<zerocopy::Sendfile>);
static_assert(zerocopy_is_simple_transfer_v<zerocopy::CopyFileRange>);

// The three predicates are fail-closed: a tag they were never told about
// answers false.  fixy::io no longer declares engine::Synchronous,
// zerocopy::Splice or ring_flag::Default, so these stand-ins are what
// witness the shape the old negative fixtures named.
struct FutureEngine final {};
struct FutureZerocopy final {};
struct FutureRingFlag final {};
static_assert(!engine_is_io_uring_v<FutureEngine>,
              "an engine tag this surface was not told about must be refused, not admitted.");
static_assert(!zerocopy_is_simple_transfer_v<FutureZerocopy>);
static_assert(!is_known_ring_flag_v<FutureRingFlag>);
static_assert(ring_flag_bits_v<FutureRingFlag> == 0, "an unmapped ring flag folds to no bits, which is a ring set "
                                                     "up without the flag the caller asked for.");
static_assert(!engine_is_io_uring_v<void>, "an empty pack names no engine, and void must not pass for one.");

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

static_assert(has_engine_atom_v<A_Engine, A_Sq8>);
static_assert(pack_engine_is_io_uring_v<A_Engine, A_Sq8>);
static_assert(!pack_engine_is_io_uring_v<A_FutureEngine, A_Sq8>);
static_assert(sq_entries_of_v<A_Engine, A_Sq8> == 8);
static_assert(sq_entries_is_pow2_v<A_Engine, A_Sq8>);
static_assert(!sq_entries_is_pow2_v<A_Engine, A_Sq7>);
static_assert(cq_entries_is_pow2_or_default_v<A_Engine, A_Sq8>);
static_assert(cq_entries_of_v<A_Engine, A_Sq8> == 0, "no cq atom means the kernel default, which is a zero here.");
static_assert(cq_entries_of_v<A_Engine, A_Sq8, A_Cq16> == 16);

static_assert(has_duplicate_engine_v<A_Engine, A_Sq8, A_Engine>);
static_assert(!has_duplicate_engine_v<A_Engine, A_Sq8>);
static_assert(has_duplicate_sq_entries_v<A_Engine, A_Sq8, A_Sq8>);
static_assert(!has_duplicate_sq_entries_v<A_Engine, A_Sq8>);
static_assert(has_duplicate_ring_flag_v<A_IoPoll, A_IoPoll>);
static_assert(!has_duplicate_ring_flag_v<A_IoPoll, A_SqPoll>);
static_assert(fold_ring_flags<A_IoPoll, A_SqPoll>() == (IORING_SETUP_IOPOLL | IORING_SETUP_SQPOLL));
static_assert(all_ring_flags_known_v<A_Engine, A_Sq8, A_IoPoll>);
static_assert(!all_ring_flags_known_v<A_Engine, A_Sq8, A_FutureFlag>);

static_assert(pack_zerocopy_is_simple_transfer_v<A_Sendfile>);
static_assert(!pack_zerocopy_is_simple_transfer_v<A_Engine>, "a pack with no zerocopy atom names no transfer.");

// The derived row and the row the old header named by hand are the same
// answer.  This is the pin on that equality.
using ExpectedIoRow = eff::Row<eff::Effect::IO, eff::Effect::Block>;
static_assert(std::is_same_v<atoms_row_t<A_Engine, A_Sq8, A_IoPoll>, ExpectedIoRow>);
static_assert(std::is_same_v<atoms_row_t<A_Sendfile>, ExpectedIoRow>);
static_assert(std::is_same_v<atoms_row_t<>, eff::Row<>>);

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using IoOnlyCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO>>;

static_assert(CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq8>);
static_assert(CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq8, A_Cq16, A_IoPoll>);
static_assert(!CtxFitsIoUringMint<IoOnlyCtx, A_Engine, A_Sq8>,
              "a context without Block must not set up a ring: the call can park.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Sq8>, "a pack with no engine atom names nothing to set up.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Engine>, "a ring needs a submission-queue size.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq7>, "the submission count must be a power of two.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq8, A_IoPoll, A_IoPoll>);
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq8, A_FutureFlag>,
              "a ring flag with no IORING_SETUP_* mapping must be refused, not folded to no bits.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_FutureEngine, A_Sq8>);
static_assert(!CtxFitsIoUringMint<IoBlockCtx>);

static_assert(CtxFitsZerocopyMint<IoBlockCtx, A_Sendfile>);
static_assert(!CtxFitsZerocopyMint<IoOnlyCtx, A_Sendfile>);
static_assert(!CtxFitsZerocopyMint<IoBlockCtx, A_Engine>);

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

// Every tag fixy::io::ring_flag declares has an IORING_SETUP_* mapping.
// is_known_ring_flag_v is a hand list, and this is what notices a tag
// the list omits.
[[nodiscard]] consteval bool every_ring_flag_is_known_() noexcept {
    static constexpr auto members = std::define_static_array(
        std::meta::members_of(^^::fixy::io::ring_flag, std::meta::access_context::unchecked()));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_type(member) && !std::meta::is_type_alias(member)
                      && std::meta::is_class_type(member)) {
            using T = [:member:];
            if (!is_known_ring_flag_v<T>) return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

static_assert(every_ring_flag_is_known_(),
              "fixy/os/Io.h: a tag declared in fixy::io::ring_flag is missing from is_known_ring_flag_v, so the "
              "gate would admit it and the fold would set no bit for it.");

}  // namespace fixy::io::detail::io_surface_invariants
