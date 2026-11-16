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
//   linear        an owner of a linear element has no move assignment,
//                 because the assignment drops the elements of the target
//   empty         a door of a moved-from owner gives an empty answer, or it
//                 ends the process through fatal().  No door reads through
//                 the null address of the empty state.  The probe reaches the
//                 moved-from owner through a reference, which the rule
//                 use-after-move does not read
//
// An owner with no element has no child move and no linear hazard.  A Hole
// has no self-move, because the rule must-consume refuses an assignment over
// a live Hole.  The count live_nodes holds the number of live nodes, so a
// lost tail or a second end shows as a wrong count.
//
// THE ROSTER
// Each owner keeps its fields in a state of the namespace detail::owner, and
// the core of foundation/core/Owner.h refuses a state of each other
// namespace.  count_owner_roster_gaps compares that namespace with the
// owners of the probes below.  Each state needs exactly one probe, and a
// probe whose owner has no core, writes a move of its own, or repeats the
// state of an earlier probe is stale.  So a new owner on the core fails this
// unit until it has a probe, and a probe of an owner off the core fails it
// too.  The check file of foundation/core/Owner.h proves with a stand-in
// roster that the count can go red.
//
// What the roster cannot see: it sees only the headers that this unit
// includes, which are the headers of the nine owners.  An owner on the core
// in a header that this unit does not include stays out of the roster until
// this unit includes that header.  An owner that writes its own moves and
// never names the core has no state in detail::owner, so the roster cannot
// see it at all.  The check finds such an owner only when a probe names it.

#include <fixy/os/OwnedMmap.h>
#include <foundation/AlignedBytes.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/SwissTableBuffer.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Grow.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include "abort_probe.h"

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

constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

// The number of live nodes of each kind.  Each hazard ends with zero.
int live_nodes = 0;

// True when a move assignment of Owner compiles.
template <class Owner>
concept MoveAssignable = requires(Owner& target, Owner&& source) { target = static_cast<Owner&&>(source); };

// The owner that a call gives back by reference.  The rule use-after-move
// does not read through it, so the hazard EMPTY reaches a moved-from owner.
// The owners of the hazard live outside the body of the abort probe, because
// the jump of an abort skips the destructors of the body.
template <class Owner>
[[nodiscard]] Owner& through(Owner& owner) noexcept {
    return owner;
}

using ::crucible::test::aborts;

// ── The chain of Boxes ──────────────────────────────────────────────────

// A node of a chain: it holds the Box of the next node, or none.
struct BoxNode {
    Option<Box<BoxNode>> next{};
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
        head->next = ::foundation::core::some(make_chain(first + 1, last));
    }
    return head;
}

// The Box of the next node, which the node of head holds.
Box<BoxNode>& next_of(Box<BoxNode>& head) {
    for (Box<BoxNode>& next : head->next) {
        return next;
    }
    ::foundation::core::fatal("the node of the probe holds no next node");
}

struct BoxProbe {
    using Owner = Box<BoxNode>;

