// The compile-time checks of foundation/core/BumpArena.h.

#include <foundation/core/BumpArena.h>

#include <foundation/core/Verbs.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::bump_arena_checks {

struct NoMarker final {};

struct Plain {
    std::uint64_t key = 0;
    std::uint32_t count = 0;
    std::uint32_t flags = 0;
};

struct Ended {
    int value = 0;
    ~Ended() {}
};

struct MoveOnly {
    int value = 0;
    MoveOnly() noexcept = default;
    MoveOnly(MoveOnly&&) noexcept = default;
    MoveOnly(MoveOnly const&) = delete;
};

struct Throws {
    explicit Throws(int /*value*/) noexcept(false) {}
};

// The fields of an arena come first and fit in one cache line, and the table
// of blocks follows them.  Each bump reads only the first three fields.
static_assert(sizeof(BumpArena) <= 64 + BumpArena::max_blocks * (sizeof(std::byte*) + sizeof(std::size_t)));

// An arena does not copy and does not move, because an epoch names it.
static_assert(!std::is_copy_constructible_v<BumpArena> && !std::is_copy_assignable_v<BumpArena>);
static_assert(!std::is_move_constructible_v<BumpArena> && !std::is_move_assignable_v<BumpArena>);
static_assert(std::is_nothrow_constructible_v<BumpArena, std::size_t>);

// make takes a type with a trivial destructor that the arguments build with
// no throw, and the allocation tag only.
static_assert(CanMakeInArena<Plain, Alloc>);
static_assert(CanMakeInArena<Plain, Alloc, Plain const&>);
static_assert(CanMakeInArena<int, Alloc, int>);
static_assert(!CanMakeInArena<Plain, NoMarker>);
static_assert(!CanMakeInArena<Ended, Alloc>);
static_assert(!CanMakeInArena<Throws, Alloc, int>);
static_assert(!CanMakeInArena<int const, Alloc, int>);
static_assert(!CanMakeInArena<int[2], Alloc>);

// array takes an element with a trivial destructor that copies with no
// throw, and the allocation tag only.
static_assert(CanFillArenaArray<Plain, Alloc>);
static_assert(!CanFillArenaArray<Plain, NoMarker>);
static_assert(!CanFillArenaArray<Ended, Alloc>);
static_assert(!CanFillArenaArray<MoveOnly, Alloc>);
static_assert(!CanFillArenaArray<int const, Alloc>);

// The body of an epoch takes no argument and does not throw.
inline constexpr auto silent_body = []() noexcept { return 3; };
inline constexpr auto throwing_body = [] { return 3; };
inline constexpr auto body_with_argument = [](int value) noexcept { return value; };
static_assert(EpochBody<decltype(silent_body) const&>);
static_assert(!EpochBody<decltype(throwing_body) const&>);
static_assert(!EpochBody<decltype(body_with_argument) const&>);

// make gives a Ref, array gives a View, and with_epoch gives the result of
// its body.
template <class Arena>
concept MakesInt = requires { std::declval<Arena>().template make<int>(Alloc{}, 5); };
template <class Arena>
concept FillsInts = requires { std::declval<Arena>().array(Alloc{}, std::size_t{4}, 0); };
template <class Arena>
concept RunsEpoch = requires { std::declval<Arena>().with_epoch(silent_body); };
static_assert(MakesInt<BumpArena&> && FillsInts<BumpArena&> && RunsEpoch<BumpArena&>);
static_assert(std::is_same_v<decltype(std::declval<BumpArena&>().array(Alloc{}, std::size_t{4}, 0)), View<int>>);
static_assert(std::is_same_v<decltype(std::declval<BumpArena&>().with_epoch(silent_body)), int>);

// An allocation and an epoch of a temporary arena are refused, because the
// arena gives back its blocks at the end of the statement.
static_assert(!MakesInt<BumpArena&&> && !FillsInts<BumpArena&&> && !RunsEpoch<BumpArena&&>);

// The target of a Ref of each brand.
template <class R>
struct RefTargetOf;
template <class T, class Brand>
struct RefTargetOf<Ref<T, Brand>> {
    using type = T;
};

// make gives a Ref to a T.  Two calls give two fresh brands.
[[nodiscard]] consteval bool make_gives_fresh_refs() noexcept {
    using First = decltype(std::declval<BumpArena&>().make<Plain>(Alloc{}));
    using Second = decltype(std::declval<BumpArena&>().make<Plain>(Alloc{}));
    return std::is_same_v<typename RefTargetOf<First>::type, Plain> && !std::is_same_v<First, Second>;
}
static_assert(make_gives_fresh_refs());

// The closed surface of the arena.
inline constexpr TextView bump_arena_names{"BumpArena make array with_epoch retained_bytes retained_bytes_limit "
                                           "max_blocks block_alignment"};
CRUCIBLE_CLOSED_SURFACE(bump_arena_names, BumpArena);

}  // namespace detail::bump_arena_checks

}  // namespace foundation::core
