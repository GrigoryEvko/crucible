// The compile-time checks of foundation/core/BumpArena.h.

#include <foundation/core/BumpArena.h>

#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/CallRef.h>
#include <foundation/core/DataAddress.h>
#include <foundation/core/Link.h>
#include <foundation/core/List.h>
#include <foundation/core/RunSet.h>
#include <foundation/core/StaticText.h>
#include <foundation/core/Verbs.h>

#include <cstddef>
#include <cstdint>
#include <meta>
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
// fields.  A BumpArena is its core and the identity that its positions
// carry, and an epoch arena is its core and nothing more.
static_assert(sizeof(BumpArenaCore) <= 64 + BumpArenaCore::max_blocks * (sizeof(std::byte*) + sizeof(std::size_t)));
static_assert(sizeof(BumpArena<Family>) == sizeof(BumpArenaCore) + sizeof(Identity));
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

// A run converts to a run of const elements with the same brand, also from a
// const run, and to no run of a different brand, and no run of const gives
// back a writable run.  A window of a run keeps the type of the run.
static_assert(std::is_convertible_v<ArenaRun<int, Family>, ArenaRun<int const, Family>>);
static_assert(std::is_convertible_v<ArenaRun<int, Family> const&, ArenaRun<int const, Family>>);
static_assert(!std::is_convertible_v<ArenaRun<int const, Family>, ArenaRun<int, Family>>);
static_assert(!std::is_constructible_v<ArenaRun<int, Owner::Member>, ArenaRun<int, Family>>);
static_assert(!std::is_constructible_v<ArenaRun<int const, Owner::Member>, ArenaRun<int, Family>>);
static_assert(
    std::is_same_v<decltype(std::declval<ArenaRun<int, Family>&>().window(RunStart{ElementCount{0}}, ElementCount{0})),
                   Option<ArenaRun<int, Family>>>);

// A const run gives only const access, as each class that states
// refers_deep{} does (the store check of foundation/core/CaptureStore.h
// trusts the mark): each const door gives a const element, and a const run
// does not copy into a writable run.
static_assert(std::is_same_v<decltype(std::declval<ArenaRun<int, Family>&>().view()), View<int>>);
static_assert(std::is_same_v<decltype(std::declval<ArenaRun<int, Family> const&>().view()), View<int const>>);
static_assert(std::is_same_v<decltype(std::declval<ArenaRun<int, Family> const&>().window(RunStart{ElementCount{0}},
                                                                                          ElementCount{0})),
                             Option<ArenaRun<int const, Family>>>);
static_assert(!std::is_constructible_v<ArenaRun<int, Family>, ArenaRun<int, Family> const&>);
static_assert(std::is_constructible_v<ArenaRun<int, Family>, ArenaRun<int, Family>&>);
static_assert(!std::is_assignable_v<ArenaRun<int, Family>&, ArenaRun<int, Family> const&>);
static_assert(sizeof(ArenaRun<int const, Family>) == sizeof(View<int const>));
// The closed surface of a run makes a new door of it come with its own pin
// of the const form here.
inline constexpr TextView arena_run_names{"ArenaRun brand_type view window size"};
CRUCIBLE_CLOSED_SURFACE(arena_run_names, ArenaRun<int, Family>);

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

// build_array takes an element with a trivial destructor that holds no
// linear value, and a callable that gives each element as a value of the
// element type with no throw, from the position of the element.  The element
// needs no copy, so a class that copies only from an object that is not
// const fits, and the fill of array does not take it.
struct CopiesFromWritable {
    int value = 0;
    CopiesFromWritable() noexcept = default;
    CopiesFromWritable(CopiesFromWritable&) noexcept = default;
    CopiesFromWritable(CopiesFromWritable&&) noexcept = default;
};

// A linear class.  A make that holds one by value binds to the deleted twin
// of build_array, and a run does not hold one as an element.
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] LinearEnd {
    int handle = 0;
    constexpr explicit LinearEnd(int value) noexcept : handle{value} {}
    constexpr LinearEnd(LinearEnd&&) noexcept = default;
    constexpr LinearEnd& operator=(LinearEnd&& other) noexcept {
        handle = other.handle;
        return *this;
    }
    LinearEnd(LinearEnd const&) = delete("a linear end of the check does not copy");
    LinearEnd& operator=(LinearEnd const&) = delete("a linear end of the check does not copy");
};

