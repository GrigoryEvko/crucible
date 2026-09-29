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
// mint_split partitions the index space, not the allocation.  Every
// sub-region points into the same buffer at a distinct chunk offset,
// and the distinct Slice tags are what prove the chunks are disjoint.
// The shards carry the parent's brand, and recombine demands it back.
//
// mint_split also writes a receipt, Disjoint below, and recombine
// consumes it beside the shards.  Holding shards is therefore not by
// itself authority to rebuild a whole: the split's own receipt is, it
// is move-only, and its type names the tag, the brand, the arity and
// the site of the split that wrote it.  Disjoint's own comment states
// which half of use-after-consume that catches and which half no
// template argument can catch.
//
// The surrendered Permission is the one door: the constructor is
// private, and adopt and wrap each take a token by rvalue.  A join
// rebuilds the parent by surrendering the shards to `recombine`, which
// combines their Slice permissions back into the parent's.  A rebuild
// has to consume the thing it reissues.  A helper that takes no
// argument proves nothing, and it can mint a Permission for any tag
// from any translation unit.

#include <fixy/Borrowed.h>
#include <foundation/Brand.h>
#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>
#include <foundation/reflect/Instance.h>

#include <array>
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
// splitting a parent into N shards declares nothing per N.
//
// parent_type is also how a shard finds its effect row: the row
// relation reads a derived tag's parent, so a shard touches the region
// under the same row as the whole.

// The name a shard carries when no split wrote it: the spelling a
// reader uses to talk about the shape of a split rather than about one
// split's shards.
struct Unsplit {};

// SplitName is the third parameter because a shard has to say which
// split cut it.  Two splits are two names, so their shards are two
// types, and a tuple that takes one shard from each does not satisfy
// recombine.  The receipt alone cannot say this: nothing in a tuple of
// shards would contradict it.
template <typename Parent, std::size_t I, typename SplitName = Unsplit>
struct Slice {
    using parent_type = Parent;
    using split_name_type = SplitName;
    static constexpr std::size_t index = I;
};

namespace detail {

// The one door to a disjointness receipt.  Only mint_split holds it.
struct split_mint_t {};

// The first index of shard `index` when a split cuts `total` elements
// into `shards` parts.  Each of the first total % shards parts holds one
// element more than the other parts.  Shard i ends where shard i + 1
// starts, shard 0 starts at 0 and shard `shards` starts at total.  So the
// shards tile [0, total) with no gap and no overlap, and no start is past
// total.  The arithmetic cannot wrap, because index * (total / shards)
// is not more than total for an index that is not more than `shards`.
[[nodiscard]] constexpr std::size_t shard_start(std::size_t total, std::size_t shards, std::size_t index) noexcept {
    const std::size_t smaller_count = total / shards;
    const std::size_t larger_shards = total % shards;
    return index * smaller_count + (index < larger_shards ? index : larger_shards);
}

}  // namespace detail

// The receipt a split writes: these shards came from one split of one
// region.  mint_split hands it back beside the shards, and recombine
// consumes both, so holding the shards is not by itself authority to
// rebuild the whole.  It carries no bytes: the proof is the type.
//
// What the receipt catches.
//
//   A rebuild from shards nobody split.  Without a receipt, a caller
//   who held shards could assemble a tuple and get a whole.  The
//   receipt has no public constructor, so a tuple alone does not reach
//   recombine.
//
//   A copied receipt.  The type is move-only, and the copy is deleted
//   with its reason.  A second move of one receipt compiles, because a
//   moved-from receipt is still an object.  scripts/check-use-after-move.py
//   refuses that second move.  The shards of the first recombine are
//   then empty at a null base, so the whole that a second recombine
//   rebuilds from them covers no byte.
//
//   A receipt from the wrong split.  The tag, the brand of the region
//   that was split, the arity and the name of the split site are all
//   in the type, so a receipt written at another site does not convert
//   to the one recombine asks for.
//
// What the receipt cannot catch, and what catches it instead.  A brand
// names a mint SITE, not a mint CALL: fact 2 of foundation/Brand.h,
// measured rather than assumed.  So two regions minted by one statement
// in a loop are one type, their shards are one type, and their receipts
// are one type, and a tuple that takes shard 0 from the first and shard
// 1 from the second is well typed here.  Every region on the erased
// brand is also one type.  A borrow checker refuses the mix because it
// reasons about the flow of one program point to another, and a
// template argument does not.  recombine therefore checks at run time
// that each shard starts where the one before it ends, and a mix
// aborts.  The rebuilt region never covers storage that no consumed
// shard owned.
// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace row_discipline {
struct owned_region;
template <std::size_t N>
struct disjoint;
}  // namespace row_discipline

