#pragma once

// The resource handle of the OS doors: one descriptor of the kernel, with
// its kind and its rights in its type.
//
//   Handle<Kind, Rights>     owns one descriptor, and closes it at its end
//   HandleRef<Kind, Rights>  the borrow of the descriptor of a Handle
//   HandleInTransit<Kind, Rights>
//                            a Handle on its way to another thread: depart()
//                            of a Handle gives it, and its arrive() gives the
//                            Handle on the other thread
//   RightSet                 the rights of a handle: a set of named bits
//   kind::*                  the kinds of a handle: a closed list of tags
//   mint_duplicate<Fewer>(ctx, borrow)
//                            a second Handle of the open file of a borrow, with
//                            the rights Fewer
//
// Rights
//   RightSet is an enum class of named bits in 64 bits.  A class type that
//   is a template argument must have public members, so a class with a
//   private mask cannot be one, and an enum class is the strong type that
//   can.  No number converts to it.  The bits combine with |, and
//   HoldsRights<Held, Needed> is the subset test.  A door asks for the bits
//   of its call in its constraint, so a call through a handle that lacks a
//   right does not compile.  The low 32 bits are the rights of the calls,
//   and the high 32 bits are the families of the closed table of ioctls, one
//   bit for each family.
//
// Kinds
//   A kind is a tag of namespace kind, and the table of the kinds
//   (detail::kind_table) has one row for each of them, so a class of a caller
//   is no kind.  A socket kind takes the tag of its socket (fixy/os/Socket.h),
//   because the doors of a socket read its triple.
//
//   The row of a kind states the type of the file of its handle and the proof
//   of it (opened_type_of), and the constructor of a Handle takes an opened
//   number of that type.  So a Handle of the kind File is a regular file, and a
//   door of a file does no check of the type again.  A kind with no row is no
//   kind, and a Handle of it does not compile.
//
// Borrow
//   HandleRef is the one borrow of a descriptor.  It holds the number of the
//   descriptor and the generation of the number by value (the stamp of a
//   descriptor of fixy/os/Fd.h), and no address of its Handle.  Each Handle
//   advances the generation at its open, at its close and at its release to
//   an ABI, so a borrow that outlives its Handle, or whose Handle a move
//   assignment closed, is not current.  A door then gives ebadf and makes no
//   call on the number, which the kernel can have given to a later open.  A
//   move of a Handle on its thread keeps the generation, so a borrow taken
//   before the move stays current while the descriptor is open.  The borrow of
//   an empty Handle, such as one that a move emptied, holds -1.
//
//   The borrow states refers{} of foundation/Transfer.h over its kind, so the
//   rule view-escape refuses a return or a store of the borrow of a local
//   Handle, and the frame doors of the base read it as a borrow (HoldsNoBorrow
//   of foundation/core/Choice.h).  It also states thread_bound{} and
//   not_shareable{}: a door on a second thread can act on the number between
//   the check of the generation and the call, while the thread of the Handle
//   closes it, so the borrow stays on its thread.  A table that stores a
//   socket holds a ForeignSocketRef of fixy/os/Socket.h, which holds the same
//   stamp and is no borrow of a frame, and which stays on its thread too.  A
//   second thread that uses the open file holds a duplicate, or the Handle
//   itself through a transit.
//   restrict<Fewer>() gives a borrow with fewer rights, and no form gives
//   more rights.  A borrow of a temporary Handle does not compile.
//
// Transit
//   A Handle states thread_bound{} too, so no door of the base moves it to
//   another thread, and no object of static storage holds it.  Else the other
//   thread could close it while a door of the first thread is between the check
//   of a borrow and its call.  A move to another thread takes two steps.
//   depart() on the thread of the Handle advances the generation of the number
//   and gives a HandleInTransit, which holds the number.  arrive() on the other
//   thread gives the Handle.  Each borrow of the first thread is stale from
//   depart() on, so its door gives ebadf and makes no call.  No door of the
//   first thread is in a call at that time, because a borrow stays on its
//   thread.  A class that holds a Handle is thread-bound by its members, and it
//   needs a transit form of its own to move to another thread.
//
//   A transit is linear and owes a consume at its end, as a live fixy::Linear
//   does, so it ends in arrive().  The drop door and end_unrun_body of
//   foundation/Linear.h refuse it, so a body that holds one and whose thread did
//   not start runs through call_once on the calling thread.  The end of a
//   transit that did not arrive closes the number, so no descriptor leaks, and
//   the rule must-consume reports such an end.  A transit holds no borrow, so
//   it is SendableOwned, and run_joined, run_beside and each other door that
//   reads the transfer walk take it.
//
// Duplicate
//   mint_duplicate gives a second number to the open file of a borrow, and a
//   Handle that owns the number (F_DUPFD_CLOEXEC).  Each of the two handles
//   closes its own number, and each number has its own generation, so a close
//   of one handle makes no borrow of the other one stale, and no door of one
//   handle can act on the number of the other.  The kernel counts the numbers
//   of the open file, and the file closes when the last of them closes.  So a
//   connection of a socket ends for its peer when the last handle of the socket
//   closes.  The rights of the duplicate are rights that the borrow holds.
//
//   A duplicate is the form of a second thread that uses the open file while
//   the first thread keeps its Handle.  A HandleRef and a ForeignSocketRef stay
//   on their thread, because a door of a second thread can act on the number
//   while the thread of the Handle closes it.  A second thread that uses the
//   open file holds a duplicate of its own, so no such time exists.
//
//   The mint can wait, because the growth of the table of the numbers of a
//   process with threads waits for each reader of the old table.
//
// Shared state
//   The two handles of a duplicate share the open file description of the
//   kernel, which holds the position of the file and the status flags (open(2)).
//   No door of the base changes the status flags of an open file.  read_full and
//   write_full of fixy/os/Fs.h read and move the position, so a duplicate of a
//   kind with a position holds no read, write or seek right (DuplicatesWith).
//   It can map the file, sync it, or look up paths beneath a directory.  An open
//   of the path gives a handle with a position of its own.
//
//   A perf event and a ring of io_uring give no duplicate.  A count of a perf
//   event is the difference of two reads, and a call of the right ioctl_perf of
//   the other handle can stop or reset the event between the two.  The queues of
//   a ring have one owner.
//
//   A socket shares its options, its queues and its shutdown state, and a
//   duplicate of it holds each right of its borrow.  The kernel orders each call
//   of the two handles, and a read takes bytes that the other handle does not
//   get, so no call reads a state that another one left half-changed.  Each door
//   of fixy/os/Socket.h makes one call for one message.  Three doors of the base
//   make more than one call for one run: read_full and write_full of
//   fixy/os/Fs.h, and zerocopy_transfer of fixy/os/Io.h.  A read of the other
//   handle between two calls takes bytes from the middle of the run, and the run
//   then holds bytes that do not follow each other in the stream.  A write of the
//   other handle puts its bytes into the middle of the run.  So two threads
//   divide a stream socket by direction: one reads, and the other writes.  No
//   type holds that rule.
//
// Doors
//   The constructor of a Handle from a descriptor is private.  Its friends
//   are the door classes that make the open, socket, perf_event_open, PTP open
//   and duplicate calls, and each door admits only its gated mints.  The
//   constructor takes an OpenedDescriptor of fixy/os/Fd.h, so each number of
//   a Handle has a generation and a file of the type of its kind.  The doors
//   of the base read the descriptor of a borrow, and an owner reads the
//   descriptor of its own Handle, through detail::HandleDoor.  A use of a
//   namespace detail outside the base is a finding of the quarantine plugin.
//
// ABI descriptor
//   A call of a C ABI, such as a call of libbpf that takes a descriptor,
//   reads the raw descriptor of a borrow with abi_descriptor().  A C ABI that
//   takes the ownership of a descriptor gets it with release_to_abi(), which
//   empties the Handle.  A socket that a C library opened comes in through
//   admit_foreign_socket of fixy/os/Socket.h.  Each such call is inside an
//   opt-out region of the class ABI, or of the class PROBE for a test of raw
//   behavior, which states the reason.  No rule of the tree keeps these
//   members and admit_foreign_socket inside such a region, so a review of
//   each new use keeps that limit.  Each other caller uses a door of the base.