using MakesPlain = decltype([](RunStart) noexcept { return Plain{}; });
using MakesCopier = decltype([](RunStart) noexcept { return CopiesFromWritable{}; });
using MakesMoveOnly = decltype([](RunStart) noexcept { return MoveOnly{}; });
using MakesEnded = decltype([](RunStart) noexcept { return Ended{}; });
using MakesLinear = decltype([](RunStart) noexcept { return LinearEnd{1}; });
using MakesThrowing = decltype([](RunStart) { return Plain{}; });
using TakesNoPosition = decltype([]() noexcept { return Plain{}; });
using HoldsEnd = decltype([held = LinearEnd{1}](RunStart) noexcept {
    return Plain{.key = static_cast<std::uint64_t>(held.handle), .count = 0, .flags = 0};
});
using GivesReference = decltype([](Plain const& kept) noexcept {
    return [&kept](RunStart) noexcept -> Plain const& { return kept; };
}(std::declval<Plain const&>()));
static_assert(CanBuildArenaArray<Plain, Alloc, MakesPlain>);
static_assert(CanBuildArenaArray<CopiesFromWritable, Alloc, MakesCopier>);
static_assert(CanBuildArenaArray<MoveOnly, Alloc, MakesMoveOnly>);
static_assert(!CanFillArenaArray<CopiesFromWritable, Alloc>);
static_assert(!CanBuildArenaArray<Plain, NoMarker, MakesPlain>);
static_assert(!CanBuildArenaArray<Plain const, Alloc, MakesPlain>);
static_assert(!CanBuildArenaArray<Ended, Alloc, MakesEnded>);
static_assert(!CanBuildArenaArray<LinearEnd, Alloc, MakesLinear>);
static_assert(!CanBuildArenaArray<Plain, Alloc, MakesThrowing>);
static_assert(!CanBuildArenaArray<Plain, Alloc, TakesNoPosition>);
static_assert(!CanBuildArenaArray<Plain, Alloc, GivesReference>);
static_assert(!CanBuildArenaArray<CopiesFromWritable, Alloc, MakesPlain>);

// build_array gives a run of the brand of the arena or of the epoch.  It
// takes the count as an ElementCount, and no arena of a temporary, no epoch
// arena and no make that holds a linear value.
template <class Arena, class Make, class Count = ElementCount>
concept BuildsPlains = requires {
    std::declval<Arena>().template build_array<Plain>(std::declval<Alloc>(), std::declval<Count>(),
                                                      std::declval<Make>());
};
static_assert(BuildsPlains<BumpArena<Family>&, MakesPlain> && !BuildsPlains<BumpArena<Family>&&, MakesPlain>);
static_assert(!BuildsPlains<BumpArena<Family>&, MakesPlain, std::size_t>);

// make takes no type that holds a linear value, because the arena ends no
// object: a linear class, and a class that is linear by its member.  The make
// of such a class binds to the deleted twin of make, on an arena and on an
// epoch.  array takes no such type either.  A class that holds a linear value
// and copies is refused by the rule linear-containment of the plugin, so no
// check of this file names one.
struct HoldsLinearEnd {
    LinearEnd end{1};
    int count = 0;
};
static_assert(std::is_trivially_destructible_v<LinearEnd> && std::is_trivially_destructible_v<HoldsLinearEnd>);
static_assert(!CanMakeInArena<LinearEnd, Alloc, LinearEnd&&> && MakesLinearInArena<LinearEnd, Alloc, LinearEnd&&>);
static_assert(!CanMakeInArena<HoldsLinearEnd, Alloc, HoldsLinearEnd&&>
              && MakesLinearInArena<HoldsLinearEnd, Alloc, HoldsLinearEnd&&>);
