// The probe of the owners of the base under the hazards of a move.
//
// Each owner of heap storage gives its storage to the target of a move and
// leaves the source empty.  The probe runs each owner through these hazards
// under the address sanitizer of the default preset:
//
//   self-move     a move of an owner into itself keeps the value
//   child move    the source lives in an element that the target owns, as
//                 in head = move(head->next).  The target must take the
//                 source before it ends its old value, because the end of
//                 the old value ends the source
//   moved-from    the source is empty after a move.  A new value in the
//                 source ends nothing of the target, and the end of each
//                 owner releases its storage one time.  The probe reads no
//                 moved-from owner, because the rule use-after-move refuses
//                 such a read
//   linear        an owner of a linear element is linear, so the rule
//                 must-consume refuses its drop, and it has no move
//                 assignment, because the assignment drops the elements of
//                 the target
//   empty         a door of a moved-from owner gives an empty answer, or it
//                 ends the process through fatal().  No door reads through
//                 the null address of the empty state.  The probe reaches the
//                 moved-from owner through a reference, which the rule
//                 use-after-move does not read
//
// An owner with no element has no child move and no linear hazard.  A Box is
// an owner of one element, its payload.  A Hole
// has no self-move, because the rule must-consume refuses an assignment over
// a live Hole.  The count live_nodes holds the number of live nodes, so a
// lost tail or a second end shows as a wrong count.
//
// THE ROSTER
// Each owner keeps its fields in a state with the marker of the core of
// foundation/core/Owner.h, in the home of its layer: detail::owner of
// foundation::core, or fixy::detail::owner.  count_owner_roster_gaps
// compares the two homes with the owners of the probes below.  An owner
// with a state outside the two homes is stale.  Each state needs exactly
// one probe, and a probe whose owner has no core, writes a move of its own,
// or repeats the state of an earlier probe is stale.  So a new owner on the
// core fails this unit until it has a probe, and a probe of an owner off the
// core fails it too.  The check file of foundation/core/Owner.h proves with
// a stand-in roster that the count can go red.
//
// What the roster cannot see: it sees only the headers that this unit
// includes, which are the headers of the probed owners.  An owner on the core
// in a header that this unit does not include stays out of the roster until
// this unit includes that header.  The core takes a state with the marker
// from each namespace, so a state outside the two homes stays out of the
// roster until a probe names its owner.  An owner that writes its own moves
// and never names the core has no state in a home, so the roster cannot see
// it at all.  The check finds such an owner only when a probe names it.

#include <fixy/os/OwnedMmap.h>
#include <fixy/os/OwnedThread.h>
#include <foundation/AlignedBytes.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/SwissTableBuffer.h>
#include <foundation/core/Array.h>
#include <foundation/core/BitSet.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Grow.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Ref.h>
#include <foundation/core/SlotMap.h>
#include <foundation/core/SmallList.h>
#include <foundation/effects/Effect.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include <cstdint>
#include <utility>

namespace {

using ::foundation::AlignedBytes;
using ::foundation::SwissTableBuffer;
using ::foundation::core::Box;
using ::foundation::core::GrowList;
using ::foundation::core::GrowText;
using ::foundation::core::HeapArray;
using ::foundation::core::HeapText;
using ::foundation::core::Hole;
using ::foundation::core::MaskedArray;
using ::foundation::core::Option;
using ::foundation::core::Ref;
using ::foundation::core::View;

constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

// The number of live nodes of each kind.  Each hazard ends with zero.
int live_nodes = 0;

// True when a move assignment of Owner compiles.
template <class Owner>
concept MoveAssignable = requires(Owner& target, Owner&& source) { target = static_cast<Owner&&>(source); };

// The owner that a call gives back by reference.  The rule use-after-move
// does not read through it, so the hazard EMPTY reaches a moved-from owner.
// Each door of the hazard that ends the process runs in a child through
// expect_dies of the Check family.
template <class Owner>
[[nodiscard]] Owner& through(Owner& owner) noexcept {
    return owner;
}

// Moves the source into the target through two names.  The kind self-move of
// the rule must-consume refuses a self-move that one function names, so each
// probe reaches the self-move of an owner through this function, as an alias
// of two references reaches it in a program.
template <class Owner>
[[gnu::noinline]] void move_into(Owner& target, Owner& source) noexcept {
    target = static_cast<Owner&&>(source);
}

using ::fixy::check::expect_dies;

// ── The chain of Boxes ──────────────────────────────────────────────────

struct BoxNode;

// The brand of the link of a node.  to() of a branded object takes the brand
// of the object, so the probe names the Ref type of a link.
struct NextLinkBrand final {};

// The link of a node to the next node.  It lives in the Option of the node,
// and the probe reaches its Box through a Ref of the link.
struct NextLink {
    using brand_type = NextLinkBrand;
    Box<BoxNode> box;
};

// A node of a chain: it holds the link to the next node, or none.
struct BoxNode {
    Option<NextLink> next{};
    int label = 0;