#include <fixy/os/Fd.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>
#include <foundation/os/PathText.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <utility>

namespace fixy::fs {
class FileDoor;
}  // namespace fixy::fs

namespace fixy::net {
class SocketDoor;
}  // namespace fixy::net

namespace fixy::time {
class PtpDeviceDoor;
}  // namespace fixy::time

namespace fixy::perf {
class PerfEventDoor;
}  // namespace fixy::perf

namespace fixy::os {

class DuplicateDoor;

// The rights of a handle.  Each named value is one bit, and none is the
// empty set.
enum class RightSet : std::uint64_t {
    none = 0,
    read = std::uint64_t{1} << 0U,
    write = std::uint64_t{1} << 1U,
    seek = std::uint64_t{1} << 2U,
    map = std::uint64_t{1} << 3U,
    map_exec = std::uint64_t{1} << 4U,
    sync = std::uint64_t{1} << 5U,
    lookup = std::uint64_t{1} << 6U,
    create = std::uint64_t{1} << 7U,
    remove = std::uint64_t{1} << 8U,
    transfer = std::uint64_t{1} << 9U,
    ioctl_ptp = std::uint64_t{1} << 32U,
    ioctl_socket = std::uint64_t{1} << 33U,
    ioctl_perf = std::uint64_t{1} << 34U,
};

// The union of two sets of rights.  The union is commutative, so the two
// arguments can swap with no change of meaning.
[[nodiscard]] constexpr RightSet operator|(RightSet left, RightSet right) noexcept {
    return static_cast<RightSet>(std::to_underlying(left) | std::to_underlying(right));
}

// True when the set Held holds each right of the set Needed.
template <RightSet Held, RightSet Needed>
concept HoldsRights = (std::to_underlying(Held) & std::to_underlying(Needed)) == std::to_underlying(Needed);

// The kinds of a handle.
namespace kind {
// A regular file of a file system.
struct File final {};
// A character device of a device node, such as /dev/null.  No door maps it.
struct CharacterDevice final {};
// A directory, the root of the paths beneath it.
struct Directory final {};
// A socket of the kind SocketKind of fixy/os/Socket.h.
template <class SocketKind>
struct Socket final {};
// An event or a counter of perf_event_open.
struct PerfEvent final {};
// A ring of io_uring.
struct IoRing final {};
// A PTP hardware clock of a /dev/ptpN node.
struct PtpClock final {};
// A memory file whose size never decreases (mint_sealed_memory of
// fixy/os/Fs.h).
struct SealedMemory final {};
}  // namespace kind

namespace detail {

// The state of an open file that two numbers of it share (the shared state).
enum class SharedState : std::uint8_t {
    // A state with one owner: the count and the enable of a perf event, and the
    // queues of a ring of io_uring.
    owned,
    // The position of the file, which read_full and write_full of fixy/os/Fs.h
    // read and move.
    position,
    // The options, the queues and the shutdown state of a socket.
    socket,
    // None: each call of the base on a PTP clock reads the device, and it keeps
    // no state in the open file.
    none,
};

// One row of the table of the kinds: a kind, or the template of a family of
// kinds, the type of the opened number of its handles (the opened descriptor
// of fixy/os/Fd.h), and the state that a duplicate shares.  A row that states
// no shared state gives no duplicate.
struct KindRow final {
    std::meta::info kind{};
    ::fixy::fs::OpenedType type{};
    SharedState shared = SharedState::owned;
};

// The table of the kinds.  A node of /dev/ptpN can be another device, and the
// kernel refuses each PTP call on one, so the row of a PTP clock states the
// device and no more.
inline constexpr KindRow kind_table[] = {
    {^^kind::File, ::fixy::fs::OpenedType::regular_file, SharedState::position},
    {^^kind::CharacterDevice, ::fixy::fs::OpenedType::character_device, SharedState::position},
    {^^kind::PtpClock, ::fixy::fs::OpenedType::character_device, SharedState::none},
    {^^kind::Directory, ::fixy::fs::OpenedType::of_call, SharedState::position},
    {^^kind::Socket, ::fixy::fs::OpenedType::of_call, SharedState::socket},
    {^^kind::PerfEvent, ::fixy::fs::OpenedType::of_call, SharedState::owned},
    {^^kind::IoRing, ::fixy::fs::OpenedType::of_call, SharedState::owned},
    {^^kind::SealedMemory, ::fixy::fs::OpenedType::of_call, SharedState::position},
};
inline constexpr std::size_t kind_count = sizeof kind_table / sizeof kind_table[0];

// The index of the row of kind, or kind_count when the table has none.  A
// specialization of a template has the row of its template.  Complexity:
// linear in the rows, at compile time.
[[nodiscard]] consteval std::size_t kind_row_of_(std::meta::info kind) noexcept {
    std::meta::info const bare = std::meta::dealias(kind);
    std::meta::info const wanted = std::meta::has_template_arguments(bare) ? std::meta::template_of(bare) : bare;
    for (std::size_t row = 0; row < kind_count; ++row) {
        if (kind_table[row].kind == wanted) return row;
    }
    return kind_count;
}

}  // namespace detail

// A kind of namespace kind: a class with a row in the table of the kinds, so a
// class of a different header is no kind.
template <class Kind>
concept HandleKind = detail::kind_row_of_(^^Kind) < detail::kind_count;

// The type of the opened number of a handle of the kind Kind.
template <class Kind>
    requires HandleKind<Kind>
[[nodiscard]] consteval ::fixy::fs::OpenedType opened_type_of() noexcept {
    return detail::kind_table[detail::kind_row_of_(^^Kind)].type;
}

namespace detail {

// True when a duplicate that shares the state can hold the rights fewer (the
// shared state).  A state with no arm gives no duplicate.
[[nodiscard]] consteval bool duplicates_with_(SharedState shared, RightSet fewer) noexcept {
    switch (shared) {
        case SharedState::none:
        case SharedState::socket:
            return true;
        case SharedState::position:
            return (std::to_underlying(fewer) & std::to_underlying(RightSet::read | RightSet::write | RightSet::seek))
                == 0;
        case SharedState::owned:
        default:
            return false;
    }
}

}  // namespace detail

// A duplicate of a handle of the kind Kind can hold the rights Fewer (the shared
// state).  A kind whose open file has a position gives no duplicate that reads,
// writes or seeks, so no two handles move one position.
template <class Kind, RightSet Fewer>
concept DuplicatesWith =
    HandleKind<Kind> && detail::duplicates_with_(detail::kind_table[detail::kind_row_of_(^^Kind)].shared, Fewer);

template <class Kind, RightSet Rights>
    requires HandleKind<Kind>
class Handle;

template <class Kind, RightSet Rights>
    requires HandleKind<Kind>
class HandleRef;

template <class Kind, RightSet Rights>
    requires HandleKind<Kind>
class HandleInTransit;

namespace detail {

// The door of the calls of the base to the descriptor of a borrow.  No
// object of it exists.  descriptor_of gives the descriptor of a current
// borrow, or -1 for an empty or a stale borrow, so the door of the call gives
// ebadf and makes no system call.  owned_descriptor_of gives the descriptor
// of a Handle to its owner, which keeps the Handle alive, so it reads no
// generation.  stamp_of gives the stamp of a borrow, for a ForeignSocketRef
// of the same socket.
struct HandleDoor final : ::foundation::NoObject<HandleDoor> {
    template <class Kind, RightSet Rights>
    [[nodiscard]] static int descriptor_of(HandleRef<Kind, Rights> borrow) noexcept;

