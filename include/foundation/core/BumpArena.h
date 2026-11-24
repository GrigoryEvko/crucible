#pragma once

// The arenas of the Ref family: bump allocators over a list of blocks, each
// with a brand that names the arena.
//
//   mint_bump_arena<Tag>(block_size)   a BumpArena<Tag> of a named arena tag
//   with_bump_arena(alloc, block_size, body)
//                                      calls body with a BumpArena of the
//                                      scope brand of the call, and ends the
//                                      arena after the body
//   mint_box<BumpArena<Tag>>(alloc, block_size)
//                                      a Box of a BumpArena of a named tag
//   arena.make<T>(alloc, args...)      one T in the arena, as a Ref<T, Brand>
//   arena.array(alloc, count, fill)    count copies of fill in the arena, as an
//                                      ArenaRun<T, Brand>.  A count of zero
//                                      gives the empty run and takes no bytes
//   arena.build_array<T>(alloc, count, make)
//                                      count elements in the arena, each the
//                                      value that make(RunStart) gives for its
//                                      position, built in its place with no
//                                      copy, as an ArenaRun<T, Brand>
//   mint_epoch_arena<Tag>(block_size)  an EpochArena<Tag> of a member tag
//   arena.with_epoch(body)             calls body with the Epoch of a new
//                                      epoch, and then ends each byte that the
//                                      epoch gave out
//   epoch.make<T>(...), epoch.array(...), epoch.build_array<T>(...)
//                                      as above, with the brand EpochOf<Tag>
//   retained_bytes()                   the bytes of each block of the arena
//   arena.position_of(object)          the ArenaPosition of an object that the
//                                      arena made, or no value for an object
//                                      of another arena
//   arena.at(position)                 the object of a position of this arena,
//                                      as a Ref of the brand, or no value for
//                                      a position of another arena
//   ArenaBlockSize                     the block_size of each door: a constant
//                                      Quantity above zero, or such a size
//                                      plus(more) bytes.  A constant of zero
//                                      bytes does not compile
//
// An allocation is a bump of an offset in the current block and a mask of
// the address for the alignment.  The allocation capability is the first
// argument, so each allocation shows at its call site.  When the block is
// full, the cold path moves to the next block that holds the request.  Block
// k holds at least block_size << k bytes (saturating), so 64 blocks hold more
// bytes than an address space has, and the list is a fixed array with no
// heap of its own.  Each block comes from the allocation door of
// foundation/core/Heap.h.  The state and the cold bodies are in one core that
// is no template, detail::BumpArenaCore, so each brand shares one copy of
// the cold code.
//
// The arena ends no object, so make, array and build_array take only a type
// with a trivial destructor, and build_array only an element that holds no
// linear value.  A Ref or an ArenaRun of an arena owns nothing, and its
// bytes die when the arena ends, or when the epoch that gave them ends.
//
// Brands of an arena
//   The brand of an arena is its first template argument, and each Ref and
//   each ArenaRun of the arena carries it.  The brand is one of three kinds:
//     - A member tag (foundation::brand::arena_member_brand).  A class nests
//       the tag beside its one member of the arena.  The rule brand-escape of
//       the quarantine plugin refuses a tag that does not name exactly one
//       member, and a local or a static object of an arena of the tag.
//     - A family tag (foundation::brand::arena_family_brand), such as the
//       arenas of the regions of the DAG.  No rule counts its arenas, so two
//       arenas of one family share the brand: a Ref of one fits a field that
//       names the other.  That instance identity needs a run-time id, or a
//       graph type that takes the brand of its own arena.
//     - The scope brand of with_bump_arena.  The arena lives in the frame of
//       the door, and the result of the body holds no borrow and names no
//       brand of the scope.
//   make, array and build_array take the arena by a parameter that
//   CRUCIBLE_LIFETIMEBOUND marks.  So the rule view-escape refuses a return of a Ref or a run of a
//   local arena.  erase_brand of foundation/core/Ref.h refuses the scope
//   brand and each brand of an arena: a member tag, a family tag and an
//   epoch brand.  So a Ref of an arena keeps the brand of its arena.  Two
//   objects of one owner class share a member tag, which no type tells
//   apart.
//
// Epochs
//   An EpochArena<Tag> of a member tag has no make, no array and no
//   build_array.  It gives them only in with_epoch(body), through the Epoch<EpochOf<Tag>> that the
//   body takes by reference.  So the arena holds no byte when an epoch opens,
//   and the epoch covers each byte that the arena gives out during the body.
//   After the body, each of those bytes dies, and the arena keeps its blocks
//   for the next epoch.  A loop that runs an epoch in each turn thus takes no
//   block in its steady state.  An arena that holds more than its retained
//   limit at the end gives back each block but the first.  The limit is
//   default_retained_bytes_limit until set_retained_limit sets a different
//   one.  The brand EpochOf<Tag> is the same for each epoch of the tag, so a
//   callee in a source file takes Epoch<EpochOf<Tag>>& with a concrete type.
//   The door refuses at compile time:
//     - a result of the body that holds a borrow or names the epoch brand
//     - a capture that reaches a place that is not const and whose type can
//       hold the epoch brand, by value, through a reference, a pointer or a
//       borrow such as a Ref
//     - a capture that reaches the arena in a place that is not const,
//       because a body that reaches the arena could open a second epoch.  A
//       const arena opens no epoch, because with_epoch is a door of an arena
//       that is not const
//   C++ const is shallow: a const place makes const only the parts that it
//   holds by value.  The target of a pointer, a reference or a borrow keeps
//   its own const, so a const pointer to an owner still reaches its arena.
//   The walk of the captures fails closed: a class whose members it cannot
//   read, and a capture that reaches more than epoch_gate_node_limit types,
//   are refusals.  The rule brand-escape refuses the epoch brand in an
//   object of static or thread storage duration, and in a member of a class
//   that does not state it as its brand_type (a carrier of the epoch).  So a
//   body reaches no arena and no value of an epoch through a static object.
//   A plain View that view() of an ArenaRun gives carries no brand, so a field
//   that stores it can outlive the epoch of its bytes.  That is the general
//   class of a dangling View, which a brand of View would close.
//
//   With AddressSanitizer, a new block and the end of an epoch poison each
//   block, and each allocation unpoisons only the bytes that it gives
//   (foundation/os/Poison.h).  A read of an ended epoch, or past the bytes of
//   an allocation, then ends the process with a report.
//
// The cold path, the end of an epoch and the end of the arena are in
// src/foundation/os/BumpArena.cpp, which calls the allocation door and the
// poison door.  The poison door is in a layer above this header, so this
// header declares the one call that the bump needs.