template <typename Tag, typename Brand, typename SplitName, std::size_t N>
class [[nodiscard]] Disjoint {
    static_assert(N > 0, "Disjoint: a split of zero shards proves nothing.");
    static_assert(::foundation::brand::IsBrand<SplitName>,
                  "Disjoint: SplitName must be an empty class type, the fresh name of one split site.");

public:
    using tag_type = Tag;
    using brand_type = Brand;
    using split_name_type = SplitName;
    static constexpr std::size_t shard_count = N;
    using row_discipline = ::fixy::row_discipline::disjoint<N>;
    using row_payload = ::foundation::diag::row_payloads<>;

    Disjoint(Disjoint const&) = delete("a second receipt would let one split authorize two rebuilds");
    Disjoint& operator=(Disjoint const&) = delete("a second receipt would let one split authorize two rebuilds");
    constexpr Disjoint(Disjoint&&) noexcept = default;
    constexpr Disjoint& operator=(Disjoint&&) noexcept = default;
    ~Disjoint() = default;

private:
    // The door.  mint_split holds the key; nothing else does.
    constexpr explicit Disjoint(detail::split_mint_t) noexcept {}

    template <typename U, typename UTag, typename UBrand>
    friend class OwnedRegion;
};

// What a split hands back: the receipt and the shards, named so a
// caller reads which is which.
template <typename Witness, typename Shards>
struct [[nodiscard]] SplitParts {
    Witness witness;
    Shards shards;
};

// The return type of mint_split, spelled without an index pack so the
// mint can be declared before the class it consumes and befriended by
// it.
template <typename T, typename Tag, typename Brand, typename SplitName, typename Seq>
struct split_parts_for_;

template <typename T, typename Tag, typename Brand, typename SplitName, std::size_t... Is>
struct split_parts_for_<T, Tag, Brand, SplitName, std::index_sequence<Is...>> {
    using type = SplitParts<Disjoint<Tag, Brand, SplitName, sizeof...(Is)>,
                            std::tuple<OwnedRegion<T, Slice<Tag, Is, SplitName>, Brand>...>>;
};

template <typename T, typename Tag, typename Brand, typename SplitName, std::size_t N>
using split_parts_t = typename split_parts_for_<T, Tag, Brand, SplitName, std::make_index_sequence<N>>::type;

// The split: it consumes the whole and issues the shards beside the
// receipt that recombines them.
//
// It is a free function rather than a member because the name of the
// split is a lambda in a defaulted trailing template parameter, and
// measured on this compiler such a default inside a member of a class
// template yields one closure for the class rather than one per call
// site.  A free function template gives each call site its own name,
// which is the whole point of the name.  Everything else about the
// shape follows the mint pattern: the pack before SplitName absorbs
// whatever a caller writes, so the name cannot be spelled.
template <std::size_t N, typename... Never, typename T, typename Tag, typename Brand,
          typename SplitName = CRUCIBLE_FRESH_BRAND>
    requires(N > 0 && std::is_object_v<T>)
[[nodiscard]] split_parts_t<T, Tag, Brand, SplitName, N>
mint_split(OwnedRegion<T, Tag, Brand>&& region) noexcept;  // MINT-PATTERN-OK: the split partitions at run time

}  // namespace fixy

namespace foundation::permissions {

template <typename Parent, typename SplitName, std::size_t... Is>
struct can_split_into_pack<Parent, ::fixy::Slice<Parent, Is, SplitName>...> : std::true_type {};

// Specialize this witness in lockstep with the specialization above.
template <typename Parent, typename SplitName, std::size_t... Is>
struct has_split_pack_authoring_witness<Parent, ::fixy::Slice<Parent, Is, SplitName>...> : std::true_type {};

}  // namespace foundation::permissions