    explicit BoxNode(int value) noexcept;
    BoxNode(BoxNode const&) = delete("a node of the probe has one identity");
    BoxNode& operator=(BoxNode const&) = delete("a node of the probe has one identity");
    ~BoxNode();
};

BoxNode::BoxNode(int value) noexcept : label{value} { ++live_nodes; }

BoxNode::~BoxNode() { --live_nodes; }

// The chain of the nodes from first to last, each node with the label of
// its place.  Complexity: O(last - first).
Box<BoxNode> make_chain(int first, int last) {
    Box<BoxNode> head = ::foundation::core::mint_box<BoxNode>(kAlloc, first);
    if (first < last) {
        head->next = ::foundation::core::some(NextLink{make_chain(first + 1, last)});
    }
    return head;
}

// The Box of the next node, which the node of head holds.  The frame door of
// the Option lends the link, and a Ref of the link leaves the frame through a
// capture.  The link lives in the node of head on the heap, so the Ref names
// it until the node ends.
Box<BoxNode>& next_of(Box<BoxNode>& head) {
    Option<Ref<NextLink, NextLinkBrand>> found{::foundation::core::none};
    head->next.with_mut(
        [&found](auto link) noexcept { found = ::foundation::core::some(::foundation::core::to(*link)); },
        [] noexcept {});
    return std::move(found).expect("the node of the probe holds a next node")->box;
}

struct BoxProbe {
    using Owner = Box<BoxNode>;

    static void self_move() {
        {
            Box<BoxNode> head = make_chain(1, 3);
            move_into(head, head);
            CRUCIBLE_FATAL_INVARIANT(live_nodes == 3);
            CRUCIBLE_FATAL_INVARIANT(head->label == 1);
        }
        CRUCIBLE_FATAL_INVARIANT(live_nodes == 0);
    }

    static void child_move() {
        {
            Box<BoxNode> head = make_chain(1, 3);
            head = static_cast<Box<BoxNode>&&>(next_of(head));
            CRUCIBLE_FATAL_INVARIANT(live_nodes == 2);
            CRUCIBLE_FATAL_INVARIANT(head->label == 2);
            CRUCIBLE_FATAL_INVARIANT(next_of(head)->label == 3);
        }
        CRUCIBLE_FATAL_INVARIANT(live_nodes == 0);
    }

    // MOVED-FROM: a new value in the source ends no node of the target.
    static void moved_from() {
        {
            Box<BoxNode> target = make_chain(1, 2);
            Box<BoxNode> source = make_chain(1, 3);
            target = static_cast<Box<BoxNode>&&>(source);
            CRUCIBLE_FATAL_INVARIANT(live_nodes == 3);
            source = make_chain(1, 1);
            CRUCIBLE_FATAL_INVARIANT(live_nodes == 4);
            Box<BoxNode> taken{static_cast<Box<BoxNode>&&>(target)};
            target = make_chain(1, 1);
            CRUCIBLE_FATAL_INVARIANT(taken->label == 1);
            CRUCIBLE_FATAL_INVARIANT(live_nodes == 5);
        }
        CRUCIBLE_FATAL_INVARIANT(live_nodes == 0);
    }

    static void linear();

    // EMPTY: a read and a borrow of a moved-from Box end through fatal().
    static void empty() {
        {
            ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
            Box<BoxNode> source = make_chain(1, 1);
            Box<BoxNode> const target{static_cast<Box<BoxNode>&&>(through(source))};
            CRUCIBLE_FATAL_INVARIANT(target->label == 1);
            expect_dies(
                test, [&source] noexcept { static_cast<void>(through(source)->label); },
                ::foundation::core::TextView{
                    "foundation: fatal: a borrow of a Box that holds no object: the Box was moved from or consumed"});
            expect_dies(
                test, [&source] noexcept { static_cast<void>(through(source).view()); },
                ::foundation::core::TextView{
                    "foundation: fatal: a borrow of a Box that holds no object: the Box was moved from or consumed"});
        }
        CRUCIBLE_FATAL_INVARIANT(live_nodes == 0);
    }
};

// ── The containers of elements ─────────────────────────────────────────

struct HeapArrayTag {};
struct MaskedArrayTag {};
struct GrowListTag {};
struct SlotMapTag {};

// The domain of the keys of the probe of a SlotMap.
struct ProbeHandleTag final {};

// The holder of the child owner of an element.  Each tag names one owner.
template <class Tag>
struct Inner;

// The brand of the Inner of a tag.  to() of a branded object takes the brand
// of the object, so the probe names the Ref type of an Inner.
template <class Tag>
struct InnerBrand final {};

// One live node: it counts itself in live_nodes from its start to its end.
// A move of a node is a copy of its count, so the count stays exact.
struct LiveNode {
    LiveNode() noexcept { ++live_nodes; }
    LiveNode(LiveNode const& /*other*/) noexcept { ++live_nodes; }
    LiveNode& operator=(LiveNode const&) = delete("a live node of the probe counts one node");
    ~LiveNode() { --live_nodes; }
};

// An element of a probed container.  It can hold an Inner, which holds a
// child owner of elements of the same type, so a move of the child into its
// parent takes a source that an element of the parent owns.  A copy holds no
// Inner, so the mint of a HeapArray can copy its fill.
template <class Tag>
struct Elem {
    Option<Box<Inner<Tag>>> inner{};
    LiveNode live{};

