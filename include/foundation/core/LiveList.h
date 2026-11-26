#pragma once

// The live append-only list of the Region family: one writer appends, and
// readers on other threads read the published elements at the same time.
//
//   LiveList<T, Writer>  the elements in the order of the appends, in the
//                       segments of a SegmentList (foundation/core/
//                       SegmentList.h).  Writer is the proof of the one
//                       writer, SoleWriterProof<Owner, Role>
//
// Operations
//   append(proof, alloc, value)
//                       puts value after the last element, and publishes it
//   append_with(proof, alloc, make)
//                       builds the element from make(position) in place, and
//                       publishes it.  A pinned element passes
//   append_each(proof, alloc, run)
//                       copies each element of a View, and publishes all of
//                       them with one store
//   prefix()            the published elements, as a SegmentPrefix: one acquire
//                       load.  Each thread can call it, and each read of the
//                       list is a read of a prefix
//
// One writer
//   Each append takes the proof of the one writer
//   (foundation/SoleWriterProof.h).  Only Owner makes the proof, and the proof
//   lives for one call of Owner.  So the one writer of the list is the code of
//   Owner, and no append tests for a second writer.  Each list of one type
//   takes the same proof type, so Owner keeps two lists apart by its structure,
//   as it does for the cell of an owner of foundation/core/Atomic.h.  The
//   comment of Owner states how it keeps one writer.
//
// Publish order
//   The writer writes the element first, and the word of the directory when a
//   segment opens.  Then it stores the count with release order.  A reader
//   loads the count with acquire order, so it sees each element below the count
//   and each word of the directory that names their segments.  The writer
//   writes each of those words and each of those elements one time, before the
//   release of the first count that covers it.  So a plain word and a plain
//   element are enough, and a read has no other atomic operation.
//
//   A reader reads no element and no word of the directory past its count.  It
//   never reads the tail, the end of the tail and the count of the writer.
//   Those are plain words that the writer reads and writes.
//
// Cache lines
//   The directory has four lines, and the writer writes a word of them only
//   when a segment opens.  The tail, its end and the count of the writer have a
//   line of their own.  The published count has a line of its own, so a reader
//   that loads it again and again takes no other line from the writer.
//
//   The newest element is on a line that the writer and a reader of the newest
//   elements share, when the next element starts on the same line.  The line
//   then moves between the two cores.  The list keeps it that way: a padding of
//   each element to its own line would give a read of the log one line for each
//   element, and the published count would move between the two cores anyway.
//
// Borrows
//   No operation moves an element or ends it before the list ends, and no
//   operation writes a published element.  A list does not move.  So a prefix,
//   a Ref of an element and a View of a segment stay correct until the list
//   ends, across each append.  A reader on another thread can hold the directory,
//   so no operation takes the elements out of a list or ends one of them.  The
//   code that owns a list ends it after its writer and its readers end.  A door
//   of threads joins each body before it returns (fixy/os/ThreadTasks.h), so its
//   bodies can borrow a list that the caller owns.
//
// Mark of the storage
//   A list states owns_its_storage{} of foundation/core/Owner.h.  Its element
//   holds no borrow.  Its members that are not const are the appends, which keep
//   each element, and no code can move or assign a list.  So the kind
//   stale-borrow of the rule view-escape reads no call with a list as the end of
//   a borrow, also when it cannot read the body of the function.
//
//   That holds for a call that takes the list by reference.  A list behind a
//   pointer ends with the owner of that pointer, such as a Box or an object that
//   a pointer of an owner names.  So a call that can replace that owner ends each
//   borrow of the list.
//
//   The rule puts a marked class on the route of a result only when the result
//   names a type behind its pointers, or the part that holds those pointers,
//   such as a prefix or a walk.  The directory of a list names its element type.
//   An owner that keeps its blocks behind a pointer to void or to bytes counts as
//   raw blocks, and the rule puts it on each route.
//
// Elements
//   A reader on another thread reads an element, maybe after the frame of the
//   writer ended.  The thread that ends the list ends each element.  So an
//   element is Shareable and SendableOwned (foundation/Transfer.h), and it holds
//   no borrow.  The end of a list drops its elements, so a linear element is
//   refused.
//
//   The code of an element that runs in an append, such as a copy or a make,
//   can reach the list, as the code of an element in an append of a SegmentList
//   can.  It sees the published elements, and an append of it ends the process.

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/SoleWriterProof.h>
#include <foundation/Transfer.h>
#include <foundation/core/AllocTag.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Region.h>
#include <foundation/core/SegmentList.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstddef>
#include <new>
#include <type_traits>