static_assert(!MakesLinearInArena<Plain, Alloc> && !MakesLinearInArena<LinearEnd, NoMarker, LinearEnd&&>);
template <class Arena, class T>
concept MakesHeld = requires { std::declval<Arena>().template make<T>(std::declval<Alloc>(), std::declval<T>()); };
static_assert(MakesHeld<BumpArena<Family>&, Plain> && MakesHeld<Epoch<EpochOf<Owner::Member>>&, Plain>);
static_assert(!MakesHeld<BumpArena<Family>&, LinearEnd> && !MakesHeld<BumpArena<Family>&, HoldsLinearEnd>);
static_assert(!MakesHeld<Epoch<EpochOf<Owner::Member>>&, LinearEnd>
              && !MakesHeld<Epoch<EpochOf<Owner::Member>>&, HoldsLinearEnd>);
static_assert(!BuildsPlains<BumpArena<Family>&, HoldsEnd> && !BuildsPlains<BumpArena<Family>&, HoldsEnd&>);
static_assert(BuildsPlains<Epoch<EpochOf<Owner::Member>>&, MakesPlain>
              && !BuildsPlains<Epoch<EpochOf<Owner::Member>>&&, MakesPlain>);
static_assert(!BuildsPlains<Epoch<EpochOf<Owner::Member>>&, HoldsEnd>);
static_assert(!BuildsPlains<EpochArena<Owner::Member>&, MakesPlain>);
static_assert(std::is_same_v<decltype(std::declval<BumpArena<Family>&>().build_array<Plain>(
                                 std::declval<Alloc>(), ElementCount{2}, std::declval<MakesPlain>())),
                             ArenaRun<Plain, Family>>);
static_assert(std::is_same_v<decltype(std::declval<Epoch<EpochOf<Owner::Member>>&>().build_array<Plain>(
                                 std::declval<Alloc>(), ElementCount{2}, std::declval<MakesPlain>())),
                             ArenaRun<Plain, EpochOf<Owner::Member>>>);

// The objects of an arena.  A class of scalars passes, and so does a borrow
// that names the brand of the arena, also at a depth, in an Option, in a list,
// in a cell and in a run set.
struct[[= ::foundation::brand::arena_family_brand{}]] OtherFamily final {};
struct HoldsBorrows {
    Ref<int, Family> ref;
    Link<Plain, Family> link{};
    ArenaRun<Plain const, Family> run{};
    Option<Ref<int const, Family>> option{none};
    FixedList<Ref<int, Family>, 2> list{};
    Atomic<Option<Ref<int const, Family>>> cell{};
    ArenaRunSet<std::uint16_t, Family, RunOf<int const, 0>> set{};
};
struct HoldsBorrowsDeep : HoldsBorrows {
    FixedArray<HoldsBorrows, 2> array;
};
static_assert(ArenaKeeps<int, Family> && ArenaKeeps<Plain, Family> && ArenaKeeps<FixedArray<Plain, 3>, Family>);
static_assert(ArenaKeeps<HoldsBorrows, Family> && ArenaKeeps<HoldsBorrowsDeep, Family>);
static_assert(!ArenaKeeps<HoldsBorrows, OtherFamily> && !ArenaKeeps<HoldsBorrowsDeep, OtherFamily>);
static_assert(ArenaKeeps<ArenaPosition<int, OtherFamily>, Family>, "a position of an object is no borrow");

// A borrow of a different named tag passes only when the tag of the arena
// states that tag.  A statement has one direction and names one tag.
struct[[= ::foundation::brand::arena_family_brand{}]][[= ::foundation::brand::outlived_by<OtherFamily>{}]]
      [[= ::foundation::brand::outlived_by<Owner::Member>{}]] Outlived final {};
struct HoldsOutliving {
    Ref<int, Outlived> own;
    Link<Plain, OtherFamily> link{};
    ArenaRun<Plain const, Owner::Member> run{};
    Option<Ref<int const, OtherFamily>> option{none};
    ArenaRunSet<std::uint16_t, OtherFamily, RunOf<int const, 0>> set{};
};
static_assert(ArenaKeeps<HoldsOutliving, Outlived> && !ArenaKeeps<HoldsOutliving, OtherFamily>);
static_assert(ArenaKeeps<Ref<int, OtherFamily>, Outlived> && !ArenaKeeps<Ref<int, Outlived>, OtherFamily>);
static_assert(ArenaKeeps<Ref<int, Owner::Member>, Outlived> && !ArenaKeeps<Ref<int, Family>, Outlived>);
static_assert(!ArenaKeeps<Ref<int, OtherFamily>, Family> && !ArenaKeeps<Ref<int, Owner::Member>, Family>);
static_assert(!ArenaKeeps<Link<int, OtherFamily>, Family> && !ArenaKeeps<ArenaRun<int, Owner::Member>, Family>);