#include <foundation/Brand.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/Transfer.h>
#include <foundation/core/AllocTag.h>
#include <foundation/core/ArenaTag.h>
#include <foundation/core/Arithmetic.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Identity.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/Report.h>
#include <foundation/core/Side.h>
#include <foundation/reflect/Members.h>
#include <foundation/reflect/TypeComponents.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <new>
#include <type_traits>

namespace foundation::core {

// The arguments of make<T>: a type that the arena can hold and that the
// arguments build with no throw, and the allocation capability.
template <class T, class Marker, class... Args>
concept CanMakeInArena = RefTarget<T> && !std::is_const_v<T> && std::is_trivially_destructible_v<T>
                      && std::is_nothrow_constructible_v<T, Args&&...> && AllocationCapability<Marker>;

// The arguments of array: an element that the arena can hold and that copies
// with no throw, and the allocation capability.
template <class T, class Marker>
concept CanFillArenaArray = ViewElement<T> && !std::is_const_v<T> && std::is_trivially_destructible_v<T>
                         && std::is_nothrow_copy_constructible_v<T> && AllocationCapability<Marker>;

// The brand of a BumpArena: a named arena tag, or the scope brand of
// with_bump_arena.
template <class Brand>
concept ArenaBrand =
    NamedArenaTag<Brand> || (::foundation::brand::IsFreshBrand<Brand> && detail::names_scope_brand(^^Brand));

template <ArenaBrand Brand>
class BumpArena;

template <ArenaMemberTag Tag>
class EpochArena;

// The brand of the bytes of the epochs of an EpochArena<Tag> (epochs above).
// The annotation epoch_brand marks it for the rule brand-escape.
template <ArenaMemberTag Tag>
struct[[= ::foundation::brand::epoch_brand{}]] EpochOf final {};

namespace detail {

template <class Brand>
struct epoch_tag_of {};

template <class Tag>
struct epoch_tag_of<EpochOf<Tag>> {
    using type = Tag;
};

}  // namespace detail

// An epoch brand: EpochOf<Tag> of a member tag.
template <class Brand>
concept EpochBrand = requires { typename detail::epoch_tag_of<Brand>::type; };

template <EpochBrand Brand>
class Epoch;

namespace side {

// The role of the offset of a window from the first element of its run.
struct RunStart final {};

}  // namespace side

// The offset of a window of an ArenaRun from the first element of the run, in
// elements.  The count of the window is an ElementCount, so the two arguments
// of window have two types.
using RunStart = Side<side::RunStart, ElementCount>;

// The arguments of build_array: an element that the arena can hold and that
// holds no linear value, because the arena ends no object, and a callable
// that gives the element of each position as a value of the element type,
// with no throw.  The value builds the element in its place, so the element
// needs no copy and no move.  The door calls make one time for each element,
// and a call is no consume, so the door also asks that make holds no linear
// value (the deleted twin of build_array).
template <class T, class Marker, class Make>
concept CanBuildArenaArray = ViewElement<T> && !std::is_const_v<T> && std::is_trivially_destructible_v<T>
                          && ::foundation::linear::HoldsNoLinearValue<T> && std::is_nothrow_invocable_v<Make&, RunStart>
                          && std::same_as<std::invoke_result_t<Make&, RunStart>, T> && AllocationCapability<Marker>;

// The door of a BumpArena and an Epoch to the door of a Ref (the doors of the
// owners above foundation/core/Ref.h).  Its one member is private, and the
// two are its friends.  The invariant: make() lends the object that the
// arguments built in new bytes of the arena, with the brand of the arena or
// of the epoch.  An Epoch lends the core of its arena to itself with the
// epoch brand, and only with_epoch builds an Epoch, in its own frame.  A home
// owner row of utils/rule-table.txt keeps this definition in this header.
class ArenaRefDoor final : ::foundation::NoObject<ArenaRefDoor> {
    template <ArenaBrand Brand>
    friend class BumpArena;
    template <EpochBrand Brand>
    friend class Epoch;

    // An object of the bytes of the arena, as a Ref of the brand.
    template <::foundation::brand::IsBrand Brand, RefTarget T>
    [[nodiscard]] CRUCIBLE_INLINE static constexpr Ref<T, Brand> lend_(T& object) noexcept {
        return detail::RefDoor::lend<Brand>(object);
    }
};

// A run of elements that an arena of the brand built: a View with the brand
// of its arena.  Only the arena builds a run that is not empty, through
// ArenaArrayDoor below.  A run converts to a run of const elements, and
// window gives a part of a run, each with the same brand.  So a field that
// holds a run of the brand takes only bytes of an arena of the brand.  view()
// gives the View of an lvalue run for a read in place.  That View carries no
// brand (epochs above).
//
// A const run gives its elements only as const, and it does not copy, as a
// Link does: a copy takes a run that is not const.  So a reader of a const
// object that holds a run writes no element through the run, and the run
// states refers_deep{} (foundation/Transfer.h).  The check file of this header
// pins each const door.
template <ViewElement T, ::foundation::brand::IsBrand Brand>
class[[= ::foundation::transfer::refers_deep{}]] ArenaRun final {
public:
    using brand_type = Brand;

    // The empty run, which names no byte.
    constexpr ArenaRun() noexcept = default;

    // A copy takes a run that is not const.  The rule holds for a run of
    // const elements too, because a class that states refers_deep{} and
    // copies from a const object holds no transfer property
    // (foundation/Transfer.h).
    constexpr ArenaRun(ArenaRun& other) noexcept = default;
    constexpr ArenaRun(ArenaRun&& other) noexcept = default;
    constexpr ArenaRun& operator=(ArenaRun& other) noexcept = default;
    constexpr ArenaRun& operator=(ArenaRun&& other) noexcept = default;
    constexpr ~ArenaRun() = default;

    // The run of the same elements as const, with the same brand, also from
    // a const run.
    template <class Writable>
        requires std::same_as<Writable const, T> && (!std::same_as<Writable, T>)
    constexpr ArenaRun(ArenaRun<Writable, Brand> const& other) noexcept : run_{other.run_} {}

    // The elements of the run, as const from a const run.  A run of a
    // temporary has no View, because the View would outlive the name of the
    // run.
    [[nodiscard]] constexpr View<T> view() & noexcept { return run_; }
    [[nodiscard]] constexpr View<T const> view() const& noexcept { return run_; }
    void view() const&& = delete("a View of a temporary run outlives the name of the run: name the run first");