namespace foundation::core {

// The element of a live list (the elements).
template <class T>
concept LiveListElement = OwnedElement<T> && DroppablePayload<T> && ::foundation::transfer::Shareable<T>
                       && ::foundation::transfer::SendableOwned<T>;

// The writer of a live list: the proof of the one writer, which only its owner
// makes.
template <class Writer>
concept LiveListWriter = ::foundation::reflect::IsInstanceOf<Writer, ^^::foundation::SoleWriterProof>;

namespace detail {

// The part of the writer of a live list: the segments, with the tail and the
// count of the writer.  Each door that writes or that reads the words of the
// writer takes a key that only a proof of the one writer opens, so no two
// threads use those words at the same time.  The one other door, prefix, is
// const, and it reads no word of the segments: the reads of the prefix reach
// only the words that the release of a count published before (the publish
// order).  So the class states synchronized{} (foundation/Transfer.h), whose
// check reads each door.
//
// The check reads a SoleWriterProof as no proof of a door, because the proof
// carries no authority{}, so the key carries it.  The check reads no template
// whose constraint needs a make that gives an element, so each template is a
// door of LiveList, and it reaches the segments only through this class.
template <class T, class Writer>
class[[= ::foundation::transfer::synchronized{}]] LiveSegments final {
    SegmentList<T> list_{};

public:
    // A slot of the segments that the writer took, and the end of its segment.
    using Claim = typename SegmentList<T>::Claim;

    // The key of the doors that write.  Only a proof of the one writer opens
    // one, and it lives for one call.
    class[[= ::foundation::linear::authority{}]][[= ::foundation::transfer::not_shareable{}]] WriteKey final {
        [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    public:
        explicit constexpr WriteKey(Writer const& /*proof*/) noexcept {}
        WriteKey(WriteKey const&) = delete("a key of the writer lives for one call of its list");
        WriteKey& operator=(WriteKey const&) = delete("a key of the writer lives for one call of its list");
        ~WriteKey() = default;
    };

    LiveSegments() noexcept = default;
    LiveSegments(LiveSegments const&) = delete("the segments of a live list do not move, because a reader on another "
                                               "thread reads them");
    LiveSegments& operator=(LiveSegments const&) = delete("the segments of a live list do not move, because a reader "
                                                          "on another thread reads them");
    ~LiveSegments() = default;

    // Moves value into the slot after the last element, or copies each element
    // of run after it.  Each gives the count of the elements after it, which the
    // list then publishes.
    [[nodiscard]] CRUCIBLE_INLINE std::size_t append(WriteKey const& /*key*/, T&& value) noexcept
        requires MovedSegmentElement<T>
    {
        list_.append_moved_(static_cast<T&&>(value));
        return list_.size();
    }
    [[nodiscard]] CRUCIBLE_INLINE std::size_t append_each(WriteKey const& /*key*/, View<T const> run) noexcept
        requires std::is_nothrow_copy_constructible_v<T>
    {
        list_.append_copies_(run);
        return list_.size();
    }

    // Takes the slot after the last element, and leaves the list with no tail
    // until settle (the code of an element in an append of SegmentList.h).
    // settle counts the element that the caller built in the slot, and gives the
    // count of the elements after it.  count gives the position of that element
    // before.
    [[nodiscard]] CRUCIBLE_INLINE Claim claim(WriteKey const& /*key*/) noexcept { return list_.claim_slot_(); }
    [[nodiscard]] CRUCIBLE_INLINE std::size_t count(WriteKey const& /*key*/) const noexcept { return list_.size(); }
    [[nodiscard]] CRUCIBLE_INLINE std::size_t settle(WriteKey const& /*key*/, Claim const& claim) noexcept {
        SegmentList<T>::settle_(list_.state_, claim, 1);
        return list_.size();
    }