    Elem() noexcept = default;
    Elem(Elem const& /*other*/) noexcept {}
    Elem(Elem&&) noexcept = default;
    Elem& operator=(Elem const&) = delete("an element of the probe has one identity");
};

template <>
struct Inner<HeapArrayTag> {
    using brand_type = InnerBrand<HeapArrayTag>;
    HeapArray<Elem<HeapArrayTag>> child{};
};

template <>
struct Inner<MaskedArrayTag> {
    using brand_type = InnerBrand<MaskedArrayTag>;
    MaskedArray<Elem<MaskedArrayTag>> child{};
};

template <>
struct Inner<GrowListTag> {
    using brand_type = InnerBrand<GrowListTag>;
    GrowList<Elem<GrowListTag>> child = ::foundation::core::mint_grow_list<Elem<GrowListTag>>(8);
};

template <>
struct Inner<SlotMapTag> {
    using brand_type = InnerBrand<SlotMapTag>;
    ::foundation::core::SlotMap<Elem<SlotMapTag>, ProbeHandleTag> child{};
};

// The Inner that an element holds.  The frame door of the Option lends the
// Box, and a Ref of the Inner leaves the frame through a capture.  The Inner
// lives in its Box on the heap, so the Ref names it until the Box ends.
template <class Tag>
Inner<Tag>& inner_of(Elem<Tag>& element) {
    Option<Ref<Inner<Tag>, InnerBrand<Tag>>> found{::foundation::core::none};
    element.inner.with_mut(
        [&found](auto inner) noexcept { found = ::foundation::core::some(::foundation::core::to(**inner)); },
        [] noexcept {});
    return *std::move(found).expect("the element of the probe holds a child owner");
}

// Gives element a new Inner in a Box, and gives that Inner.  The Inner lives
// in its Box, so no move of the owner of the element moves it.
template <class Tag>
Inner<Tag>& give_an_inner(Elem<Tag>& element) {
    element.inner = ::foundation::core::some(::foundation::core::mint_box<Inner<Tag>>(kAlloc));
    return inner_of(element);
}

// The hazards of a container of elements.  The probe derives from this
// class, and it gives the types Owner and Tag, make(count), which builds an
// owner of count elements, and give_first_an_inner(owner), which gives the
// first element a new Inner and gives that Inner.  The count of each make is
// a power of two, as a MaskedArray needs.  The probe is incomplete where it
// names this base, so only the bodies read it.
template <class Probe>
struct ElementArms {
    // Gives the first element of parent a child owner of count elements.
    static auto& put_child(auto& parent, unsigned count) {
        auto& inner = Probe::give_first_an_inner(parent);
        inner.child = Probe::make(count);
        return inner.child;
    }

    static void self_move() {
        using Owner = Probe::Owner;
        {
            Owner held = Probe::make(2);
            move_into(held, held);
            CRUCIBLE_FATAL_INVARIANT(held.size() == 2);
            CRUCIBLE_FATAL_INVARIANT(live_nodes == 2);
        }
        CRUCIBLE_FATAL_INVARIANT(live_nodes == 0);
    }

    static void child_move() {
        using Owner = Probe::Owner;
        {
            Owner parent = Probe::make(1);
            Owner& child = put_child(parent, 2);
            CRUCIBLE_FATAL_INVARIANT(live_nodes == 3);
            parent = static_cast<Owner&&>(child);
            CRUCIBLE_FATAL_INVARIANT(parent.size() == 2);
            CRUCIBLE_FATAL_INVARIANT(live_nodes == 2);
        }
        CRUCIBLE_FATAL_INVARIANT(live_nodes == 0);
    }

    // MOVED-FROM: a new value in the source ends no element of the target.
    static void moved_from() {
        using Owner = Probe::Owner;
        {
            Owner target = Probe::make(1);
            Owner source = Probe::make(2);
            target = static_cast<Owner&&>(source);
            CRUCIBLE_FATAL_INVARIANT(target.size() == 2);
            CRUCIBLE_FATAL_INVARIANT(live_nodes == 2);
            source = Probe::make(1);
            CRUCIBLE_FATAL_INVARIANT(live_nodes == 3);
            Owner taken{static_cast<Owner&&>(target)};
            target = Probe::make(1);
            CRUCIBLE_FATAL_INVARIANT(taken.size() == 2);
            CRUCIBLE_FATAL_INVARIANT(live_nodes == 4);
        }
        CRUCIBLE_FATAL_INVARIANT(live_nodes == 0);
    }
};

// An element that a holder must not drop.  It moves and does not copy.  The
// linear marker needs a user-provided move assignment, and the rule
// linear-forge needs the refusal of a lifetime start over bytes.
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] LinearElement {
    LinearElement() noexcept = default;
    LinearElement(LinearElement&&) noexcept = default;
    LinearElement& operator=(LinearElement&& /*other*/) noexcept { return *this; }
    LinearElement(LinearElement const&) = delete("a linear element of the probe does not copy");
    LinearElement& operator=(LinearElement const&) = delete("a linear element of the probe does not copy");
};

// An owner of a linear element is linear, so the rule must-consume refuses
// its drop, and it has no move assignment.  The trait reads the owner where
// the element is complete.
template <class Owner>
[[nodiscard]] constexpr bool keeps_its_linear_element() noexcept {
    return ::foundation::linear::is_linear_v<Owner> && !MoveAssignable<Owner>;
}

// LINEAR: a Box of a linear payload is linear and has no move assignment.
void BoxProbe::linear() {
    CRUCIBLE_FATAL_INVARIANT(keeps_its_linear_element<Box<LinearElement>>());
    CRUCIBLE_FATAL_INVARIANT(!::foundation::linear::is_linear_v<Box<BoxNode>>);
}

struct HeapArrayProbe : ElementArms<HeapArrayProbe> {
    using Tag = HeapArrayTag;
    using Owner = HeapArray<Elem<Tag>>;

    static Owner make(unsigned count) { return ::foundation::core::mint_heap_array(kAlloc, count, Elem<Tag>{}); }

    // The element door with_element lends the first element, and a Ref of the
    // new Inner leaves the door through a capture.  The Inner lives in its Box
    // and not in the storage of the array, so no move of the array ends the
    // Ref.
    using InnerRef = Ref<Inner<Tag>, InnerBrand<Tag>>;