    // The count elements from start, as a run of the same brand, or no run
    // when they do not fit in this run.  A const run gives a run of const
    // elements.  The result is an Option<ArenaRun>, which the class cannot
    // name before it is complete.
    [[nodiscard]] constexpr auto window(RunStart start, ElementCount count) & noexcept {
        Option<View<T>> const part = run_.window(start.value().raw(), count.raw());
        return part.match([](View<T> const& found) noexcept { return Option<ArenaRun>::some(ArenaRun{found}); },
                          []() noexcept { return Option<ArenaRun>{}; });
    }
    [[nodiscard]] constexpr auto window(RunStart start, ElementCount count) const& noexcept {
        using ConstRun = ArenaRun<T const, Brand>;
        Option<View<T>> const part = run_.window(start.value().raw(), count.raw());
        return part.match(
            [](View<T> const& found) noexcept { return Option<ConstRun>::some(ConstRun{View<T const>{found}}); },
            []() noexcept { return Option<ConstRun>{}; });
    }

    // The count of the elements.
    [[nodiscard]] constexpr std::size_t size() const noexcept { return run_.size(); }

private:
    friend class ArenaArrayDoor;
    template <ViewElement, ::foundation::brand::IsBrand>
    friend class ArenaRun;

    constexpr explicit ArenaRun(View<T> run) noexcept : run_{run} {}

    View<T> run_{};
};

namespace detail {

// Marks the count bytes from first as addressable, for the bump.  Only a
// build with AddressSanitizer has a body, in src/foundation/os/BumpArena.cpp.
// In each other build the call costs nothing.
#if defined(__SANITIZE_ADDRESS__)
void bump_unpoison_(void const* first, std::size_t count) noexcept;
#else
inline void bump_unpoison_(void const* /*first*/, std::size_t /*count*/) noexcept {}
#endif

// Ends the process for an array of count elements of element_bytes bytes
// past the largest count of a View.  The report is out of line, so the frame
// of array() holds no arguments of a report and no stack canary.
[[noreturn, gnu::cold, gnu::noinline]] void refuse_arena_array_(std::size_t count, std::size_t element_bytes) noexcept;

// Ends the process for a next address of an arena that does not fit in the
// address space at the alignment.  The report is out of line, as the report
// of refuse_arena_array_ is, so the frame of the bump holds no stack canary.
[[noreturn, gnu::cold, gnu::noinline]] void refuse_arena_address_(std::size_t alignment) noexcept;

// The address at the alignment, or the end of the process when the rounding
// does not fit in the address space.  An alignment of a type is at most the
// size of half the address space.  On x86_64, bit 63 of a user address is
// zero, also with linear address masking, so the optimizer reads that the
// rounding does not wrap, and the test of the Option folds away.  On aarch64
// the top byte of an address can hold a tag (TBI, MTE, HWASan), so an
// address can be above __PTRDIFF_MAX__, and that arm makes no assumption
// about the address.  The refusal is out of line.
[[nodiscard, gnu::always_inline]] inline std::uintptr_t arena_aligned_(std::uintptr_t address,
                                                                       std::size_t alignment) noexcept {
#if defined(__x86_64__)
    [[assume(address <= static_cast<std::uintptr_t>(__PTRDIFF_MAX__))]];
#endif
    [[assume(alignment <= static_cast<std::size_t>(__PTRDIFF_MAX__))]];
    Option<std::uintptr_t> rounded = align_up(address, std::uintptr_t{alignment});
    if (rounded.is_none()) [[unlikely]] {
        refuse_arena_address_(alignment);
    }
    return std::move(rounded).value_or(0);
}

class BumpArenaCore;

// ArenaBlockSize calls this function for a size of zero bytes.  It has no
// definition, so the call is no constant expression, and the error of the
// compiler names the rule.
void arena_block_size_must_be_above_zero() noexcept;

}  // namespace detail

// The size of each block of an arena: a count of bytes above zero.  A
// constant Quantity converts at compile time, and a constant of zero bytes
// does not compile.  plus(more) gives a larger size, which stays above zero,
// so a size that a caller adds up at run time takes no check.
class ArenaBlockSize final {
public:
    // The size of a constant Quantity.  The constructor is implicit, so a
    // door of an arena takes a constant Quantity as it is.
    consteval ArenaBlockSize(Quantity<unit::Byte, std::size_t> bytes) noexcept : bytes_{bytes.raw()} {
        if (bytes_ == 0) detail::arena_block_size_must_be_above_zero();
    }

    // This size plus more bytes.  The sum saturates at the largest size_t,
    // so it stays above zero.  The sum is a copy of this size with the new
    // count, so no constructor takes a count of run time.
    [[nodiscard]] constexpr ArenaBlockSize plus(Quantity<unit::Byte, std::size_t> more) const noexcept {
        ArenaBlockSize sum = *this;
        sum.bytes_ = saturating_add(bytes(), more).raw();
        return sum;
    }

    // The count of bytes, above zero.
    [[nodiscard]] constexpr Quantity<unit::Byte, std::size_t> bytes() const noexcept {
        return Quantity<unit::Byte, std::size_t>{bytes_};
    }

private:
    std::size_t bytes_ = 1;
};

// The door of an arena to the raw constructors of a View (the doors of the
// owners above this header in foundation/core/Region.h) and to the
// constructor of an ArenaRun.  The invariant: the bump of the arena gave
// count * sizeof(T) bytes at the alignment of T, and its loop built each
// element.  So the run of the count elements holds live elements only.  The
// core of the arena is the one friend, and a home owner row of
// utils/rule-table.txt keeps this definition in this header.
class ArenaArrayDoor final : ::foundation::NoObject<ArenaArrayDoor> {
    friend class detail::BumpArenaCore;

    // The count elements that the arena built from first, as a run of the
    // brand of the arena.
    template <class T, class Brand>
    [[nodiscard]] static ArenaRun<T, Brand> built_run_(T* first, std::size_t count) noexcept {
        return ArenaRun<T, Brand>{View<T>{first, count}};
    }
};

namespace detail {

// The state of an arena of each brand and its cold bodies.  Only BumpArena,
// EpochArena and Epoch reach it.  The arena owns its blocks, and no door
// stores a pointer or a borrow that a caller gives.
class BumpArenaCore final {
public:
    // The bytes of blocks that an epoch arena keeps when an epoch ends, until
    // set_retained_limit sets a different limit.  An arena that holds more
    // gives back each block but the first, so one epoch with an unusual peak
    // does not pin its peak for the life of the arena.
    static constexpr std::size_t default_retained_bytes_limit = std::size_t{64} << 20;

    // The count of blocks that one arena can hold.
    static constexpr std::uint32_t max_blocks = 64;

    // The alignment of each block.  A larger alignment of an element costs
    // slack bytes in the block, because the bump aligns the address.
    static constexpr std::size_t block_alignment = 16;

    BumpArenaCore(BumpArenaCore const&) = delete("an arena owns its blocks alone: a copy would free them two times");
    BumpArenaCore&
    operator=(BumpArenaCore const&) = delete("an arena owns its blocks alone: a copy would free them two times");
    BumpArenaCore(BumpArenaCore&&) = delete("a Ref of an arena names its blocks: build a new arena instead");
    BumpArenaCore& operator=(BumpArenaCore&&) = delete("a Ref of an arena names its blocks: build a new arena instead");