    template <class Kind, RightSet Rights>
    [[nodiscard]] static int owned_descriptor_of(Handle<Kind, Rights> const& owner) noexcept;

    template <class Kind, RightSet Rights>
    [[nodiscard]] static ::fixy::fs::DescriptorStamp stamp_of(HandleRef<Kind, Rights> borrow) noexcept;
};

}  // namespace detail

template <class Kind, RightSet Rights>
    requires HandleKind<Kind>
class [[nodiscard]][[= ::foundation::transfer::refers{}]][[= ::foundation::transfer::thread_bound{}]]
                   [[= ::foundation::transfer::not_shareable{}]] HandleRef final {
    // The number of the descriptor of the Handle and its generation, or the
    // empty stamp for the borrow of an empty Handle.  The stamp holds the
    // seal of foundation/ByteSeal.h, so no copy of bytes builds a borrow.
    ::fixy::fs::DescriptorStamp stamp_{};

    explicit constexpr HandleRef(::fixy::fs::DescriptorStamp stamp) noexcept : stamp_{stamp} {}

    template <class OtherKind, RightSet OtherRights>
        requires HandleKind<OtherKind>
    friend class Handle;

    template <class OtherKind, RightSet OtherRights>
        requires HandleKind<OtherKind>
    friend class HandleRef;

    friend struct detail::HandleDoor;

public:
    using kind_type = Kind;
    static constexpr RightSet rights = Rights;

    // A borrow with the rights Fewer, which this borrow holds.  It keeps the
    // stamp of this borrow.
    template <RightSet Fewer>
        requires HoldsRights<Rights, Fewer>
    [[nodiscard]] constexpr HandleRef<Kind, Fewer> restrict() const noexcept {
        return HandleRef<Kind, Fewer>{stamp_};
    }

    // True when the Handle still owns the descriptor of the borrow.  False
    // for the borrow of an empty Handle, and for a borrow whose Handle closed
    // or released the descriptor since the borrow.  Complexity: one acquire
    // load.
    [[nodiscard]] bool is_open() const noexcept { return stamp_.is_current(); }

    // The raw descriptor, for a call of a C ABI only (the ABI descriptor).
    // The descriptor of the borrow of an empty Handle, and of a borrow that
    // is not open, is -1, so a C ABI gets no number of a later open.  A
    // constant evaluation reads no generation and gives the number.
    [[nodiscard]] constexpr int abi_descriptor() const noexcept {
        if consteval {
            return stamp_.number();
        }
        return stamp_.current_number();
    }
};

template <class Kind, RightSet Rights>
[[nodiscard]] int detail::HandleDoor::descriptor_of(HandleRef<Kind, Rights> borrow) noexcept {
    return borrow.stamp_.current_number();
}

template <class Kind, RightSet Rights>
[[nodiscard]] ::fixy::fs::DescriptorStamp detail::HandleDoor::stamp_of(HandleRef<Kind, Rights> borrow) noexcept {
    return borrow.stamp_;
}

// Appends the number of the descriptor of borrow to path, in decimal, for a
// path under /proc/self/fd or /dev/fd that names the open file of the
// descriptor.  A borrow that is not open gives ebadf, and a number that does
// not fit gives enametoolong.  Each error leaves the path as it was.
template <class Kind, RightSet Rights, std::size_t Capacity>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
append_descriptor_number(::foundation::os::PathText<Capacity>& path, HandleRef<Kind, Rights> borrow) noexcept {
    int const descriptor = detail::HandleDoor::descriptor_of(borrow);
    if (descriptor < 0) {
        return ::foundation::core::err(::foundation::os::Errno::ebadf);
    }
    return path.append_decimal(static_cast<std::uint64_t>(descriptor));
}

// Exclusive ownership of one descriptor of the kind Kind with the rights
// Rights, closed at the end of the Handle.  The Handle stays on its thread
// (thread_bound{}), and depart() moves it to another thread (the transit).
template <class Kind, RightSet Rights>
    requires HandleKind<Kind>
class [[nodiscard]][[= ::foundation::transfer::thread_bound{}]] Handle final {
    int descriptor_ = -1;

