#pragma once

// A pointer and a count over one contiguous buffer, carried together
// with a Permission that proves exclusive ownership of the bytes in
// [base, base + count) for as long as the region is alive.
//
// split_into partitions the index space, not the allocation.  Every
// sub-region points into the same buffer at a distinct chunk offset,
// and the distinct Slice tags are what prove the chunks are disjoint.
//
// The surrendered Permission is the one door: the constructor is
// private, and adopt and wrap each take a token by rvalue.  The old
// header also befriended the three structured-parallel helpers of
// safety/Workload.h so they could rebuild the parent region after a
// join through a private static.  Those helpers are not ported, and a
// friend naming an absent function is an open door, so the friends
// and the static are gone.  A join rebuilds the parent by surrendering
// the shards to `recombine`, which combines their Slice permissions
// back into the parent's.
//
// It briefly did so through `rebuild_parent_after_fork_<Whole>()`
// instead.  That helper took no argument, so it proved nothing, and it
// minted a Permission for any tag from any translation unit.  It is
// gone (#169).  A rebuild has to consume the thing it reissues.
//
// Old spelling: include/crucible/safety/OwnedRegion.h, the detection
// surface of include/crucible/safety/IsOwnedRegion.h and the Slice half
// of include/crucible/safety/PermissionTreeGenerator.h.

#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <meta>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fixy {

// A generated tag tree: the shards of a parent, indexed.  Index-pack
// deduction covers every arity in one specialization, so a caller
// splitting a parent into N shards declares nothing per N.  It comes
// from safety/PermissionTreeGenerator.h, which is not ported; the
// remainder of that header (auto_split_n, can_split_n_v) has no
// consumer here.
template <typename Parent, std::size_t I>
struct Slice {
    using parent_type = Parent;
    static constexpr std::size_t index = I;
};

}  // namespace fixy

namespace foundation::permissions {

template <typename Parent, std::size_t... Is>
struct splits_into_pack<Parent, ::fixy::Slice<Parent, Is>...> : std::true_type {};

// Specialize this witness in lockstep with the specialization above.
template <typename Parent, std::size_t... Is>
struct splits_into_pack_authoring_witness<Parent, ::fixy::Slice<Parent, Is>...> : std::true_type {};

}  // namespace foundation::permissions

namespace fixy {

// The old signature named the crucible Arena, which this layer cannot.
// What adopt reads of it is one member template, so that is the whole
// requirement: alloc_array<T>(token, count) returning T*, with a null
// result for a zero count left to adopt itself.
template <typename Allocator, typename T>
concept ArrayArena = requires(Allocator& arena, ::foundation::effects::Alloc token, std::size_t count) {
    { arena.template alloc_array<T>(token, count) } -> std::same_as<T*>;
};

template <typename T, typename Tag>
class [[nodiscard]] OwnedRegion {
    T* base_ = nullptr;
    std::size_t count_ = 0;
    [[no_unique_address]] ::foundation::permissions::Permission<Tag> perm_;

    constexpr OwnedRegion(T* base, std::size_t count, ::foundation::permissions::Permission<Tag>&& p) noexcept
        : base_{base}, count_{count}, perm_{std::move(p)} {}

    // split_into builds sub-regions whose tag differs from its own.
    template <typename U, typename UTag>
    friend class OwnedRegion;

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
    template <typename Allocator>
        requires ArrayArena<Allocator, T>
    [[nodiscard]] static OwnedRegion adopt(::foundation::effects::Alloc alloc_token, Allocator& arena,
                                           std::size_t count,
                                           ::foundation::permissions::Permission<Tag>&& perm) noexcept {
        T* base = (count == 0) ? nullptr : arena.template alloc_array<T>(alloc_token, count);
        return OwnedRegion{base, count, std::move(perm)};
    }

