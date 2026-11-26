#pragma once

// The frame of an arena of a family tag: a store of a link proves at compile
// time that its target is in the arena of its holder.
//
//   with_arena_frame(arena, body)   body takes ArenaFrame<Family, Fresh>&, a
//                                   frame on the arena with the scope brand
//                                   Fresh of the call site
//   with_arena_frame(std::move(box), body)
//                                   the same frame on the arena of a Box.  The
//                                   door gives the Box back
//   frame.make<T>(alloc, args...)   one T in the arena, as a FrameRef.  A
//                                   FrameRef of a derived class converts to a
//                                   FrameRef of a base of the family
//   frame.array(alloc, count, fill), frame.build_array<T>(alloc, count, make)
//                                   a run in the arena, as a FrameRun
//   run.element(start)              the FrameElement of one element of a run
//   run.window(start, count)        a part of a run, as a FrameRun of the same
//                                   frame
//   frame.store(holder, &H::slot, value)
//                                   fills a FrameLink or a FrameRunSlot of the
//                                   holder with a value of the same frame
//   frame.clear(holder, &H::slot)   empties a FrameLink of the holder
//   frame.follow(holder, &H::slot)  the object of a FrameLink of the holder, as
//                                   a FrameRef of the frame
//   frame.store_runs<Slot, Is...>(holder, &H::slot, runs...)
//                                   fills one slot of counts of a
//                                   FrameRunSetSlot and its runs Is
//   frame.seal(runs...)             the runs, sealed for a parcel
//   mint_arena_parcel(std::move(box), body)
//                                   an ArenaParcel: the Box of an arena and the
//                                   runs that a frame on it sealed
//   with_arena_frame(std::move(parcel), body)
//                                   a new frame on the arena of the parcel,
//                                   with each run of the parcel as a FrameRun
//                                   of the new frame.  The door gives the parcel
//                                   back
//   parcel.view<I>()                the elements of run I of a parcel, as const
//   parcel.retained_bytes()         the bytes of each block of the arena of a
//                                   parcel
//
// Family
//   Two arenas of one family tag share the tag (foundation/core/BumpArena.h).
//   So a Ref, a Link or an ArenaRun of the tag can name an object of any arena
//   of the family, and a field of the tag in an object of arena A takes a Ref
//   of an object of arena B.  A frame gives each object that it makes the
//   scope brand of its call site in the type: a FrameRef, a FrameRun or a
//   FrameElement.  A slot, FrameLink or FrameRunSlot, keeps the family form.
//   Its store is private, and ArenaFrame is its one friend, so only a store of
//   a frame fills a slot, and that store takes the holder and the value of one
//   frame.  No member of the class of a holder fills a slot.  One frame is on
//   one arena, so the target of each link of a slot is in the arena of its
//   holder, and it lives as long as the holder.  A family Ref that to() gives
//   fills no slot.  A slot has no public store, so a class shows its slots as
//   public members.  A slot befriends the class template ArenaFrame, and the
//   rule specialization-home refuses an explicit specialization of it outside
//   this header, so no specialization reaches the store of a slot.
//
// Scope brand
//   Each call site of a door gives its frame a brand of its own
//   (CRUCIBLE_SCOPE_BRAND of foundation/Brand.h), and no caller names it.  The
//   rule brand-escape keeps a value that names the brand in its frame: no
//   result of a body, no member and no object of static storage holds one.  So
//   no class holds a FrameRef, a FrameRun or a FrameElement, and no object of
//   an arena holds one either.  weaken() gives the family form of
//   a FrameRef or a FrameRun for a read or for a result, and no door gives the
//   brand back.  Two frames on one arena are two proofs of the same fact, so a
//   frame in a frame on one arena is sound.  A call site that runs two times,
//   in a loop or in a recursion, gives its two frames one brand.  That is sound
//   too: no value of the brand outlives the call of its door, so no value of the
//   first run reaches a store of the second.
//
// Parcel
//   A frame lives in one call, and a callee in a source file cannot name its
//   brand.  A build that crosses the stages of a pipeline seals its runs into
//   a parcel: mint_arena_parcel takes the Box of the arena by move, its body
//   makes the runs in a frame on the Box, and the body gives frame.seal(runs).
//   The seal names the brand of that frame, and the mint takes only a seal of
//   its own frame.  So each run of a parcel is in the arena of its Box.  The
//   parcel moves with the Box, and with_arena_frame of a parcel opens a new
//   frame on the same Box and gives each run as a FrameRun of the new frame.
//   The proof is in the type of the parcel, so no door tests an address.
//
// Cost
//   A FrameRef and a FrameElement are one address, a FrameRun is the run, and
//   a store writes the family form of its value.  The brands are types, so the
//   checks cost no instruction.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Convert.h>
#include <foundation/core/Link.h>
#include <foundation/core/Record.h>
#include <foundation/core/Ref.h>
#include <foundation/core/RunSet.h>
#include <foundation/reflect/TypeList.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace foundation::core {

template <class Family, class Fresh>
class ArenaFrame;

template <class Family, class... Ts>
class ArenaParcel;

// The one builder of a frame and of a parcel (the doors below).
class ArenaFrameDoor;

// A slot of a link: one object of the family, or no object, as a Link is.
// Its reads are the reads of a Link, and only a frame fills it (the family).
// An empty slot states the absence of its object, and option() gives that
// absence as an Option for a match.  A slot does not copy or move, because a
// copy would carry its link into an object of a different arena with no store
// of a frame.
template <RefTarget T, ::foundation::brand::IsBrand Family>
class[[= ::foundation::transfer::refers_deep{}]][[= detail::names_owner_second{}]] FrameLink final {
public:
    constexpr FrameLink() noexcept = default;
    FrameLink(FrameLink const&) = delete("a slot takes a link only through a store of a frame");
    FrameLink& operator=(FrameLink const&) = delete("a slot takes a link only through a store of a frame");
    constexpr ~FrameLink() = default;

