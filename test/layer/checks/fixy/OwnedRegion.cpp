// The compile-time checks of fixy/OwnedRegion.h.

#include <fixy/OwnedRegion.h>

namespace fixy {

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
static_assert(::foundation::permissions::has_permission_row(^^Slice<detail::owned_region_test_tag, 3>),
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

// A branded region keeps the erased layout, and no conversion drops or
// adds a brand.
static_assert(sizeof(OR_int_a_branded) == sizeof(OR_int_a));
static_assert(!std::is_constructible_v<OR_int_a, OR_int_a_branded&&>, "a branded region does not erase");
static_assert(!std::is_constructible_v<OR_int_a_branded, OR_int_a&&>, "an erased region does not acquire a brand");

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
static_assert(!std::is_trivially_copyable_v<ReceiptA2>
                  && !::foundation::lifetime::ImplicitLifetimeThroughout<ReceiptA2>,
              "std::bit_cast and std::start_lifetime_as must not write a receipt");
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
