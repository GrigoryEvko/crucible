#pragma once

// ─── crucible::safety::OwnedRegion<T, Tag> ───────────────────────────
//
// THE contiguous-buffer model that substitutes for Rust's borrow
// checker WITHOUT Rust's allocation fragmentation.
//
//   sizeof(OwnedRegion<T, Tag>) == sizeof(T*) + sizeof(size_t)
//                                                                 == 16 bytes
//   No virtual, no atomic, no allocation in any method.
//
// An OwnedRegion is a (T*, size_t) pair PLUS a phantom Permission<Tag>
// that proves exclusive ownership over a contiguous arena-backed
// buffer.  Same memory layout as std::span<T>, but the type system
// proves no other thread can read or write the bytes in [base, base+count)
// while this OwnedRegion is alive.
//
// ─── The structural inversion of Rust's allocation model ────────────
//
// In Rust:                                       In Crucible:
//   Vec<Box<T>>      → N+1 allocations            OwnedRegion<T, Tag>  → 1 allocation
//   Arc<Mutex<T>>    → atomic refcount + mutex    Permission<Tag>      → 0 bytes, compile-time
//   Rc<Vec<T>>       → refcount + vector          OwnedRegion          → 16 bytes total
//   par_iter()       → rayon thread pool          parallel_for_views<N> → stack jthreads
//   Iterator chains  → boxed dyn Iterator         std::span<T>          → native pointer
//
// The Permission tag carries the safety proof; the buffer is
// contiguous; partitioning slices the INDEX SPACE (not the
// allocation); workers iterate with zero indirection.  This is the
// strict opposite of fragmented allocation.
//
// ─── The Slice<Parent, I> partition pattern ─────────────────────────
//
// `split_into<N>()` partitions an OwnedRegion<T, Whole> into a
// std::tuple of N sub-regions, each tagged Slice<Whole, I> for
// distinct compile-time I in [0, N).  All sub-regions point INTO
// THE SAME ARENA BUFFER at consecutive chunk offsets — no copy, no
// allocation.  The Permission tags prove disjointness; the buffer is
// physically contiguous.
//
// One partial-specialization of splits_into_pack handles all N values
// via index-pack deduction:
//
//   template <typename Parent, std::size_t... Is>
//   struct splits_into_pack<Parent, Slice<Parent, Is>...>
//       : std::true_type {};
//
// permission_split_n then succeeds for any (Parent, N) pair without
// per-N user-side declaration.
//
// ─── Why this composes with everything else ─────────────────────────
//
// OwnedRegion is move-only by virtue of its embedded Permission's
// linearity (deleted copy with reason).  It plays cleanly with:
//
//   * Linear<>  — already linear; double-wrapping is redundant
//   * Refined<> — wrap to add value-level invariants over the buffer
//   * Tagged<>  — combine Tag with provenance/access markers
//   * Pinned<>  — handles holding an OwnedRegion typically Pinned
//   * Permission<>/SharedPermission — compose for exclusive vs shared
//
// And with the Workload primitives (safety/Workload.h):
//
//   * parallel_for_views<N>(region, body)
//   * parallel_reduce_views<N, R>(region, init, mapper, reducer)
//   * parallel_apply_pair<N>(rA, rB, body)
//
// All zero-allocation (jthreads stack-array; sub-regions are span
// subranges).  All zero user-level atomics (RAII join provides
// happens-before).

#include <crucible/Arena.h>
#include <crucible/Effects.h>
#include <crucible/Platform.h>
#include <crucible/safety/Permission.h>

#include <array>
#include <cstddef>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// ── Slice<Parent, I> — phantom slice tag ─────────────────────────────
//
// One distinct type per (Parent, I) pair.  Carries no data; pure
// compile-time identity.  The splits_into_pack partial specialization
// below proves disjointness for the Slice<Parent, 0..N-1> pattern.

template <typename Parent, std::size_t I>
struct Slice {
    static constexpr std::size_t index = I;
    using parent_type = Parent;
};

// One partial specialization handles ALL N values via index-pack
// deduction.  permission_split_n<Slice<Parent, Is>...> succeeds for
// any (Parent, sizeof...(Is)) pair without per-N user-side work.
template <typename Parent, std::size_t... Is>
struct splits_into_pack<Parent, Slice<Parent, Is>...>
    : std::true_type {};

// ── OwnedRegion<T, Tag> ──────────────────────────────────────────────

template <typename T, typename Tag>
class [[nodiscard]] OwnedRegion {
    T*          base_  = nullptr;
    std::size_t count_ = 0;
    [[no_unique_address]] Permission<Tag> perm_;