    static Inner<Tag>& give_first_an_inner(Owner& owner) {
        Option<InnerRef> given{::foundation::core::none};
        bool const is_given = owner.with_element(
            0,
            [&given](Elem<Tag>& first) noexcept {
                given = Option<InnerRef>::some(::foundation::core::to(give_an_inner(first)));
                return true;
            },
            [] noexcept { return false; });
        if (!is_given) ::foundation::core::fatal("the HeapArray of the probe holds no element");
        return *static_cast<Option<InnerRef>&&>(given).expect("the frame of the first element gave its Inner");
    }

    static void linear() { CRUCIBLE_FATAL_INVARIANT(keeps_its_linear_element<HeapArray<LinearElement>>()); }

    // EMPTY: a moved-from HeapArray gives no element and an empty View.
    static void empty() {
        {
            Owner source = make(2);
            Owner const target{static_cast<Owner&&>(through(source))};
            CRUCIBLE_FATAL_INVARIANT(target.size() == 2);
            CRUCIBLE_FATAL_INVARIANT(through(source).size() == 0);
            CRUCIBLE_FATAL_INVARIANT(through(source).view().size() == 0);
            CRUCIBLE_FATAL_INVARIANT(through(source).get(0).is_none());
        }
        CRUCIBLE_FATAL_INVARIANT(live_nodes == 0);
    }
};

struct MaskedArrayProbe : ElementArms<MaskedArrayProbe> {
    using Tag = MaskedArrayTag;
    using Owner = MaskedArray<Elem<Tag>>;

    static Owner make(unsigned count) { return ::foundation::core::mint_masked_array<Elem<Tag>>(kAlloc, count); }

    static Inner<Tag>& give_first_an_inner(Owner& owner) { return give_an_inner(owner.at_masked(0)); }

    static void linear() { CRUCIBLE_FATAL_INVARIANT(keeps_its_linear_element<MaskedArray<LinearElement>>()); }

    // EMPTY: an element of a moved-from MaskedArray or of a default one ends
    // through fatal(), and its View is empty.
    static void empty() {
        {
            ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
            Owner source = make(2);
            Owner const target{static_cast<Owner&&>(through(source))};
            Owner const fresh{};
            CRUCIBLE_FATAL_INVARIANT(target.size() == 2);
            CRUCIBLE_FATAL_INVARIANT(through(source).view().size() == 0);
            expect_dies(
                test, [&source] noexcept { static_cast<void>(through(source).at_masked(1)); },
                ::foundation::core::TextView{
                    "foundation: fatal: an element of a MaskedArray that holds no storage: the array is default or "
                    "moved from"});
            expect_dies(
                test, [&fresh] noexcept { static_cast<void>(fresh.at_masked(0)); },
                ::foundation::core::TextView{
                    "foundation: fatal: an element of a MaskedArray that holds no storage: the array is default or "
                    "moved from"});
        }
        CRUCIBLE_FATAL_INVARIANT(live_nodes == 0);
    }
};

struct GrowListProbe : ElementArms<GrowListProbe> {
    using Tag = GrowListTag;
    using Owner = GrowList<Elem<Tag>>;

    static Owner make(unsigned count) {
        Owner list = ::foundation::core::mint_grow_list<Elem<Tag>>(8);
        for (unsigned appended = 0; appended < count; ++appended) {
            list.append(kAlloc, Elem<Tag>{}).expect("the list of the probe has room below its maximum");
        }
        return list;
    }

    // The frame door with_element lends the first element, and a Ref of the
    // new Inner leaves the frame through a capture.  The Inner lives in its
    // Box and not in the storage of the list, so no growth ends the Ref.
    using InnerRef = Ref<Inner<Tag>, InnerBrand<Tag>>;

    static Inner<Tag>& give_first_an_inner(Owner& owner) {
        Option<InnerRef> given{::foundation::core::none};
        bool const is_given = owner.with_element(
            0,
            [&given](Elem<Tag>& first) noexcept {
                given = Option<InnerRef>::some(::foundation::core::to(give_an_inner(first)));
                return true;
            },
            [] noexcept { return false; });
        if (!is_given) ::foundation::core::fatal("the GrowList of the probe holds no element");
        return *static_cast<Option<InnerRef>&&>(given).expect("the frame of the first element gave its Inner");
    }

    static void linear() { CRUCIBLE_FATAL_INVARIANT(keeps_its_linear_element<GrowList<LinearElement>>()); }

    // EMPTY: a moved-from GrowList gives no element, and an append takes new
    // storage below the maximum that the list keeps.
    static void empty() {
        {
            Owner source = make(2);
            Owner const target{static_cast<Owner&&>(through(source))};
            CRUCIBLE_FATAL_INVARIANT(target.size() == 2);
            CRUCIBLE_FATAL_INVARIANT(
                through(source).with_view([](View<Elem<Tag> const> view) noexcept { return view.size(); }) == 0);
            CRUCIBLE_FATAL_INVARIANT(through(source).with_element(
                0, [](Elem<Tag>&) noexcept { return false; }, [] noexcept { return true; }));
            through(source).append(kAlloc, Elem<Tag>{}).expect("a moved-from list keeps its maximum");
            CRUCIBLE_FATAL_INVARIANT(through(source).size() == 1);
        }
        CRUCIBLE_FATAL_INVARIANT(live_nodes == 0);
    }
};