namespace fixy {

// This layer cannot name the crucible Arena.  What adopt reads of an
// allocator is one member template, so that is the whole requirement:
// alloc_array<T>(token, count) returning T*, with a null result for a
// zero count left to adopt itself.
template <typename Allocator, typename T>
concept ArrayArena = requires(Allocator& arena, ::foundation::effects::Alloc token, std::size_t count) {
    { arena.template alloc_array<T>(token, count) } -> std::same_as<T*>;
};

template <typename T, typename Tag, typename Brand = ::foundation::brand::DefaultBrand>
class OwnedRegion;

// fixy/SharedRegion.h parks a region's permission in a pool so several
// readers hold a share of it at once.  Its door consumes the exclusive
// region, which is the evidence, and it reaches the same three private
// members the split reaches, so it is a friend below for the reason the
// split is: the alternative is a public accessor that hands the
// permission to anyone.
template <typename T, typename Tag, typename Brand>
class SharedRegion;

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

    // The shared door, and its inverse: SharedRegion takes the base,
    // the count and the permission on the way in, and rebuilds a region
    // from the same three when the pool hands the exclusive back.
    template <typename U, typename UTag, typename UBrand>
    friend class SharedRegion;

    template <typename U, typename UTag, typename UBrand>
        requires std::is_object_v<U>
    friend constexpr OwnedRegion<U, UTag, UBrand>
    mint_owned_region(U* base, std::size_t count, ::foundation::permissions::Permission<UTag, UBrand>&& perm) noexcept;

    template <typename U, typename Allocator, typename UTag, typename UBrand>
        requires ArrayArena<Allocator, U>
    friend OwnedRegion<U, UTag, UBrand>
    mint_owned_region(::foundation::effects::Alloc alloc_token, Allocator& arena, std::size_t count,
                      ::foundation::permissions::Permission<UTag, UBrand>&& perm) noexcept;

public:
    using value_type = T;
    using tag_type = Tag;
    using brand_type = Brand;
    using row_discipline = ::fixy::row_discipline::owned_region;
    using row_payload = T;

    OwnedRegion(const OwnedRegion&) = delete("OwnedRegion owns a Permission — copy would duplicate the linear token");
    OwnedRegion& operator=(const OwnedRegion&) =
        delete("OwnedRegion owns a Permission — assignment would overwrite the linear token");

    // Each door that consumes a region takes its base and its count, so
    // the consumed region is empty.  A shard that a caller moves out of a
    // tuple therefore covers no byte of the whole that recombine rebuilds,
    // and recombine aborts on the tuple.  The use-after-move guard sees a
    // moved name, but not a moved tuple element.
    constexpr OwnedRegion(OwnedRegion&& other) noexcept
        : base_{std::exchange(other.base_, nullptr)},
          count_{std::exchange(other.count_, 0)},
          perm_{std::move(other.perm_)} {}
    constexpr OwnedRegion& operator=(OwnedRegion&& other) noexcept {
        base_ = std::exchange(other.base_, nullptr);
        count_ = std::exchange(other.count_, 0);
        perm_ = std::move(other.perm_);
        return *this;
    }
    ~OwnedRegion() = default;

    // Erasure, one way only: a region of one instance becomes a region
    // on the erased identity, consuming the branded one.  Nothing gives
    // an erased region a brand.
    template <typename Other>
        requires(std::is_same_v<Brand, ::foundation::brand::DefaultBrand> && ::foundation::brand::IsFreshBrand<Other>)
    constexpr OwnedRegion(OwnedRegion<T, Tag, Other>&& other) noexcept
        : base_{std::exchange(other.base_, nullptr)},
          count_{std::exchange(other.count_, 0)},
          perm_{std::move(other.perm_)} {}