    static void self_move() {
        {
            Box<BoxNode> head = make_chain(1, 3);
            Box<BoxNode>& alias = head;
            head = static_cast<Box<BoxNode>&&>(alias);
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

    static void linear() {}

    // EMPTY: a read and a borrow of a moved-from Box end through fatal().
    static void empty() {
        {
            Box<BoxNode> source = make_chain(1, 1);
            Box<BoxNode> const target{static_cast<Box<BoxNode>&&>(through(source))};
            CRUCIBLE_FATAL_INVARIANT(target->label == 1);
            CRUCIBLE_FATAL_INVARIANT(aborts([&] { static_cast<void>(through(source)->label); }));
            CRUCIBLE_FATAL_INVARIANT(aborts([&] { static_cast<void>(through(source).view()); }));
        }
        CRUCIBLE_FATAL_INVARIANT(live_nodes == 0);
    }
};

// ── The containers of elements ─────────────────────────────────────────

struct HeapArrayTag {};
struct MaskedArrayTag {};
struct GrowListTag {};

// The holder of the child owner of an element.  Each tag names one owner.
template <class Tag>
struct Inner;

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
    HeapArray<Elem<HeapArrayTag>> child{};
};

template <>
struct Inner<MaskedArrayTag> {
    MaskedArray<Elem<MaskedArrayTag>> child{};
};

template <>
struct Inner<GrowListTag> {
    GrowList<Elem<GrowListTag>> child = ::foundation::core::mint_grow_list<Elem<GrowListTag>>(8);
};

// The Inner that an element holds.
template <class Tag>
Inner<Tag>& inner_of(Elem<Tag>& element) {
    for (Box<Inner<Tag>>& inner : element.inner) {
        return *inner;
    }
    ::foundation::core::fatal("the element of the probe holds no child owner");
}

// The hazards of a container of elements.  The probe derives from this
// class, and it gives the types Owner and Tag, make(count), which builds an
// owner of count elements, and first_of(owner), the first element.  The
// count of each make is a power of two, as a MaskedArray needs.  The probe
// is incomplete where it names this base, so only the bodies read it.
template <class Probe>
struct ElementArms {
    // Gives the first element of parent a child owner of count elements.
    static auto& put_child(auto& parent, unsigned count) {
        using Tag = Probe::Tag;
        Elem<Tag>& first = Probe::first_of(parent);
        first.inner = ::foundation::core::some(::foundation::core::mint_box<Inner<Tag>>(kAlloc));
        Inner<Tag>& inner = inner_of(first);
        inner.child = Probe::make(count);
        return inner.child;
    }