    // The open gave the number to this Handle, so the generation of the
    // number advances (the generation of a descriptor of fixy/os/Fd.h).  The
    // number is below the slots of the generations, and its file has the type
    // of the kind (the opened descriptor of fixy/os/Fd.h).
    explicit Handle(::fixy::fs::OpenedDescriptor<opened_type_of<Kind>()>&& opened) noexcept
        : descriptor_{opened.number()} {
        ::fixy::fs::detail::advance_descriptor_generation_(descriptor_);
    }

    friend class ::fixy::fs::FileDoor;
    friend class ::fixy::net::SocketDoor;
    friend class ::fixy::time::PtpDeviceDoor;
    friend class ::fixy::perf::PerfEventDoor;
    friend class DuplicateDoor;
    friend struct detail::HandleDoor;
    friend class HandleInTransit<Kind, Rights>;

public:
    using kind_type = Kind;
    static constexpr RightSet rights = Rights;

    // The empty handle owns nothing and closes nothing, so it stays public:
    // it claims no descriptor.
    Handle() noexcept = default;

    Handle(Handle const&) = delete("a descriptor has one owner: a copy would close it two times");
    Handle& operator=(Handle const&) = delete("a descriptor has one owner: a copy would close it two times");

    // A move takes the descriptor, and the source holds none.
    Handle(Handle&& other) noexcept : descriptor_{std::exchange(other.descriptor_, -1)} {}

    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            close_();
            descriptor_ = std::exchange(other.descriptor_, -1);
        }
        return *this;
    }

    ~Handle() noexcept { close_(); }

    [[nodiscard]] bool is_open() const noexcept { return descriptor_ >= 0; }

    // The borrow of the descriptor with each right of this Handle.  The
    // borrow holds the stamp of the descriptor now, so it fails closed when
    // this Handle closes or releases the descriptor.  The borrow of an empty
    // Handle holds the empty stamp.  Complexity: one acquire load.
    [[nodiscard]] HandleRef<Kind, Rights> ref() const& noexcept {
        return HandleRef<Kind, Rights>{::fixy::fs::DescriptorStamp::read_now(descriptor_)};
    }
    void ref() const&& = delete("the borrow of a temporary handle dangles at the end of the full expression");

    // Gives the descriptor to the caller, which closes it from then on, and
    // leaves this Handle empty (the ABI descriptor).  The one caller is a C
    // ABI that takes the ownership of a descriptor, such as a link of libbpf,
    // inside an opt-out region of the class ABI.  An empty Handle gives -1.
    // The base gives up the number, so its generation advances, and no
    // ForeignSocketRef of the number is current after the release.
    [[nodiscard]] int release_to_abi() && noexcept {
        ::fixy::fs::detail::advance_descriptor_generation_(descriptor_);
        return std::exchange(descriptor_, -1);
    }

    // Gives the descriptor to a transit for another thread, and leaves this
    // Handle empty (the transit).  The generation of the number advances
    // first, on this thread, so each borrow and each ForeignSocketRef of the
    // number is stale from then on.  A door that reads such a borrow after the
    // departure gives ebadf at run time and makes no call.  The plugin does not
    // refuse that read at compile time yet.  An empty Handle gives an empty
    // transit.  Complexity: one atomic step.
    [[nodiscard]] HandleInTransit<Kind, Rights> depart() && noexcept {
        ::fixy::fs::detail::advance_descriptor_generation_(descriptor_);
        int const descriptor = descriptor_;
        descriptor_ = -1;
        return HandleInTransit<Kind, Rights>{descriptor};
    }