    // The caller proves exclusive ownership by surrendering the
    // Permission token.  The body is the allocating mint, so the two
    // spellings cannot drift apart.
    template <typename Allocator>
        requires ArrayArena<Allocator, T>
    [[nodiscard]] static OwnedRegion adopt(::foundation::effects::Alloc alloc_token, Allocator& arena,
                                           std::size_t count,
                                           ::foundation::permissions::Permission<Tag, Brand>&& perm) noexcept {
        return ::fixy::mint_owned_region<T>(alloc_token, arena, count, std::move(perm));
    }

    // Wraps storage allocated elsewhere.  The region owns the
    // Permission proof but not the storage, so the caller keeps
    // responsibility for the buffer's lifetime.  The body is the mint
    // over a pointer.
    [[nodiscard]] static constexpr OwnedRegion wrap(T* base, std::size_t count,
                                                    ::foundation::permissions::Permission<Tag, Brand>&& perm) noexcept {
        return ::fixy::mint_owned_region(base, count, std::move(perm));
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

    // The split is mint_split, declared above.  It reaches the private
    // partition through this friendship, so the only way to a shard is
    // through a call that also writes the receipt.
    template <std::size_t UN, typename... UNever, typename U, typename UTag, typename UBrand, typename USplitName>
        requires(UN > 0 && std::is_object_v<U>)
    friend split_parts_t<U, UTag, UBrand, USplitName, UN> mint_split(OwnedRegion<U, UTag, UBrand>&& region) noexcept;

    // The inverse of split_into.  The receipt and every shard are
    // surrendered here, and the shards' Slice permissions are combined
    // back into the parent's.  mint_permission_combine_n checks that the
    // shard tags mirror a declared can_split_into_pack and that every shard
    // carries this region's brand; the receipt adds what the shards
    // cannot say, which is that one split produced them and that this
    // rebuild is the only one that split authorizes.
    //
    // This is the only way to recover a parent permission after a split.
    // There is deliberately no nullary rebuild.  One that took no
    // argument would prove nothing, and it could mint a Permission for
    // any tag from any translation unit.
    template <typename SplitName, std::size_t... Is>
    [[nodiscard]] static OwnedRegion
    recombine(Disjoint<Tag, Brand, SplitName, sizeof...(Is)>&& witness,
              std::tuple<OwnedRegion<T, Slice<Tag, Is, SplitName>, Brand>...>&& shards) noexcept {
        static_assert(sizeof...(Is) > 0, "recombine() needs at least one shard.");
        // The receipt is spent by being taken by rvalue and named here.
        // It carries no bytes, so there is nothing else to consume.
        [[maybe_unused]] Disjoint<Tag, Brand, SplitName, sizeof...(Is)> spent{std::move(witness)};
        // Each shard gives up its base and its count here, so no shard in
        // the consumed tuple keeps its bytes beside the rebuilt whole.
        std::array<T*, sizeof...(Is)> const bases{std::exchange(std::get<Is>(shards).base_, nullptr)...};
        std::array<std::size_t, sizeof...(Is)> const counts{std::exchange(std::get<Is>(shards).count_, 0)...};
        // Shard 0 starts at offset 0, so its base is the whole's base.
        // Each shard must start where the one before it ends.  Two regions
        // split at one site share a brand, so their shards and receipts are
        // one type, and only their addresses tell them apart.  A shard that
        // was moved out of the tuple is empty at a null base, so it breaks
        // the chain too.  With the check the rebuilt span is exactly the
        // storage of the shards that were consumed.  Complexity: one
        // comparison per shard.
        T* const base = bases[0];
        T* next = base;
        bool contiguous = true;
        std::size_t total = 0;
        for (std::size_t index = 0; index < sizeof...(Is); ++index) {
            contiguous = contiguous && bases[index] == next;
            next = bases[index] + counts[index];
            total += counts[index];
        }
        CRUCIBLE_FATAL_INVARIANT(contiguous);
        return OwnedRegion{
            base, total,
            ::foundation::permissions::mint_permission_combine_n<Tag>(std::move(std::get<Is>(shards).perm_)...)};
    }

private:
    template <std::size_t N, typename SplitName, std::size_t... Is>
    auto split_into_impl_(std::index_sequence<Is...>) && noexcept;
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
[[nodiscard]] constexpr Borrowed<T, Tag, Brand> mint_borrowed(OwnedRegion<T, Tag, Brand>& region
                                                              CRUCIBLE_LIFETIMEBOUND) noexcept {
    return Borrowed<T, Tag, Brand>{detail::borrow_mint_t{}, region.span()};
}

template <typename T, typename Tag, typename Brand>
    requires ::foundation::brand::IsBrand<Brand>
constexpr Borrowed<T, Tag, Brand> mint_borrowed(OwnedRegion<T, Tag, Brand>&&) =
    delete("a borrow of a temporary region outlives it; bind the region to a name that outlives the borrow");

template <std::size_t N, typename... Never, typename T, typename Tag, typename Brand, typename SplitName>
    requires(N > 0 && std::is_object_v<T>)
[[nodiscard]] split_parts_t<T, Tag, Brand, SplitName, N>
mint_split(OwnedRegion<T, Tag, Brand>&& region) noexcept {  // MINT-PATTERN-OK: the split partitions at run time
    static_assert(sizeof...(Never) == 0,
                  "mint_split<N>(region) takes one template argument. The name of the split follows it and is "
                  "minted here, because a caller who could spell it could write a receipt for a split it did "
                  "not perform.");
    return std::move(region).template split_into_impl_<N, SplitName>(std::make_index_sequence<N>{});
}

template <typename T, typename Tag, typename Brand>
template <std::size_t N, typename SplitName, std::size_t... Is>
auto OwnedRegion<T, Tag, Brand>::split_into_impl_(std::index_sequence<Is...>) && noexcept {
    static_assert(sizeof...(Is) == N, "index_sequence size mismatch");

    // The split takes the base and the count out of the region it
    // consumes, so that region is empty and no longer covers the bytes
    // that the shards cover.
    T* const base = std::exchange(base_, nullptr);
    const std::size_t total = std::exchange(count_, 0);

    auto sub_perms = ::foundation::permissions::mint_permission_split_n<Slice<Tag, Is, SplitName>...>(std::move(perm_));

    using Shards = std::tuple<OwnedRegion<T, Slice<Tag, Is, SplitName>, Brand>...>;
    using Witness = Disjoint<Tag, Brand, SplitName, N>;
    return SplitParts<Witness, Shards>{Witness{detail::split_mint_t{}},
                                       Shards{OwnedRegion<T, Slice<Tag, Is, SplitName>, Brand>{
                                           base + detail::shard_start(total, N, Is),
                                           detail::shard_start(total, N, Is + 1) - detail::shard_start(total, N, Is),
                                           std::move(std::get<Is>(sub_perms))}...}};
}

// The detection surface of OwnedRegion.  One reflection query answers
// it, and the associated types are read off the wrapper's own typedefs.
// The two extractors are constrained rather than left to a primary
// template, which would hand back void for an unrelated argument
// instead of failing.

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
    ::foundation::permissions::can_split_into_pack_v<
        detail::owned_region_test_tag, Slice<detail::owned_region_test_tag, 0>, Slice<detail::owned_region_test_tag, 1>,
        Slice<detail::owned_region_test_tag, 2>, Slice<detail::owned_region_test_tag, 3>>,
    "Slice<Parent, 0..N-1> must auto-specialize can_split_into_pack");
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

// The partition of a split tiles its region.  Over each total from 0 to
// 64 and each shard count from 1 to 16: shard 0 starts at 0, the last
// shard ends at the total, and the length of each shard is total / N or
// one element more.  test/fixy/test_owned_region.cpp does the split and
// the recombine at run time over the same grid.
[[nodiscard]] consteval bool shard_starts_tile_every_total() noexcept {
    for (std::size_t total = 0; total <= 64; ++total) {
        for (std::size_t shards = 1; shards <= 16; ++shards) {
            if (detail::shard_start(total, shards, 0) != 0) return false;
            if (detail::shard_start(total, shards, shards) != total) return false;
            for (std::size_t index = 0; index < shards; ++index) {
                const std::size_t start = detail::shard_start(total, shards, index);
                const std::size_t next = detail::shard_start(total, shards, index + 1);
                if (next < start || next - start > total / shards + 1 || next - start < total / shards) return false;
            }
        }
    }
    return true;
}
static_assert(shard_starts_tile_every_total());
static_assert(detail::shard_start(5, 4, 3) == 4, "5 elements into 4 shards puts the last shard at index 4");
static_assert(detail::shard_start(~std::size_t{0}, 3, 3) == ~std::size_t{0}, "the largest total does not wrap");

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
// fixed block.  This layer cannot name the crucible Arena, so the self
// test holds its own.  The lifetime start gives a live object only to a
// type whose every subobject is an implicit-lifetime type.  For a proof
// type, or an aggregate that holds one, it gives a pointer to an object
// whose lifetime never started, so the constraint refuses that type.
class BumpArena {
    alignas(std::max_align_t) std::array<unsigned char, 4096> block_{};
    std::size_t used_ = 0;

public:
    template <typename T>
        requires ::foundation::lifetime::ImplicitLifetimeThroughout<T>
    [[nodiscard]] T* alloc_array(::foundation::effects::Alloc, std::size_t n) noexcept {
        if (n == 0) return nullptr;
        const std::size_t misalign = used_ % alignof(T);
        const std::size_t start = misalign == 0 ? used_ : used_ + (alignof(T) - misalign);
        const std::size_t nbytes = n * sizeof(T);
        if (start + nbytes > block_.size()) std::abort();
        used_ = start + nbytes;
        return ::foundation::lifetime::start_as_array<T>(block_.data() + start, n).data();
    }
};
struct HoldsPermission {
    [[no_unique_address]] ::foundation::permissions::Permission<test_tag_a> proof;
};
static_assert(ArrayArena<BumpArena, int>);
static_assert(!ArrayArena<int, int>);
static_assert(!ArrayArena<BumpArena, ::foundation::permissions::Permission<test_tag_a>>,
              "the arena must not start the lifetime of a proof type over its bytes");
static_assert(!ArrayArena<BumpArena, HoldsPermission>,
              "the arena must not start the lifetime of an aggregate that holds a proof type");

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

// The receipt a split writes.  It costs no bytes, it cannot be built
// from nothing, and it cannot be copied, so one split authorizes one
// rebuild.  Two spellings that differ in any of the four things the
// type carries are two types, and neither converts to the other.
using SplitA = decltype([] {});
using SplitB = decltype([] {});
using ReceiptA2 = Disjoint<test_tag_a, ::foundation::brand::DefaultBrand, SplitA, 2>;
using ReceiptB2 = Disjoint<test_tag_a, ::foundation::brand::DefaultBrand, SplitB, 2>;
using ReceiptA4 = Disjoint<test_tag_a, ::foundation::brand::DefaultBrand, SplitA, 4>;
using ReceiptOtherTag = Disjoint<test_tag_b, ::foundation::brand::DefaultBrand, SplitA, 2>;

static_assert(sizeof(ReceiptA2) == 1, "a receipt is a type, not a byte of state");
static_assert(std::is_empty_v<ReceiptA2>);
static_assert(!std::is_default_constructible_v<ReceiptA2>, "a receipt nobody wrote proves nothing");
static_assert(!std::is_copy_constructible_v<ReceiptA2>, "a copied receipt would authorize two rebuilds");
static_assert(!std::is_copy_assignable_v<ReceiptA2>);
static_assert(std::is_move_constructible_v<ReceiptA2>);
static_assert(std::is_nothrow_move_constructible_v<ReceiptA2>);
static_assert(!std::is_convertible_v<ReceiptB2&&, ReceiptA2>, "another split's receipt is another type");
static_assert(!std::is_convertible_v<ReceiptA4&&, ReceiptA2>, "another arity's receipt is another type");
static_assert(!std::is_convertible_v<ReceiptOtherTag&&, ReceiptA2>, "another tag's receipt is another type");
static_assert(ReceiptA2::shard_count == 2);
static_assert(std::is_same_v<ReceiptA2::tag_type, test_tag_a>);

}  // namespace detail::owned_region_self_test

}  // namespace fixy