// An object of an epoch keeps only borrows of that epoch, also when the member
// tag of its arena states a tag.
using EpochRef = Ref<int, EpochOf<Owner::Member>>;
struct StatingOwner {
    struct[[= ::foundation::brand::arena_member_brand{}]][[= ::foundation::brand::outlived_by<Family>{}]] Tag final {};
    EpochArena<Tag> arena = mint_epoch_arena<Tag>(Bytes{4096});
};
static_assert(ArenaKeeps<EpochRef, EpochOf<Owner::Member>> && !ArenaKeeps<EpochRef, Family>);
static_assert(!ArenaKeeps<EpochRef, Owner::Member> && !ArenaKeeps<EpochRef, EpochOf<StatingOwner::Tag>>);
static_assert(!ArenaKeeps<Ref<int, Family>, EpochOf<Owner::Member>>);
static_assert(!ArenaKeeps<ArenaRun<int, Owner::Member>, EpochOf<Owner::Member>>);
static_assert(ArenaKeeps<Ref<int, Family>, StatingOwner::Tag>
              && !ArenaKeeps<Ref<int, Family>, EpochOf<StatingOwner::Tag>>);

// An object of the arena of a scope keeps only borrows of that scope, and a
// class that names a scope brand proves a fact of a frame.
template <class Brand>
struct Proof {};
[[nodiscard]] consteval bool keeps_a_scope_brand_in_its_arena() {
    using Scope = CRUCIBLE_SCOPE_BRAND;
    return ArenaKeeps<Ref<int, Scope>, Scope> && ArenaKeeps<ArenaRun<int, Scope>, Scope>
        && !ArenaKeeps<Ref<int, Scope>, Family> && !ArenaKeeps<Ref<int, Scope>, EpochOf<Owner::Member>>
        && !ArenaKeeps<Ref<int, Family>, Scope> && !ArenaKeeps<ViewMut<int, Scope>, Scope>
        && !ArenaKeeps<Proof<Scope>, Family>;
}
static_assert(keeps_a_scope_brand_in_its_arena());

// A borrow that names no arena is a refusal: a View, a Ref of a brand that is
// no arena tag, an erased Ref, a CallRef, a set of runs of any owner, and each
// class that holds one by value.
struct HoldsView {
    View<int const> view{};
};
struct HoldsViewDeep {
    FixedArray<HoldsView, 2> array{};
};
struct DerivedOfView : HoldsView {};
using ErasedRef = decltype(erase_brand(std::declval<Ref<int, Unmarked>>()));
static_assert(!ArenaKeeps<View<int const>, Family> && !ArenaKeeps<TextView, Family>);
static_assert(!ArenaKeeps<Ref<int, Unmarked>, Family> && !ArenaKeeps<ErasedRef, Family>);
// A statement admits no brand that is no named tag.
struct[[= ::foundation::brand::arena_family_brand{}]][[= ::foundation::brand::outlived_by<Unmarked>{}]]
      [[= ::foundation::brand::outlived_by<::foundation::brand::DefaultBrand>{}]]
      [[= ::foundation::brand::outlived_by<EpochOf<Owner::Member>>{}]] StatesNoTag final {};