private:
    // The generation advances before the close (close_owned_descriptor_ of
    // fixy/os/Fd.h), so no open of another thread takes the number first.
    void close_() noexcept {
        if (descriptor_ >= 0) {
            ::fixy::fs::detail::close_owned_descriptor_(descriptor_);
            descriptor_ = -1;
        }
    }
};

template <class Kind, RightSet Rights>
[[nodiscard]] int detail::HandleDoor::owned_descriptor_of(Handle<Kind, Rights> const& owner) noexcept {
    return owner.descriptor_;
}

// A Handle on its way to another thread (the transit): the number of its
// descriptor, which depart() of the Handle made stale for each borrow of the
// sending thread.  Its one consumer is arrive().
template <class Kind, RightSet Rights>
    requires HandleKind<Kind>
class [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::linear::owes_consume{}]]
    HandleInTransit final {
    // The number of the descriptor, or -1 for the transit of an empty Handle
    // and for a transit that a move emptied.
    int descriptor_ = -1;
    // No start of a lifetime over bytes builds a transit
    // (foundation/ByteSeal.h).
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    // The one door is depart() of a Handle with the same kind and rights.
    explicit HandleInTransit(int descriptor) noexcept : descriptor_{descriptor} {}

    friend class Handle<Kind, Rights>;

public:
    using kind_type = Kind;
    static constexpr RightSet rights = Rights;

    HandleInTransit(HandleInTransit const&) =
        delete("a transit carries one descriptor: a copy would give it to two Handles");
    HandleInTransit&
    operator=(HandleInTransit const&) = delete("a transit carries one descriptor: a copy would give it to two Handles");

    // A move takes the descriptor, and the source holds none.
    HandleInTransit(HandleInTransit&& other) noexcept : descriptor_{other.descriptor_} { other.descriptor_ = -1; }
    HandleInTransit& operator=(HandleInTransit&&) =
        delete("a transit ends in its arrive: an assignment would end the transit that it replaces");

    // The end of a transit that did not arrive closes the descriptor, so no
    // descriptor leaks.  The rule must-consume reports such an end.
    ~HandleInTransit() noexcept {
        if (descriptor_ >= 0) ::fixy::fs::detail::close_owned_descriptor_(descriptor_);
    }

    // The Handle of the descriptor, on the calling thread.  It keeps the
    // generation that depart() gave: the transit gives no borrow, so no stamp
    // of that generation exists before this call.  The transit of an empty
    // Handle gives an empty Handle.
    [[nodiscard]] Handle<Kind, Rights> arrive() && noexcept {
        Handle<Kind, Rights> handle{};
        handle.descriptor_ = descriptor_;
        descriptor_ = -1;
        return handle;
    }
};