    // An arena with no block.  The type of the size holds it above zero.  The
    // constructor is open, because a core gives nothing by itself: each door
    // that gives bytes is private, and the arenas are its friends.
    explicit BumpArenaCore(ArenaBlockSize block_size) noexcept : block_size_{block_size.bytes().raw()} {}

    // Gives each block back to the allocation door.
    ~BumpArenaCore();

private:
    template <ArenaBrand Brand>
    friend class ::foundation::core::BumpArena;
    template <ArenaMemberTag Tag>
    friend class ::foundation::core::EpochArena;
    template <EpochBrand Brand>
    friend class ::foundation::core::Epoch;

    // One T that the arguments build in new bytes, at the alignment of T.
    template <class T, class... Args>
    [[nodiscard, gnu::always_inline]] T& build_(Args&&... args) noexcept {
        return *::new(bump_(sizeof(T), alignof(T))) T(static_cast<Args&&>(args)...);
    }

    // count copies of fill in new bytes, at the alignment of T, as a run of
    // the brand.  A count of zero gives the empty run and takes no bytes.  A
    // count past the largest count of a View ends the process
    // (run_storage_).  Complexity: O(count).
    template <class T, class Brand>
    [[nodiscard]] ArenaRun<T, Brand> fill_(std::size_t count, T const& fill) noexcept {
        if (count == 0) {
            return ArenaRun<T, Brand>{};
        }
        T* const first = run_storage_<T>(count);
        for (std::size_t position = 0; position != count; ++position) {
            ::new(static_cast<void*>(first + position)) T(fill);
        }
        return ArenaArrayDoor::built_run_<T, Brand>(first, count);
    }

    // count elements in new bytes, at the alignment of T, as a run of the
    // brand.  make gives the element of each position as a value of T, from
    // the first position to the last, and the value builds the element in
    // its place, so no element copies or moves.  A count of zero gives the
    // empty run, takes no bytes and calls make no time.  A count past the
    // largest count of a View ends the process (run_storage_).  Complexity:
    // O(count) calls of make.
    template <class T, class Brand, class Make>
    [[nodiscard]] ArenaRun<T, Brand> build_run_(std::size_t count, Make& make) noexcept {
        if (count == 0) {
            return ArenaRun<T, Brand>{};
        }
        T* const first = run_storage_<T>(count);
        for (std::size_t position = 0; position != count; ++position) {
            ::new(static_cast<void*>(first + position)) T(make(RunStart{ElementCount{position}}));
        }
        return ArenaArrayDoor::built_run_<T, Brand>(first, count);
    }

    // The bytes of count elements of T, above zero, at the alignment of T.  A
    // count past the largest count of a View ends the process.  The callers
    // give counts of run time from their data, so no type holds the bound.
    // The check runs one time for each run, before the loop of the caller,
    // and its refusal is cold and out of line.
    template <class T>
    [[nodiscard, gnu::always_inline]] T* run_storage_(std::size_t count) noexcept {
        if (count > max_view_count(sizeof(T))) [[unlikely]] {
            refuse_arena_array_(count, sizeof(T));
        }
        std::size_t const bytes =
            checked_mul(count, sizeof(T)).expect("the bytes of an array below the largest View fit in size_t");
        return static_cast<T*>(bump_(bytes, alignof(T)));
    }

    // The bump: bytes bytes at the alignment, from the current block.  bytes
    // is above zero and at most __PTRDIFF_MAX__, and the alignment is a power
    // of two, so the rounding below does not wrap.  An arena with no block has
    // an end of zero, so its first request takes the cold path.
    [[nodiscard, gnu::always_inline]] void* bump_(std::size_t bytes, std::size_t alignment) noexcept {
        std::uintptr_t const base = __builtin_bit_cast(std::uintptr_t, cur_block_);
        std::uintptr_t const aligned_address = arena_aligned_(base + offset_, alignment);
        std::size_t const aligned = aligned_address - base;
        if (aligned + bytes <= end_offset_) [[likely]] {
            std::byte* const storage = cur_block_ + aligned;
            offset_ = aligned + bytes;
            bump_unpoison_(storage, bytes);
            return storage;
        }
        return bump_slow_(bytes, alignment);
    }

    // The bytes from the first block after the current block that holds the
    // request, or from a new block at the end of the list.
    [[nodiscard, gnu::cold, gnu::noinline, gnu::returns_nonnull]] void* bump_slow_(std::size_t bytes,
                                                                                   std::size_t alignment) noexcept;

    // Makes the first block from first_index that holds needed bytes the
    // current block.  A block of an earlier epoch serves again when it holds
    // needed bytes, and a smaller one stays for a later epoch.
    void enter_block_(std::uint32_t first_index, std::size_t needed) noexcept;

    // Gives each block from first_index to the end of the list back to the
    // allocation door.
    void free_blocks_from_(std::uint32_t first_index) noexcept;

    // Each byte that the arena gave out dies.  Above the retained limit, each
    // block but the first goes back to the door.  The blocks that stay are
    // poisoned, and the first block becomes the current block.
    void end_epoch_() noexcept;

    // The hot fields, read on each bump, share one cache line.
    std::byte* cur_block_ = nullptr;
    std::size_t offset_ = 0;
    std::size_t end_offset_ = 0;

    // The cold fields, read on the cold path, by the epoch and by the query.
    std::size_t block_size_ = 0;
    std::size_t retained_bytes_ = 0;
    std::size_t retained_limit_ = default_retained_bytes_limit;
    std::uint32_t cur_index_ = 0;
    std::uint32_t block_count_ = 0;
    // The blocks in the order of their allocation, and the bytes of each.
    std::byte* blocks_[max_blocks]{};
    std::size_t block_bytes_[max_blocks]{};
};

// The one builder of an arena: mint_bump_arena, with_bump_arena, mint_box of
// a BumpArena and mint_epoch_arena call it.  The arena is a prvalue, so the
// caller builds it in place by guaranteed elision.
struct BumpArenaDoor final : ::foundation::NoObject<BumpArenaDoor> {
    template <ArenaBrand Brand>
    [[nodiscard]] static BumpArena<Brand> open(ArenaBlockSize block_size) noexcept {
        return BumpArena<Brand>{block_size};
    }

