#pragma once

// A pointer and a count over one contiguous buffer, carried together
// with a Permission that proves exclusive ownership of the bytes in
// [base, base + count) for as long as the region is alive.
//
// split_into partitions the index space, not the allocation.  Every
// sub-region points into the same buffer at a distinct chunk offset,
// and the distinct Slice tags are what prove the chunks are disjoint.

#include <crucible/Arena.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/Platform.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/PermissionTreeGenerator.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace crucible::safety {

template <typename T, typename Tag>
class [[nodiscard]] OwnedRegion {
    T* base_ = nullptr;
    std::size_t count_ = 0;
    [[no_unique_address]] Permission<Tag> perm_;

    constexpr OwnedRegion(T* base, std::size_t count, Permission<Tag>&& p) noexcept
        : base_{base}, count_{count}, perm_{std::move(p)} {}

    // split_into builds sub-regions whose tag differs from its own.
    template <typename U, typename UTag>
    friend class OwnedRegion;

    // Reconstructs the parent after the workers join.  Producing a
    // post-join parent permission is passkey gated, and this is one of
    // the two places allowed to do it.
    template <typename Parent>
    static OwnedRegion<T, Parent> rebuild_parent_(T* base, std::size_t count) noexcept {
        return OwnedRegion<T, Parent>{base, count, ::crucible::safety::detail::rebuild_parent_after_fork_<Parent>()};
    }

    template <std::size_t N, typename U, typename Whole, typename Body>
    friend OwnedRegion<U, Whole> parallel_for_views(OwnedRegion<U, Whole>&&, Body) noexcept;

    template <std::size_t N, typename R, typename U, typename Whole, typename Mapper, typename Reducer>
    friend std::pair<R, OwnedRegion<U, Whole>> parallel_reduce_views(OwnedRegion<U, Whole>&&, R, Mapper,
                                                                     Reducer) noexcept;

    template <std::size_t N, typename T1, typename W1, typename T2, typename W2, typename Body>
    friend std::pair<OwnedRegion<T1, W1>, OwnedRegion<T2, W2>>
    parallel_apply_pair(OwnedRegion<T1, W1>&&, OwnedRegion<T2, W2>&&, Body) noexcept;

public:
    using value_type = T;
    using tag_type = Tag;

    OwnedRegion(const OwnedRegion&) = delete("OwnedRegion owns a Permission — copy would duplicate the linear token");
    OwnedRegion& operator=(const OwnedRegion&) =
        delete("OwnedRegion owns a Permission — assignment would overwrite the linear token");
    constexpr OwnedRegion(OwnedRegion&&) noexcept = default;
    constexpr OwnedRegion& operator=(OwnedRegion&&) noexcept = default;
    ~OwnedRegion() = default;

    // The caller proves exclusive ownership by surrendering the
    // Permission token.
    [[nodiscard]] static OwnedRegion adopt(effects::Alloc alloc_token, Arena& arena, std::size_t count,
                                           Permission<Tag>&& perm) noexcept {
        T* base = (count == 0) ? nullptr : arena.alloc_array<T>(alloc_token, count);
        return OwnedRegion{base, count, std::move(perm)};
    }

    // Wraps storage allocated elsewhere.  The region owns the
    // Permission proof but not the storage, so the caller keeps
    // responsibility for the buffer's lifetime.
    [[nodiscard]] static OwnedRegion wrap(T* base, std::size_t count, Permission<Tag>&& perm) noexcept {
        return OwnedRegion{base, count, std::move(perm)};
    }

    [[nodiscard]] constexpr std::span<T> span() noexcept { return std::span<T>{base_, count_}; }
    [[nodiscard]] constexpr std::span<T const> cspan() const noexcept { return std::span<T const>{base_, count_}; }

    [[nodiscard]] constexpr T* data() noexcept { return base_; }
    [[nodiscard]] constexpr T const* data() const noexcept { return base_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }

    [[nodiscard]] constexpr T* begin() noexcept { return base_; }
    [[nodiscard]] constexpr T* end() noexcept { return base_ + count_; }
    [[nodiscard]] constexpr T const* begin() const noexcept { return base_; }
    [[nodiscard]] constexpr T const* end() const noexcept { return base_ + count_; }

    // The result is a tuple and not an array because each shard has a
    // distinct Slice tag, so the element types differ.
    template <std::size_t N>
    [[nodiscard]] auto split_into() && noexcept;

private:
    template <std::size_t N, std::size_t... Is>
    auto split_into_impl_(std::index_sequence<Is...>) && noexcept;

    // Returns the start offset and the length of shard i.
    static constexpr std::pair<std::size_t, std::size_t> chunk_range_(std::size_t total, std::size_t n,
                                                                      std::size_t i) noexcept {
        if (n == 0) return {0, 0};
        const std::size_t chunk = (total + n - 1) / n;
        const std::size_t start = i * chunk;
        if (start >= total) return {start, 0};
        const std::size_t end_ = (i + 1) * chunk;
        const std::size_t bound = (end_ > total) ? total : end_;
        return {start, bound - start};
    }
};