// The gate of a duplicate: the context admits a call that can wait, the borrow
// holds the rights Fewer, and the kind gives a duplicate with them.
template <typename Ctx, class Kind, RightSet Held, RightSet Fewer>
concept CtxFitsDuplicate = ::fixy::fs::CtxAdmitsFs<Ctx> && HoldsRights<Held, Fewer> && DuplicatesWith<Kind, Fewer>;

// Declared here and defined below DuplicateDoor, because DuplicateDoor names it
// as its friend, and a friend must already have been declared.
template <RightSet Fewer, class Kind, RightSet Rights, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsDuplicate<Ctx, Kind, Rights, Fewer>
[[nodiscard]] inline ::foundation::core::Result<Handle<Kind, Fewer>, ::foundation::os::Errno>
mint_duplicate(Ctx const&, HandleRef<Kind, Rights> borrow) noexcept;

// The door to the duplicate of a number.  No object of it exists.  Its member
// is private, and its one friend is mint_duplicate.  A Handle befriends this
// class, so a duplicate comes only from that mint, after its gate.  The two
// numbers have one open file, whose type the Handle of the first one states.
class DuplicateDoor final : ::foundation::NoObject<DuplicateDoor> {
    template <RightSet FriendFewer, class FriendKind, RightSet FriendRights, ::foundation::effects::IsExecCtx FriendCtx>
        requires CtxFitsDuplicate<FriendCtx, FriendKind, FriendRights, FriendFewer>
    friend auto mint_duplicate(FriendCtx const&, HandleRef<FriendKind, FriendRights>) noexcept
        -> ::foundation::core::Result<Handle<FriendKind, FriendFewer>, ::foundation::os::Errno>;