struct SlotMapProbe : ElementArms<SlotMapProbe> {
    using Tag = SlotMapTag;
    using Owner = ::foundation::core::SlotMap<Elem<Tag>, ProbeHandleTag>;
    using Key = ::foundation::core::SlotKey<ProbeHandleTag>;

    static Owner make(unsigned count) {
        Owner map = ::foundation::core::mint_slot_map<Elem<Tag>, ProbeHandleTag>(
            kAlloc, ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>{8});
        for (unsigned inserted = 0; inserted < count; ++inserted) {
            static_cast<void>(map.insert(Elem<Tag>{}).expect("the map of the probe has a free slot"));
        }
        return map;
    }

    // The first value in the order of the slots.  The values live in their
    // slots, which never move, so the Inner stays where it is.
    static Inner<Tag>& give_first_an_inner(Owner& owner) {
        for (Elem<Tag>& element : owner) {
            return give_an_inner(element);
        }
        ::foundation::core::fatal("the SlotMap of the probe holds no value");
    }

    using LinearOwner = ::foundation::core::SlotMap<LinearElement, ProbeHandleTag>;

    static void linear() { CRUCIBLE_FATAL_INVARIANT(keeps_its_linear_element<LinearOwner>()); }

    // EMPTY: a moved-from SlotMap holds no slot: a read of a key finds no
    // value, its walk gives nothing, and an insert gives Full.
    static void empty() {
        {
            Owner source = make(2);
            Key const first{std::uint64_t{1} << 32};
            Owner const target{static_cast<Owner&&>(through(source))};
            CRUCIBLE_FATAL_INVARIANT(target.size() == 2 && target.get(first).is_some());
            CRUCIBLE_FATAL_INVARIANT(through(source).size() == 0 && through(source).capacity().raw() == 0);
            CRUCIBLE_FATAL_INVARIANT(through(source).get(first).is_none());
            CRUCIBLE_FATAL_INVARIANT(through(source).begin() == through(source).end());
            CRUCIBLE_FATAL_INVARIANT(through(source).insert(Elem<Tag>{}).is_err());
        }
        CRUCIBLE_FATAL_INVARIANT(live_nodes == 0);
    }
};

// ── The owners with no element ──────────────────────────────────────────

// A Hole is linear, and the rule must-consume refuses an assignment over a
// live Hole, because the assignment drops the storage that it holds.  So the
// probe has no self-move of a Hole, and it moves a Hole only into a
// moved-from Hole.  The other owners run the self-move of the same core.
struct HoleProbe {
    using Owner = Hole<16, 8>;

    static void self_move() {}

    static void child_move() {}

    // MOVED-FROM: new storage in the source is a second allocation, and the
    // ASan run finds a second release if the source kept the first one.
    static void moved_from() {
        Owner source = ::foundation::core::mint_hole<16, 8>(kAlloc);
        Owner target{static_cast<Owner&&>(source)};
        source = ::foundation::core::mint_hole<16, 8>(kAlloc);
        static_cast<Owner&&>(source).consume();
        static_cast<Owner&&>(target).consume();
    }

    static void linear() {}

    // EMPTY: a consume of a moved-from Hole ends through fatal().  The body of
    // the death test takes a Ref of the spent Hole by value, so no closure
    // captures the linear Hole.
    static void empty() {
        ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
        Owner source = ::foundation::core::mint_hole<16, 8>(kAlloc);
        auto const spent = ::foundation::core::to(source);
        Owner target{static_cast<Owner&&>(source)};
        expect_dies(
            test, [spent] noexcept { static_cast<Owner&&>(*spent).consume(); },
            ::foundation::core::TextView{
                "foundation: fatal: a spend of a Hole that holds no storage: the Hole was moved from or spent"});
        static_cast<Owner&&>(target).consume();
    }
};

struct GrowTextProbe {
    using Owner = GrowText;

    static Owner make(char character) {
        Owner text = ::foundation::core::mint_grow_text(4);
        text.append(kAlloc, character).expect("the text of the probe has room below its maximum");
        return text;
    }

    // The first character of a text that holds one, through the frame door
    // with_view.
    static char first_of(Owner const& text) {
        return text.with_view([](::foundation::core::TextView characters) noexcept {
            for (char const character : characters) {
                return character;
            }
            ::foundation::core::fatal("the GrowText of the probe holds no character");
        });
    }

    static void self_move() {
        Owner held = make('a');
        move_into(held, held);
        CRUCIBLE_FATAL_INVARIANT(held.size() == 1);
    }

    static void child_move() {}

    // MOVED-FROM: a new value in the source leaves the characters of the
    // target in place, and the ASan run finds a read of freed storage if the
    // source kept them.
    static void moved_from() {
        Owner target = make('a');
        Owner source = make('b');
        target = static_cast<Owner&&>(source);
        source = make('c');
        CRUCIBLE_FATAL_INVARIANT(first_of(target) == 'b');
        Owner taken{static_cast<Owner&&>(target)};
        target = make('d');
        CRUCIBLE_FATAL_INVARIANT(first_of(taken) == 'b');
    }

    static void linear() {}

    // EMPTY: a moved-from GrowText gives an empty view, and an append takes
    // new storage below the maximum that the text keeps.
    static void empty() {
        Owner source = make('a');
        Owner const target{static_cast<Owner&&>(through(source))};
        CRUCIBLE_FATAL_INVARIANT(target.size() == 1);
        CRUCIBLE_FATAL_INVARIANT(through(source).with_view([](::foundation::core::TextView characters) noexcept {
            return characters.size();
        }) == 0);
        through(source).append(kAlloc, 'e').expect("a moved-from text keeps its maximum");
        CRUCIBLE_FATAL_INVARIANT(first_of(through(source)) == 'e');
    }
};