    static void self_move() {
        using Owner = Probe::Owner;
        {
            Owner held = Probe::make(2);
            Owner& alias = held;
            held = static_cast<Owner&&>(alias);
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

struct HeapArrayProbe : ElementArms<HeapArrayProbe> {
    using Tag = HeapArrayTag;
    using Owner = HeapArray<Elem<Tag>>;

    static Owner make(unsigned count) { return ::foundation::core::mint_heap_array(kAlloc, count, Elem<Tag>{}); }

    static Elem<Tag>& first_of(Owner& owner) {
        for (Elem<Tag>& element : owner) {
            return element;
        }
        ::foundation::core::fatal("the HeapArray of the probe holds no element");
    }

    static void linear() { CRUCIBLE_FATAL_INVARIANT(!MoveAssignable<HeapArray<LinearElement>>); }

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

    static Elem<Tag>& first_of(Owner& owner) { return owner.at_masked(0); }

    static void linear() { CRUCIBLE_FATAL_INVARIANT(!MoveAssignable<MaskedArray<LinearElement>>); }

    // EMPTY: an element of a moved-from MaskedArray or of a default one ends
    // through fatal(), and its View is empty.
    static void empty() {
        {
            Owner source = make(2);
            Owner const target{static_cast<Owner&&>(through(source))};
            Owner const fresh{};
            CRUCIBLE_FATAL_INVARIANT(target.size() == 2);
            CRUCIBLE_FATAL_INVARIANT(through(source).view().size() == 0);
            CRUCIBLE_FATAL_INVARIANT(aborts([&] { static_cast<void>(through(source).at_masked(1)); }));
            CRUCIBLE_FATAL_INVARIANT(aborts([&] { static_cast<void>(fresh.at_masked(0)); }));
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

    static Elem<Tag>& first_of(Owner& owner) {
        for (Elem<Tag>& element : owner) {
            return element;
        }
        ::foundation::core::fatal("the GrowList of the probe holds no element");
    }

    static void linear() { CRUCIBLE_FATAL_INVARIANT(!MoveAssignable<GrowList<LinearElement>>); }

    // EMPTY: a moved-from GrowList gives no element, and an append takes new
    // storage below the maximum that the list keeps.
    static void empty() {
        {
            Owner source = make(2);
            Owner const target{static_cast<Owner&&>(through(source))};
            CRUCIBLE_FATAL_INVARIANT(target.size() == 2);
            CRUCIBLE_FATAL_INVARIANT(through(source).view().size() == 0);
            CRUCIBLE_FATAL_INVARIANT(through(source).get(0).is_none());
            through(source).append(kAlloc, Elem<Tag>{}).expect("a moved-from list keeps its maximum");
            CRUCIBLE_FATAL_INVARIANT(through(source).size() == 1);
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

    // EMPTY: a consume of a moved-from Hole ends through fatal().  The probe
    // body takes a Ref of the spent Hole by value, so no closure captures
    // the linear Hole.
    static void empty() {
        Owner source = ::foundation::core::mint_hole<16, 8>(kAlloc);
        auto const spent = ::foundation::core::to(source);
        Owner target{static_cast<Owner&&>(source)};
        CRUCIBLE_FATAL_INVARIANT(aborts([spent] { static_cast<Owner&&>(*spent).consume(); }));
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

    // The first character of a text that holds one.
    static char first_of(Owner const& text) {
        for (char const character : text) {
            return character;
        }
        ::foundation::core::fatal("the GrowText of the probe holds no character");
    }

    static void self_move() {
        Owner held = make('a');
        Owner& alias = held;
        held = static_cast<Owner&&>(alias);
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
        CRUCIBLE_FATAL_INVARIANT(through(source).view().size() == 0);
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
        Owner& alias = held;
        held = static_cast<Owner&&>(alias);
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

    static void self_move() {
        Owner held = ::foundation::mint_aligned_bytes(kAlloc, 64, 64);
        Owner& alias = held;
        held = static_cast<Owner&&>(alias);
        CRUCIBLE_FATAL_INVARIANT(held.size() == 64);
        CRUCIBLE_FATAL_INVARIANT(held.alignment() == 64);
    }

    static void child_move() {}

    // MOVED-FROM: a new block in the source is a second allocation, and the
    // ASan run finds a second release if the source kept the first one.
    static void moved_from() {
        Owner target = ::foundation::mint_aligned_bytes(kAlloc, 32, 8);
        Owner source = ::foundation::mint_aligned_bytes(kAlloc, 64, 64);
        target = static_cast<Owner&&>(source);
        source = ::foundation::mint_aligned_bytes(kAlloc, 16, 8);
        CRUCIBLE_FATAL_INVARIANT(target.size() == 64);
        Owner taken{static_cast<Owner&&>(target)};
        target = ::foundation::mint_aligned_bytes(kAlloc, 16, 8);
        CRUCIBLE_FATAL_INVARIANT(taken.size() == 64);
        CRUCIBLE_FATAL_INVARIANT(taken.alignment() == 64);
    }

    static void linear() {}

    // EMPTY: a moved-from AlignedBytes gives an empty View.
    static void empty() {
        Owner source = ::foundation::mint_aligned_bytes(kAlloc, 64, 64);
        Owner const target{static_cast<Owner&&>(through(source))};
        CRUCIBLE_FATAL_INVARIANT(target.size() == 64);
        CRUCIBLE_FATAL_INVARIANT(through(source).size() == 0);
        CRUCIBLE_FATAL_INVARIANT(through(source).view().size() == 0);
    }
};

struct SwissTableBufferProbe {
    using Owner = SwissTableBuffer<int>;

    static void self_move() {
        Owner held = Owner::allocate(16);
        Owner& alias = held;
        held = static_cast<Owner&&>(alias);
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
        Owner& alias = held;
        held = static_cast<Owner&&>(alias);
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
    static constexpr auto gaps = ::foundation::core::count_owner_roster_gaps<typename Probes::Owner...>();

    static void run() { (run_probe<Probes>(), ...); }
};

using AllProbes = ProbeList<BoxProbe, HoleProbe, HeapArrayProbe, MaskedArrayProbe, GrowListProbe, GrowTextProbe,
                            HeapTextProbe, AlignedBytesProbe, SwissTableBufferProbe, OwnedMmapProbe>;

static_assert(AllProbes::gaps == 0,
              "an owner state of the core has no probe in this file, or a probe names an owner off the core, an owner "
              "with a move of its own, or the state of another probe");

}  // namespace

int main() {
    AllProbes::run();
    return 0;
}