    [[nodiscard]] constexpr bool is_some() const noexcept { return link_.is_some(); }
    [[nodiscard]] constexpr bool is_none() const noexcept { return link_.is_none(); }

    // The slot as an Option of a Ref.  A const slot gives a Ref of const.
    [[nodiscard]] constexpr Option<Ref<T, Family>> option() & noexcept { return link_.option(); }
    [[nodiscard]] constexpr Option<Ref<T const, Family>> option() const& noexcept { return link_.option(); }

    // The Ref, or the end of the process with the reason why.
    [[nodiscard]] constexpr Ref<T, Family> expect(Fmt<> why) & noexcept { return link_.expect(why); }
    [[nodiscard]] constexpr Ref<T const, Family> expect(Fmt<> why) const& noexcept { return link_.expect(why); }

    // One pass over the object, or no pass.  A const slot gives a Ref of const.
    [[nodiscard]] constexpr LinkCursor<T, Family> begin() & noexcept { return link_.begin(); }
    [[nodiscard]] constexpr LinkCursor<T, Family> end() & noexcept { return link_.end(); }
    [[nodiscard]] constexpr LinkCursor<T const, Family> begin() const& noexcept { return link_.begin(); }
    [[nodiscard]] constexpr LinkCursor<T const, Family> end() const& noexcept { return link_.end(); }

    // on_some with the Ref, or on_none, as match of a Link.  A const slot gives
    // on_some a Ref of const.
    template <class OnSome, class OnNone>
    constexpr auto match(OnSome&& on_some, OnNone&& on_none) & {
        return link_.match(static_cast<OnSome&&>(on_some), static_cast<OnNone&&>(on_none));
    }
    template <class OnSome, class OnNone>
    constexpr auto match(OnSome&& on_some, OnNone&& on_none) const& {
        return link_.match(static_cast<OnSome&&>(on_some), static_cast<OnNone&&>(on_none));
    }

    // True when the slot holds an object and holds gives true for its Ref of
    // const, as is_some_and of a Link.
    template <class Holds>
    [[nodiscard]] constexpr bool is_some_and(Holds&& holds) const {
        return link_.is_some_and(static_cast<Holds&&>(holds));
    }

private:
    template <class FrameFamily, class Fresh>
    friend class ArenaFrame;

    // The store of a frame (the family).
    constexpr void set_(Option<Ref<T, Family>> target) noexcept { link_ = Link<T, Family>{target}; }

    Link<T, Family> link_{};
};

// A slot of a run of the family: the run, or the empty run.  Its reads are
// the reads of an ArenaRun, and only a frame fills it, as for FrameLink.
template <ViewElement T, ::foundation::brand::IsBrand Family>
class[[= ::foundation::transfer::refers_deep{}]][[= detail::names_owner_second{}]] FrameRunSlot final {
public:
    constexpr FrameRunSlot() noexcept = default;
    FrameRunSlot(FrameRunSlot const&) = delete("a slot takes a run only through a store of a frame");
    FrameRunSlot& operator=(FrameRunSlot const&) = delete("a slot takes a run only through a store of a frame");
    constexpr ~FrameRunSlot() = default;

    // The elements of the run, as const from a const slot.
    [[nodiscard]] constexpr View<T> view() & noexcept { return run_.view(); }
    [[nodiscard]] constexpr View<T const> view() const& noexcept { return run_.view(); }
    void view() const&& = delete("a View of a temporary slot outlives the name of the slot: name the slot first");

    // The count of the elements.
    [[nodiscard]] constexpr std::size_t size() const noexcept { return run_.size(); }

    // The count elements from start, as a run of the family, or no run when
    // they do not fit, as window of an ArenaRun.  A const slot gives a run of
    // const elements.
    [[nodiscard]] constexpr auto window(RunStart start, ElementCount count) & noexcept {
        return run_.window(start, count);
    }
    [[nodiscard]] constexpr auto window(RunStart start, ElementCount count) const& noexcept {
        return run_.window(start, count);
    }

private:
    template <class FrameFamily, class Fresh>
    friend class ArenaFrame;

    // The store of a frame (the family).
    constexpr void set_(ArenaRun<T, Family> run) noexcept { run_ = run; }

    ArenaRun<T, Family> run_{};
};

// A slot of a set of runs of the family (foundation/core/RunSet.h): a table of
// counts and the first element of each run.  Its reads are the reads of the
// ArenaRunSet, and only a frame fills it, as for FrameLink: frame.store_runs
// sets the count of one slot of counts and each run of that slot together.
template <UnsignedInteger Count, ::foundation::brand::IsFreshBrand Family, class... Runs>
    requires detail::RunsOfSet<Runs...>
class FrameRunSetSlot final {
public:
    constexpr FrameRunSetSlot() noexcept = default;
    FrameRunSetSlot(FrameRunSetSlot const&) = delete("a slot takes its runs only through a store of a frame");
    FrameRunSetSlot&
    operator=(FrameRunSetSlot const&) = delete("a slot takes its runs only through a store of a frame");
    constexpr ~FrameRunSetSlot() = default;

    // The count at the slot of counts.
    template <std::size_t Slot>
    [[nodiscard]] constexpr Count count() const noexcept {
        return set_.template count<Slot>();
    }

    // True when the run at index I holds the count of its slot.
    template <std::size_t I>
    [[nodiscard]] constexpr bool has_run() const noexcept {
        return set_.template has_run<I>();
    }

