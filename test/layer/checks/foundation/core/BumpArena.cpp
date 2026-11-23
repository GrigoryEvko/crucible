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

using Bytes = Quantity<unit::Byte, std::size_t>;

// The tags of the checks: a member tag nested in its owner, a family tag, a
// tag with no mark, and a tag with both marks.
struct Owner {
    struct[[= ::foundation::brand::arena_member_brand{}]] Member final {};
    EpochArena<Member> arena = mint_epoch_arena<Member>(Bytes{4096});
};
struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};
struct Unmarked final {};
struct[[= ::foundation::brand::arena_member_brand{}]][[= ::foundation::brand::arena_family_brand{}]] Both final {};

static_assert(ArenaMemberTag<Owner::Member> && !ArenaFamilyTag<Owner::Member>);
static_assert(ArenaFamilyTag<Family> && !ArenaMemberTag<Family>);
static_assert(!NamedArenaTag<Unmarked> && !NamedArenaTag<Both>);
static_assert(!ArenaBrand<Unmarked> && !ArenaBrand<::foundation::brand::DefaultBrand>);
static_assert(EpochBrand<EpochOf<Owner::Member>> && !EpochBrand<Owner::Member>);

// A block size comes only from a constant Quantity or from plus, so it has no
// default value and no constructor from a plain number.  It is one size_t.
static_assert(!std::is_default_constructible_v<ArenaBlockSize>);
static_assert(!std::is_constructible_v<ArenaBlockSize, std::size_t>);
static_assert(std::is_trivially_copyable_v<ArenaBlockSize> && sizeof(ArenaBlockSize) == sizeof(std::size_t));
static_assert(ArenaBlockSize{Bytes{64}}.plus(Bytes{64}).bytes() == Bytes{128});

// The core of an arena keeps its fields first, in one cache line, and the
// table of blocks follows them.  Each bump reads only the first three
// fields.  An arena of each kind is its core and nothing more.
static_assert(sizeof(BumpArenaCore) <= 64 + BumpArenaCore::max_blocks * (sizeof(std::byte*) + sizeof(std::size_t)));
static_assert(sizeof(BumpArena<Family>) == sizeof(BumpArenaCore));
static_assert(sizeof(EpochArena<Owner::Member>) == sizeof(BumpArenaCore));
static_assert(sizeof(Epoch<EpochOf<Owner::Member>>) == sizeof(void*));
static_assert(sizeof(ArenaRun<int, Family>) == sizeof(View<int>));

// An arena does not copy, does not move, and has no public constructor.
static_assert(!std::is_copy_constructible_v<BumpArena<Family>> && !std::is_copy_assignable_v<BumpArena<Family>>);
static_assert(!std::is_move_constructible_v<BumpArena<Family>> && !std::is_move_assignable_v<BumpArena<Family>>);
static_assert(!std::is_constructible_v<BumpArena<Family>, Bytes> && !std::is_constructible_v<BumpArena<Family>>);
static_assert(!std::is_constructible_v<EpochArena<Owner::Member>, Bytes>);
static_assert(!std::is_move_constructible_v<EpochArena<Owner::Member>>);
static_assert(!std::is_move_constructible_v<Epoch<EpochOf<Owner::Member>>>);
static_assert(!std::is_constructible_v<ArenaRun<int, Family>, View<int>>);

// A run converts to a run of const elements with the same brand, and to no
// run of a different brand, and no run of const gives back a writable run.
// A window of a run keeps the type of the run.
static_assert(std::is_convertible_v<ArenaRun<int, Family>, ArenaRun<int const, Family>>);
static_assert(!std::is_convertible_v<ArenaRun<int const, Family>, ArenaRun<int, Family>>);
static_assert(!std::is_constructible_v<ArenaRun<int, Owner::Member>, ArenaRun<int, Family>>);
static_assert(!std::is_constructible_v<ArenaRun<int const, Owner::Member>, ArenaRun<int, Family>>);
static_assert(std::is_same_v<decltype(std::declval<ArenaRun<int, Family> const&>().window(RunStart{ElementCount{0}},
                                                                                          ElementCount{0})),
                             Option<ArenaRun<int, Family>>>);
static_assert(sizeof(ArenaRun<int const, Family>) == sizeof(View<int const>));

// A window takes its start and its count in two types, so no call swaps the
// two, and a plain number is neither.  The start is a Side of the count, and
// both have the size of one size_t.
template <class Start, class Count>
constexpr bool window_takes =
    requires(ArenaRun<int, Family> const& run, Start start, Count count) { run.window(start, count); };
static_assert(window_takes<RunStart, ElementCount>);
static_assert(!window_takes<ElementCount, RunStart>);
static_assert(!window_takes<ElementCount, ElementCount>);
static_assert(!window_takes<std::size_t, std::size_t>);
static_assert(!window_takes<RunStart, std::size_t>);
static_assert(sizeof(RunStart) == sizeof(std::size_t) && std::is_trivially_copyable_v<RunStart>);

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

// make gives a Ref of the brand of the arena, array gives a run of it, and
// an arena of a temporary is refused.
template <class Arena>
concept MakesInt = requires { std::declval<Arena>().template make<int>(std::declval<Alloc>(), 5); };
template <class Arena>
concept FillsInts = requires { std::declval<Arena>().array(std::declval<Alloc>(), std::size_t{4}, 0); };
static_assert(MakesInt<BumpArena<Family>&> && FillsInts<BumpArena<Family>&>);
static_assert(!MakesInt<BumpArena<Family>&&> && !FillsInts<BumpArena<Family>&&>);
static_assert(
    std::is_same_v<decltype(std::declval<BumpArena<Family>&>().make<int>(std::declval<Alloc>(), 5)), Ref<int, Family>>);
