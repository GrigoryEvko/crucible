#pragma once

#include <crucible/fixy/_Grant.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/_Linear.h>

#include <crucible/effects/_ExecCtx.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_Capabilities.h>

#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/io_uring.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <system_error>
#include <type_traits>
#include <utility>

// A kernel header older than the flag does not declare it, so each value is
// spelled here as well.  COOP_TASKRUN arrives in Linux 5.19, SINGLE_ISSUER in
// 6.0, DEFER_TASKRUN in 6.1.

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

namespace crucible::fixy::io {

// A grants pack engages exactly one of these.  Only IoUring reaches a mint
// here: Synchronous names the plain read and write path that needs no ring,
// and Aio is present so collision rules can name it, with no implementation
// behind it.

namespace engine {
struct Synchronous final {};  // read(2) / write(2), through a userspace bounce buffer.
struct IoUring final {};  // io_uring_setup(2), Linux 5.1 and later.
struct Aio final {};  // libaio; review policy forbids it.
}  // namespace engine

template <typename E>
struct engine_is_io_uring : std::false_type {};
template <>
struct engine_is_io_uring<engine::IoUring> : std::true_type {};
template <typename E>
inline constexpr bool engine_is_io_uring_v = engine_is_io_uring<E>::value;

// Only the two primitives with a uniform fd-to-fd signature are reachable
// from a mint here.  Splice needs a pipe as intermediary and MsgZerocopy
// needs socket-message arguments, so both exist as tags that collision rules
// can name while the mint's gate refuses them.  `None` is refused too: it
// means "no zerocopy at all", and a caller who wants that path should not
// arrive at this mint by accident.

namespace zerocopy {
struct None final {};  // sentinel; refused at the mint boundary
struct Sendfile final {};  // sendfile(2), fd to fd, no userspace copy
struct Splice final {};  // splice(2), needs a pipe as intermediary
struct CopyFileRange final {};  // copy_file_range(2), same filesystem or NFSv4.2
struct MsgZerocopy final {};  // send(MSG_ZEROCOPY), sockets only
}  // namespace zerocopy

template <typename Z>
struct zerocopy_is_none : std::false_type {};
template <>
struct zerocopy_is_none<zerocopy::None> : std::true_type {};
template <typename Z>
inline constexpr bool zerocopy_is_none_v = zerocopy_is_none<Z>::value;

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

namespace ring_flag {
struct Default final {};  // no bits set
struct IoPoll final {};  // IORING_SETUP_IOPOLL, busy-poll completion
struct SqPoll final {};  // IORING_SETUP_SQPOLL, kernel-side submission thread
struct SingleIssuer final {};  // IORING_SETUP_SINGLE_ISSUER, Linux 6.0 and later
struct CoopTaskrun final {};  // IORING_SETUP_COOP_TASKRUN, Linux 5.19 and later
struct DeferTaskrun final {};  // IORING_SETUP_DEFER_TASKRUN, Linux 6.1 and later
}  // namespace ring_flag

template <typename F>
struct ring_flag_bits : std::integral_constant<std::uint32_t, 0> {};
template <>
struct ring_flag_bits<ring_flag::Default> : std::integral_constant<std::uint32_t, 0> {};
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

}  // namespace crucible::fixy::io

namespace crucible::fixy::grant {

namespace io {

template <typename E>
struct engine final : grant_base {};

template <typename Z>
struct zerocopy final : grant_base {};

template <typename F>
struct ring_flag final : grant_base {};

template <std::uint32_t N>
struct sq_entries final : grant_base {
    static constexpr std::uint32_t value = N;
};

template <std::uint32_t N>
struct cq_entries final : grant_base {
    static constexpr std::uint32_t value = N;
};

}  // namespace io

template <typename E>
struct which_dim<io::engine<E>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <typename Z>
struct which_dim<io::zerocopy<Z>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <typename F>
struct which_dim<io::ring_flag<F>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <std::uint32_t N>
struct which_dim<io::sq_entries<N>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};
template <std::uint32_t N>
struct which_dim<io::cq_entries<N>> : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::SyscallSurface> {};

}  // namespace crucible::fixy::grant