static_assert(!ArenaKeeps<Ref<int, Unmarked>, StatesNoTag> && !ArenaKeeps<ErasedRef, StatesNoTag>);
static_assert(!ArenaKeeps<EpochRef, StatesNoTag> && ArenaKeeps<Ref<int, StatesNoTag>, StatesNoTag>);
static_assert(!ArenaKeeps<Link<int, Unmarked>, Family> && !ArenaKeeps<ArenaRun<int, Unmarked>, Family>);
static_assert(!ArenaKeeps<HoldsView, Family> && !ArenaKeeps<HoldsViewDeep, Family>);
static_assert(!ArenaKeeps<DerivedOfView, Family> && !ArenaKeeps<Option<View<int const>>, Family>);
static_assert(!ArenaKeeps<FixedList<View<int const>, 2>, Family>);
static_assert(!ArenaKeeps<Atomic<Option<Ref<int const, Unmarked>>>, Family>);
static_assert(!ArenaKeeps<RunSet<std::uint16_t, RunOf<int const, 0>>, Family>);
static_assert(!ArenaKeeps<CallRef<int() noexcept>, Family>);
static_assert(!ArenaKeeps<RunAddress, Family> && !ArenaKeeps<StaticText, Family>,
              "each holds a pointer that its type arguments do not name");

// A reference member, a pointer to an object and an incomplete class are
// refusals.  A pointer to a function and a pointer to member pass.  A void*
// passes only in a class that names it as a type argument.
struct HoldsReference {
    Plain& target;
};
struct Declared;
template <class Value>
struct Stated {
    Value value{};
};
static_assert(!ArenaKeeps<HoldsReference, Family> && !ArenaKeeps<Declared, Family>);
// A reference to a scalar or to a byte borrows its target too, at each depth.
struct HoldsScalarReference {
    int& number;
};
struct HoldsByteReference {
    unsigned char const& byte;
};
struct HoldsScalarReferenceDeep {
    HoldsScalarReference inner;
};
struct DerivedOfScalarReference : HoldsScalarReference {};
enum class Shade : unsigned char {
    dark,
};
struct HoldsEnumReference {
    Shade& shade;
};
static_assert(!ArenaKeeps<HoldsScalarReference, Family> && !ArenaKeeps<HoldsByteReference, Family>);
static_assert(!ArenaKeeps<HoldsEnumReference, Family> && !ArenaKeeps<DerivedOfScalarReference, Family>);
static_assert(!ArenaKeeps<HoldsScalarReferenceDeep, Family> && !ArenaKeeps<FixedList<HoldsScalarReference, 2>, Family>);
static_assert(!ArenaKeeps<Option<HoldsByteReference>, Family> && !ArenaKeeps<HoldsByteReference, OtherFamily>);
static_assert(!holds_loose_borrow_(^^void (*)(int), ^^Family, PartNaming::by_member));
static_assert(!holds_loose_borrow_(^^int Plain::*, ^^Family, PartNaming::by_member));
static_assert(holds_loose_borrow_(^^int*, ^^Family, PartNaming::by_argument));
static_assert(holds_loose_borrow_(^^void*, ^^Family, PartNaming::by_member));
static_assert(!holds_loose_borrow_(^^void*, ^^Family, PartNaming::by_argument));
static_assert(!holds_loose_borrow_(^^void const*, ^^Family, PartNaming::by_argument));
static_assert(!holds_loose_borrow_(^^Stated<void*>, ^^Family, PartNaming::by_member));
static_assert(!holds_loose_borrow_(^^Stated<Stated<void*>>, ^^Family, PartNaming::by_member));
static_assert(holds_loose_borrow_(^^Stated<int*>, ^^Family, PartNaming::by_member));