    template <class Kind, RightSet Fewer>
    [[nodiscard]] static ::foundation::core::Result<Handle<Kind, Fewer>, ::foundation::os::Errno>
    own_(int descriptor) noexcept {
        constexpr ::fixy::fs::OpenedType type = opened_type_of<Kind>();
        return ::fixy::fs::detail::OpenedDescriptorDoor::admit_duplicate<type>(
            ::fixy::fs::detail::duplicate_descriptor_(descriptor),
            [](::fixy::fs::OpenedDescriptor<type> opened) noexcept { return Handle<Kind, Fewer>{std::move(opened)}; });
    }
};

// A Handle with the rights Fewer of the open file of borrow (the duplicate).  A
// borrow that is not open gives ebadf, and a process with no free number gives
// emfile.
template <RightSet Fewer, class Kind, RightSet Rights, ::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsDuplicate<Ctx, Kind, Rights, Fewer>
[[nodiscard]] inline ::foundation::core::Result<Handle<Kind, Fewer>, ::foundation::os::Errno>
mint_duplicate(Ctx const&, HandleRef<Kind, Rights> borrow) noexcept {
    int const descriptor = detail::HandleDoor::descriptor_of(borrow);
    if (descriptor < 0) {
        return ::foundation::core::err(::foundation::os::Errno::ebadf);
    }
    return DuplicateDoor::own_<Kind, Fewer>(descriptor);
}

}  // namespace fixy::os
