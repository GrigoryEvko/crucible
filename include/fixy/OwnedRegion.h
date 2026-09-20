#pragma once

// A pointer and a count over one contiguous buffer, carried together
// with a Permission that proves exclusive ownership of the bytes in
// [base, base + count) for as long as the region is alive.
//
// The region carries the brand of the permission it was built from, so
// the region's identity is the permission's: a borrow of this region
// carries the same brand, and a borrow of another region of the same
// tag cannot stand in for it.  A region built from a permission on the
// erased identity is itself erased.  foundation/Brand.h states the
// facts a brand rests on.
//
// split_into partitions the index space, not the allocation.  Every
// sub-region points into the same buffer at a distinct chunk offset,
// and the distinct Slice tags are what prove the chunks are disjoint.
// The shards carry the parent's brand, and recombine demands it back.
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
// gone.  A rebuild has to consume the thing it reissues.
//
// Old spelling: include/crucible/safety/OwnedRegion.h, the detection
// surface of include/crucible/safety/IsOwnedRegion.h and the Slice half
// of include/crucible/safety/PermissionTreeGenerator.h.

#include <fixy/Borrowed.h>
#include <foundation/Brand.h>
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
//
// parent_type is also how a shard finds its effect row: the row
// relation reads a derived tag's parent, so a shard touches the region
// under the same row as the whole.
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

template <typename T, typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
class OwnedRegion;

// The branded doors.  Each takes the permission by rvalue and hands
// back a region of that permission's brand, so `mint_owned_region(base,
// count, std::move(perm))` is the whole spelling and the brand is never
// written.  The static members adopt and wrap on the class are the
// erased doors: they take a permission of the class's own brand, and a
// class spelled without one is on the erased identity.
template <typename T, typename Tag, typename Brand>
    requires std::is_object_v<T>
[[nodiscard]] constexpr OwnedRegion<T, Tag, Brand>
mint_owned_region(T* base, std::size_t count, ::foundation::permissions::Permission<Tag, Brand>&& perm) noexcept;

template <typename T, typename Allocator, typename Tag, typename Brand>
    requires ArrayArena<Allocator, T>
[[nodiscard]] OwnedRegion<T, Tag, Brand> mint_owned_region(  // MINT-PATTERN-OK: allocating
    ::foundation::effects::Alloc alloc_token, Allocator& arena, std::size_t count,
    ::foundation::permissions::Permission<Tag, Brand>&& perm) noexcept;

template <typename T, typename Tag, typename Brand>
class [[nodiscard]] OwnedRegion {
    static_assert(::foundation::brand::IsBrand<Brand>, "OwnedRegion<T, Tag, Brand>: Brand must be an empty class "
                                                       "type: the brand of the permission the region was built "
                                                       "from, or DefaultBrand.");

    T* base_ = nullptr;
    std::size_t count_ = 0;
    [[no_unique_address]] ::foundation::permissions::Permission<Tag, Brand> perm_;

    constexpr OwnedRegion(T* base, std::size_t count, ::foundation::permissions::Permission<Tag, Brand>&& p) noexcept
        : base_{base}, count_{count}, perm_{std::move(p)} {}

    // split_into builds sub-regions whose tag differs from its own.
    template <typename U, typename UTag, typename UBrand>
    friend class OwnedRegion;

    template <typename U, typename UTag, typename UBrand>
        requires std::is_object_v<U>
    friend constexpr OwnedRegion<U, UTag, UBrand>
    mint_owned_region(U* base, std::size_t count, ::foundation::permissions::Permission<UTag, UBrand>&& perm) noexcept;

    template <typename U, typename Allocator, typename UTag, typename UBrand>
        requires ArrayArena<Allocator, U>
    friend OwnedRegion<U, UTag, UBrand> mint_owned_region(::foundation::effects::Alloc alloc_token, Allocator& arena,
                                                          std::size_t count,
                                                          ::foundation::permissions::Permission<UTag, UBrand>&& perm) noexcept;

public:
    using value_type = T;
    using tag_type = Tag;
    using brand_type = Brand;

    OwnedRegion(const OwnedRegion&) = delete("OwnedRegion owns a Permission — copy would duplicate the linear token");
    OwnedRegion& operator=(const OwnedRegion&) =
        delete("OwnedRegion owns a Permission — assignment would overwrite the linear token");
    constexpr OwnedRegion(OwnedRegion&&) noexcept = default;
    constexpr OwnedRegion& operator=(OwnedRegion&&) noexcept = default;
    ~OwnedRegion() = default;