    // The run at index I, as a View of const elements.
    template <std::size_t I>
    [[nodiscard]] constexpr auto view() const& noexcept {
        return set_.template view<I>();
    }
    template <std::size_t I>
    void view() const&& = delete("a View of a temporary slot dangles when the owner of its runs ends");

private:
    template <class FrameFamily, class Fresh>
    friend class ArenaFrame;

    // The set may overlap, so the members that follow a slot of an object
    // take the bytes of pad at the end of the set, as they take them after
    // the set itself.  The slot then keeps the size of its object.
    [[no_unique_address]] ArenaRunSet<Count, Family, Runs...> set_{};
};

namespace detail {

// True when a FrameRef of Derived converts to a FrameRef of Base: Base is a
// public and unambiguous base of Derived, or that base as const, and it
// states the family as its brand_type.  So to() of the base gives a Ref of
// the family, as for a link to a base (LinkTakes below).
template <class Base, class Derived, class Family>
concept FrameUpcast =
    (!std::same_as<std::remove_cv_t<Base>, std::remove_cv_t<Derived>>)
    && std::is_convertible_v<Derived*, Base*> && requires { typename std::remove_const_t<Base>::brand_type; }
    && std::same_as<typename std::remove_const_t<Base>::brand_type, Family>;

// The family Ref of the base of the object of a family Ref.
template <class Base, class Derived, class Family>
[[nodiscard]] constexpr Ref<Base, Family> base_ref_(Ref<Derived, Family> const& derived) noexcept {
    Base& base = *derived;
    return to(base);
}

}  // namespace detail

// One object that a frame made, with the brand of the frame.  It gives the
// object, and weaken() gives the family Ref (the scope brand).
template <RefTarget T, ::foundation::brand::IsBrand Family, ::foundation::brand::IsBrand Fresh>
class[[= ::foundation::transfer::refers{}]] FrameRef final {
public:
    // The FrameRef of a public base of the object, of the same frame
    // (detail::FrameUpcast).  The object and the frame stay the same, so the
    // FrameRef is sound.  No door converts a base to a derived class, and no
    // conversion changes the family or the frame.
    template <class Derived>
        requires detail::FrameUpcast<T, Derived, Family>
    constexpr FrameRef(FrameRef<Derived, Family, Fresh> const& derived) noexcept
        : ref_{detail::base_ref_<T>(derived.weaken())} {}

    [[nodiscard]] constexpr T& operator*() const noexcept { return *ref_; }
    [[nodiscard]] constexpr T* operator->() const noexcept { return __builtin_addressof(*ref_); }

    // The family Ref of the object, for a read or a result.
    [[nodiscard]] constexpr Ref<T, Family> weaken() const noexcept { return ref_; }

private:
    friend class ArenaFrame<Family, Fresh>;

    constexpr explicit FrameRef(Ref<T, Family> ref) noexcept : ref_{ref} {}

    Ref<T, Family> ref_;
};

// One element of a run that a frame made, with the brand of the frame.  It is
// a holder of a store, as a FrameRef is.  It has no family Ref, because the
// run gives no Ref of one element, so the element is no value of a store.
template <class T, ::foundation::brand::IsBrand Family, ::foundation::brand::IsBrand Fresh>
class[[= ::foundation::transfer::refers{}]] FrameElement final {
public:
    [[nodiscard]] constexpr T& operator*() const noexcept { return *object_; }
    [[nodiscard]] constexpr T* operator->() const noexcept { return object_; }

private:
    template <ViewElement, ::foundation::brand::IsBrand, ::foundation::brand::IsBrand>
    friend class FrameRun;

    constexpr explicit FrameElement(T& object) noexcept : object_{__builtin_addressof(object)} {}

    T* object_ = nullptr;
    // The seal makes the class no image of bytes, so no byte route builds one.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

// A run that a frame made, with the brand of the frame.  weaken() gives the
// family run (the scope brand), and element() gives one element as a holder of
// a store.
template <ViewElement T, ::foundation::brand::IsBrand Family, ::foundation::brand::IsBrand Fresh>
class[[= ::foundation::transfer::refers_deep{}]] FrameRun final {
public:
    // The run of the same elements as const, of the same frame.
    template <class Writable>
        requires std::same_as<Writable const, T> && (!std::same_as<Writable, T>)
    constexpr FrameRun(FrameRun<Writable, Family, Fresh> const& other) noexcept : run_{other.run_} {}

    [[nodiscard]] constexpr View<T> view() & noexcept { return run_.view(); }
    [[nodiscard]] constexpr View<T const> view() const& noexcept { return run_.view(); }
    void view() const&& = delete("a View of a temporary run outlives the name of the run: name the run first");

    [[nodiscard]] constexpr std::size_t size() const noexcept { return run_.size(); }

    // The family run, for a read, a result or a seal.  A const run gives a
    // run of const elements, as a copy of a const ArenaRun does.
    [[nodiscard]] constexpr ArenaRun<T, Family> weaken() & noexcept { return run_; }
    [[nodiscard]] constexpr ArenaRun<T const, Family> weaken() const& noexcept
        requires(!std::is_const_v<T>)
    {
        return ArenaRun<T const, Family>{run_};
    }

    // The element at start, or no value at or past the count.  Complexity:
    // one compare.
    [[nodiscard]] constexpr Option<FrameElement<T, Family, Fresh>> element(RunStart start) noexcept {
        std::size_t const position = start.value().raw();
        if (position >= run_.size()) {
            return Option<FrameElement<T, Family, Fresh>>{};
        }
        T& object = *run_.view().get(position).expect("a position below the count of a run names an element");
        return Option<FrameElement<T, Family, Fresh>>::some(FrameElement<T, Family, Fresh>{object});
    }