// Each door of an arena and of an epoch takes a type that it can keep, and no
// other one.
template <class Arena, class T>
concept FillsHeld = requires { std::declval<Arena>().array(std::declval<Alloc>(), 2, std::declval<T const&>()); };
struct MakesView {
    [[nodiscard]] View<int const> operator()(RunStart) const noexcept { return {}; }
};
struct MakesFamilyRun {
    [[nodiscard]] ArenaRun<int, Family> operator()(RunStart) const noexcept { return {}; }
};
using EpochRun = ArenaRun<int, EpochOf<Owner::Member>>;
struct MakesEpochRun {
    [[nodiscard]] EpochRun operator()(RunStart) const noexcept { return {}; }
};
template <class Arena, class T, class Make>
concept BuildsHeld = requires {
    std::declval<Arena>().template build_array<T>(std::declval<Alloc>(), ElementCount{2}, std::declval<Make>());
};
using FamilyArena = BumpArena<Family>&;
using MemberEpochArena = Epoch<EpochOf<Owner::Member>>&;
static_assert(MakesHeld<FamilyArena, Ref<int, Family>> && !MakesHeld<MemberEpochArena, Ref<int, Family>>);
static_assert(!MakesHeld<FamilyArena, HoldsView> && !MakesHeld<MemberEpochArena, HoldsView>);
static_assert(MakesHeld<MemberEpochArena, EpochRef> && !MakesHeld<FamilyArena, EpochRef>);
static_assert(!MakesHeld<FamilyArena, Ref<int, OtherFamily>>);
static_assert(FillsHeld<FamilyArena, Ref<int, Family>> && !FillsHeld<MemberEpochArena, Ref<int, Family>>);
static_assert(FillsHeld<MemberEpochArena, EpochRef> && !FillsHeld<FamilyArena, Ref<int, OtherFamily>>);
static_assert(!FillsHeld<FamilyArena, HoldsView> && !FillsHeld<MemberEpochArena, HoldsView>);
static_assert(BuildsHeld<FamilyArena, ArenaRun<int, Family>, MakesFamilyRun>);
static_assert(BuildsHeld<MemberEpochArena, EpochRun, MakesEpochRun>);
static_assert(!BuildsHeld<MemberEpochArena, ArenaRun<int, Family>, MakesFamilyRun>);
static_assert(!BuildsHeld<FamilyArena, View<int const>, MakesView>);
static_assert(!BuildsHeld<MemberEpochArena, View<int const>, MakesView>);

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