    // Wraps storage allocated elsewhere.  The region owns the
    // Permission proof but not the storage, so the caller keeps
    // responsibility for the buffer's lifetime.
    [[nodiscard]] static OwnedRegion wrap(T* base, std::size_t count,
                                          ::foundation::permissions::Permission<Tag>&& perm) noexcept {
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

    // The inverse of split_into.  Every shard is surrendered here, and
    // their Slice permissions are combined back into the parent's, so
    // the shards themselves are the proof that the whole is exclusively
    // owned again.  mint_permission_combine_n checks that the shard tags
    // mirror a declared splits_into_pack, so a tuple assembled from
    // somewhere other than a real split does not combine.
    //
    // This is the only way to recover a parent permission after a split.
    // There is deliberately no nullary rebuild: one that took no
    // argument would prove nothing, and the previous such helper minted
    // a Permission for any tag from any translation unit (#169).
    template <std::size_t... Is>
    [[nodiscard]] static OwnedRegion recombine(std::tuple<OwnedRegion<T, Slice<Tag, Is>>...>&& shards) noexcept {
        static_assert(sizeof...(Is) > 0, "recombine() needs at least one shard.");
        // Shard 0 starts at offset 0, so its base is the whole's base.
        T* const base = std::get<0>(shards).base_;
        std::size_t const total = (std::size_t{0} + ... + std::get<Is>(shards).count_);
        return OwnedRegion{
            base, total,
            ::foundation::permissions::mint_permission_combine_n<Tag>(std::move(std::get<Is>(shards).perm_)...)};
    }

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

    auto sub_perms = ::foundation::permissions::mint_permission_split_n<Slice<Tag, Is>...>(std::move(perm_));

    return std::tuple<OwnedRegion<T, Slice<Tag, Is>>...>{
        OwnedRegion<T, Slice<Tag, Is>>{base + chunk_range_(total, N, Is).first, chunk_range_(total, N, Is).second,
                                       std::move(std::get<Is>(sub_perms))}...};
}

// The detection surface of the old IsOwnedRegion.h.  One reflection
// query answers it, and the associated types are read off the
// wrapper's own typedefs.  The two extractors are constrained rather
// than left to a primary template, which would hand back void for an
// unrelated argument instead of failing.

template <typename T>
inline constexpr bool is_owned_region_v = ::foundation::reflect::is_instance_of_v<T, ^^OwnedRegion>;

template <typename T>
concept IsOwnedRegion = is_owned_region_v<T>;

template <typename T>
    requires is_owned_region_v<T>
using owned_region_value_t = typename std::remove_cvref_t<T>::value_type;

template <typename T>
    requires is_owned_region_v<T>
using owned_region_tag_t = typename std::remove_cvref_t<T>::tag_type;

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
static_assert(std::is_empty_v<Slice<detail::owned_region_test_tag, 0>>);
static_assert(!std::is_same_v<Slice<detail::owned_region_test_tag, 0>, Slice<detail::owned_region_test_tag, 1>>);
static_assert(std::is_same_v<Slice<detail::owned_region_test_tag, 5>::parent_type, detail::owned_region_test_tag>);
static_assert(Slice<detail::owned_region_test_tag, 5>::index == 5);

static_assert(
    ::foundation::permissions::splits_into_pack_v<
        detail::owned_region_test_tag, Slice<detail::owned_region_test_tag, 0>, Slice<detail::owned_region_test_tag, 1>,
        Slice<detail::owned_region_test_tag, 2>, Slice<detail::owned_region_test_tag, 3>>,
    "Slice<Parent, 0..N-1> must auto-specialize splits_into_pack");
static_assert(::foundation::permissions::well_authored_split_pack_v<detail::owned_region_test_tag,
                                                                    Slice<detail::owned_region_test_tag, 0>>,
              "Slice<Parent, 0..N-1> must ship the authoring witness beside the manifest");

namespace detail::owned_region_self_test {

struct smoke_tag {};
struct test_tag_a {};
struct test_tag_b {};

using OR_int_a = OwnedRegion<int, test_tag_a>;
using OR_double_a = OwnedRegion<double, test_tag_a>;
using OR_int_b = OwnedRegion<int, test_tag_b>;

static_assert(is_owned_region_v<OR_int_a>);
static_assert(is_owned_region_v<OR_double_a>);
static_assert(is_owned_region_v<OR_int_b>);

static_assert(is_owned_region_v<OR_int_a&>);
static_assert(is_owned_region_v<OR_int_a&&>);
static_assert(is_owned_region_v<OR_int_a const&>);
static_assert(is_owned_region_v<OR_int_a const>);
static_assert(is_owned_region_v<OR_int_a const&&>);

static_assert(!is_owned_region_v<int>);
static_assert(!is_owned_region_v<int*>);
static_assert(!is_owned_region_v<int&>);
static_assert(!is_owned_region_v<void>);
static_assert(!is_owned_region_v<test_tag_a>);

struct LookalikeRegion {
    int* base;
    std::size_t count;
};
static_assert(!is_owned_region_v<LookalikeRegion>);

static_assert(IsOwnedRegion<OR_int_a>);
static_assert(IsOwnedRegion<OR_int_a&&>);
static_assert(!IsOwnedRegion<int>);

static_assert(std::is_same_v<owned_region_value_t<OR_int_a>, int>);
static_assert(std::is_same_v<owned_region_value_t<OR_double_a>, double>);
static_assert(std::is_same_v<owned_region_tag_t<OR_int_a>, test_tag_a>);
static_assert(std::is_same_v<owned_region_tag_t<OR_int_b>, test_tag_b>);

static_assert(std::is_same_v<owned_region_value_t<OR_int_a const&>, int>);
static_assert(std::is_same_v<owned_region_tag_t<OR_int_a&&>, test_tag_a>);

static_assert(std::is_same_v<owned_region_value_t<OR_int_a>, owned_region_value_t<OR_int_b>>);
static_assert(!std::is_same_v<owned_region_tag_t<OR_int_a>, owned_region_tag_t<OR_int_b>>);

// The smallest thing adopt asks of an arena: one bump pointer over a
// fixed block.  The old smoke test used the crucible Arena, which this
// layer cannot name.
class BumpArena {
    alignas(std::max_align_t) unsigned char block_[4096]{};
    std::size_t used_ = 0;

public:
    template <typename T>
    [[nodiscard]] T* alloc_array(::foundation::effects::Alloc, std::size_t n) noexcept {
        if (n == 0) return nullptr;
        const std::size_t misalign = used_ % alignof(T);
        const std::size_t start = misalign == 0 ? used_ : used_ + (alignof(T) - misalign);
        const std::size_t nbytes = n * sizeof(T);
        if (start + nbytes > sizeof(block_)) std::abort();
        used_ = start + nbytes;
        return std::start_lifetime_as_array<T>(block_ + start, n);
    }
};
static_assert(ArrayArena<BumpArena, int>);
static_assert(!ArrayArena<int, int>);

inline void runtime_smoke_test() {
    int seed = 7;
    ::foundation::effects::Alloc alloc_token{};
    BumpArena arena{};

    auto perm = ::foundation::permissions::mint_permission_root<smoke_tag>();
    auto region =
        OwnedRegion<int, smoke_tag>::adopt(alloc_token, arena, static_cast<std::size_t>(seed + 1), std::move(perm));
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
    auto wrap_perm = ::foundation::permissions::mint_permission_root<smoke_tag>();
    auto wrapped = OwnedRegion<int, smoke_tag>::wrap(storage, 3u, std::move(wrap_perm));
    if (wrapped.size() != 3u) std::abort();
    if (wrapped.data() != storage) std::abort();
    if (wrapped.data()[2] != seed + 2) std::abort();

    auto empty_perm = ::foundation::permissions::mint_permission_root<smoke_tag>();
    auto empty = OwnedRegion<int, smoke_tag>::adopt(alloc_token, arena, 0u, std::move(empty_perm));
    if (!empty.empty()) std::abort();
    if (empty.data() != nullptr) std::abort();
    if (empty.size() != 0u) std::abort();

    // The volatile bound defeats constant folding, so the trait reads
    // survive dead-code elimination.
    volatile std::size_t const cap = 4;
    for (std::size_t i = 0; i < cap; ++i) {
        if (!is_owned_region_v<OR_int_a>) std::abort();
        if (is_owned_region_v<int>) std::abort();
        if (!IsOwnedRegion<OR_int_a&&>) std::abort();
    }
}

}  // namespace detail::owned_region_self_test

}  // namespace fixy