namespace crucible::fixy::io {

namespace detail {

template <typename G>
struct is_engine_grant : std::false_type {};
template <typename E>
struct is_engine_grant<::crucible::fixy::grant::io::engine<E>> : std::true_type {};
template <typename G>
inline constexpr bool is_engine_grant_v = is_engine_grant<G>::value;

template <typename G>
struct is_zerocopy_grant : std::false_type {};
template <typename Z>
struct is_zerocopy_grant<::crucible::fixy::grant::io::zerocopy<Z>> : std::true_type {};
template <typename G>
inline constexpr bool is_zerocopy_grant_v = is_zerocopy_grant<G>::value;

template <typename G>
struct is_ring_flag_grant : std::false_type {};
template <typename F>
struct is_ring_flag_grant<::crucible::fixy::grant::io::ring_flag<F>> : std::true_type {};
template <typename G>
inline constexpr bool is_ring_flag_grant_v = is_ring_flag_grant<G>::value;

template <typename G>
struct is_sq_entries_grant : std::false_type {};
template <std::uint32_t N>
struct is_sq_entries_grant<::crucible::fixy::grant::io::sq_entries<N>> : std::true_type {};
template <typename G>
inline constexpr bool is_sq_entries_grant_v = is_sq_entries_grant<G>::value;

template <typename G>
struct is_cq_entries_grant : std::false_type {};
template <std::uint32_t N>
struct is_cq_entries_grant<::crucible::fixy::grant::io::cq_entries<N>> : std::true_type {};
template <typename G>
inline constexpr bool is_cq_entries_grant_v = is_cq_entries_grant<G>::value;

template <typename... Grants>
inline constexpr bool has_engine_grant_v = (is_engine_grant_v<Grants> || ...);
template <typename... Grants>
inline constexpr bool has_zerocopy_grant_v = (is_zerocopy_grant_v<Grants> || ...);
template <typename... Grants>
inline constexpr bool has_sq_entries_grant_v = (is_sq_entries_grant_v<Grants> || ...);
template <typename... Grants>
inline constexpr bool has_cq_entries_grant_v = (is_cq_entries_grant_v<Grants> || ...);

template <typename... Grants>
inline constexpr bool has_duplicate_engine_v = (static_cast<int>(is_engine_grant_v<Grants>) + ...) > 1;
template <typename... Grants>
inline constexpr bool has_duplicate_zerocopy_v = (static_cast<int>(is_zerocopy_grant_v<Grants>) + ...) > 1;
template <typename... Grants>
inline constexpr bool has_duplicate_sq_entries_v = (static_cast<int>(is_sq_entries_grant_v<Grants>) + ...) > 1;
template <typename... Grants>
inline constexpr bool has_duplicate_cq_entries_v = (static_cast<int>(is_cq_entries_grant_v<Grants>) + ...) > 1;

template <typename G>
struct extract_engine {
    using type = void;
};
template <typename E>
struct extract_engine<::crucible::fixy::grant::io::engine<E>> {
    using type = E;
};
template <typename G>
using extract_engine_t = typename extract_engine<G>::type;

template <typename... Grants>
struct engine_of;
template <typename First, typename... Rest>
struct engine_of<First, Rest...> {
    using type =
        std::conditional_t<is_engine_grant_v<First>, extract_engine_t<First>, typename engine_of<Rest...>::type>;
};
template <>
struct engine_of<> {
    using type = void;
};
template <typename... Grants>
using engine_of_t = typename engine_of<Grants...>::type;

template <typename... Grants>
inline constexpr bool engine_is_io_uring_v =
    !std::is_same_v<engine_of_t<Grants...>, void> && ::crucible::fixy::io::engine_is_io_uring_v<engine_of_t<Grants...>>;

template <typename G>
struct extract_zerocopy {
    using type = void;
};
template <typename Z>
struct extract_zerocopy<::crucible::fixy::grant::io::zerocopy<Z>> {
    using type = Z;
};
template <typename G>
using extract_zerocopy_t = typename extract_zerocopy<G>::type;

template <typename... Grants>
struct zerocopy_of;
template <typename First, typename... Rest>
struct zerocopy_of<First, Rest...> {
    using type =
        std::conditional_t<is_zerocopy_grant_v<First>, extract_zerocopy_t<First>, typename zerocopy_of<Rest...>::type>;
};
template <>
struct zerocopy_of<> {
    using type = void;
};
template <typename... Grants>
using zerocopy_of_t = typename zerocopy_of<Grants...>::type;

template <typename... Grants>
inline constexpr bool pack_zerocopy_is_none_v =
    std::is_same_v<zerocopy_of_t<Grants...>, ::crucible::fixy::io::zerocopy::None>;

template <typename... Grants>
inline constexpr bool pack_zerocopy_is_simple_transfer_v =
    !std::is_same_v<zerocopy_of_t<Grants...>, void>
    && ::crucible::fixy::io::zerocopy_is_simple_transfer_v<zerocopy_of_t<Grants...>>;

template <typename G>
struct extract_sq_entries : std::integral_constant<std::uint32_t, 0> {};
template <std::uint32_t N>
struct extract_sq_entries<::crucible::fixy::grant::io::sq_entries<N>> : std::integral_constant<std::uint32_t, N> {};
template <typename G>
inline constexpr std::uint32_t extract_sq_entries_v = extract_sq_entries<G>::value;

template <typename... Grants>
inline constexpr std::uint32_t sq_entries_of_v = (extract_sq_entries_v<Grants> + ...);  // exactly one engaged; rest 0

template <typename G>
struct extract_cq_entries : std::integral_constant<std::uint32_t, 0> {};
template <std::uint32_t N>
struct extract_cq_entries<::crucible::fixy::grant::io::cq_entries<N>> : std::integral_constant<std::uint32_t, N> {};
template <typename G>
inline constexpr std::uint32_t extract_cq_entries_v = extract_cq_entries<G>::value;

template <typename... Grants>
inline constexpr std::uint32_t cq_entries_of_v = (extract_cq_entries_v<Grants> + ...);  // 0 = "kernel default = 2 * SQ"

[[nodiscard]] inline constexpr bool is_pow2_(std::uint32_t v) noexcept { return v > 0 && (v & (v - 1)) == 0; }

template <typename... Grants>
inline constexpr bool sq_entries_is_pow2_v =
    has_sq_entries_grant_v<Grants...> && is_pow2_(sq_entries_of_v<Grants...>) && sq_entries_of_v<Grants...> <= 32768;

template <typename... Grants>
inline constexpr bool cq_entries_is_pow2_or_default_v =
    !has_cq_entries_grant_v<Grants...> || (is_pow2_(cq_entries_of_v<Grants...>) && cq_entries_of_v<Grants...> <= 65536);

template <typename G>
struct grant_ring_flag_bits : std::integral_constant<std::uint32_t, 0> {};
template <typename F>
struct grant_ring_flag_bits<::crucible::fixy::grant::io::ring_flag<F>>
    : std::integral_constant<std::uint32_t, ::crucible::fixy::io::ring_flag_bits_v<F>> {};
template <typename G>
inline constexpr std::uint32_t grant_ring_flag_bits_v = grant_ring_flag_bits<G>::value;

template <typename... Grants>
[[nodiscard]] inline constexpr std::uint32_t fold_ring_flags() noexcept {
    std::uint32_t acc = 0;
    ((acc |= grant_ring_flag_bits_v<Grants>), ...);
    return acc;
}

// A repeat of one kind is a duplicate.  Two different kinds are not, because
// their bits fold together, so the count has to be per kind.

template <typename Target, typename G>
struct is_specific_ring_flag : std::false_type {};
template <typename Target>
struct is_specific_ring_flag<Target, ::crucible::fixy::grant::io::ring_flag<Target>> : std::true_type {};
template <typename Target, typename G>
inline constexpr bool is_specific_ring_flag_v = is_specific_ring_flag<Target, G>::value;

template <typename Target, typename... Grants>
inline constexpr bool has_duplicate_specific_ring_flag_v =
    (static_cast<int>(is_specific_ring_flag_v<Target, Grants>) + ...) > 1;

template <typename... Grants>
inline constexpr bool has_duplicate_ring_flag_v =
    has_duplicate_specific_ring_flag_v<::crucible::fixy::io::ring_flag::IoPoll, Grants...>
    || has_duplicate_specific_ring_flag_v<::crucible::fixy::io::ring_flag::SqPoll, Grants...>
    || has_duplicate_specific_ring_flag_v<::crucible::fixy::io::ring_flag::SingleIssuer, Grants...>
    || has_duplicate_specific_ring_flag_v<::crucible::fixy::io::ring_flag::CoopTaskrun, Grants...>
    || has_duplicate_specific_ring_flag_v<::crucible::fixy::io::ring_flag::DeferTaskrun, Grants...>;

}  // namespace detail

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

public:
    IoUringRing() noexcept = default;

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
            ::munmap(sqes_, sqes_size_);
            sqes_ = MAP_FAILED;
            sqes_size_ = 0;
        }
        // cq_size_ == 0 marks IORING_FEAT_SINGLE_MMAP (cq aliases sq).
        if (cq_size_ != 0 && cq_ring_ != MAP_FAILED && cq_ring_ != nullptr) {
            ::munmap(cq_ring_, cq_size_);
        }
        cq_ring_ = MAP_FAILED;
        cq_size_ = 0;
        if (sq_ring_ != MAP_FAILED && sq_ring_ != nullptr) {
            ::munmap(sq_ring_, sq_size_);
            sq_ring_ = MAP_FAILED;
            sq_size_ = 0;
        }
        if (ring_fd_ >= 0) {
            ::close(ring_fd_);
            ring_fd_ = -1;
        }
        sq_entries_n_ = 0;
        cq_entries_n_ = 0;
    }
};