    // The first count elements.  The caller read count from the published count
    // with acquire order.
    [[nodiscard]] CRUCIBLE_INLINE SegmentPrefix<T const> prefix(std::size_t count) const noexcept {
        return list_.prefix_(count);
    }
};

}  // namespace detail

// Two threads use one list at the same time: each private part is a class that
// two threads can write.  The list owns its elements, so it moves to another
// thread behind an owner, such as a Box, as they do (by_arguments<0>{}).  The
// list holds no value of the type of its writer proof.
template <LiveListElement T, LiveListWriter Writer>
class [[nodiscard]][[= ::foundation::transfer::synchronized{}]][[= ::foundation::transfer::by_arguments<0>{}]]
                   [[= owns_its_storage{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] LiveList final
    : ::foundation::Pinned<LiveList<T, Writer>> {
    using Segments = detail::LiveSegments<T, Writer>;
    using WriteKey = typename Segments::WriteKey;

    // The directory starts on a line, so the words of the writer after it have
    // a line of their own (the cache lines).
    alignas(64) Segments segments_{};
    CacheLine<Atomic<std::size_t, Writer>> published_{std::size_t{0}};

public:
    using element_type = T;
    using writer_type = Writer;

    // The empty list.  It holds no storage.
    LiveList() noexcept = default;

    LiveList(LiveList const&) = delete("a LiveList does not copy and does not move: a reader on another thread reads "
                                       "its directory.  Move a Box of the list instead");
    LiveList& operator=(LiveList const&) = delete("a LiveList takes no assignment: it would end the elements that a "
                                                  "reader on another thread reads");
    ~LiveList() = default;

    // Puts value after the last element, and publishes it.  One compare, one
    // release store, and one allocation when the last segment is full.  No
    // element moves.
    template <class Marker>
        requires AllocationCapability<Marker> && MovedSegmentElement<T>
    void append(Writer const& proof, Marker /*allocation*/, T value) & noexcept {
        published_->store_release(proof, segments_.append(WriteKey{proof}, static_cast<T&&>(value)));
    }
    template <class Marker>
        requires AllocationCapability<Marker> && (!MovedSegmentElement<T>)
    void append(Writer const& proof, Marker allocation, T const& value) & =
        delete("append moves the value into its slot, and this element does not move: build it in place with "
               "append_with");

    // Builds the element from make(position) in the slot after the last
    // element, and publishes it after the call of make.  The result of make is a
    // T, so the element takes it with no copy and no move.  Complexity: one call
    // of make, one compare, one release store, and one allocation when the last
    // segment is full.
    template <class Marker, class Make>
        requires MakesSegmentElement<T, Marker, Make>
    [[= ::foundation::linear::borrow_match{}]] void append_with(Writer const& proof, Marker /*allocation*/,
                                                                Make&& make) & noexcept {
        typename Segments::Claim const claim = segments_.claim(WriteKey{proof});
        ::new(static_cast<void*>(claim.slot)) T(make(segments_.count(WriteKey{proof})));
        published_->store_release(proof, segments_.settle(WriteKey{proof}, claim));
    }
    template <class Marker, class Make>
        requires AllocationCapability<Marker> && std::is_invocable_v<Make&, std::size_t>
                  && (!::foundation::linear::HoldsNoLinearValue<Make>)
    void append_with(Writer const& proof, Marker allocation, Make&& make) & = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

    // Copies each element of run after the last element, in order, and publishes
    // all of them at one time.  A reader sees none of them or each of them.
    // Complexity: one copy for each element, one release store, and one
    // allocation for each segment that opens.
    template <class Marker>
        requires AllocationCapability<Marker> && std::is_nothrow_copy_constructible_v<T>
    void append_each(Writer const& proof, Marker /*allocation*/, View<T const> run) & noexcept {
        published_->store_release(proof, segments_.append_each(WriteKey{proof}, run));
    }

    // The published elements.  Each thread can call it at each time: one acquire
    // load, which sees each element below the count (the publish order).  A
    // later prefix of the same thread has at least the elements of an earlier
    // one.
    [[nodiscard]] SegmentPrefix<T const> prefix() const& noexcept {
        return segments_.prefix(published_->load_acquire());
    }
    void prefix() && = delete("a prefix of a temporary LiveList dangles at the end of the full expression");
    void prefix() const&& = delete("a prefix of a temporary LiveList dangles at the end of the full expression");
};

}  // namespace foundation::core