    // The count elements from start, as a run of this frame, or no run when
    // they do not fit in this run (window of an ArenaRun).  A window holds
    // elements of this run, so it is in the arena of the frame too.  A const
    // run gives a run of const elements.  The result is an Option<FrameRun>,
    // which the class cannot name before it is complete.  Complexity: one
    // compare.
    [[nodiscard]] constexpr auto window(RunStart start, ElementCount count) & noexcept {
        Option<ArenaRun<T, Family>> part = run_.window(start, count);
        if (part.is_none()) {
            return Option<FrameRun>{};
        }
        return Option<FrameRun>::some(
            FrameRun{static_cast<Option<ArenaRun<T, Family>>&&>(part).expect("the test above found the window")});
    }
    [[nodiscard]] constexpr auto window(RunStart start, ElementCount count) const& noexcept {
        using ConstRun = FrameRun<T const, Family, Fresh>;
        Option<ArenaRun<T const, Family>> part = run_.window(start, count);
        if (part.is_none()) {
            return Option<ConstRun>{};
        }
        return Option<ConstRun>::some(
            ConstRun{static_cast<Option<ArenaRun<T const, Family>>&&>(part).expect("the test above found the window")});
    }

private:
    friend class ArenaFrame<Family, Fresh>;
    friend class ArenaFrameDoor;
    template <ViewElement, ::foundation::brand::IsBrand, ::foundation::brand::IsBrand>
    friend class FrameRun;

    constexpr explicit FrameRun(ArenaRun<T, Family> run) noexcept : run_{run} {}

    ArenaRun<T, Family> run_{};
};

// The runs that one frame sealed for a parcel (the parcel).  It names the
// brand of its frame, so it stays in the body that made it.
template <class Family, class Fresh, class... Ts>
class [[nodiscard]] ArenaSeal final {
public:
    ArenaSeal(ArenaSeal&&) noexcept = default;
    ArenaSeal(ArenaSeal const&) = delete("a seal goes to one parcel");
    ArenaSeal& operator=(ArenaSeal const&) = delete("a seal goes to one parcel");
    ArenaSeal& operator=(ArenaSeal&&) = delete("a seal goes to one parcel");

private:
    friend class ArenaFrame<Family, Fresh>;
    friend class ArenaFrameDoor;

    constexpr explicit ArenaSeal(ArenaRun<Ts, Family>... runs) noexcept : runs_{runs...} {}

    Record<ArenaRun<Ts, Family>...> runs_;
};

namespace detail {

// True for a holder of a store of the frame: a FrameRef or a FrameElement of
// the family and the brand.
template <class Holder, class Family, class Fresh>
inline constexpr bool is_frame_holder_ = false;
template <class T, class Family, class Fresh>
inline constexpr bool is_frame_holder_<FrameRef<T, Family, Fresh>, Family, Fresh> = true;
template <class T, class Family, class Fresh>
inline constexpr bool is_frame_holder_<FrameElement<T, Family, Fresh>, Family, Fresh> = true;

// The object of a holder.
template <class Holder>
using held_t_ = std::remove_reference_t<decltype(*std::declval<Holder const&>())>;

// True when a link of type Target takes an object of type Made of the family:
// the same type, the type with const, or a base that states the family as its
// brand_type, so that to() gives the Ref of the base with the family brand.
template <class Target, class Made, class Family>
concept LinkTakes = std::same_as<Target, Made> || std::same_as<Target, Made const>
                 || (std::is_base_of_v<std::remove_const_t<Target>, Made> && requires { typename Target::brand_type; }
                     && std::same_as<typename std::remove_const_t<Target>::brand_type, Family>);

// The Ref of a link of type Target to the object of a family Ref of type Made.
template <class Target, class Made, class Family>
[[nodiscard]] constexpr Ref<Target, Family> link_ref_(Ref<Made, Family> const& made) noexcept {
    if constexpr (std::same_as<Target, Made>) {
        return made;
    } else if constexpr (std::same_as<Target, Made const>) {
        return Ref<Target, Family>{made};
    } else {
        Target& base = *made;
        return to(base);
    }
}

}  // namespace detail

// A holder of a store of the frame: a FrameRef or a FrameElement of its
// family and its brand.
template <class Holder, class Family, class Fresh>
concept FrameHolder = detail::is_frame_holder_<std::remove_cvref_t<Holder>, Family, Fresh>;

// The run at index I of a set of the runs Runs has its count at the slot Slot.
template <std::size_t Slot, std::size_t I, class... Runs>
concept RunOfSlot =
    (I < sizeof...(Runs)) && (::foundation::reflect::pack_at_t<I, ::foundation::reflect::Pack<Runs...>>::slot == Slot);

// The frame of one arena of a family, with the scope brand Fresh of the call
// site of its door (the family).  Only the doors of this header build a frame,
// and the frame lives in the frame of the door.  It has no copy and no move.
template <class Family, class Fresh>
class [[nodiscard]] ArenaFrame final {
public:
    ArenaFrame(ArenaFrame const&) = delete("a frame of an arena lives in the frame of its door");
    ArenaFrame& operator=(ArenaFrame const&) = delete("a frame of an arena lives in the frame of its door");

    // One T in the arena, as a FrameRef (make of BumpArena).
    template <class T, class Marker, class... Args>
        requires ArenaMakes<T, Family, Marker, Args...> && ArenaKeeps<T, Family>
    [[nodiscard]] FrameRef<T, Family, Fresh> make(Marker allocation, Args&&... args) noexcept {
        return FrameRef<T, Family, Fresh>{arena_->template make<T>(allocation, static_cast<Args&&>(args)...)};
    }

    // count copies of fill, as a FrameRun (array of BumpArena).
    template <class T, class Marker>
        requires CanFillArenaArray<T, Marker> && ArenaKeeps<T, Family>
    [[nodiscard]] FrameRun<T, Family, Fresh> array(Marker allocation, std::size_t count, T const& fill) noexcept {
        return FrameRun<T, Family, Fresh>{arena_->array(allocation, count, fill)};
    }

