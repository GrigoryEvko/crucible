// The compile-time checks of foundation/core/ArenaFrame.h.

#include <foundation/core/ArenaFrame.h>

#include <foundation/Brand.h>
#include <foundation/Transfer.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Link.h>
#include <foundation/core/Ref.h>

#include <cstdint>
#include <type_traits>

namespace foundation::core {

namespace detail::arena_frame_checks {

// The family of the checks, and a second family that states no relation to
// it.
struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};
struct[[= ::foundation::brand::arena_family_brand{}]] OtherFamily final {};

// An object with the two slots of the family.
struct WithSlots {
    std::uint32_t value = 0;
    FrameLink<WithSlots, Family> next{};
    FrameRunSlot<std::uint32_t const, Family> items{};
};

// A slot does not copy and does not move, so no copy carries a link into an
// object of a different arena.
static_assert(!std::is_copy_constructible_v<FrameLink<WithSlots, Family>>);
static_assert(!std::is_move_constructible_v<FrameLink<WithSlots, Family>>);
static_assert(!std::is_copy_assignable_v<FrameLink<WithSlots, Family>>);
static_assert(!std::is_copy_constructible_v<FrameRunSlot<std::uint32_t const, Family>>);
static_assert(!std::is_copy_assignable_v<FrameRunSlot<std::uint32_t const, Family>>);

// An arena of the family keeps an object with the slots of its family, and an
// arena of another family refuses it (ArenaKeeps of foundation/core/BumpArena.h).
static_assert(ArenaKeeps<WithSlots, Family>);
static_assert(!ArenaKeeps<WithSlots, OtherFamily>);

// The same object with a Link and an ArenaRun in place of the two slots.
struct WithFamilyForms {
    std::uint32_t value = 0;
    Link<WithFamilyForms, Family> next{};
    ArenaRun<std::uint32_t const, Family> items{};
};

// A slot takes the bytes of the family form that it holds, so an object keeps
// its size when a slot takes the place of a Link or an ArenaRun.
static_assert(sizeof(WithSlots) == sizeof(WithFamilyForms));
static_assert(sizeof(FrameLink<WithSlots, Family>) == sizeof(Link<WithSlots, Family>));
static_assert(sizeof(FrameRunSlot<std::uint32_t const, Family>) == sizeof(ArenaRun<std::uint32_t const, Family>));

// A set of runs with two slots of counts: two counts of 16 bits and two
// addresses, with bytes of pad at its end.
using CountedRuns = ArenaRunSet<std::uint16_t, Family, RunOf<std::uint32_t const, 0>, RunOf<std::uint64_t const, 1>>;
using CountedSlot =
    FrameRunSetSlot<std::uint16_t, Family, RunOf<std::uint32_t const, 0>, RunOf<std::uint64_t const, 1>>;

// An object that holds the set and then two small members, as TraceEntry of
// crucible/MerkleDag.h holds its arrays and then its flags.  The members take
// the bytes of pad at the end of the set, also when a slot holds the set.
struct WithCountedRuns {
    [[no_unique_address]] CountedRuns runs{};
    std::uint8_t first = 0;
    std::uint8_t second = 0;
};
struct WithCountedSlot {
    [[no_unique_address]] CountedSlot runs{};
    std::uint8_t first = 0;
    std::uint8_t second = 0;
};
static_assert(sizeof(CountedSlot) == sizeof(CountedRuns));
static_assert(sizeof(WithCountedRuns) == sizeof(CountedRuns));
static_assert(sizeof(WithCountedSlot) == sizeof(WithCountedRuns));

// A slot moves between threads and is shared as the family form that it holds
// is (foundation/Transfer.h).
static_assert(::foundation::transfer::Sendable<WithSlots> == ::foundation::transfer::Sendable<WithFamilyForms>);
static_assert(::foundation::transfer::Shareable<WithSlots> == ::foundation::transfer::Shareable<WithFamilyForms>);

// Only a frame makes a FrameRef: a family Ref gives none.  No class holds a
// FrameRef, a FrameRun or a FrameElement as a member, because the rule
// brand-escape refuses a member that names a scope brand.  The check reads the
// type in the body of a function, where the scope brand of a frame lives.
consteval bool is_frame_ref_only_of_a_frame() {
    using Fresh = CRUCIBLE_SCOPE_BRAND;
    return !std::is_constructible_v<FrameRef<std::uint32_t, Family, Fresh>, Ref<std::uint32_t, Family>>
        && !std::is_constructible_v<FrameRun<std::uint32_t, Family, Fresh>, ArenaRun<std::uint32_t, Family>>;
}
static_assert(is_frame_ref_only_of_a_frame());

// A parcel owns its family tag and each element type (rule 6 of
// foundation/Transfer.h), so it holds a property when each element type holds
// it.  An element type at the second position that is not Sendable refuses
// the parcel, as one at the first position does.
using ScalarParcel = ArenaParcel<Family, std::uint32_t, std::uint64_t>;
static_assert(::foundation::transfer::Sendable<ScalarParcel> && ::foundation::transfer::SendableOwned<ScalarParcel>);
static_assert(::foundation::transfer::SendableBeside<ScalarParcel> && ::foundation::transfer::Shareable<ScalarParcel>);
static_assert(!::foundation::transfer::Sendable<ArenaParcel<Family, std::uint32_t, int*>>);
static_assert(!::foundation::transfer::Sendable<ArenaParcel<Family, int*, std::uint32_t>>);
static_assert(!::foundation::transfer::Shareable<ArenaParcel<Family, std::uint32_t, int*>>);

}  // namespace detail::arena_frame_checks

}  // namespace foundation::core