    // Erasure, one way only: a region of one instance becomes a region
    // on the erased identity, consuming the branded one.  Nothing gives
    // an erased region a brand.
    template <typename Other>
        requires(std::is_same_v<Brand, ::foundation::brand::DefaultBrand> && ::foundation::brand::IsFreshBrand<Other>)
    constexpr OwnedRegion(OwnedRegion<T, Tag, Other>&& other) noexcept
        : base_{other.base_}, count_{other.count_}, perm_{std::move(other.perm_)} {}

    // The caller proves exclusive ownership by surrendering the
    // Permission token.
    template <typename Allocator>
        requires ArrayArena<Allocator, T>
    [[nodiscard]] static OwnedRegion adopt(::foundation::effects::Alloc alloc_token, Allocator& arena,
                                           std::size_t count,
                                           ::foundation::permissions::Permission<Tag, Brand>&& perm) noexcept {
        T* base = (count == 0) ? nullptr : arena.template alloc_array<T>(alloc_token, count);
        return OwnedRegion{base, count, std::move(perm)};
    }

    // Wraps storage allocated elsewhere.  The region owns the
    // Permission proof but not the storage, so the caller keeps
    // responsibility for the buffer's lifetime.
    [[nodiscard]] static OwnedRegion wrap(T* base, std::size_t count,
                                          ::foundation::permissions::Permission<Tag, Brand>&& perm) noexcept {
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
    // mirror a declared splits_into_pack, and that every shard carries
    // this region's brand, so a tuple assembled from somewhere other
    // than a real split of this region does not combine.
    //
    // This is the only way to recover a parent permission after a split.
    // There is deliberately no nullary rebuild: one that took no
    // argument would prove nothing, and the previous such helper minted
    // a Permission for any tag from any translation unit.
    template <std::size_t... Is>
    [[nodiscard]] static OwnedRegion
    recombine(std::tuple<OwnedRegion<T, Slice<Tag, Is>, Brand>...>&& shards) noexcept {
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

template <typename T, typename Tag, typename Brand>
    requires std::is_object_v<T>
[[nodiscard]] constexpr OwnedRegion<T, Tag, Brand>
mint_owned_region(T* base, std::size_t count, ::foundation::permissions::Permission<Tag, Brand>&& perm) noexcept {
    return OwnedRegion<T, Tag, Brand>{base, count, std::move(perm)};
}

template <typename T, typename Allocator, typename Tag, typename Brand>
    requires ArrayArena<Allocator, T>
[[nodiscard]] OwnedRegion<T, Tag, Brand> mint_owned_region(  // MINT-PATTERN-OK: allocating
    ::foundation::effects::Alloc alloc_token, Allocator& arena, std::size_t count,
    ::foundation::permissions::Permission<Tag, Brand>&& perm) noexcept {
    T* base = (count == 0) ? nullptr : arena.template alloc_array<T>(alloc_token, count);
    return OwnedRegion<T, Tag, Brand>{base, count, std::move(perm)};
}

// A borrow of a region carries the region's brand and its tag as the
// owner.  The region is taken by lvalue reference and the twin refuses
// a temporary region, whose storage may be gone at the end of the
// statement.  Declared in Borrowed.h, which befriends it.
template <typename T, typename Tag, typename Brand>
    requires ::foundation::brand::IsBrand<Brand>
[[nodiscard]] constexpr Borrowed<T, Tag, Brand>
mint_borrowed(OwnedRegion<T, Tag, Brand>& region CRUCIBLE_LIFETIMEBOUND) noexcept {
    return Borrowed<T, Tag, Brand>{detail::borrow_mint_t{}, region.span()};
}

template <typename T, typename Tag, typename Brand>
    requires ::foundation::brand::IsBrand<Brand>
constexpr Borrowed<T, Tag, Brand> mint_borrowed(OwnedRegion<T, Tag, Brand>&&) =
    delete("a borrow of a temporary region outlives it; bind the region to a name that outlives the borrow");

template <typename T, typename Tag, typename Brand>
template <std::size_t N>
auto OwnedRegion<T, Tag, Brand>::split_into() && noexcept {
    static_assert(N > 0, "split_into<N>() requires N > 0");
    return std::move(*this).template split_into_impl_<N>(std::make_index_sequence<N>{});
}

template <typename T, typename Tag, typename Brand>
template <std::size_t N, std::size_t... Is>
auto OwnedRegion<T, Tag, Brand>::split_into_impl_(std::index_sequence<Is...>) && noexcept {
    static_assert(sizeof...(Is) == N, "index_sequence size mismatch");

    // Snapshot the base and the count before the permission is
    // consumed.  The split leaves perm_ moved-from, while base_ and
    // count_ stay readable until this object is destroyed.
    T* base = base_;
    const std::size_t total = count_;

    auto sub_perms = ::foundation::permissions::mint_permission_split_n<Slice<Tag, Is>...>(std::move(perm_));

    return std::tuple<OwnedRegion<T, Slice<Tag, Is>, Brand>...>{OwnedRegion<T, Slice<Tag, Is>, Brand>{
        base + chunk_range_(total, N, Is).first, chunk_range_(total, N, Is).second,
        std::move(std::get<Is>(sub_perms))}...};
}

// The detection surface of the old IsOwnedRegion.h.  One reflection
// query answers it, and the associated types are read off the
// wrapper's own typedefs.  The two extractors are constrained rather
// than left to a primary template, which would hand back void for an
// unrelated argument instead of failing.

// The concept is the question; the value spelling is derived from it
// and read by nothing that gates.
template <typename T>
concept IsOwnedRegion = ::foundation::reflect::IsInstanceOf<T, ^^OwnedRegion>;

template <typename T>
inline constexpr bool is_owned_region_v = IsOwnedRegion<T>;

template <typename T>
    requires IsOwnedRegion<T>
using owned_region_value_t = typename std::remove_cvref_t<T>::value_type;

template <typename T>
    requires is_owned_region_v<T>
using owned_region_tag_t = typename std::remove_cvref_t<T>::tag_type;

namespace detail {
struct owned_region_test_tag {
    using permission_row = ::foundation::effects::Row<>;
};
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
static_assert(::foundation::permissions::has_permission_row_v<Slice<detail::owned_region_test_tag, 3>>,
              "a shard has its parent's row");

static_assert(
    ::foundation::permissions::splits_into_pack_v<
        detail::owned_region_test_tag, Slice<detail::owned_region_test_tag, 0>, Slice<detail::owned_region_test_tag, 1>,
        Slice<detail::owned_region_test_tag, 2>, Slice<detail::owned_region_test_tag, 3>>,
    "Slice<Parent, 0..N-1> must auto-specialize splits_into_pack");
static_assert(::foundation::permissions::well_authored_split_pack_v<detail::owned_region_test_tag,
                                                                    Slice<detail::owned_region_test_tag, 0>>,
              "Slice<Parent, 0..N-1> must ship the authoring witness beside the manifest");

namespace detail::owned_region_self_test {

struct test_tag_a {
    using permission_row = ::foundation::effects::Row<>;
};
struct test_tag_b {
    using permission_row = ::foundation::effects::Row<>;
};
struct brand_a {};

using OR_int_a = OwnedRegion<int, test_tag_a>;
using OR_double_a = OwnedRegion<double, test_tag_a>;
using OR_int_b = OwnedRegion<int, test_tag_b>;
using OR_int_a_branded = OwnedRegion<int, test_tag_a, brand_a>;

static_assert(is_owned_region_v<OR_int_a>);
static_assert(is_owned_region_v<OR_double_a>);
static_assert(is_owned_region_v<OR_int_b>);
static_assert(is_owned_region_v<OR_int_a_branded>);

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

// A branded region keeps the erased layout, erases one way, and does
// not rebrand.
static_assert(sizeof(OR_int_a_branded) == sizeof(OR_int_a));
static_assert(std::is_convertible_v<OR_int_a_branded&&, OR_int_a>, "a branded region erases to the unbranded spelling");
static_assert(!std::is_constructible_v<OR_int_a_branded, OR_int_a&&>, "an erased region does not acquire a brand");
static_assert(!std::is_constructible_v<OR_int_a, OR_int_a_branded const&>, "erasure consumes the branded region");

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

// A region minted from a branded permission carries that brand and its
// borrow carries it.  The split and the recombine are not constexpr, so
// the brand of a shard is pinned at runtime in test/fixy/test_owned_region.cpp.
[[nodiscard]] consteval bool region_carries_its_permission_brand() noexcept {
    int storage[4] = {1, 2, 3, 4};
    auto perm = ::foundation::permissions::mint_permission_root<test_tag_a>();
    using Brand = ::foundation::brand::brand_of_t<decltype(perm)>;
    auto region = mint_owned_region(storage, 4, std::move(perm));
    static_assert(std::is_same_v<decltype(region), OwnedRegion<int, test_tag_a, Brand>>);
    static_assert(::foundation::brand::IsBranded<decltype(region)>);
    auto borrow = mint_borrowed(region);
    static_assert(std::is_same_v<decltype(borrow), Borrowed<int, test_tag_a, Brand>>);
    return borrow.size() == 4 && borrow[2] == 3 && region.data() == storage;
}
static_assert(region_carries_its_permission_brand());

}  // namespace detail::owned_region_self_test

}  // namespace fixy