// Every syscall reached from this header can park the caller — on page-cache
// pressure, on fd-lock contention, on a NUMA-remote page fault — so Block is
// required alongside IO, as for any other filesystem-touching call.

template <typename Ctx>
concept CtxAdmitsIoBlock =
    ::crucible::effects::IsExecCtx<Ctx>
    && ::crucible::effects::row_contains_v<::crucible::effects::row_type_of_t<Ctx>, ::crucible::effects::Effect::IO>
    && ::crucible::effects::row_contains_v<::crucible::effects::row_type_of_t<Ctx>, ::crucible::effects::Effect::Block>;

template <typename Ctx, typename... Grants>
concept CtxFitsIoUringMint =
    CtxAdmitsIoBlock<Ctx> && detail::has_engine_grant_v<Grants...> && !detail::has_duplicate_engine_v<Grants...>
    && detail::engine_is_io_uring_v<Grants...> && detail::has_sq_entries_grant_v<Grants...>
    && !detail::has_duplicate_sq_entries_v<Grants...> && detail::sq_entries_is_pow2_v<Grants...>
    && !detail::has_duplicate_cq_entries_v<Grants...> && detail::cq_entries_is_pow2_or_default_v<Grants...>
    && !detail::has_duplicate_ring_flag_v<Grants...>;