    // count elements, each the value that make gives for its position, as a
    // FrameRun (build_array of BumpArena).
    template <class T, class Marker, class Make>
        requires CanBuildArenaArray<T, Marker, Make> && ::foundation::linear::HoldsNoLinearValue<Make>
              && ArenaKeeps<T, Family>
    [[nodiscard]] FrameRun<T, Family, Fresh> build_array(Marker allocation, ElementCount count, Make&& make) noexcept {
        return FrameRun<T, Family, Fresh>{
            arena_->template build_array<T>(allocation, count, static_cast<Make&&>(make))};
    }

    // Fills the link slot of the holder with the object of value.  The two are
    // of this frame, so the object is in the arena of the holder.
    template <class Holder, class Base, class Target, class Made>
        requires FrameHolder<Holder, Family, Fresh> && std::is_base_of_v<Base, detail::held_t_<Holder>>
              && detail::LinkTakes<Target, Made, Family>
    constexpr void store(Holder const& holder, FrameLink<Target, Family> Base::* slot,
                         FrameRef<Made, Family, Fresh> const& value) noexcept {
        ((*holder).*slot).set_(some(detail::link_ref_<Target>(value.weaken())));
    }

    // Fills the run slot of the holder with the run.  The two are of this
    // frame, so the run is in the arena of the holder.
    template <class Holder, class Base, class Element, class Made>
        requires FrameHolder<Holder, Family, Fresh> && std::is_base_of_v<Base, detail::held_t_<Holder>>
              && (std::same_as<Element, Made> || std::same_as<Element, Made const>)
    constexpr void store(Holder const& holder, FrameRunSlot<Element, Family> Base::* slot,
                         FrameRun<Made, Family, Fresh> value) noexcept {
        FrameRun<Element, Family, Fresh> run{value};
        ((*holder).*slot).set_(run.weaken());
    }

    // Empties the link slot of the holder.
    template <class Holder, class Base, class Target>
        requires FrameHolder<Holder, Family, Fresh> && std::is_base_of_v<Base, detail::held_t_<Holder>>
    constexpr void clear(Holder const& holder, FrameLink<Target, Family> Base::* slot) noexcept {
        ((*holder).*slot).set_(Option<Ref<Target, Family>>{});
    }

    // The object of the link slot of the holder, as a FrameRef of this frame,
    // or no value for an empty slot.  A walk in a frame takes each node of a
    // chain through it.  Complexity: one load.
    //
    // The proof is an induction over the steps of each frame on the arenas of
    // the family.  The claim holds two facts.  A FrameRef or a FrameElement
    // of a frame names an object in the arena of the frame.  And the object of
    // each link slot is in the arena of the holder of the slot.
    //   - A new slot is empty.  A slot has no copy, and only a store or a
    //     clear of a frame fills it.
    //   - make, array, build_array, window and element of a frame give objects
    //     of its own arena.  A parcel gives back runs that a frame on its Box
    //     sealed, and the Box holds that one arena.
    //   - A store takes a holder and a value of one frame, so both are in the
    //     arena of that frame, and the object of the slot is in the arena of
    //     its holder.
    //   - A follow takes a holder of this frame, which is in the arena of this
    //     frame.  The object of its slot is in the arena of the holder, so it
    //     is in the arena of this frame too, and the FrameRef is sound.
    // No other step makes a FrameRef or fills a slot, so each step keeps the
    // two facts.
    template <class Holder, class Base, class Target>
        requires FrameHolder<Holder, Family, Fresh> && std::is_base_of_v<Base, detail::held_t_<Holder>>
    [[nodiscard]] constexpr Option<FrameRef<Target, Family, Fresh>>
    follow(Holder const& holder, FrameLink<Target, Family> Base::* slot) const noexcept {
        Option<Ref<Target, Family>> const target = ((*holder).*slot).link_.option();
        return target.match(
            [](Ref<Target, Family> const& object) noexcept {
                return Option<FrameRef<Target, Family, Fresh>>::some(FrameRef<Target, Family, Fresh>{object});
            },
            []() noexcept { return Option<FrameRef<Target, Family, Fresh>>{}; });
    }

    // Fills one slot of counts of a set of runs of the holder: the count of
    // the slot Slot takes the size of the first run, and the run at each index
    // of Is takes the run at the same position of runs.  Each index names a
    // run of the slot Slot, and each run is of this frame.  A run of another
    // size, or a size above the count type, ends the process in each build,
    // as set of the ArenaRunSet does.
    template <std::size_t Slot, std::size_t... Is, class Holder, class Base, class Count, class... Runs, class... Mades>
        requires FrameHolder<Holder, Family, Fresh> && std::is_base_of_v<Base, detail::held_t_<Holder>>
              && (sizeof...(Is) == sizeof...(Mades)) && (sizeof...(Is) > 0) && (RunOfSlot<Slot, Is, Runs...> && ...)
    constexpr void store_runs(Holder const& holder, FrameRunSetSlot<Count, Family, Runs...> Base::* slot,
                              FrameRun<Mades, Family, Fresh>... runs) noexcept {
        using Set = ArenaRunSet<Count, Family, Runs...>;
        Set& set = ((*holder).*slot).set_;
        Record<FrameRun<Mades, Family, Fresh>...> all{runs...};
        std::size_t const size = all.at(index<0>()).size();
        set.template set_count<Slot>(
            narrow<Count>(size).expect("a run of a set holds more elements than the count type of the set holds"));
        auto const put = [&set]<std::size_t I, class Made>(FrameRun<Made, Family, Fresh>& made) noexcept {
            FrameRun<typename Set::template element_t<I>, Family, Fresh> run{made};
            set.template set<I>(run.weaken());
        };
        [&put, &all]<std::size_t... Positions>(std::index_sequence<Positions...> /*order*/) noexcept {
            (put.template operator()<Is>(all.at(index<Positions>())), ...);
        }(std::index_sequence_for<Mades...>{});
    }