struct HeapTextProbe {
    using Owner = HeapText;

    static Owner make(char character) {
        GrowText text = ::foundation::core::mint_grow_text(4);
        text.append(kAlloc, character).expect("the text of the probe has room below its maximum");
        return ::foundation::core::freeze(static_cast<GrowText&&>(text));
    }

    // The first character of a text that holds one.
    static char first_of(Owner const& text) {
        for (char const character : text) {
            return character;
        }
        ::foundation::core::fatal("the HeapText of the probe holds no character");
    }

    static void self_move() {
        Owner held = make('a');
        move_into(held, held);
        CRUCIBLE_FATAL_INVARIANT(held.size() == 1);
    }

    static void child_move() {}

    // MOVED-FROM: a new value in the source leaves the characters of the
    // target in place, and the ASan run finds a read of freed storage if the
    // source kept them.
    static void moved_from() {
        Owner target = make('a');
        Owner source = make('b');
        target = static_cast<Owner&&>(source);
        source = make('c');
        CRUCIBLE_FATAL_INVARIANT(first_of(target) == 'b');
        Owner taken{static_cast<Owner&&>(target)};
        target = make('d');
        CRUCIBLE_FATAL_INVARIANT(first_of(taken) == 'b');
    }

    static void linear() {}

    // EMPTY: a moved-from HeapText gives an empty view and no character.
    static void empty() {
        Owner source = make('a');
        Owner const target{static_cast<Owner&&>(through(source))};
        CRUCIBLE_FATAL_INVARIANT(target.size() == 1);
        CRUCIBLE_FATAL_INVARIANT(through(source).size() == 0 && through(source).view().size() == 0);
    }
};

struct AlignedBytesProbe {
    using Owner = AlignedBytes;

    // A zeroed block of bytes bytes at the alignment, which the probe gives
    // as a layout.
    [[nodiscard]] static Owner zeroed(::foundation::core::ByteCount bytes,
                                      ::foundation::alloc::Alignment alignment) noexcept {
        return ::foundation::mint_aligned_bytes_zeroed(
            kAlloc, ::foundation::alloc::mint_layout(bytes, alignment).expect("the probe gives a layout"));
    }

    static void self_move() {
        Owner held = zeroed(::foundation::core::ByteCount{64}, ::foundation::alloc::Alignment{64});
        move_into(held, held);
        CRUCIBLE_FATAL_INVARIANT(held.size() == 64);
        CRUCIBLE_FATAL_INVARIANT(held.alignment() == 64);
    }

    static void child_move() {}

    // MOVED-FROM: a new block in the source is a second allocation, and the
    // ASan run finds a second release if the source kept the first one.
    static void moved_from() {
        Owner target = zeroed(::foundation::core::ByteCount{32}, ::foundation::alloc::Alignment{8});
        Owner source = zeroed(::foundation::core::ByteCount{64}, ::foundation::alloc::Alignment{64});
        target = static_cast<Owner&&>(source);
        source = zeroed(::foundation::core::ByteCount{16}, ::foundation::alloc::Alignment{8});
        CRUCIBLE_FATAL_INVARIANT(target.size() == 64);
        Owner taken{static_cast<Owner&&>(target)};
        target = zeroed(::foundation::core::ByteCount{16}, ::foundation::alloc::Alignment{8});
        CRUCIBLE_FATAL_INVARIANT(taken.size() == 64);
        CRUCIBLE_FATAL_INVARIANT(taken.alignment() == 64);
    }

    static void linear() {}

    // EMPTY: a moved-from AlignedBytes gives an empty View.
    static void empty() {
        Owner source = zeroed(::foundation::core::ByteCount{64}, ::foundation::alloc::Alignment{64});
        Owner const target{static_cast<Owner&&>(through(source))};
        CRUCIBLE_FATAL_INVARIANT(target.size() == 64);
        CRUCIBLE_FATAL_INVARIANT(through(source).size() == 0);
        CRUCIBLE_FATAL_INVARIANT(through(source).view().size() == 0);
    }
};

// A SmallList holds bytewise elements only, so no element owns a child list
// and no element is linear.  The probe runs the self-move, the moved-from
// source and the empty list, inline and on the heap.
struct SmallListProbe {
    using Owner = ::foundation::core::SmallList<std::uint64_t, 2>;
    using Maximum = ::foundation::core::Quantity<::foundation::core::unit::Element, std::size_t>;

    // A list of count elements from first: inline for a count of two or less,
    // and on the heap past it.
    static Owner make(std::uint64_t first, std::uint64_t count) {
        Owner list = ::foundation::core::mint_small_list<std::uint64_t, 2>(Maximum{16});
        for (std::uint64_t value = first; value != first + count; ++value) {
            list.append(kAlloc, value).expect("the list of the probe has room below its maximum");
        }
        return list;
    }

    // The sum of the elements, through the frame door with_view.
    static std::uint64_t sum_of(Owner const& list) {
        return list.with_view([](View<std::uint64_t const> elements) noexcept {
            std::uint64_t sum = 0;
            for (std::uint64_t const element : elements) {
                sum += element;
            }
            return sum;
        });
    }