    template <ArenaMemberTag Tag>
    [[nodiscard]] static EpochArena<Tag> open_epoch(ArenaBlockSize block_size) noexcept {
        return EpochArena<Tag>{block_size};
    }
};

}  // namespace detail

// The position of one object of T in one BumpArena of the brand: the
// identity of the arena, the block of the object and its offset in that
// block.  It holds no address, so the body of a frame door can give it out
// as plain data (FrameResult of foundation/core/Choice.h), and the arena
// gives the object back after the frame.  Two arenas of one family tag share
// the brand, so the identity tells them apart.  Only the arena that holds the
// object makes a position of it (position_of), and only that arena gives the
// object of a position (at).
template <RefTarget T, ArenaBrand Brand>
class ArenaPosition final {
public:
    [[nodiscard]] friend constexpr bool operator==(ArenaPosition const& left, ArenaPosition const& right) noexcept {
        return left.arena_ == right.arena_ && left.block_ == right.block_ && left.offset_ == right.offset_;
    }

private:
    friend class BumpArena<Brand>;

    constexpr ArenaPosition(Identity arena, Index<detail::BumpArenaCore::max_blocks> block,
                            Quantity<unit::Byte, std::size_t> offset) noexcept
        : arena_{arena}, block_{block}, offset_{offset} {}

    Identity arena_;
    Index<detail::BumpArenaCore::max_blocks> block_;
    Quantity<unit::Byte, std::size_t> offset_;
};

// An arena of the brand (brands of an arena above).  It cannot copy or move,
// because each Ref of it names its blocks.  Only the door builds it.
template <ArenaBrand Brand>
class[[= owns_its_storage{}]][[= ::foundation::brand::branded_arena{}]] BumpArena final {
public:
    using brand_type = Brand;

    // The count of blocks that one arena can hold, and the alignment of each.
    static constexpr std::uint32_t max_blocks = detail::BumpArenaCore::max_blocks;
    static constexpr std::size_t block_alignment = detail::BumpArenaCore::block_alignment;

    BumpArena(BumpArena const&) = delete("a BumpArena owns its blocks alone: a copy would free them two times");
    BumpArena& operator=(BumpArena const&) = delete("a BumpArena owns its blocks alone: a copy would free them two "
                                                    "times");
    BumpArena(BumpArena&&) = delete("a Ref of a BumpArena names its blocks: build a new arena instead");
    BumpArena& operator=(BumpArena&&) = delete("a Ref of a BumpArena names its blocks: build a new arena instead");
    ~BumpArena() = default;

    // One T that the arguments build in new bytes of the arena, at the
    // alignment of T, as a Ref of the brand of the arena.
    template <class T, class Marker, class... Args>
        requires CanMakeInArena<T, Marker, Args...>
    [[nodiscard]] Ref<T, Brand> make(this BumpArena& self CRUCIBLE_LIFETIMEBOUND, Marker /*allocation*/,
                                     Args&&... args) noexcept {
        return ArenaRefDoor::lend_<Brand>(self.core_.template build_<T>(static_cast<Args&&>(args)...));
    }

    template <class T, class... Rest>
    void make(this BumpArena&& self, Rest&&...) = delete("a temporary arena gives back its blocks at the end of the "
                                                         "statement");

    // count copies of fill in new bytes of the arena, at the alignment of T,
    // as a run of the brand of the arena.  A count of zero gives the empty
    // run and takes no bytes.  A count past the largest count of a View ends
    // the process.  Complexity: O(count).
    template <class T, class Marker>
        requires CanFillArenaArray<T, Marker>
    [[nodiscard]] ArenaRun<T, Brand> array(this BumpArena& self CRUCIBLE_LIFETIMEBOUND, Marker /*allocation*/,
                                           std::size_t count, T const& fill) noexcept {
        return self.core_.template fill_<T, Brand>(count, fill);
    }

    template <class T, class Marker>
    void array(this BumpArena&& self, Marker, std::size_t,
               T const&) = delete("a temporary arena gives back its blocks at the end of the statement");

    // count elements in new bytes of the arena, at the alignment of T, as a
    // run of the brand of the arena.  make gives the element of each
    // position as a value of T, from the first position to the last, and
    // the value builds the element in its place.  So a run can hold an
    // element that does not copy from a const object, such as a class that
    // holds a Link.  A count of zero gives the empty run and calls make no
    // time.  A count past the largest count of a View ends the process.
    // Complexity: O(count) calls of make.
    template <class T, class Marker, class Make>
        requires CanBuildArenaArray<T, Marker, Make> && ::foundation::linear::HoldsNoLinearValue<Make>
    [[nodiscard]] ArenaRun<T, Brand> build_array(this BumpArena& self CRUCIBLE_LIFETIMEBOUND, Marker /*allocation*/,
                                                 ElementCount count, Make&& make) noexcept {
        return self.core_.template build_run_<T, Brand>(count.raw(), make);
    }

    // A make that holds a linear value by value binds to this overload, so
    // the refusal gives its reason.
    template <class T, class Marker, class Make>
        requires CanBuildArenaArray<T, Marker, Make> && (!::foundation::linear::HoldsNoLinearValue<Make>)
    void build_array(this BumpArena& self, Marker, ElementCount, Make&&) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

    template <class T, class Marker, class Make>
    void build_array(this BumpArena&& self, Marker, ElementCount,
                     Make&&) = delete("a temporary arena gives back its blocks at the end of the statement");

    // The bytes of each block that the arena holds.
    [[nodiscard]] std::size_t retained_bytes() const noexcept { return core_.retained_bytes_; }

    // The position of object in this arena, or no value when object lies in
    // no block of this arena.  A Ref of the brand comes only from an arena of
    // the brand, so an object in a block of this arena is an object that
    // this arena made.  Complexity: O(blocks), at most max_blocks.
    template <RefTarget T>
    [[nodiscard]] Option<ArenaPosition<T, Brand>> position_of(Ref<T, Brand> const& object) const noexcept {
        std::uintptr_t const address = __builtin_bit_cast(std::uintptr_t, object.operator->());
        for (std::uint32_t block = 0; block != core_.block_count_; ++block) {
            std::uintptr_t const first = __builtin_bit_cast(std::uintptr_t, core_.blocks_[block]);
            if (address >= first && address - first < core_.block_bytes_[block]) {
                return Option<ArenaPosition<T, Brand>>::some(ArenaPosition<T, Brand>{
                    identity_, Index<max_blocks>::check(block).expect("a block of the arena is below max_blocks"),
                    Quantity<unit::Byte, std::size_t>{address - first}});
            }
        }
        return Option<ArenaPosition<T, Brand>>{none};
    }

    // The object at position, as a Ref of the brand, or no value for a
    // position of another arena.  The arena keeps each block for its life,
    // so a position of this arena names the object that position_of read.
    // Complexity: O(1).
    template <RefTarget T>
    [[nodiscard]] Option<Ref<T, Brand>> at(this BumpArena& self CRUCIBLE_LIFETIMEBOUND,
                                           ArenaPosition<T, Brand> const& position) noexcept {
        if (!(position.arena_ == self.identity_)) [[unlikely]] {
            return Option<Ref<T, Brand>>{none};
        }
        std::byte* const bytes = self.core_.blocks_[position.block_.raw()] + position.offset_.raw();
        T& object = *__builtin_launder(static_cast<T*>(static_cast<void*>(bytes)));
        return Option<Ref<T, Brand>>::some(ArenaRefDoor::lend_<Brand>(object));
    }