    // The runs, sealed for the parcel of this frame (the parcel).
    template <class... Ts>
    [[nodiscard]] constexpr ArenaSeal<Family, Fresh, Ts...> seal(FrameRun<Ts, Family, Fresh>... runs) noexcept {
        return ArenaSeal<Family, Fresh, Ts...>{runs.weaken()...};
    }

private:
    friend class ArenaFrameDoor;

    constexpr explicit ArenaFrame(BumpArena<Family>& arena) noexcept : arena_{__builtin_addressof(arena)} {}

    BumpArena<Family>* arena_ = nullptr;
    // The seal makes the class no image of bytes, so no byte route builds one.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

// The parcel of an arena: the Box of the arena and the runs that a frame on
// it sealed (the parcel).  Only mint_arena_parcel builds one.  The parcel moves
// with its Box, and a move moves no run.  view<I>() gives the elements of run
// I as const for a read outside a frame.
//
// The parcel owns its family tag and an object of each element type
// (by_arguments{}, rule 6 of foundation/Transfer.h).  So it holds a transfer
// property when each element type holds it, and the walk reads no member.
// The walk refuses a BumpArena, because its pointers name its blocks.  Thus
// the arena is the one part of the parcel that the walk does not ask.  The
// parcel holds the one Box of the arena, and the brand of a frame keeps each
// FrameRef in its frame.  So no object outside the parcel points into the
// arena.
//
// The walk also reads a Link, a Ref or an ArenaRun of the family in an element
// as a borrow, also when its target is in the same arena.  Thus a parcel of the
// nodes of crucible/MerkleDag.h is not SendableOwned: TraceNode, RegionNode,
// BranchNode, LoopNode, MemoryPlan and TraceEntry hold such borrows.  The four
// node classes are also not Sendable.  A walk that reads a frame slot of the
// family in a parcel as a part of the parcel gives these properties.
template <class Family, class... Ts>
class [[nodiscard]][[= ::foundation::transfer::by_arguments{}]] ArenaParcel final {
public:
    ArenaParcel(ArenaParcel&&) noexcept = default;
    ArenaParcel& operator=(ArenaParcel&&) noexcept = default;
    ArenaParcel(ArenaParcel const&) = delete("a parcel owns its arena alone: a copy would free it two times");
    ArenaParcel&
    operator=(ArenaParcel const&) = delete("a parcel owns its arena alone: a copy would free it two times");
    ~ArenaParcel() = default;

    // The elements of run I, as const.
    template <std::size_t I>
        requires(I < sizeof...(Ts))
    [[nodiscard]] constexpr auto view() const& noexcept {
        return runs_.at(index<I>()).view();
    }
    template <std::size_t I>
    void view() const&& = delete("a View of a temporary parcel outlives its arena: name the parcel first");

    // The bytes of each block of the arena of the parcel.
    [[nodiscard]] constexpr std::size_t retained_bytes() const noexcept { return arena_->retained_bytes(); }

private:
    friend class ArenaFrameDoor;

    ArenaParcel(Box<BumpArena<Family>>&& arena, Record<ArenaRun<Ts, Family>...>& runs) noexcept
        : arena_{static_cast<Box<BumpArena<Family>>&&>(arena)}, runs_{runs} {}