template <typename Ctx, typename... Grants>
concept CtxFitsZerocopyMint =
    CtxAdmitsIoBlock<Ctx> && detail::has_zerocopy_grant_v<Grants...> && !detail::has_duplicate_zerocopy_v<Grants...>
    && !detail::pack_zerocopy_is_none_v<Grants...> && detail::pack_zerocopy_is_simple_transfer_v<Grants...>;

template <typename... Grants, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsIoUringMint<Ctx, Grants...>
[[nodiscard]] inline std::expected<::crucible::safety::Linear<IoUringRing>, std::error_code>
mint_io_uring_ring(Ctx const&) noexcept {
    constexpr std::uint32_t sq_n = detail::sq_entries_of_v<Grants...>;
    constexpr std::uint32_t cq_n = detail::cq_entries_of_v<Grants...>;
    constexpr std::uint32_t flags = detail::fold_ring_flags<Grants...>();

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
        const int e = errno;
        ::close(fd);  // SYSCALL-CAP-OK: mint_io_uring_ring body (CtxFitsIoUringMint, IO+Block)
        return std::unexpected{std::error_code{e, std::system_category()}};
    }

    void* cq_ring = MAP_FAILED;
    std::size_t cq_size_for_dtor = 0;
    if ((params.features & IORING_FEAT_SINGLE_MMAP) != 0) {
        // Linux 5.4 and later hand back one allocation that both rings live
        // in, and aliasing them is the documented contract.  A zero size is
        // what tells the destructor not to unmap the second one.
        cq_ring = sq_ring;
    } else {
        cq_ring = ::mmap(  // SYSCALL-CAP-OK: mint_io_uring_ring body (CtxFitsIoUringMint, IO+Block)
            nullptr, cq_ring_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
        if (cq_ring == MAP_FAILED) {
            const int e = errno;
            ::munmap(sq_ring, sq_ring_size);  // SYSCALL-CAP-OK: mint_io_uring_ring body (CtxFitsIoUringMint, IO+Block)
            ::close(fd);  // SYSCALL-CAP-OK: mint_io_uring_ring body (CtxFitsIoUringMint, IO+Block)
            return std::unexpected{std::error_code{e, std::system_category()}};
        }
        cq_size_for_dtor = cq_ring_size;
    }

    void* const sqes = ::mmap(  // SYSCALL-CAP-OK: mint_io_uring_ring body (CtxFitsIoUringMint, IO+Block)
        nullptr, sqes_array_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
    if (sqes == MAP_FAILED) {
        const int e = errno;
        if (cq_size_for_dtor != 0) {
            ::munmap(cq_ring, cq_ring_size);  // SYSCALL-CAP-OK: mint_io_uring_ring body (CtxFitsIoUringMint, IO+Block)
        }
        ::munmap(sq_ring, sq_ring_size);  // SYSCALL-CAP-OK: mint_io_uring_ring body (CtxFitsIoUringMint, IO+Block)
        ::close(fd);  // SYSCALL-CAP-OK: mint_io_uring_ring body (CtxFitsIoUringMint, IO+Block)
        return std::unexpected{std::error_code{e, std::system_category()}};
    }

    return ::crucible::safety::Linear<IoUringRing>{IoUringRing{fd, sq_ring, sq_ring_size, cq_ring, cq_size_for_dtor,
                                                               sqes, sqes_array_size, params.sq_entries,
                                                               params.cq_entries}};
}

template <typename... Grants, ::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsZerocopyMint<Ctx, Grants...>
[[nodiscard]] inline std::expected<std::size_t, std::error_code>
mint_zerocopy_transfer(Ctx const&, int src_fd, int dst_fd, std::size_t length, ::off_t off_in = 0,
                       ::off_t off_out = 0) noexcept {
    using Z = detail::zerocopy_of_t<Grants...>;
    ::ssize_t n = -1;
    if constexpr (std::is_same_v<Z, zerocopy::Sendfile>) {
        ::off_t off = off_in;
        n = ::sendfile(dst_fd, src_fd, &off, length);
        (void)off_out;  // sendfile writes to dst_fd's current position.
    } else if constexpr (std::is_same_v<Z, zerocopy::CopyFileRange>) {
        ::off_t in_off = off_in;
        ::off_t out_off = off_out;
        n = ::copy_file_range(src_fd, &in_off, dst_fd, &out_off, length, 0);
    } else {
        static_assert(sizeof(Z) == 0, "mint_zerocopy_transfer: unsupported zerocopy tag "
                                      "reached body — concept gate should have rejected.");
    }
    if (n < 0) {
        return std::unexpected{std::error_code{errno, std::system_category()}};
    }
    return static_cast<std::size_t>(n);
}

}  // namespace crucible::fixy::io