    template <class T>
    void at(this BumpArena&& self, ArenaPosition<T, Brand> const&) =
        delete("a temporary arena gives back its blocks at the end of the statement");

private:
    friend struct detail::BumpArenaDoor;

    explicit BumpArena(ArenaBlockSize block_size) noexcept : core_{block_size}, identity_{mint_identity()} {}

    detail::BumpArenaCore core_;
    // The identity of this arena, which each position of it carries.
    Identity identity_;
};

// The epoch of an EpochArena (epochs above): the one door of make, array and
// build_array of the arena while the body of with_epoch runs.  It cannot copy or move, and
// only with_epoch builds it, in its own frame.
template <EpochBrand Brand>
class Epoch final {
public:
    using brand_type = Brand;

    Epoch(Epoch const&) = delete("an epoch lives in the frame of with_epoch, and the body takes it by reference");
    Epoch& operator=(Epoch const&) = delete("an epoch names one epoch of one arena");
    Epoch(Epoch&&) = delete("an epoch lives in the frame of with_epoch, and the body takes it by reference");
    Epoch& operator=(Epoch&&) = delete("an epoch names one epoch of one arena");
    ~Epoch() = default;

    // One T that the arguments build in new bytes of the epoch, at the
    // alignment of T, as a Ref of the epoch brand.
    template <class T, class Marker, class... Args>
        requires CanMakeInArena<T, Marker, Args...>
    [[nodiscard]] Ref<T, Brand> make(this Epoch& self CRUCIBLE_LIFETIMEBOUND, Marker /*allocation*/,
                                     Args&&... args) noexcept {
        return ArenaRefDoor::lend_<Brand>((*self.core_).template build_<T>(static_cast<Args&&>(args)...));
    }

    template <class T, class... Rest>
    void make(this Epoch&& self, Rest&&...) = delete("an epoch lives in the frame of with_epoch: take it by reference");

    // count copies of fill in new bytes of the epoch, as a run of the epoch
    // brand.  A count of zero gives the empty run and takes no bytes.
    // Complexity: O(count).
    template <class T, class Marker>
        requires CanFillArenaArray<T, Marker>
    [[nodiscard]] ArenaRun<T, Brand> array(this Epoch& self CRUCIBLE_LIFETIMEBOUND, Marker /*allocation*/,
                                           std::size_t count, T const& fill) noexcept {
        return (*self.core_).template fill_<T, Brand>(count, fill);
    }

    template <class T, class Marker>
    void array(this Epoch&& self, Marker, std::size_t,
               T const&) = delete("an epoch lives in the frame of with_epoch: take it by reference");

    // count elements in new bytes of the epoch, each the value that make
    // gives for its position, as a run of the epoch brand (build_array of
    // BumpArena).  Complexity: O(count) calls of make.
    template <class T, class Marker, class Make>
        requires CanBuildArenaArray<T, Marker, Make> && ::foundation::linear::HoldsNoLinearValue<Make>
    [[nodiscard]] ArenaRun<T, Brand> build_array(this Epoch& self CRUCIBLE_LIFETIMEBOUND, Marker /*allocation*/,
                                                 ElementCount count, Make&& make) noexcept {
        return (*self.core_).template build_run_<T, Brand>(count.raw(), make);
    }

    // A make that holds a linear value by value binds to this overload, so
    // the refusal gives its reason.
    template <class T, class Marker, class Make>
        requires CanBuildArenaArray<T, Marker, Make> && (!::foundation::linear::HoldsNoLinearValue<Make>)
    void build_array(this Epoch& self, Marker, ElementCount, Make&&) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

    template <class T, class Marker, class Make>
    void build_array(this Epoch&& self, Marker, ElementCount,
                     Make&&) = delete("an epoch lives in the frame of with_epoch: take it by reference");

private:
    template <ArenaMemberTag Tag>
    friend class EpochArena;

    explicit Epoch(detail::BumpArenaCore& core) noexcept : core_{ArenaRefDoor::lend_<Brand>(core)} {}