template <typename T, typename Tag>
template <std::size_t N>
auto OwnedRegion<T, Tag>::split_into() && noexcept {
    static_assert(N > 0, "split_into<N>() requires N > 0");
    return std::move(*this).template split_into_impl_<N>(std::make_index_sequence<N>{});
}

template <typename T, typename Tag>
template <std::size_t N, std::size_t... Is>
auto OwnedRegion<T, Tag>::split_into_impl_(std::index_sequence<Is...>) && noexcept {
    static_assert(sizeof...(Is) == N, "index_sequence size mismatch");

    // Snapshot the base and the count before the permission is
    // consumed.  The split leaves perm_ moved-from, while base_ and
    // count_ stay readable until this object is destroyed.
    T* base = base_;
    const std::size_t total = count_;

    auto sub_perms = mint_permission_split_n<Slice<Tag, Is>...>(std::move(perm_));

    return std::tuple<OwnedRegion<T, Slice<Tag, Is>>...>{
        OwnedRegion<T, Slice<Tag, Is>>{base + chunk_range_(total, N, Is).first, chunk_range_(total, N, Is).second,
                                       std::move(std::get<Is>(sub_perms))}...};
}

namespace detail {
struct owned_region_test_tag {};
}  // namespace detail

static_assert(sizeof(OwnedRegion<int, detail::owned_region_test_tag>) == sizeof(int*) + sizeof(std::size_t),
              "OwnedRegion<T, Tag> must be exactly (T*, size_t); Permission EBO-collapses");

static_assert(!std::is_copy_constructible_v<OwnedRegion<int, detail::owned_region_test_tag>>);
static_assert(std::is_move_constructible_v<OwnedRegion<int, detail::owned_region_test_tag>>);
static_assert(std::is_nothrow_move_constructible_v<OwnedRegion<int, detail::owned_region_test_tag>>);

static_assert(sizeof(Slice<detail::owned_region_test_tag, 0>) == 1);
static_assert(std::is_trivially_destructible_v<Slice<detail::owned_region_test_tag, 0>>);

static_assert(splits_into_pack_v<detail::owned_region_test_tag, Slice<detail::owned_region_test_tag, 0>,
                                 Slice<detail::owned_region_test_tag, 1>, Slice<detail::owned_region_test_tag, 2>,
                                 Slice<detail::owned_region_test_tag, 3>>,
              "Slice<Parent, 0..N-1> must auto-specialize splits_into_pack");

namespace detail::owned_region_self_test {

struct smoke_tag {};

inline void runtime_smoke_test() {
    int seed = 7;
    auto test_ctx = effects::testing::test();
    Arena arena{};

    auto perm = mint_permission_root<smoke_tag>();
    auto region =
        OwnedRegion<int, smoke_tag>::adopt(test_ctx.alloc, arena, static_cast<std::size_t>(seed + 1), std::move(perm));
    if (region.size() != 8u) std::abort();
    if (region.empty()) std::abort();
    if (region.data() == nullptr) std::abort();

    for (std::size_t i = 0; i < region.size(); ++i) {
        region.data()[i] = static_cast<int>(i) * seed;
    }
    int sum = 0;
    for (int v : region)
        sum += v;
    if (sum != (0 + 1 + 2 + 3 + 4 + 5 + 6 + 7) * seed) std::abort();

    std::span<int> view = region.span();
    if (view.size() != 8u) std::abort();
    if (view.data() != region.data()) std::abort();
    std::span<int const> cview = region.cspan();
    if (cview.size() != 8u) std::abort();

    auto shards = std::move(region).split_into<4>();
    auto& s0 = std::get<0>(shards);
    auto& s1 = std::get<1>(shards);
    auto& s2 = std::get<2>(shards);
    auto& s3 = std::get<3>(shards);
    if (s0.size() != 2u || s1.size() != 2u || s2.size() != 2u || s3.size() != 2u) std::abort();
    if (s0.data()[0] != 0 || s0.data()[1] != seed) std::abort();
    if (s3.data()[1] != 7 * seed) std::abort();

    int storage[3] = {seed, seed + 1, seed + 2};
    auto wrap_perm = mint_permission_root<smoke_tag>();
    auto wrapped = OwnedRegion<int, smoke_tag>::wrap(storage, 3u, std::move(wrap_perm));
    if (wrapped.size() != 3u) std::abort();
    if (wrapped.data() != storage) std::abort();
    if (wrapped.data()[2] != seed + 2) std::abort();

    auto empty_perm = mint_permission_root<smoke_tag>();
    auto empty = OwnedRegion<int, smoke_tag>::adopt(test_ctx.alloc, arena, 0u, std::move(empty_perm));
    if (!empty.empty()) std::abort();
    if (empty.data() != nullptr) std::abort();
    if (empty.size() != 0u) std::abort();
}

}  // namespace detail::owned_region_self_test

}  // namespace crucible::safety