// The build key.  A class that states an arena tag gives that tag to each of its
// objects only when each constructor takes the key of the class and the tag by
// value, and when no copy, no move and no bytes build an object.  Each plant
// below breaks one part of the rule.
template <class T>
using FamilyKey = ArenaBuildKey<Family, T>;
struct Keyed {
    using brand_type = Family;
    explicit constexpr Keyed(FamilyKey<Keyed> /*key*/) noexcept {}
    Keyed(Keyed const&) = delete("an object of the arena stays in it");
    Keyed(Keyed&&) = delete("an object of the arena stays in it");
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
    int value = 0;
};
struct DerivedKeyed : Keyed {
    explicit constexpr DerivedKeyed(FamilyKey<DerivedKeyed> key) noexcept : Keyed{std::move(key).for_base<Keyed>()} {}
};
struct Loan {
    using brand_type = Family;
    int value = 0;
};
struct NoSeal {
    using brand_type = Family;
    explicit constexpr NoSeal(FamilyKey<NoSeal> /*key*/) noexcept {}
    NoSeal(NoSeal const&) = delete("an object of the arena stays in it");
    NoSeal(NoSeal&&) = delete("an object of the arena stays in it");
};
struct OpenBeside : Keyed {
    explicit constexpr OpenBeside(FamilyKey<OpenBeside> key) noexcept : Keyed{std::move(key).for_base<Keyed>()} {}
    explicit constexpr OpenBeside(FamilyKey<Keyed>& /*key*/, int /*number*/) noexcept;
};
struct TemplateBuilt : Keyed {
    explicit constexpr TemplateBuilt(FamilyKey<TemplateBuilt> key) noexcept : Keyed{std::move(key).for_base<Keyed>()} {}
    template <class Given>
    explicit constexpr TemplateBuilt(Given const& given, int number) noexcept;
};
struct DefaultBuilt {
    using brand_type = Family;
    constexpr DefaultBuilt() noexcept {}
    explicit constexpr DefaultBuilt(FamilyKey<DefaultBuilt> /*key*/) noexcept {}
    DefaultBuilt(DefaultBuilt const&) = delete("an object of the arena stays in it");
    DefaultBuilt(DefaultBuilt&&) = delete("an object of the arena stays in it");
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};
struct CopyBuilt {
    using brand_type = Family;
    explicit constexpr CopyBuilt(FamilyKey<CopyBuilt> /*key*/) noexcept {}
    constexpr CopyBuilt(CopyBuilt const& /*other*/) noexcept {}
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};
struct MoveBuilt {
    using brand_type = Family;
    explicit constexpr MoveBuilt(FamilyKey<MoveBuilt> /*key*/) noexcept {}
    MoveBuilt(MoveBuilt const&) = delete("an object of the arena stays in it");
    constexpr MoveBuilt(MoveBuilt&& /*other*/) noexcept {}
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};
struct OpenBase {
    explicit constexpr OpenBase(int number) noexcept : value{number} {}
    int value = 0;
};
struct InheritsOpen : OpenBase {
    using brand_type = Family;
    using OpenBase::OpenBase;
    explicit constexpr InheritsOpen(FamilyKey<InheritsOpen> /*key*/) noexcept : OpenBase{0} {}
    InheritsOpen(InheritsOpen const&) = delete("an object of the arena stays in it");
    InheritsOpen(InheritsOpen&&) = delete("an object of the arena stays in it");
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};
struct OtherTagBuilt {
    using brand_type = Family;
    explicit constexpr OtherTagBuilt(ArenaBuildKey<OtherFamily, OtherTagBuilt> /*key*/) noexcept {}
    OtherTagBuilt(OtherTagBuilt const&) = delete("an object of the arena stays in it");
    OtherTagBuilt(OtherTagBuilt&&) = delete("an object of the arena stays in it");
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};
struct OtherClassBuilt {
    using brand_type = Family;
    explicit constexpr OtherClassBuilt(FamilyKey<Keyed> /*key*/) noexcept {}
    OtherClassBuilt(OtherClassBuilt const&) = delete("an object of the arena stays in it");
    OtherClassBuilt(OtherClassBuilt&&) = delete("an object of the arena stays in it");
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};
struct ReferenceBuilt {
    using brand_type = Family;
    explicit constexpr ReferenceBuilt(FamilyKey<ReferenceBuilt> const& /*key*/) noexcept {}
    ReferenceBuilt(ReferenceBuilt const&) = delete("an object of the arena stays in it");
    ReferenceBuilt(ReferenceBuilt&&) = delete("an object of the arena stays in it");
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};
struct OwnKey final {};
struct OwnKeyBuilt {
    using brand_type = Family;
    explicit constexpr OwnKeyBuilt(OwnKey /*key*/) noexcept {}
    OwnKeyBuilt(OwnKeyBuilt const&) = delete("an object of the arena stays in it");
    OwnKeyBuilt(OwnKeyBuilt&&) = delete("an object of the arena stays in it");
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};
template <class T>
consteval bool is_built_by_family() {
    return is_built_only_by_arena_(^^T, ^^Family) && brand_statement_v<T> == BrandStatement::of_object
        && brand_statement_v<T const&> == BrandStatement::of_object;
}
template <class T>
consteval bool is_unproven() {
    return !is_built_only_by_arena_(^^T, ^^Family) && brand_statement_v<T> == BrandStatement::unproven;
}
static_assert(is_built_by_family<Keyed>() && is_built_by_family<DerivedKeyed>());
static_assert(is_unproven<Loan>() && is_unproven<NoSeal>() && is_unproven<OpenBeside>());
static_assert(is_unproven<TemplateBuilt>() && is_unproven<DefaultBuilt>() && is_unproven<CopyBuilt>());
static_assert(is_unproven<MoveBuilt>() && is_unproven<InheritsOpen>() && is_unproven<OtherTagBuilt>());
static_assert(is_unproven<OtherClassBuilt>() && is_unproven<ReferenceBuilt>() && is_unproven<OwnKeyBuilt>());
static_assert(std::is_same_v<decltype(to(std::declval<Keyed&>())), Ref<Keyed, Family>>);
static_assert(std::is_same_v<decltype(to(std::declval<DerivedKeyed const&>())), Ref<DerivedKeyed const, Family>>);

// An owner of an arena tag is no object of an arena, so a door gives it a fresh
// brand.  A class outside the base that states branded_arena stays unproven (the
// fixture neg_core_to_of_a_product_class_with_branded_arena).
static_assert(brand_statement_v<BumpArena<Family>> == BrandStatement::of_owner);
static_assert(brand_statement_v<EpochArena<Member>> == BrandStatement::of_owner);
static_assert(brand_statement_v<MemberEpoch> == BrandStatement::of_owner);
template <class Object, class Tag>
constexpr bool has_brand_of_tag = std::is_same_v<decltype(to(std::declval<Object&>())), Ref<Object, Tag>>;
static_assert(!has_brand_of_tag<BumpArena<Family>, Family> && !has_brand_of_tag<EpochArena<Member>, Member>);
static_assert(!has_brand_of_tag<MemberEpoch, EpochOf<Member>> && has_brand_of_tag<Keyed, Family>);