    // Private constructor — only friended factories may construct.
    constexpr OwnedRegion(T* base, std::size_t count,
                          Permission<Tag>&& p) noexcept
        : base_{base}, count_{count}, perm_{std::move(p)} {}

    // Allow split_into to construct sub-regions of different Slice<...> types.
    template <typename U, typename UTag>
    friend class OwnedRegion;

    // parallel_for_views and friends in safety/Workload.h need to
    // reconstruct the parent OwnedRegion after worker join.  The
    // helper rebuild_parent_ takes a (base, count) pair plus a
    // freshly-rebuilt Permission and constructs the OwnedRegion.
    template <typename Parent>
    static OwnedRegion<T, Parent> rebuild_parent_(T* base, std::size_t count) noexcept {
        return OwnedRegion<T, Parent>{base, count, permission_fork_rebuild_<Parent>()};
    }

    // Friend the Workload primitives so they may rebuild parents.
    template <std::size_t N, typename U, typename Whole, typename Body>
    friend OwnedRegion<U, Whole>
    parallel_for_views(OwnedRegion<U, Whole>&&, Body) noexcept;

    template <std::size_t N, typename R, typename U, typename Whole,
              typename Mapper, typename Reducer>
    friend std::pair<R, OwnedRegion<U, Whole>>
    parallel_reduce_views(OwnedRegion<U, Whole>&&, R, Mapper, Reducer) noexcept;

public:
    using value_type = T;
    using tag_type   = Tag;

    // Move-only by virtue of Permission's linearity.
    OwnedRegion(const OwnedRegion&)
        = delete("OwnedRegion owns a Permission — copy would duplicate the linear token");
    OwnedRegion& operator=(const OwnedRegion&)
        = delete("OwnedRegion owns a Permission — assignment would overwrite the linear token");
    constexpr OwnedRegion(OwnedRegion&&) noexcept            = default;
    constexpr OwnedRegion& operator=(OwnedRegion&&) noexcept = default;
    ~OwnedRegion() = default;

    // ── Static factories ──────────────────────────────────────────

    // adopt(arena, count, perm) — single arena bump-pointer alloc, then
    // wrap with the consumed Permission.  Caller proves exclusive
    // ownership by surrendering the Permission token.
    //
    // count == 0: returns OwnedRegion with base_ == nullptr, count_ == 0.
    [[nodiscard]] static OwnedRegion adopt(
        fx::Alloc alloc_token,
        Arena& arena,
        std::size_t count,
        Permission<Tag>&& perm) noexcept
    {
        T* base = (count == 0) ? nullptr
                               : arena.alloc_array<T>(alloc_token, count);
        return OwnedRegion{base, count, std::move(perm)};
    }

    // wrap(base, count, perm) — wrap an externally-allocated buffer.
    // Caller is responsible for buffer lifetime; the OwnedRegion does
    // NOT own the storage (just the Permission proof).  Useful for
    // migration: existing (T*, size_t) pairs can be wrapped without
    // re-allocation.
    [[nodiscard]] static OwnedRegion wrap(
        T* base, std::size_t count, Permission<Tag>&& perm) noexcept
    {
        return OwnedRegion{base, count, std::move(perm)};
    }

    // ── Views (zero indirection over contiguous bytes) ────────────

    [[nodiscard]] constexpr std::span<T> span() noexcept {
        return std::span<T>{base_, count_};
    }
    [[nodiscard]] constexpr std::span<T const> cspan() const noexcept {
        return std::span<T const>{base_, count_};
    }

    // ── Accessors ─────────────────────────────────────────────────

    [[nodiscard]] constexpr T*          data()       noexcept { return base_; }
    [[nodiscard]] constexpr T const*    data() const noexcept { return base_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }
    [[nodiscard]] constexpr bool        empty() const noexcept { return count_ == 0; }

    // Range-based for support — iteration over native pointer.
    [[nodiscard]] constexpr T*       begin()       noexcept { return base_; }
    [[nodiscard]] constexpr T*       end()         noexcept { return base_ + count_; }
    [[nodiscard]] constexpr T const* begin() const noexcept { return base_; }
    [[nodiscard]] constexpr T const* end()   const noexcept { return base_ + count_; }

