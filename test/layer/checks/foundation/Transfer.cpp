// The compile-time checks of foundation/Transfer.h.

#include <foundation/Transfer.h>

#include <foundation/ChannelBinding.h>
#include <foundation/SoleWriterProof.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Id.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/RunSet.h>
#include <foundation/core/Strided.h>
#include <foundation/reflect/TypeComponents.h>

namespace foundation::transfer {

namespace detail::transfer_self_test {

// Rule 2 and rule 3: a scalar and a pointer to a function hold each property,
// and a pointer to an object holds none.
static_assert(Sendable<int> && Shareable<int> && Sendable<double> && Sendable<void (*)()>);
static_assert(!Sendable<int*> && !Shareable<int*> && !Sendable<int const*>);

// Rule 1: a reference to const asks Shareable of its target.  A reference to a
// target that is not const asks that two threads can write the target.
struct ConstRef {
    int const& value;
};
struct PlainRef {
    int& value;
};
struct AtomicRef {
    ::foundation::core::Atomic<int>& value;
};
static_assert(Sendable<ConstRef> && !Sendable<PlainRef> && Sendable<AtomicRef>);

// Rule 4: an array holds a property when its element holds it.
static_assert(Sendable<int[4]> && !Sendable<int* [2]>);

// Rule 5: a statement wins over the fields.  The field of StatedSend refers to
// a plain int, which alone refuses each property.
struct[[= sendable{}]] StatedSend {
    int& value;
};
struct[[= not_sendable{}]] RefusedSend {
    int value = 0;
};
static_assert(Sendable<StatedSend> && !Shareable<StatedSend> && !Sendable<RefusedSend> && Shareable<RefusedSend>);

// Rule 6: a class that states by_arguments holds a property when its first
// type argument holds it, and its member is not read: the member refers to a
// plain int, which alone refuses each property.  The class holds no value of
// its other type arguments, so the walk asks nothing of them, also of an
// argument that is only declared.
template <class T>
struct[[= by_arguments{}]] Owner {
    int& unread;
};
template <class T, class Authority>
struct[[= by_arguments{}]] OwnerOfAuthority {
    int& unread;
};
struct DeclaredAuthority;
static_assert(Sendable<Owner<int>> && !Sendable<Owner<int*>>);
static_assert(Sendable<OwnerOfAuthority<int, int*>> && Sendable<OwnerOfAuthority<int, DeclaredAuthority>>);
static_assert(!Sendable<OwnerOfAuthority<int*, int>>);
static_assert(Sendable<::foundation::core::Box<int>>);
static_assert(Sendable<::foundation::core::BoxWord<int, DeclaredAuthority>>);

// Rule 1 for a class that states refers: a Ref of a target that two threads
// can write is Sendable, and a Ref of a plain int is not.
consteval bool are_refs_checked() {
    ::foundation::core::Atomic<int> cell{0};
    int plain = 0;
    return holds(^^decltype(::foundation::core::to(cell)), Property::Send)
        && !holds(^^decltype(::foundation::core::to(plain)), Property::Send);
}
static_assert(are_refs_checked());

// Rule 1 for a class that states refers_unique: a ViewMut and a StridedViewMut
// are Sendable when their element is Sendable.  They are Shareable only when
// two threads can write their element, because a const one can still write
// the run.  Two threads never write one of them at the same time.
struct ViewBrand {};
using IntRun = ::foundation::core::ViewMut<int, ViewBrand>;
using IntRows = ::foundation::core::StridedViewMut<int, 2, ViewBrand>;
using PointerRun = ::foundation::core::ViewMut<int*, ViewBrand>;
static_assert(Sendable<IntRun> && !Shareable<IntRun> && !Synchronized<IntRun>);
static_assert(Sendable<IntRows> && !Shareable<IntRows> && !Synchronized<IntRows>);
static_assert(!Sendable<PointerRun> && !Shareable<PointerRun>);
static_assert(!Sendable<IntRun const&> && !Sendable<IntRun&>);
static_assert(first_refusal(^^IntRun, Property::Share) == std::meta::dealias(^^IntRun));
static_assert(first_refusal(^^PointerRun, Property::Send) == ^^int*);

// Rule 1 for each run of a RunSet: the first element of a run states
// refers{}.  A copy of a set refers to the same runs, and a set that is not
// const writes them, so a set of runs of plain ints holds no property.  A set
// of runs of const elements gives only const elements, also through a copy,
// so it holds each property when two threads can read each element.
struct HoldsBorrow {
    int& target;
};
using WritableRuns =
    ::foundation::core::RunSet<unsigned short, ::foundation::core::RunOf<int, 0>, ::foundation::core::RunOf<double, 1>>;
using ReadRuns = ::foundation::core::RunSet<unsigned short, ::foundation::core::RunOf<int const, 0>,
                                            ::foundation::core::RunOf<double const, 1>>;
using AddressRuns = ::foundation::core::RunSet<unsigned short, ::foundation::core::RunOf<HoldsBorrow const, 0>>;
using CellRuns =
    ::foundation::core::RunSet<unsigned short, ::foundation::core::RunOf<::foundation::core::Atomic<int>, 0>>;
static_assert(!Shareable<WritableRuns> && !Sendable<WritableRuns> && !SendableBeside<WritableRuns>);
static_assert(Shareable<ReadRuns> && Sendable<ReadRuns> && !SendableBeside<ReadRuns>);
static_assert(Shareable<CellRuns> && Sendable<CellRuns> && SendableBeside<CellRuns>);
static_assert(!Shareable<AddressRuns> && first_refusal(^^AddressRuns, Property::Share) == ^^HoldsBorrow);
static_assert(!Synchronized<ReadRuns>, "two threads cannot write the counts of one set");

// Rule 7: a lambda with no capture holds each property.  A capture by
// reference of a plain int holds none: a compiler that hides the capture
// fields gives a closure with unreadable state, and a compiler that shows them
// gives a field int&, which asks a shared write of an int.  A capture by copy
// of an int holds Send exactly when the compiler shows its field, so the guard
// of the unreadable state refuses it on a compiler that hides the field.
consteval bool are_lambdas_checked() {
    int local = 0;
    auto by_reference = [&local] { return local; };
    auto by_copy = [local] { return local; };
    auto empty = [] { return 1; };
    bool const shows_fields =
        ::foundation::reflect::member_components_of(::foundation::reflect::TypeNode{^^decltype(by_copy), true}).size()
        == 1;
    return !holds(^^decltype(by_reference), Property::Send) && holds(^^decltype(empty), Property::Send)
        && holds(^^decltype(by_copy), Property::Send) == shows_fields;
}
static_assert(are_lambdas_checked());

// Rule 8: a class holds a property when each part holds it, and the type
// arguments of a specialization count, because an Option keeps its payload in
// bytes.
struct Plain {
    int value = 0;
    double weight = 0.0;
};
static_assert(Sendable<Plain> && Shareable<Plain>);
static_assert(Sendable<::foundation::core::Option<int>> && !Sendable<::foundation::core::Option<int*>>);

// A mutable member makes a class not Shareable, unless two threads can write
// the member (THE SHARED WRITE).
struct MutableCount {
    mutable int count = 0;
};
struct MutableAtomic {
    mutable ::foundation::core::Atomic<int> count{0};
};
static_assert(Sendable<MutableCount> && !Shareable<MutableCount>);
static_assert(Shareable<MutableAtomic>);

// Rule 1 for a class that states refers_deep{} but copies from a const
// object: the copy would write the target, so the class holds no property.
template <class T>
struct[[= refers_deep{}]] CopiesFromConst {
    T& target;
};
static_assert(!Shareable<CopiesFromConst<int>> && !Sendable<CopiesFromConst<int>>);

// A class that states shareable{} is a target that two threads can write.
// EndpointClaim states it over its atomic cell.
struct ClaimRef {
    ::foundation::EndpointClaim& claim;
};
static_assert(Sendable<ClaimRef>);

// The shared write: a class is Synchronized when each part is Synchronized or
// is const and Shareable.  A plain scalar, a reference member and a mutable
// plain member are not.
struct CountWithLimit {
    ::foundation::core::Atomic<int> count{0};
    int const limit = 8;
};
struct CountWithPlainLimit {
    ::foundation::core::Atomic<int> count{0};
    int limit = 8;
};
struct CountWithReference {
    ::foundation::core::Atomic<int> count{0};
    int const& limit;
};
static_assert(Synchronized<::foundation::core::Atomic<int>> && Synchronized<::foundation::EndpointClaim>);
static_assert(Synchronized<::foundation::core::Atomic<int, ::foundation::core::Counter>>);
static_assert(Synchronized<CountWithLimit> && Synchronized<::foundation::core::Atomic<int>[4]>);
static_assert(!Synchronized<int> && !Synchronized<CountWithPlainLimit> && !Synchronized<CountWithReference>);
static_assert(!Synchronized<MutableCount> && Synchronized<MutableAtomic>);
// The refusal names the first part that refuses: a scalar, or the class that
// holds a reference.
static_assert((first_synchronized_refusal(^^int) == ^^int)&&(first_synchronized_refusal(^^CountWithPlainLimit)
                                                             == ^^int));
static_assert(first_synchronized_refusal(^^CountWithReference) == ^^CountWithReference);
static_assert(first_synchronized_refusal(^^CountWithLimit) == std::meta::info{});

// An empty base, such as the Pinned mark of a class that does not move, holds
// no state, so it does not stop the shared write of the class.
struct EmptyMark {};
struct PinnedCount : EmptyMark {
    ::foundation::core::Atomic<int> count{0};
};
static_assert(Synchronized<EmptyMark> && Synchronized<PinnedCount>);

// A class that states synchronized{} orders its private parts in its own
// code, so a private plain member passes the check.  Each part that is not
// private is Synchronized, or is const and Shareable.  The statement states
// the read of two threads too.
class[[= synchronized{}]] OrdersItsCount {
public:
    ::foundation::core::Atomic<int> calls{0};
    int const limit = 8;

private:
    int count_ = 0;
};
static_assert(Synchronized<OrdersItsCount> && Shareable<OrdersItsCount> && Sendable<OrdersItsCount>);
static_assert(first_synchronized_refusal(^^OrdersItsCount) == std::meta::info{});

// A class of one owner thread does not move and is not a target of a shared
// write, but a read through a reference to const is safe when its parts are.
// The same holds for a class that holds it.  A reference to const of it is
// Sendable, and a reference that is not const is not.
struct[[= thread_bound{}]] OwnerThreadCount {
    ::foundation::core::Atomic<int> count{0};
};
struct HoldsOwnerThreadCount {
    OwnerThreadCount owned{};
};
struct ReadsOwnerThreadCount {
    OwnerThreadCount const& read;
};
struct WritesOwnerThreadCount {
    OwnerThreadCount& written;
};
static_assert(!Sendable<OwnerThreadCount> && Shareable<OwnerThreadCount> && !Synchronized<OwnerThreadCount>);

// An Atomic is a cell of its payload: it holds each property, the shared
// write included, exactly when its payload is Sendable.  So a cell of an
// address of a plain object, or of a reference to an object of one owner
// thread, moves no such object to another thread.
static_assert(Synchronized<::foundation::core::Atomic<int>> && Sendable<::foundation::core::Atomic<int>>);
static_assert(!Synchronized<::foundation::core::Atomic<int*>> && !Shareable<::foundation::core::Atomic<int*>>
              && !Sendable<::foundation::core::Atomic<int*>>);
static_assert(!SendableBeside<::foundation::core::Atomic<int*>> && SendableBeside<::foundation::core::Atomic<int>>);
static_assert(!Sendable<HoldsOwnerThreadCount> && Shareable<HoldsOwnerThreadCount>);
static_assert(!Synchronized<HoldsOwnerThreadCount>);
static_assert(Sendable<ReadsOwnerThreadCount> && !Sendable<WritesOwnerThreadCount>);

// The Tally of the base holds a count cell, so two threads can add to it.
static_assert(Synchronized<::foundation::core::Tally> && Sendable<::foundation::core::Tally const&>);

// A proof of the one writer moves to no other thread for a share: two threads
// do not read one proof, so it is not Shareable.  The check file of
// Permission.h and test_core_atomic check the same facts of a Permission and a
// SoleWriter.
struct TokenRole {};
using WriterProof = ::foundation::SoleWriterProof<Plain, TokenRole>;
template <class Held>
struct HoldsOne {
    Held held;
};
static_assert(!Shareable<WriterProof> && !Synchronized<WriterProof>);

// A tag type that is only declared holds each property as a type argument
// that rule 8 reads, as for an Id over a tag that its spelling declares.
struct DeclaredTag;
template <class Tag>
struct Tagged {
    int value = 0;
};
static_assert(Sendable<Tagged<DeclaredTag>>);
using ElaboratedId = ::foundation::core::Id<struct ElaboratedTag, unsigned>;
static_assert(Sendable<ElaboratedId> && Shareable<ElaboratedId>);

// A type argument of an owner is a part, because the owner holds an object of
// it (rule 6).  So a Box of a class that is only declared holds no property,
// and a Box of a complete class holds what the class holds.
struct DeclaredPayload;
using DeclaredBox = ::foundation::core::Box<DeclaredPayload>;
static_assert(!Sendable<DeclaredBox> && !Shareable<DeclaredBox> && !SendableBeside<DeclaredBox>);
static_assert(first_refusal(^^DeclaredBox, Property::Send) == ^^DeclaredPayload);
static_assert(Sendable<::foundation::core::Box<Plain>> && Shareable<::foundation::core::Box<Plain>>);
static_assert(!Sendable<::foundation::core::Box<OwnerThreadCount>>);

// A type that reaches itself through an owner ends the walk.
struct Node {
    ::foundation::core::Option<::foundation::core::Box<Node>> next;
};
static_assert(Sendable<Node>);

// THE VALUE BESIDE ITS SENDER: each target of a reference, of refers and of
// refers_unique is Synchronized, also a target that is const.  An owner, a
// statement of sendable and a statement of by_arguments work as for Send.
using IntReads = ::foundation::core::View<int const>;
static_assert(Sendable<IntReads> && !SendableBeside<IntReads>);
static_assert(Sendable<ConstRef> && !SendableBeside<ConstRef> && !SendableBeside<PlainRef>);
static_assert(SendableBeside<AtomicRef> && SendableBeside<int> && SendableBeside<Node>);
static_assert(Sendable<IntRun> && !SendableBeside<IntRun> && !SendableBeside<IntRows>);
static_assert(SendableBeside<StatedSend> && !SendableBeside<RefusedSend> && !SendableBeside<OwnerThreadCount>);
static_assert(SendableBeside<Owner<int>> && !SendableBeside<Owner<int*>>);
static_assert(SendableBeside<::foundation::core::Box<int>>);
consteval bool are_refs_checked_beside() {
    ::foundation::core::Atomic<int> cell{0};
    int const plain = 0;
    return holds(^^decltype(::foundation::core::to(cell)), Property::SendBeside)
        && holds(^^decltype(::foundation::core::to(plain)), Property::Send)
        && !holds(^^decltype(::foundation::core::to(plain)), Property::SendBeside);
}
static_assert(are_refs_checked_beside());

// THE VALUE THAT OUTLIVES ITS SENDER: each reference and each class that
// refers to an object refuses, also a reference to an Atomic.  An owner holds
// it when the object that it owns holds it, and a statement of sendable wins
// as it does for Send.
static_assert(Sendable<IntReads> && !SendableOwned<IntReads> && !SendableOwned<IntRun>);
static_assert(Sendable<AtomicRef> && !SendableOwned<AtomicRef> && !SendableOwned<ConstRef>);
static_assert(!SendableOwned<int const&> && !SendableOwned<CopiesFromConst<int>> && !SendableOwned<int*>);
static_assert(SendableOwned<int> && SendableOwned<Plain> && SendableOwned<Node> && SendableOwned<StatedSend>);
static_assert(SendableOwned<::foundation::core::Box<int>> && !SendableOwned<::foundation::core::Box<IntReads>>);
static_assert(SendableOwned<::foundation::core::BoxWord<Plain, DeclaredAuthority>>);
static_assert(SendableOwned<::foundation::core::Atomic<int>> && !SendableOwned<RefusedSend>);
static_assert(first_refusal(^^HoldsOne<IntReads>, Property::SendOwned) == std::meta::dealias(^^IntReads));
consteval bool are_refs_owned() {
    ::foundation::core::Atomic<int> cell{0};
    return holds(^^decltype(::foundation::core::to(cell)), Property::Send)
        && !holds(^^decltype(::foundation::core::to(cell)), Property::SendOwned);
}
static_assert(are_refs_owned());

// THE OPAQUE TYPES: a class that is only declared holds what the statement of
// its registry gives.  A definition must hold each property that its
// statement gives, so the definition of StatedWithBorrow, whose reference
// refuses Share, disagrees with its statement.
struct OpaqueHandle;
struct UnstatedHandle;
struct StatedPlain {
    int value = 0;
};
struct StatedWithBorrow {
    int& target;
};
struct StatedWithCellBorrow {
    ::foundation::core::Atomic<int>& cell;
};
namespace transfer_facts {
// No thread writes the handle after its build.
inline constexpr opaque_facts opaque_handle{.type = ^^OpaqueHandle, .is_sendable = true, .is_shareable = true};
// The class holds one int.
inline constexpr opaque_facts stated_plain{.type = ^^StatedPlain, .is_sendable = true, .is_shareable = true};
// A statement that the definition of the class refuses.
inline constexpr opaque_facts stated_with_borrow{.type = ^^StatedWithBorrow, .is_shareable = true};
// A statement of Send that the definition refuses, because its reference to
// an Atomic can outlive the cell (THE VALUE THAT OUTLIVES ITS SENDER).
inline constexpr opaque_facts stated_with_cell_borrow{.type = ^^StatedWithCellBorrow, .is_sendable = true};
inline constexpr opaque_seal seal{.count = 4};
}  // namespace transfer_facts
struct ReadsOpaque {
    OpaqueHandle const& handle;
};
struct WritesOpaque {
    OpaqueHandle& handle;
};
struct ReadsUnstated {
    UnstatedHandle const& handle;
};
static_assert(Sendable<ReadsOpaque> && Shareable<ReadsOpaque> && !SendableBeside<ReadsOpaque>);
static_assert(!Sendable<WritesOpaque>, "the statement does not state that two threads can write the handle");
static_assert(!Sendable<ReadsUnstated> && first_refusal(^^ReadsUnstated, Property::Send) == ^^UnstatedHandle);
// An owner of a class that is only declared holds what the statement gives.
static_assert(Sendable<::foundation::core::Box<OpaqueHandle>> && Shareable<::foundation::core::Box<OpaqueHandle>>);
static_assert(!Sendable<::foundation::core::Box<UnstatedHandle>>);
static_assert(holds_its_statement(^^StatedPlain) && !holds_its_statement(^^StatedWithBorrow));
static_assert(Sendable<StatedWithCellBorrow> && SendableBeside<StatedWithCellBorrow>
              && !holds_its_statement(^^StatedWithCellBorrow));
static_assert(!holds_its_statement(^^Plain), "a class with no statement holds none");

}  // namespace detail::transfer_self_test

}  // namespace foundation::transfer