    Ref<detail::BumpArenaCore, Brand> core_;
};

namespace detail {

// True when the class states brand as its member brand_type: a carrier of
// the brand.  The one read of the members finds the alias by its name.
// Complexity: linear in the members of the class, at compile time only.
[[nodiscard]] consteval bool states_brand_(std::meta::info type, std::meta::info brand) {
    if (!std::meta::is_class_type(type) || !std::meta::is_complete_type(type)) {
        return false;
    }
    std::meta::info const alias =
        ::foundation::reflect::member_named_in(type, std::meta::access_context::unprivileged(), "brand_type");
    return alias != std::meta::info{} && std::meta::is_type_alias(alias) && std::meta::dealias(alias) == brand;
}

// The count of the types that one walk of the gate of an epoch reads.  A
// capture whose type reaches more types is a refusal, so the gate fails
// closed and gives its reason, and the constant evaluation stays in its
// limit.  A body captures each value that it needs, not a large owner.
inline constexpr std::size_t epoch_gate_node_limit = 512;

// What one walk of the gate of an epoch looks for (epochs above).
enum class GateTarget : unsigned char {
    arena,  // the arena itself, which can open a second epoch
    brand,  // a place whose type names the epoch brand or states it
};

// True when a part of a body, the closure type root, reaches the target
// through a place that is not const, a class whose members the walk cannot
// read, or more types than epoch_gate_node_limit (epochs above).  C++ const
// is shallow, so the walk carries the const of a place only to the parts that
// the place holds by value, and not to a mutable member.  The target of a
// pointer, of a reference and of a borrow (a class that states refers{},
// refers_unique{} or refers_deep{} of foundation/Transfer.h, such as a Ref)
// keeps its own const.  So a const pointer, a const reference to a Ref and a
// const Ref of an owner still reach the arena of the owner.  The walk reads
// the bases, the members, the targets of references and pointers, and the
// template arguments of a class that states that it holds their values, with
// the instantiating read.  The brand form finds
// a place whose type names the brand in its template arguments or states it
// as its brand_type, and not the empty brand type itself.  It is a function
// that is no template, so no unit can specialize what the gate admits.
// Complexity: quadratic in the distinct pairs of a type and its const, because
// of the scan of the visited list, and at most epoch_gate_node_limit of them,
// at compile time only.
[[nodiscard]] consteval bool reaches_writable_(std::meta::info root, std::meta::info target, GateTarget form) {
    namespace refl = ::foundation::reflect;
    namespace tr = ::foundation::transfer;
    constexpr refl::SpecializationRead read = refl::SpecializationRead::Instantiating;
    refl::TypeNodeList listed;
    listed.push_back(refl::TypeNode{refl::bare_type(root), true});
    std::size_t read_count = 0;
    for (std::size_t next = 0; next < listed.size(); ++next) {
        refl::TypeNode const node = listed.begin()[next];
        bool was_read = false;
        for (std::size_t earlier = 0; earlier < next; ++earlier) {
            refl::TypeNode const& seen = listed.begin()[earlier];
            if (seen.type == node.type && (!seen.was_const || node.was_const)
                && (seen.may_read_members || !node.may_read_members)) {
                was_read = true;
                break;
            }
        }
        if (was_read) {
            continue;
        }
        if (++read_count > epoch_gate_node_limit) {
            return true;
        }
        bool const is_hit =
            form == GateTarget::arena
                ? node.type == target
                : node.type != target && (names_brand(node.type, target) || states_brand_(node.type, target));
        if (is_hit && !node.was_const) {
            return true;
        }
        bool const is_class = std::meta::is_class_type(node.type) || std::meta::is_union_type(node.type);
        if ((is_class && !node.may_read_members) || refl::holds_unreadable_state(node)) {
            return true;
        }
        // A class holds what its members show: a Ref holds the address of its
        // target, a Box the address of its object.  A type argument of any
        // other class can be a phantom type that the class holds no value of,
        // such as the owner of a context, so the walk reads the arguments of a
        // class only when the class states that it holds their values in its
        // own bytes (by_arguments{} or payload_cell{} of foundation/Transfer.h).
        bool const holds_arguments =
            is_class
            && (tr::detail::states(node.type, ^^tr::by_arguments) || tr::detail::states(node.type, ^^tr::payload_cell));
        bool const keeps_own_const = std::meta::is_pointer_type(node.type) || std::meta::is_function_type(node.type)
                                  || std::meta::is_member_pointer_type(node.type);
        if (!is_class || holds_arguments) {
            for (refl::TypeNode component : refl::argument_components_of(node, read)) {
                if (!keeps_own_const && !component.was_reference) {
                    component.was_const = component.was_const || node.was_const;
                }
                listed.push_back(component);
            }
        }
        for (refl::TypeNode component : refl::member_components_of(node, read)) {
            if (!component.was_reference && !component.is_mutable) {
                component.was_const = component.was_const || node.was_const;
            }
            listed.push_back(component);
        }
    }
    return false;
}

}  // namespace detail

// The result of the body of an epoch: void, or a value that holds no borrow,
// names no epoch brand of the tag and states none (epochs above).
template <class Given, class Tag>
concept EpochResult = std::is_void_v<Given>
                   || (HoldsNoBorrow<Given> && !detail::names_brand(^^Given, ^^EpochOf<Tag>)
                       && !detail::states_brand_(std::meta::dealias(^^std::remove_cvref_t<Given>), ^^EpochOf<Tag>));

// The captures of the body of an epoch of the tag: no capture reaches a place
// that is not const of a type that can hold the epoch brand, and no capture
// reaches the arena in a place that is not const (epochs above).
template <class Body, class Tag>
concept EpochCaptures =
    !detail::reaches_writable_(^^std::remove_cvref_t<Body>, ^^EpochOf<Tag>, detail::GateTarget::brand)
    && !detail::reaches_writable_(^^std::remove_cvref_t<Body>, ^^EpochArena<Tag>, detail::GateTarget::arena);

// The call of the body of an epoch of the tag: a call with the Epoch by
// reference and no throw, of a callable that holds no linear value by value.
template <class Body, class Tag>
concept EpochCall =
    std::is_nothrow_invocable_v<Body&&, Epoch<EpochOf<Tag>>&> && ::foundation::linear::HoldsNoLinearValue<Body>;

// The body of an epoch of the tag.
template <class Body, class Tag>
concept EpochBody = EpochCall<Body, Tag> && EpochCaptures<Body, Tag>
                 && EpochResult<std::invoke_result_t<Body&&, Epoch<EpochOf<Tag>>&>, Tag>;

// An arena of a member tag whose bytes live in epochs (epochs above).  It
// cannot copy or move.  Only the door builds it.
template <ArenaMemberTag Tag>
class[[= owns_its_storage{}]][[= ::foundation::brand::branded_arena{}]] EpochArena final {
public:
    using brand_type = Tag;

    // The bytes of blocks that the arena keeps when an epoch ends, until
    // set_retained_limit sets a different limit.
    static constexpr std::size_t default_retained_bytes_limit = detail::BumpArenaCore::default_retained_bytes_limit;

    // The count of blocks that one arena can hold, and the alignment of each.
    static constexpr std::uint32_t max_blocks = detail::BumpArenaCore::max_blocks;
    static constexpr std::size_t block_alignment = detail::BumpArenaCore::block_alignment;

    EpochArena(EpochArena const&) = delete("an EpochArena owns its blocks alone: a copy would free them two times");
    EpochArena& operator=(EpochArena const&) = delete("an EpochArena owns its blocks alone: a copy would free them two "
                                                      "times");
    EpochArena(EpochArena&&) = delete("an epoch names its arena: build a new arena instead");
    EpochArena& operator=(EpochArena&&) = delete("an epoch names its arena: build a new arena instead");
    ~EpochArena() = default;

    // Calls body with the Epoch of a new epoch, and gives its result.  After
    // the body, each byte that the epoch gave out dies.  The arena holds no
    // byte here, because only an epoch gives bytes and each epoch ends them.
    // No second epoch of the arena is open, because the gate refuses a body
    // that reaches the arena and the rule brand-escape refuses a static that
    // reaches it.  So the door checks neither at run time.
    template <class Body>
        requires EpochBody<Body, Tag>
    std::invoke_result_t<Body&&, Epoch<EpochOf<Tag>>&> with_epoch(Body&& body) & noexcept {
        Epoch<EpochOf<Tag>> epoch{core_};
        if constexpr (std::is_void_v<std::invoke_result_t<Body&&, Epoch<EpochOf<Tag>>&>>) {
            static_cast<Body&&>(body)(epoch);
            core_.end_epoch_();
        } else {
            std::invoke_result_t<Body&&, Epoch<EpochOf<Tag>>&> result = static_cast<Body&&>(body)(epoch);
            core_.end_epoch_();
            return result;
        }
    }

    // A body whose result holds a borrow or names the epoch brand binds to
    // this overload, so the refusal gives its reason.
    template <class Body>
        requires EpochCall<Body, Tag> && (!EpochResult<std::invoke_result_t<Body &&, Epoch<EpochOf<Tag>>&>, Tag>)
    void with_epoch(Body&&) & = delete(
        "the result of an epoch holds no borrow and names no epoch brand, because the end of the epoch ends each "
        "byte that the epoch gave out: give a value with no reference and no address, or write a value that owns "
        "its storage to a variable that the body captures");