    // ── Partition (index-space, not memory-space) ─────────────────
    //
    // split_into<N>() && yields a std::tuple<OwnedRegion<T, Slice<Tag, I>>...>
    // where each sub-region points INTO THE SAME arena buffer at a
    // distinct chunk offset.  No copy, no allocation.  The Permission
    // tags prove the slices don't overlap.
    //
    // Heterogeneous types (each I is a distinct Slice<Tag, I>) require
    // std::tuple — std::array is homogeneous-only and won't work here.
    // Structured bindings on the tuple destructure cleanly:
    //
    //   auto [s0, s1, s2, s3] = std::move(region).split_into<4>();
    //
    // For very large N where std::tuple destructuring becomes verbose,
    // consider parallel_for_views<N> which manages the tuple internally.
    //
    // Chunk math: chunk_size = ceil(count / N).  For count = 1000, N = 8:
    //   chunks 0..6 have 125 elements, chunk 7 has 125 elements (1000/8).
    // For count = 1001, N = 8:
    //   chunk = 126, chunks 0..6 have 126 elements (882 total),
    //   chunk 7 has 119 elements (1001 - 882).
    // For count = 5, N = 8:
    //   chunk = 1, chunks 0..4 have 1 element, chunks 5..7 are empty.
    template <std::size_t N>
    [[nodiscard]] auto split_into() && noexcept;

private:
    // Index-sequence-driven implementation of split_into<N>.
    template <std::size_t N, std::size_t... Is>
    auto split_into_impl_(std::index_sequence<Is...>) && noexcept;

    // Per-shard chunk math — returns (start_offset, length) for shard I.
    static constexpr std::pair<std::size_t, std::size_t>
    chunk_range_(std::size_t total, std::size_t n, std::size_t i) noexcept {
        if (n == 0) return {0, 0};
        const std::size_t chunk = (total + n - 1) / n;
        const std::size_t start = i * chunk;
        if (start >= total) return {start, 0};
        const std::size_t end_ = (i + 1) * chunk;
        const std::size_t bound = (end_ > total) ? total : end_;
        return {start, bound - start};
    }
};

// ── split_into<N> — out-of-line definition ──────────────────────────

template <typename T, typename Tag>
template <std::size_t N>
auto OwnedRegion<T, Tag>::split_into() && noexcept {
    static_assert(N > 0, "split_into<N>() requires N > 0");
    return std::move(*this).template split_into_impl_<N>(
        std::make_index_sequence<N>{});
}

template <typename T, typename Tag>
template <std::size_t N, std::size_t... Is>
auto OwnedRegion<T, Tag>::split_into_impl_(std::index_sequence<Is...>) && noexcept {
    static_assert(sizeof...(Is) == N, "index_sequence size mismatch");

    // Snapshot base+count BEFORE consuming the permission.  After
    // permission_split_n the parent's `perm_` is moved-from but base_/
    // count_ remain readable until *this destructs.
    T*                base  = base_;
    const std::size_t total = count_;

    // Split the parent permission into N child Slice<Tag, I> permissions.
    auto sub_perms = permission_split_n<Slice<Tag, Is>...>(std::move(perm_));

    // Construct one OwnedRegion<T, Slice<Tag, I>> per shard, pointing
    // into the same buffer at chunk offsets.
    return std::tuple<OwnedRegion<T, Slice<Tag, Is>>...>{
        OwnedRegion<T, Slice<Tag, Is>>{
            base + chunk_range_(total, N, Is).first,
            chunk_range_(total, N, Is).second,
            std::move(std::get<Is>(sub_perms))
        }...
    };
}

// ── Zero-cost guarantees ────────────────────────────────────────────

namespace detail {
    struct owned_region_test_tag {};
}

// sizeof = (T*) + (size_t).  Permission EBO-collapses to 0.
static_assert(sizeof(OwnedRegion<int, detail::owned_region_test_tag>) ==
              sizeof(int*) + sizeof(std::size_t),
              "OwnedRegion<T, Tag> must be exactly (T*, size_t); Permission EBO-collapses");

// Move-only.
static_assert(!std::is_copy_constructible_v<OwnedRegion<int, detail::owned_region_test_tag>>);
static_assert(std::is_move_constructible_v<OwnedRegion<int, detail::owned_region_test_tag>>);
static_assert(std::is_nothrow_move_constructible_v<OwnedRegion<int, detail::owned_region_test_tag>>);

// Slice<> tag is a 1-byte empty class.
static_assert(sizeof(Slice<detail::owned_region_test_tag, 0>) == 1);
static_assert(std::is_trivially_destructible_v<Slice<detail::owned_region_test_tag, 0>>);

// splits_into_pack auto-specialization.
static_assert(splits_into_pack_v<
    detail::owned_region_test_tag,
    Slice<detail::owned_region_test_tag, 0>,
    Slice<detail::owned_region_test_tag, 1>,
    Slice<detail::owned_region_test_tag, 2>,
    Slice<detail::owned_region_test_tag, 3>>,
    "Slice<Parent, 0..N-1> must auto-specialize splits_into_pack");

}  // namespace crucible::safety