    Box<BumpArena<Family>> arena_;
    Record<ArenaRun<Ts, Family>...> runs_;
};

namespace detail {

// True for a seal of the frame of the family and the brand (the parcel).
template <class Sealed, class Family, class Fresh>
inline constexpr bool is_seal_of_ = false;
template <class Family, class Fresh, class... Ts>
inline constexpr bool is_seal_of_<ArenaSeal<Family, Fresh, Ts...>, Family, Fresh> = true;

// The parcel of a seal.
template <class Sealed>
struct parcel_of_;
template <class Family, class Fresh, class... Ts>
struct parcel_of_<ArenaSeal<Family, Fresh, Ts...>> {
    using type = ArenaParcel<Family, Ts...>;
};

}  // namespace detail

// A result of the body of a frame: void, or a value that is no reference and
// names no brand of the frame.  It can hold a family Ref, because the arena
// outlives the frame.
template <class Given, class Fresh>
concept ArenaFrameResult =
    std::is_void_v<Given> || (!std::is_reference_v<Given> && !detail::names_brand(^^Given, ^^Fresh));

// The body of with_arena_frame: it takes the frame by reference with no throw,
// it holds no linear value, because the door calls it and a call is no consume,
// and its result names no brand of the frame.
template <class Body, class Family, class Fresh>
concept ArenaFrameBody =
    std::is_nothrow_invocable_v<Body&&, ArenaFrame<Family, Fresh>&> && ::foundation::linear::HoldsNoLinearValue<Body>
    && ArenaFrameResult<std::invoke_result_t<Body&&, ArenaFrame<Family, Fresh>&>, Fresh>;

// The body of mint_arena_parcel: as ArenaFrameBody, and it gives a seal of its
// own frame (the parcel).
template <class Body, class Family, class Fresh>
concept ArenaParcelBody =
    std::is_nothrow_invocable_v<Body&&, ArenaFrame<Family, Fresh>&> && ::foundation::linear::HoldsNoLinearValue<Body>
    && detail::is_seal_of_<std::invoke_result_t<Body&&, ArenaFrame<Family, Fresh>&>, Family, Fresh>;

// The body of with_arena_frame of a parcel: it takes the frame and one FrameRun
// of each run of the parcel, as ArenaFrameBody.
template <class Body, class Family, class Fresh, class... Ts>
concept ParcelFrameBody =
    std::is_nothrow_invocable_v<Body&&, ArenaFrame<Family, Fresh>&, FrameRun<Ts, Family, Fresh>&...>
    && ::foundation::linear::HoldsNoLinearValue<Body>
    && ArenaFrameResult<std::invoke_result_t<Body&&, ArenaFrame<Family, Fresh>&, FrameRun<Ts, Family, Fresh>&...>,
                        Fresh>;

// The four doors of this header (the definitions below).  The door class
// ArenaFrameDoor names them as friends, so these declarations stand before it.
template <class... Refused, class Family, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && ArenaFrameBody<Body, Family, Fresh>
std::invoke_result_t<Body&&, ArenaFrame<Family, Fresh>&>
with_arena_frame(BumpArena<Family>& arena CRUCIBLE_LIFETIMEBOUND, Body&& body) noexcept;

template <class... Refused, class Family, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && ArenaFrameBody<Body, Family, Fresh>
[[nodiscard]] auto with_arena_frame(Box<BumpArena<Family>>&& arena, Body&& body) noexcept;

template <class... Refused, class Family, class Body, ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && ArenaParcelBody<Body, Family, Fresh>
[[nodiscard]] typename detail::parcel_of_<std::invoke_result_t<Body&&, ArenaFrame<Family, Fresh>&>>::type
mint_arena_parcel(Box<BumpArena<Family>>&& arena, Body&& body) noexcept;

template <class... Refused, class Family, class... Ts, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && ParcelFrameBody<Body, Family, Fresh, Ts...>
[[nodiscard]] auto with_arena_frame(ArenaParcel<Family, Ts...>&& parcel, Body&& body) noexcept;

// The one builder of a frame and of a parcel.  Its members are private, and
// the four doors of this header are its friends: each door opens a frame of
// the scope brand of its own call site, on an arena that it holds, and only
// the mint seals a Box with the seal of its own frame.  A frame is a
// prvalue, so a door builds it in place by guaranteed elision.  A home owner
// row of utils/rule-table.txt keeps this definition in this header.
class ArenaFrameDoor final : ::foundation::NoObject<ArenaFrameDoor> {
    template <class... Refused, class Family, class Body, ::foundation::brand::IsBrand Fresh>
        requires(sizeof...(Refused) == 0) && ArenaFrameBody<Body, Family, Fresh>
    friend auto ::foundation::core::with_arena_frame(BumpArena<Family>& arena, Body&& body) noexcept
        -> std::invoke_result_t<Body&&, ArenaFrame<Family, Fresh>&>;
    template <class... Refused, class Family, class Body, ::foundation::brand::IsBrand Fresh>
        requires(sizeof...(Refused) == 0) && ArenaFrameBody<Body, Family, Fresh>
    friend auto ::foundation::core::with_arena_frame(Box<BumpArena<Family>>&& arena, Body&& body) noexcept;
    template <class... Refused, class Family, class Body, ::foundation::brand::IsBrand Fresh>
        requires(sizeof...(Refused) == 0) && ArenaParcelBody<Body, Family, Fresh>
    friend auto ::foundation::core::mint_arena_parcel(Box<BumpArena<Family>>&& arena, Body&& body) noexcept ->
        typename detail::parcel_of_<std::invoke_result_t<Body&&, ArenaFrame<Family, Fresh>&>>::type;
    template <class... Refused, class Family, class... Ts, class Body, ::foundation::brand::IsBrand Fresh>
        requires(sizeof...(Refused) == 0) && ParcelFrameBody<Body, Family, Fresh, Ts...>
    friend auto ::foundation::core::with_arena_frame(ArenaParcel<Family, Ts...>&& parcel, Body&& body) noexcept;

    template <class Family, class Fresh>
    [[nodiscard]] static ArenaFrame<Family, Fresh> open(BumpArena<Family>& arena) noexcept {
        return ArenaFrame<Family, Fresh>{arena};
    }

    // A frame on the arena of a Box.  The door that calls it holds the Box by
    // value, so no other name of the Box can end the arena while the frame is
    // open.  operator-> of the Box gives the arena that the Box holds.
    template <class Family, class Fresh>
    [[nodiscard]] static ArenaFrame<Family, Fresh> open(Box<BumpArena<Family>>& arena) noexcept {
        return ArenaFrame<Family, Fresh>{*arena.operator->()};
    }

    // A frame on the arena of the Box of a parcel.
    template <class Family, class Fresh, class... Ts>
    [[nodiscard]] static ArenaFrame<Family, Fresh> open(ArenaParcel<Family, Ts...>& parcel) noexcept {
        return open<Family, Fresh>(parcel.arena_);
    }

    template <class Family, class Fresh, class... Ts>
    [[nodiscard]] static ArenaParcel<Family, Ts...> seal(Box<BumpArena<Family>>&& arena,
                                                         ArenaSeal<Family, Fresh, Ts...>& sealed) noexcept {
        return ArenaParcel<Family, Ts...>{static_cast<Box<BumpArena<Family>>&&>(arena), sealed.runs_};
    }