    static void self_move() {
        Owner inline_held = make(1, 2);
        move_into(inline_held, inline_held);
        CRUCIBLE_FATAL_INVARIANT(inline_held.size() == 2 && sum_of(inline_held) == 3);
        Owner heap_held = make(1, 5);
        move_into(heap_held, heap_held);
        CRUCIBLE_FATAL_INVARIANT(heap_held.size() == 5 && sum_of(heap_held) == 15);
    }

    static void child_move() {}

    // MOVED-FROM: a new value in the source leaves the elements of the target
    // in place, and the ASan run finds a read of freed storage or a second
    // release if the source kept the heap storage.
    static void moved_from() {
        Owner target = make(1, 1);
        Owner source = make(10, 5);
        target = static_cast<Owner&&>(source);
        source = make(100, 4);
        CRUCIBLE_FATAL_INVARIANT(sum_of(target) == 60);
        Owner taken{static_cast<Owner&&>(target)};
        target = make(1, 2);
        CRUCIBLE_FATAL_INVARIANT(sum_of(taken) == 60 && sum_of(source) == 406);
    }

    static void linear() {}

    // EMPTY: a moved-from SmallList holds no element, and an append takes the
    // inline slots again below the maximum that the list keeps.
    static void empty() {
        Owner source = make(1, 5);
        Owner const target{static_cast<Owner&&>(through(source))};
        CRUCIBLE_FATAL_INVARIANT(target.size() == 5);
        CRUCIBLE_FATAL_INVARIANT(through(source).is_empty() && sum_of(through(source)) == 0);
        through(source).append(kAlloc, 7).expect("a moved-from list keeps its maximum");
        CRUCIBLE_FATAL_INVARIANT(sum_of(through(source)) == 7);
    }
};

// The domain of the ids of the probe of a HeapBitSet.
struct ProbeSlotTag final {};

struct HeapBitSetProbe {
    using Owner = ::foundation::core::HeapBitSet<ProbeSlotTag>;
    using SlotId = ::foundation::core::BitId<ProbeSlotTag>;

    // A set of count ids that holds its last id.
    static Owner make(std::uint32_t count) {
        Owner set = ::foundation::core::mint_heap_bit_set<ProbeSlotTag>(
            kAlloc, ::foundation::core::Quantity<::foundation::core::unit::Element, std::uint32_t>{count});
        set.set(SlotId{count - 1});
        return set;
    }

    static void self_move() {
        Owner held = make(70);
        move_into(held, held);
        CRUCIBLE_FATAL_INVARIANT(held.extent().raw() == 70 && held.test(SlotId{69}));
    }

    static void child_move() {}

    // MOVED-FROM: new words in the source are a second allocation, and the
    // ASan run finds a second release if the source kept the first one.
    static void moved_from() {
        Owner target = make(8);
        Owner source = make(130);
        target = static_cast<Owner&&>(source);
        source = make(16);
        CRUCIBLE_FATAL_INVARIANT(target.extent().raw() == 130 && target.test(SlotId{129}));
        Owner taken{static_cast<Owner&&>(target)};
        target = make(16);
        CRUCIBLE_FATAL_INVARIANT(taken.size() == 1 && taken.test(SlotId{129}));
    }

    static void linear() {}

    // EMPTY: a moved-from HeapBitSet holds no id, its walk gives nothing, and
    // a verb with an id ends through fatal().
    static void empty() {
        ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
        Owner source = make(64);
        Owner const target{static_cast<Owner&&>(through(source))};
        CRUCIBLE_FATAL_INVARIANT(target.extent().raw() == 64);
        CRUCIBLE_FATAL_INVARIANT(through(source).extent().raw() == 0 && through(source).is_empty());
        CRUCIBLE_FATAL_INVARIANT(through(source).begin() == through(source).end());
        expect_dies(
            test, [&source] noexcept { static_cast<void>(through(source).test(SlotId{0})); },
            ::foundation::core::TextView{"foundation: fatal: a HeapBitSet got an id at or past the count of its mint"});
    }
};

struct SwissTableBufferProbe {
    using Owner = SwissTableBuffer<int>;

    static void self_move() {
        Owner held = Owner::allocate(16);
        move_into(held, held);
        CRUCIBLE_FATAL_INVARIANT(held.capacity() == 16);
    }

    static void child_move() {}

    // MOVED-FROM: a new value in the source is a second allocation, and the
    // ASan run finds a second release if the source kept the first one.
    static void moved_from() {
        Owner target = Owner::allocate(16);
        Owner source = Owner::allocate(32);
        target = static_cast<Owner&&>(source);
        source = Owner::allocate(16);
        CRUCIBLE_FATAL_INVARIANT(target.capacity() == 32);
        Owner taken{static_cast<Owner&&>(target)};
        target = Owner::allocate(16);
        CRUCIBLE_FATAL_INVARIANT(taken.capacity() == 32);
    }

    static void linear() {}

    // EMPTY: a moved-from SwissTableBuffer has no slot and no storage.
    static void empty() {
        Owner source = Owner::allocate(16);
        Owner const target{static_cast<Owner&&>(through(source))};
        CRUCIBLE_FATAL_INVARIANT(target.capacity() == 16);
        CRUCIBLE_FATAL_INVARIANT(through(source).capacity() == 0 && through(source).empty());
        CRUCIBLE_FATAL_INVARIANT(!static_cast<bool>(through(source)));
    }
};

// ── The mapping ─────────────────────────────────────────────────────────

// The tag and the brand of the mappings of the probe.
struct MappingProbeTag {
    using permission_row = ::foundation::effects::Row<>;
};
struct MappingProbeBrand {};