namespace crucible::fixy::io::detail {

static_assert(std::is_empty_v<::crucible::fixy::io::engine::Synchronous>);
static_assert(std::is_empty_v<::crucible::fixy::io::engine::IoUring>);
static_assert(std::is_empty_v<::crucible::fixy::io::engine::Aio>);

static_assert(std::is_empty_v<::crucible::fixy::io::zerocopy::None>);
static_assert(std::is_empty_v<::crucible::fixy::io::zerocopy::Sendfile>);
static_assert(std::is_empty_v<::crucible::fixy::io::zerocopy::Splice>);
static_assert(std::is_empty_v<::crucible::fixy::io::zerocopy::CopyFileRange>);
static_assert(std::is_empty_v<::crucible::fixy::io::zerocopy::MsgZerocopy>);

static_assert(std::is_empty_v<::crucible::fixy::io::ring_flag::Default>);
static_assert(std::is_empty_v<::crucible::fixy::io::ring_flag::IoPoll>);
static_assert(std::is_empty_v<::crucible::fixy::io::ring_flag::SqPoll>);

static_assert(::crucible::fixy::io::ring_flag_bits_v<::crucible::fixy::io::ring_flag::Default> == 0);
static_assert(::crucible::fixy::io::ring_flag_bits_v<::crucible::fixy::io::ring_flag::IoPoll> == IORING_SETUP_IOPOLL);
static_assert(::crucible::fixy::io::ring_flag_bits_v<::crucible::fixy::io::ring_flag::SqPoll> == IORING_SETUP_SQPOLL);
static_assert(::crucible::fixy::io::ring_flag_bits_v<::crucible::fixy::io::ring_flag::SingleIssuer>
              == IORING_SETUP_SINGLE_ISSUER);
static_assert(::crucible::fixy::io::ring_flag_bits_v<::crucible::fixy::io::ring_flag::CoopTaskrun>
              == IORING_SETUP_COOP_TASKRUN);
static_assert(::crucible::fixy::io::ring_flag_bits_v<::crucible::fixy::io::ring_flag::DeferTaskrun>
              == IORING_SETUP_DEFER_TASKRUN);

static_assert(::crucible::fixy::io::engine_is_io_uring_v<::crucible::fixy::io::engine::IoUring>);
static_assert(!::crucible::fixy::io::engine_is_io_uring_v<::crucible::fixy::io::engine::Synchronous>);
static_assert(!::crucible::fixy::io::engine_is_io_uring_v<::crucible::fixy::io::engine::Aio>);

static_assert(::crucible::fixy::io::zerocopy_is_simple_transfer_v<::crucible::fixy::io::zerocopy::Sendfile>);
static_assert(::crucible::fixy::io::zerocopy_is_simple_transfer_v<::crucible::fixy::io::zerocopy::CopyFileRange>);
static_assert(!::crucible::fixy::io::zerocopy_is_simple_transfer_v<::crucible::fixy::io::zerocopy::None>);
static_assert(!::crucible::fixy::io::zerocopy_is_simple_transfer_v<::crucible::fixy::io::zerocopy::Splice>);
static_assert(!::crucible::fixy::io::zerocopy_is_simple_transfer_v<::crucible::fixy::io::zerocopy::MsgZerocopy>);

static_assert(!is_pow2_(0));
static_assert(is_pow2_(1));
static_assert(is_pow2_(2));
static_assert(is_pow2_(8));
static_assert(!is_pow2_(3));
static_assert(!is_pow2_(7));
static_assert(is_pow2_(32768));
static_assert(!is_pow2_(32769));

using EmptyGrants = std::tuple<>;
using SingleEngine = ::crucible::fixy::grant::io::engine<::crucible::fixy::io::engine::IoUring>;
using SingleSqN8 = ::crucible::fixy::grant::io::sq_entries<8>;
using SingleZc = ::crucible::fixy::grant::io::zerocopy<::crucible::fixy::io::zerocopy::Sendfile>;

static_assert(is_engine_grant_v<SingleEngine>);
static_assert(!is_engine_grant_v<SingleZc>);
static_assert(is_sq_entries_grant_v<SingleSqN8>);
static_assert(extract_sq_entries_v<SingleSqN8> == 8);
static_assert(is_zerocopy_grant_v<SingleZc>);

static_assert(has_engine_grant_v<SingleEngine, SingleSqN8>);
static_assert(has_sq_entries_grant_v<SingleEngine, SingleSqN8>);
static_assert(engine_is_io_uring_v<SingleEngine, SingleSqN8>);
static_assert(sq_entries_of_v<SingleEngine, SingleSqN8> == 8);
static_assert(sq_entries_is_pow2_v<SingleEngine, SingleSqN8>);
static_assert(cq_entries_is_pow2_or_default_v<SingleEngine, SingleSqN8>);

static_assert(has_duplicate_engine_v<SingleEngine, SingleSqN8, SingleEngine>);
static_assert(!has_duplicate_engine_v<SingleEngine, SingleSqN8>);
static_assert(has_duplicate_sq_entries_v<SingleEngine, SingleSqN8, SingleSqN8>);
static_assert(!has_duplicate_sq_entries_v<SingleEngine, SingleSqN8>);

using FIoPoll = ::crucible::fixy::grant::io::ring_flag<::crucible::fixy::io::ring_flag::IoPoll>;
using FSqPoll = ::crucible::fixy::grant::io::ring_flag<::crucible::fixy::io::ring_flag::SqPoll>;
static_assert(has_duplicate_ring_flag_v<FIoPoll, FIoPoll>);
static_assert(!has_duplicate_ring_flag_v<FIoPoll, FSqPoll>);
static_assert(fold_ring_flags<FIoPoll, FSqPoll>() == (IORING_SETUP_IOPOLL | IORING_SETUP_SQPOLL));

static_assert(pack_zerocopy_is_simple_transfer_v<SingleZc>);
static_assert(!pack_zerocopy_is_none_v<SingleZc>);

using ZcNone = ::crucible::fixy::grant::io::zerocopy<::crucible::fixy::io::zerocopy::None>;
static_assert(pack_zerocopy_is_none_v<ZcNone>);
static_assert(!pack_zerocopy_is_simple_transfer_v<ZcNone>);

using ZcSplice = ::crucible::fixy::grant::io::zerocopy<::crucible::fixy::io::zerocopy::Splice>;
static_assert(!pack_zerocopy_is_none_v<ZcSplice>);
static_assert(!pack_zerocopy_is_simple_transfer_v<ZcSplice>);

static_assert(std::is_default_constructible_v<::crucible::fixy::io::IoUringRing>);
static_assert(std::is_nothrow_move_constructible_v<::crucible::fixy::io::IoUringRing>);
static_assert(std::is_nothrow_move_assignable_v<::crucible::fixy::io::IoUringRing>);
static_assert(!std::is_copy_constructible_v<::crucible::fixy::io::IoUringRing>);
static_assert(!std::is_copy_assignable_v<::crucible::fixy::io::IoUringRing>);

static_assert(::crucible::fixy::grant::which_dim<SingleEngine>::value
              == ::crucible::safety::DimensionAxis::SyscallSurface);
static_assert(::crucible::fixy::grant::which_dim<SingleSqN8>::value
              == ::crucible::safety::DimensionAxis::SyscallSurface);
static_assert(::crucible::fixy::grant::which_dim<SingleZc>::value == ::crucible::safety::DimensionAxis::SyscallSurface);
static_assert(::crucible::fixy::grant::which_dim<FIoPoll>::value == ::crucible::safety::DimensionAxis::SyscallSurface);

}  // namespace crucible::fixy::io::detail
