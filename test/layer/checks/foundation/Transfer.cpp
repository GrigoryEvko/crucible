// The compile-time checks of foundation/Transfer.h.

#include <foundation/Transfer.h>

#include <foundation/ChannelBinding.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
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

// Rule 6: a class that states by_arguments holds a property when each type
// argument holds it, and its member is not read: the member refers to a plain
// int, which alone refuses each property.
template <class T>
struct[[= by_arguments{}]] Owner {
    int& unread;
};
static_assert(Sendable<Owner<int>> && !Sendable<Owner<int*>>);
static_assert(Sendable<::foundation::core::Box<int>>);

// Rule 1 for a class that states refers: a Ref of a target that two threads
// can write is Sendable, and a Ref of a plain int is not.
consteval bool are_refs_checked() {
    ::foundation::core::Atomic<int> cell{0};
    int plain = 0;
    return holds(^^decltype(::foundation::core::to(cell)), Property::Send)
        && !holds(^^decltype(::foundation::core::to(plain)), Property::Send);
}
static_assert(are_refs_checked());

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

// An empty base, such as the Pinned mark of a class that does not move, holds
// no state, so it does not stop the shared write of the class.
struct EmptyMark {};
struct PinnedCount : EmptyMark {
    ::foundation::core::Atomic<int> count{0};
};
static_assert(Synchronized<EmptyMark> && Synchronized<PinnedCount>);

// A tag type that is only declared holds each property as a type argument.
struct DeclaredTag;
template <class Tag>
struct Tagged {
    int value = 0;
};
static_assert(Sendable<Tagged<DeclaredTag>>);

// A type that reaches itself through an owner ends the walk.
struct Node {
    ::foundation::core::Option<::foundation::core::Box<Node>> next;
};
static_assert(Sendable<Node>);

}  // namespace detail::transfer_self_test

}  // namespace foundation::transfer