// The probe holds empty mappings only.  A mapping with pages comes only from
// mint_region, which gives a std::expected, and the rule quarantine counts
// each use of it in this directory.  test/fixy/test_owned_mmap.cpp moves and
// assigns mappings that hold pages, and it releases one with a leak atom.
struct OwnedMmapProbe {
    using Owner = ::fixy::OwnedMmap<MappingProbeTag, ::fixy::mmap::prot::WriteCopy, ::fixy::mmap::share::Anonymous,
                                    MappingProbeBrand>;

    static void self_move() {
        Owner held{};
        move_into(held, held);
        CRUCIBLE_FATAL_INVARIANT(!held.is_mapped() && held.size() == 0);
    }

    // A child move needs an OwnedMmap that lives in the pages of a mapping.
    // A View of a mapping starts the lifetime of an implicit-lifetime
    // element only, and an owner is no such element, so no door of the base
    // builds that child.
    static void child_move() {}

    static void moved_from() {
        Owner target{};
        Owner source{};
        target = static_cast<Owner&&>(source);
        source = Owner{};
        Owner taken{static_cast<Owner&&>(target)};
        CRUCIBLE_FATAL_INVARIANT(!taken.is_mapped() && taken.size() == 0);
    }

    static void linear() {}

    // EMPTY: a moved-from OwnedMmap holds no address, and its View is empty.
    static void empty() {
        Owner source{};
        Owner const target{static_cast<Owner&&>(through(source))};
        CRUCIBLE_FATAL_INVARIANT(!target.is_mapped());
        CRUCIBLE_FATAL_INVARIANT(!through(source).is_mapped() && through(source).size() == 0);
        CRUCIBLE_FATAL_INVARIANT(
            through(source).template view<std::byte>().expect("an empty mapping gives an empty View").size() == 0);
    }
};

// ── The owned thread ────────────────────────────────────────────────────

// The context that starts the threads of the probe: it owns Bg, which starts
// threads, and Alloc for the frame of the thread.
using ThreadStarterCtx = ::foundation::effects::ExecCtx<
    ::foundation::effects::Bg,
    ::foundation::effects::Row<::foundation::effects::Effect::Bg, ::foundation::effects::Effect::Alloc>>;

// An owned thread whose body does nothing.
::fixy::thread::OwnedThread start_idle_thread() {
    return ::fixy::thread::mint_owned_thread(ThreadStarterCtx{::foundation::effects::testing::bg()},
                                             ::fixy::thread::ThreadOptions{}, [] noexcept {})
        .expect("the thread door starts a thread with the default options");
}

// An owned thread has no element, so it has no child move and no linear
// hazard.  Its move assignment is deleted, because an assignment over a live
// owner would join its thread at a line that shows no join, so it has no
// self-move.  test/fixy/test_os_owned_thread.cpp runs its joins.
struct OwnedThreadProbe {
    using Owner = ::fixy::thread::OwnedThread;

    static void self_move() { static_assert(!MoveAssignable<Owner>); }

    static void child_move() {}

    // MOVED-FROM: the target takes the thread and joins it.
    static void moved_from() {
        Owner source = start_idle_thread();
        Owner taken{static_cast<Owner&&>(source)};
        CRUCIBLE_FATAL_INVARIANT(taken.holds_thread());
        static_cast<Owner&&>(taken).join();
    }

    static void linear() {}

    // EMPTY: a moved-from owner holds no thread, and its join ends the
    // process.
    static void empty() {
        ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
        Owner source = start_idle_thread();
        Owner target{static_cast<Owner&&>(through(source))};
        CRUCIBLE_FATAL_INVARIANT(target.holds_thread());
        CRUCIBLE_FATAL_INVARIANT(!through(source).holds_thread());
        // The target joins first, so the child of the death test starts from a
        // process with no other thread.
        static_cast<Owner&&>(target).join();
        expect_dies(
            test, [&source] noexcept { static_cast<Owner&&>(through(source)).join(); },
            ::foundation::core::TextView{
                "foundation: fatal: a join of an OwnedThread that holds no thread: a move or a join emptied it"});
    }
};

// Runs each hazard of one probe.  Each hazard ends with no live node.
template <class Probe>
void run_probe() {
    Probe::self_move();
    Probe::moved_from();
    Probe::child_move();
    Probe::linear();
    Probe::empty();
    CRUCIBLE_FATAL_INVARIANT(live_nodes == 0);
}

// ── The roster ──────────────────────────────────────────────────────────

// The probes, and the gaps of their owners against the roster of the core.
// The check file of foundation/core/Owner.h proves with a stand-in roster
// that the count can go above zero.
template <class... Probes>
struct ProbeList {
    using Homes = ::foundation::core::OwnerHomes<::foundation::core::owner_state_home, ::fixy::owner_state_home>;
    static constexpr auto gaps = ::foundation::core::count_owner_roster_gaps<Homes, typename Probes::Owner...>();

    static void run() { (run_probe<Probes>(), ...); }
};

using AllProbes = ProbeList<BoxProbe, HoleProbe, HeapArrayProbe, MaskedArrayProbe, GrowListProbe, GrowTextProbe,
                            HeapTextProbe, SmallListProbe, AlignedBytesProbe, HeapBitSetProbe, SlotMapProbe,
                            SwissTableBufferProbe, OwnedMmapProbe, OwnedThreadProbe>;

static_assert(AllProbes::gaps == 0,
              "an owner state of the core has no probe in this file, or a probe names an owner off the core, an owner "
              "with a move of its own, or the state of another probe");

}  // namespace

int main() {
    AllProbes::run();
    return 0;
}