    // A body whose captures can keep a value of the epoch or reach the arena
    // binds to this overload, so the refusal gives its reason.
    template <class Body>
        requires EpochCall<Body, Tag> && EpochResult<std::invoke_result_t<Body&&, Epoch<EpochOf<Tag>>&>, Tag>
                  && (!EpochCaptures<Body, Tag>)
    void with_epoch(Body&&) & = delete(
        "the body of an epoch captures a writer of a type that can hold the epoch brand, or reaches the arena "
        "through a path that is not const, or captures a writer of a type too large for the gate to read: capture "
        "each value that the body needs, by value or by a const reference, and not the owner of the arena");

    // A body that holds a linear value by value binds to this overload, so
    // the refusal gives its reason.
    template <class Body>
        requires std::is_invocable_v<Body&&, Epoch<EpochOf<Tag>>&> && (!::foundation::linear::HoldsNoLinearValue<Body>)
    void with_epoch(Body&&) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

    template <class Body>
    void with_epoch(Body&&) && = delete("a temporary arena gives back its blocks at the end of the statement");

    // The bytes of each block that the arena holds, also the blocks that it
    // keeps for the next epoch.
    [[nodiscard]] std::size_t retained_bytes() const noexcept { return core_.retained_bytes_; }

    // The bytes of blocks that the arena keeps when an epoch ends.  The end
    // of each later epoch reads the limit.  A limit of zero keeps only the
    // first block.
    void set_retained_limit(Quantity<unit::Byte, std::size_t> limit) & noexcept { core_.retained_limit_ = limit.raw(); }

    [[nodiscard]] Quantity<unit::Byte, std::size_t> retained_limit() const noexcept {
        return Quantity<unit::Byte, std::size_t>{core_.retained_limit_};
    }

private:
    friend struct detail::BumpArenaDoor;

    explicit EpochArena(ArenaBlockSize block_size) noexcept : core_{block_size} {}

    detail::BumpArenaCore core_;
};

// The arena of a named arena tag, built in place by guaranteed elision, so a
// member or a local of the type takes it with no move.
template <class Tag, class... Refused>
    requires NamedArenaTag<Tag> && (sizeof...(Refused) == 0)
[[nodiscard]] BumpArena<Tag> mint_bump_arena(ArenaBlockSize block_size) noexcept {
    return detail::BumpArenaDoor::open<Tag>(block_size);
}

// The epoch arena of a member tag, built in place by guaranteed elision.
template <class Tag, class... Refused>
    requires ArenaMemberTag<Tag> && (sizeof...(Refused) == 0)
[[nodiscard]] EpochArena<Tag> mint_epoch_arena(ArenaBlockSize block_size) noexcept {
    return detail::BumpArenaDoor::open_epoch<Tag>(block_size);
}

// The arena type of a Box of a named arena tag.
template <class T>
concept BoxedArena = requires { typename T::brand_type; }
                  && std::same_as<T, BumpArena<typename T::brand_type>> && NamedArenaTag<typename T::brand_type>;

// A Box of the arena of a named arena tag.  The Box builds the arena from the
// door in its heap storage, with no move.
template <class T, class Marker>
    requires BoxedArena<T> && AllocationCapability<Marker>
[[nodiscard]] Box<T> mint_box(Marker /*allocation*/, ArenaBlockSize block_size) noexcept {
    return Box<T>{detail::BoxDoor::key(), detail::BoxBuild{},
                  [block_size]() noexcept { return detail::BumpArenaDoor::open<typename T::brand_type>(block_size); }};
}

namespace detail {

// The result of the body of with_bump_arena.
template <class Body, class Brand>
using bump_scope_result_t = std::invoke_result_t<Body&&, BumpArena<Brand>&>;

}  // namespace detail

// A result of the body of with_bump_arena: void, or a value that holds no
// borrow and names no brand of the scope.  The arena ends at the end of the
// scope, so a borrow of it would dangle.
template <class Given, class Brand>
concept BumpScopeResult = std::is_void_v<Given> || (HoldsNoBorrow<Given> && !detail::names_brand(^^Given, ^^Brand));

// The body of with_bump_arena: it takes the arena of the scope by reference
// with no throw, it holds no linear value by value, because the door calls it
// and a call is no consume, and its result leaves the scope.
template <class Body, class Brand>
concept BumpScopeBody =
    std::is_nothrow_invocable_v<Body&&, BumpArena<Brand>&> && ::foundation::linear::HoldsNoLinearValue<Body>
    && BumpScopeResult<detail::bump_scope_result_t<Body, Brand>, Brand>;

// A body whose result does not leave the scope: the refused twin of the door
// binds to it.
template <class Body, class Brand>
concept RefusedBumpScopeBody =
    std::is_nothrow_invocable_v<Body&&, BumpArena<Brand>&> && ::foundation::linear::HoldsNoLinearValue<Body>
    && (!BumpScopeResult<detail::bump_scope_result_t<Body, Brand>, Brand>);

// Calls body with an arena of the scope brand of this call site, and gives
// the result of body.  The arena lives in the frame of the door and ends
// after the body.  The capability is the authority to allocate the blocks of
// the arena.  The pack refuses each explicit argument, so no caller names
// the brand.
template <class... Refused, class Marker, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && AllocationCapability<Marker> && BumpScopeBody<Body, Fresh>
detail::bump_scope_result_t<Body, Fresh> with_bump_arena(Marker /*allocation*/, ArenaBlockSize block_size,
                                                         Body&& body) noexcept {
    BumpArena<Fresh> arena = detail::BumpArenaDoor::open<Fresh>(block_size);
    return static_cast<Body&&>(body)(arena);
}

// A body whose result holds a borrow or names the brand of the scope binds to
// this overload, so the refusal gives its reason.
template <class... Refused, class Marker, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && AllocationCapability<Marker> && RefusedBumpScopeBody<Body, Fresh>
void with_bump_arena(Marker allocation, ArenaBlockSize block_size, Body&& body) =
    delete("the end of the scope of with_bump_arena ends the arena, so the result of the body holds no reference "
           "and no address, and it names no brand of the arena: give a plain value");

// A body that holds a linear value by value binds to this overload, so the
// refusal gives its reason.
template <class... Refused, class Marker, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && AllocationCapability<Marker> && std::is_invocable_v<Body&&, BumpArena<Fresh>&>
             && (!::foundation::linear::HoldsNoLinearValue<Body>)
void with_bump_arena(Marker allocation, ArenaBlockSize block_size, Body&& body) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

}  // namespace foundation::core