static_assert(
    std::is_same_v<decltype(std::declval<BumpArena<Family>&>().array(std::declval<Alloc>(), std::size_t{4}, 0)),
                   ArenaRun<int, Family>>);

// An epoch arena has no make and no array.  Its epoch gives them with the
// epoch brand.
static_assert(!MakesInt<EpochArena<Owner::Member>&> && !FillsInts<EpochArena<Owner::Member>&>);
static_assert(MakesInt<Epoch<EpochOf<Owner::Member>>&> && !MakesInt<Epoch<EpochOf<Owner::Member>>&&>);
static_assert(
    std::is_same_v<decltype(std::declval<Epoch<EpochOf<Owner::Member>>&>().make<int>(std::declval<Alloc>(), 5)),
                   Ref<int, EpochOf<Owner::Member>>>);

// A Ref of a family arena converts to no Ref of a different family, and
// erase_brand refuses a member tag, a family tag and an epoch brand, so a Ref
// of an arena keeps the brand of its arena.
static_assert(!std::is_convertible_v<Ref<int, Family>, Ref<int, Owner::Member>>);
static_assert(!ErasableBrand<Family> && !ErasableBrand<Owner::Member> && !ErasableBrand<EpochOf<Owner::Member>>);
static_assert(ErasableBrand<Unmarked>);

// The view of a run of a temporary is refused.
template <class Run>
concept GivesView = requires { std::declval<Run>().view(); };
static_assert(GivesView<ArenaRun<int, Family> const&> && !GivesView<ArenaRun<int, Family>&&>);

// The gate of an epoch: a body with no capture, a capture by value and a
// capture by a const reference of the owner pass.  A capture by a reference
// that is not const of the owner reaches the arena, and a capture by such a
// reference of an Option of a Ref of the epoch can keep a value of it.
using Member = Owner::Member;
using MemberEpoch = Epoch<EpochOf<Member>>;
using KeptRef = Option<Ref<int, EpochOf<Member>>>;
inline constexpr auto silent_body = [](MemberEpoch& /*epoch*/) noexcept { return 3; };
static_assert(EpochBody<decltype(silent_body) const&, Member>);
using ConstOwnerBody = decltype([](Owner const& owner) noexcept {
    return [&owner](MemberEpoch&) noexcept { return owner.arena.retained_bytes(); };
}(std::declval<Owner const&>()));
static_assert(EpochCaptures<ConstOwnerBody, Member>);
using OwnerBody = decltype([](Owner& owner) noexcept {
    return [&owner](MemberEpoch&) noexcept { return owner.arena.retained_bytes(); };
}(std::declval<Owner&>()));
static_assert(!EpochCaptures<OwnerBody, Member>);
using KeepingBody = decltype([](KeptRef& kept, Alloc alloc) noexcept {
    return [&kept, alloc](MemberEpoch& epoch) noexcept { kept = some(epoch.make<int>(alloc, 1)); };
}(std::declval<KeptRef&>(), std::declval<Alloc>()));
static_assert(!EpochCaptures<KeepingBody, Member>);

// An Address holds no borrow and names no brand, so a capture by a reference
// that is not const of an Option of an Address passes.  A body writes the
// address of a value of the epoch there, and the caller compares it after the
// epoch with no read of the bytes.
using AddressBody = decltype([](Option<Address>& seen) noexcept {
    return [&seen](MemberEpoch&) noexcept { seen = none; };
}(std::declval<Option<Address>&>()));
static_assert(EpochCaptures<AddressBody, Member>);

// C++ const is shallow: a const Ref of the owner and a const reference to a
// Ref of an Option still reach a place that is not const.
using ConstRefOwnerBody = decltype([](Ref<Owner, Family> const owner) noexcept {
    return [owner](MemberEpoch&) noexcept { return owner->arena.retained_bytes(); };
}(std::declval<Ref<Owner, Family>>()));
static_assert(!EpochCaptures<ConstRefOwnerBody, Member>);
using ConstRefKeepingBody = decltype([](Ref<KeptRef, Family> const& kept, Alloc alloc) noexcept {
    return [&kept, alloc](MemberEpoch& epoch) noexcept { *kept = some(epoch.make<int>(alloc, 1)); };
}(std::declval<Ref<KeptRef, Family> const&>(), std::declval<Alloc>()));
static_assert(!EpochCaptures<ConstRefKeepingBody, Member>);

// The result of an epoch holds no borrow and names no epoch brand.
using RefResult = decltype(std::declval<MemberEpoch&>().make<int>(std::declval<Alloc>(), 1));
static_assert(!EpochResult<RefResult, Member> && !EpochResult<ArenaRun<int, EpochOf<Member>>, Member>);
static_assert(EpochResult<int, Member> && EpochResult<void, Member>);

// The closed surface of each arena.
inline constexpr TextView bump_arena_names{"BumpArena make array retained_bytes max_blocks block_alignment brand_type"};
CRUCIBLE_CLOSED_SURFACE(bump_arena_names, BumpArena<Family>);
inline constexpr TextView epoch_arena_names{"EpochArena with_epoch retained_bytes set_retained_limit retained_limit "
                                            "default_retained_bytes_limit max_blocks block_alignment brand_type"};
CRUCIBLE_CLOSED_SURFACE(epoch_arena_names, EpochArena<Member>);

}  // namespace detail::bump_arena_checks

}  // namespace foundation::core