    // Calls the body with the frame and each run of the parcel as a FrameRun
    // of the frame, and gives the value of the body.
    template <class Fresh, class Family, class... Ts, class Body, std::size_t... Is>
    static auto call_with_runs(ArenaParcel<Family, Ts...>& parcel, ArenaFrame<Family, Fresh>& frame, Body&& body,
                               std::index_sequence<Is...> /*order*/) noexcept {
        Record<FrameRun<Ts, Family, Fresh>...> runs{FrameRun<Ts, Family, Fresh>{parcel.runs_.at(index<Is>())}...};
        return static_cast<Body&&>(body)(frame, runs.at(index<Is>())...);
    }
};

// Calls body with a frame on the arena, of the scope brand of this call site,
// and gives the result of body (the family).  The pack refuses each explicit
// argument, so no caller names the brand.  CRUCIBLE_LIFETIMEBOUND marks the
// arena, so the rule view-escape refuses a return of a family Ref of a local
// arena that the result holds.
template <class... Refused, class Family, class Body, ::foundation::brand::IsBrand Fresh>
    requires(sizeof...(Refused) == 0) && ArenaFrameBody<Body, Family, Fresh>
std::invoke_result_t<Body&&, ArenaFrame<Family, Fresh>&>
with_arena_frame(BumpArena<Family>& arena CRUCIBLE_LIFETIMEBOUND, Body&& body) noexcept {
    ArenaFrame<Family, Fresh> frame = ArenaFrameDoor::open<Family, Fresh>(arena);
    return static_cast<Body&&>(body)(frame);
}

// The same frame on the arena of a Box, and the Box back: alone for a body
// that gives nothing, and else in a Record after the value of the body.  The
// Box moves into the frame of the door, so no other name of it can end the
// arena while the frame is open.
template <class... Refused, class Family, class Body, ::foundation::brand::IsBrand Fresh>
    requires(sizeof...(Refused) == 0) && ArenaFrameBody<Body, Family, Fresh>
[[nodiscard]] auto with_arena_frame(Box<BumpArena<Family>>&& arena, Body&& body) noexcept {
    Box<BumpArena<Family>> held = static_cast<Box<BumpArena<Family>>&&>(arena);
    ArenaFrame<Family, Fresh> frame = ArenaFrameDoor::open<Family, Fresh>(held);
    using Result = std::invoke_result_t<Body&&, ArenaFrame<Family, Fresh>&>;
    if constexpr (std::is_void_v<Result>) {
        static_cast<Body&&>(body)(frame);
        return held;
    } else {
        Result value = static_cast<Body&&>(body)(frame);
        return Record<Result, Box<BumpArena<Family>>>{static_cast<Result&&>(value),
                                                      static_cast<Box<BumpArena<Family>>&&>(held)};
    }
}

// A Box that the caller names as an lvalue stays an object of the caller,
// which could end its arena while the frame is open.
template <class... Refused, class Family, class Body>
void with_arena_frame(Box<BumpArena<Family>>& arena, Body&& body) =
    delete("with_arena_frame takes a Box by move and gives it back: pass std::move(box)");

// A frame on a temporary arena would end the arena at the end of the statement.
template <class... Refused, class Family, class Body>
void with_arena_frame(BumpArena<Family>&& arena, Body&& body) =
    delete("a temporary arena gives back its blocks at the end of the statement: name the arena");

// An ArenaParcel: the Box of the arena and the runs that the body seals in a
// frame on it (the parcel).  The body gives frame.seal(runs...) of its own
// frame, so each run of the parcel is in the arena of its Box.
template <class... Refused, class Family, class Body, ::foundation::brand::IsBrand Fresh>
    requires(sizeof...(Refused) == 0) && ArenaParcelBody<Body, Family, Fresh>
[[nodiscard]] typename detail::parcel_of_<std::invoke_result_t<Body&&, ArenaFrame<Family, Fresh>&>>::type
mint_arena_parcel(Box<BumpArena<Family>>&& arena, Body&& body) noexcept {
    Box<BumpArena<Family>> held = static_cast<Box<BumpArena<Family>>&&>(arena);
    ArenaFrame<Family, Fresh> frame = ArenaFrameDoor::open<Family, Fresh>(held);
    std::invoke_result_t<Body&&, ArenaFrame<Family, Fresh>&> sealed = static_cast<Body&&>(body)(frame);
    return ArenaFrameDoor::seal(static_cast<Box<BumpArena<Family>>&&>(held), sealed);
}

// Calls body with a new frame on the arena of the parcel and each run of the
// parcel as a FrameRun of that frame, and gives the parcel back: alone for a
// body that gives nothing, and else in a Record after the value of the body.
// The parcel moves into the frame of the door, so no other name of it reaches
// the arena while the frame is open.
template <class... Refused, class Family, class... Ts, class Body, ::foundation::brand::IsBrand Fresh>
    requires(sizeof...(Refused) == 0) && ParcelFrameBody<Body, Family, Fresh, Ts...>
[[nodiscard]] auto with_arena_frame(ArenaParcel<Family, Ts...>&& parcel, Body&& body) noexcept {
    ArenaParcel<Family, Ts...> held = static_cast<ArenaParcel<Family, Ts...>&&>(parcel);
    ArenaFrame<Family, Fresh> frame = ArenaFrameDoor::open<Family, Fresh>(held);
    using Result = std::invoke_result_t<Body&&, ArenaFrame<Family, Fresh>&, FrameRun<Ts, Family, Fresh>&...>;
    if constexpr (std::is_void_v<Result>) {
        ArenaFrameDoor::call_with_runs<Fresh>(held, frame, static_cast<Body&&>(body), std::index_sequence_for<Ts...>{});
        return held;
    } else {
        Result value = ArenaFrameDoor::call_with_runs<Fresh>(held, frame, static_cast<Body&&>(body),
                                                             std::index_sequence_for<Ts...>{});
        return Record<Result, ArenaParcel<Family, Ts...>>{static_cast<Result&&>(value),
                                                          static_cast<ArenaParcel<Family, Ts...>&&>(held)};
    }
}

// A parcel that the caller names as an lvalue stays an object of the caller,
// which could end its arena while the frame is open.
template <class... Refused, class Family, class... Ts, class Body>
void with_arena_frame(ArenaParcel<Family, Ts...>& parcel, Body&& body) =
    delete("with_arena_frame takes a parcel by move and gives it back: pass std::move(parcel)");

}  // namespace foundation::core