// make gives the key to a class that takes it, with and with no arguments after
// it, and no other code makes one.  A key has no copy, no move and no bytes form.
struct KeyedWithValue : Keyed {
    constexpr KeyedWithValue(FamilyKey<KeyedWithValue> key, int number) noexcept
        : Keyed{std::move(key).for_base<Keyed>()} {
        value = number;
    }
};
template <class T, class... Args>
constexpr bool family_makes =
    requires(BumpArena<Family>& arena, Alloc alloc, Args... args) { arena.make<T>(alloc, args...); };
static_assert(family_makes<Keyed> && family_makes<DerivedKeyed> && family_makes<KeyedWithValue, int>);
static_assert(!family_makes<KeyedWithValue> && !family_makes<OtherTagBuilt> && !family_makes<OtherClassBuilt>);
static_assert(ArenaMakes<Plain, Family, Alloc> && !ArenaMakes<Keyed, OtherFamily, Alloc>);
static_assert(!std::is_default_constructible_v<FamilyKey<Keyed>> && !std::is_copy_constructible_v<FamilyKey<Keyed>>);
static_assert(!std::is_move_constructible_v<FamilyKey<Keyed>> && !std::is_trivially_copyable_v<FamilyKey<Keyed>>);
static_assert(!std::is_constructible_v<FamilyKey<Keyed>, FamilyKey<DerivedKeyed>>);
static_assert(!ChoicePayload<FamilyKey<Keyed>> && !std::is_constructible_v<Record<FamilyKey<Keyed>>, FamilyKey<Keyed>>);

// The closed surface of each arena.
inline constexpr TextView bump_arena_names{
    "BumpArena make array build_array retained_bytes max_blocks block_alignment brand_type position_of"};
CRUCIBLE_CLOSED_SURFACE(bump_arena_names, BumpArena<Family>);
inline constexpr TextView epoch_arena_names{"EpochArena with_epoch retained_bytes set_retained_limit retained_limit "
                                            "default_retained_bytes_limit max_blocks block_alignment brand_type"};
CRUCIBLE_CLOSED_SURFACE(epoch_arena_names, EpochArena<Member>);

// The doors of an epoch.  An epoch states keeps_own_borrows{}, so the store
// check of a frame body reads no member of it.  That holds because each door
// that stores an object, make, array and build_array, runs ArenaKeeps (the pins
// MakesHeld, FillsHeld and BuildsHeld above) and no door writes a member of the
// epoch.  The closed surface pins that list of doors.  The control: a class
// with the same doors is a closed surface of its list, and with one more door
// it is not.
inline constexpr TextView epoch_names{"Epoch make array build_array brand_type"};
CRUCIBLE_CLOSED_SURFACE(epoch_names, MemberEpoch);
static_assert(type_parts_v<MemberEpoch>.keeps_own_borrows && !type_parts_v<BumpArena<Family>>.keeps_own_borrows);
template <class Brand, bool HasStore>
struct EpochShape {
    using brand_type = Brand;
    template <class T>
    void make(T const& /*value*/) noexcept {}
    template <class T>
    void array(T const& /*value*/) noexcept {}
    template <class T>
    void build_array(T const& /*value*/) noexcept {}
    void keep(Ref<int, Brand> /*borrow*/) noexcept
        requires HasStore
    {}
};
inline constexpr TextView epoch_shape_names{"EpochShape make array build_array brand_type"};
static_assert(closed_surface<EpochShape<EpochOf<Member>, false>>(epoch_shape_names).is_closed());
static_assert(!closed_surface<EpochShape<EpochOf<Member>, true>>(epoch_shape_names).is_closed());

}  // namespace detail::bump_arena_checks

}  // namespace foundation::core
