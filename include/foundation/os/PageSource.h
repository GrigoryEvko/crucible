#pragma once

// The page source door of the base: a range of address space that a
// Reservation owns, and the calls that commit, release, place and advise its
// pages.  An allocator of the base takes each of its pages from this door.
//
// THE RESERVATION
//   reserve(bytes, alignment) maps bytes of address space with no access and
//   with no reserve of swap (PROT_NONE, MAP_NORESERVE), at the alignment of a
//   page or of a huge page.  No page of the range has a frame, and the range
//   counts toward no limit of committed memory.  The Reservation owns the
//   map: it moves, it does not copy, and its destructor unmaps the whole
//   range.  A count of bytes that is not a whole count of pages rounds up.
//   reserve(bytes, alignment, cap) refuses with enomem each request above
//   the cap and issues no call, so a caller can run the path of a refused
//   reserve in each build.
//
// THE RANGES
//   A ReservedRange is a part of one reservation: an offset from its first
//   byte and a count of bytes, each a whole count of pages.  Only range() and
//   whole() of a Reservation make one.  commit() of a ReservedRange gives a
//   CommittedRange, and only commit() makes one.  A range holds no address:
//   each call of a Reservation compares the origin of the range with its own
//   first byte, and checks that the part of the range lies inside it.  A
//   range of a different reservation, or of an ended reservation whose
//   address a smaller one took, ends the process.  So no range reaches
//   memory outside a live reservation.  grow() can move the first byte, and
//   each range of the old place then ends the process at its next use.
//
// THE CALLS
//   commit(range)          mprotect to read and write.  The pages fault in
//                          at their first touch, and each reads zero
//   decommit(range, kind)  MADV_DONTNEED or MADV_FREE.  The range keeps its
//                          access, and a page of DONTNEED reads zero again
//   bind(range, placement) mbind with MPOL_PREFERRED for one node or
//                          MPOL_INTERLEAVE for a set of nodes
//   advise_huge(range)     MADV_HUGEPAGE
//   collapse(range)        MADV_COLLAPSE of each whole huge page
//   populate(range)        MADV_POPULATE_WRITE
//   node_of_first_page(range)
//                          move_pages with no target node: the census of the
//                          node of the first page
//   grow(bytes)            mremap of the whole reservation to more bytes
//   current_node()         getcpu: the node of the CPU of the calling thread
//
// THE FALLBACKS
//   A request that cannot fail stays a request that cannot fail.  Each call
//   that can be refused has a result that names what the range got, and the
//   caller takes that result in place of an error:
//     decommit    a refused MADV_FREE, as a kernel below 4.5 gives, takes
//                 MADV_DONTNEED, and the result names the kind that ran.  A
//                 refused MADV_DONTNEED of a range of this door is a defect,
//                 and the process ends
//     bind        a refused mbind keeps the policy of the thread, and the
//                 result is the unknown placement
//     advise_huge a refused advice keeps the pages of the base size
//     collapse    a refused collapse keeps the pages of the base size
//     populate    a kernel below 5.14 refuses MADV_POPULATE_WRITE with
//                 einval, and the door then faults in each page with one
//                 atomic or of zero, which changes no byte.  Each other
//                 refusal leaves the pages to fault in at their first touch
//   reserve, commit and grow give the error of the kernel in their Result:
//   their caller can run out of address space or of memory, as with an
//   allocator of the C library.
//
// THE SANITIZERS
//   The door issues each call through raw_syscall of foundation/os/Syscall.h.
//   In a build with ThreadSanitizer or AddressSanitizer, the primitive gives
//   the map and the unmap to the C library, so the sanitizer resets its
//   shadow of the range (rule R18).  So a poison of an owner of the pages
//   does not stay on a later map at the same addresses
//   (foundation/os/Poison.h).  The interceptor of AddressSanitizer resets the
//   shadow of a large range with a new map of its shadow pages, so the end of
//   a reservation of 1 TiB touches no page of shadow.  mremap stays raw: the
//   sanitizer has no interceptor of it.  The old range of a move is unmapped,
//   and the next map of its addresses goes through the interceptor, which
//   resets the shadow.  The new range of a move was free, and the unmap of
//   each range of the process went through the interceptor, so its shadow is
//   clean.
//
// This header is in the layer F0_doors of utils/rule-table.txt.  The door file
// is src/foundation/os/PageSource.cpp, which issues raw_syscall.

#include <foundation/NoObject.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Id.h>
#include <foundation/core/Index.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Region.h>
#include <foundation/core/Side.h>
#include <foundation/os/Errno.h>

#include <cstddef>
#include <cstdint>

namespace foundation::os {

class Reservation;

}  // namespace foundation::os

namespace foundation::core {

// The door of a Reservation to the raw constructors of a View (THE DOORS OF
// THE OWNERS ABOVE THIS HEADER of foundation/core/Region.h).  The invariant: a
// CommittedRange comes only from commit() of its Reservation, which maps its
// whole pages for reads and writes, and the range stays committed until the
// reservation decommits it or ends.  So its bytes are live while a reference
// to the reservation lives.  Reservation is the one friend, and a home owner
// row of utils/rule-table.txt keeps this definition in this header.
class CommittedRangeDoor final : ::foundation::NoObject<CommittedRangeDoor> {
    friend class ::foundation::os::Reservation;

    // The count committed bytes from first.
    [[nodiscard]] static View<std::uint8_t> committed_bytes_(std::uint8_t* first, std::size_t count) noexcept {
        return View<std::uint8_t>{first, count};
    }
};

}  // namespace foundation::core

namespace foundation::os {

using core::ByteCount;

// The bytes of one huge page of the page source: 2 MiB on x86_64, and on
// aarch64 with pages of 4 KiB.
inline constexpr std::size_t kPageSourceHugePageBytes = std::size_t{2} << 20;

// The first byte of a reservation: at the alignment of a page, or of a huge
// page of kPageSourceHugePageBytes.
enum class ReserveAlignment : std::uint8_t {
    page,
    huge_page,
};

// The largest count of bytes of a reserve that a caller permits (THE
// RESERVATION).
struct ReserveCapTag final {};
using ReserveCap = core::Id<ReserveCapTag, std::size_t>;

// The release of the pages of a committed range (THE CALLS).
enum class PurgeKind : std::uint8_t {
    // MADV_DONTNEED: the kernel takes each frame back at once, and each page
    // reads zero at its next touch.
    dont_need,
    // MADV_FREE: the kernel takes a frame back only under memory pressure.
    // Until then a page keeps its bytes, and a write to the page keeps it.
    lazy_free,
};

// The result of advise_huge (THE FALLBACKS).
enum class HugePageAdvice : std::uint8_t {
    refused,
    advised,
};

// The pages of a range after a collapse (THE FALLBACKS).
enum class PageKind : std::uint8_t {
    base,
    huge,
};

// The result of populate (THE FALLBACKS).
enum class PopulateKind : std::uint8_t {
    // The kernel faulted in each page with MADV_POPULATE_WRITE.
    kernel,
    // The door faulted in each page with one atomic or of zero.
    touched,
    // The kernel refused the advice with an error other than einval, and the
    // pages fault in at their first touch.
    deferred,
};

// A NUMA node of the host.
struct NumaNodeTag final {};
using NumaNodeId = core::Id<NumaNodeTag, std::uint32_t>;

// A set of at most kNumaNodeSetBits NUMA nodes, one bit for each node.
inline constexpr std::uint32_t kNumaNodeSetBits = 64;

// The slot of a node in a set: a node below kNumaNodeSetBits, which the type
// of the index proves.
using NumaNodeSlot = core::Index<kNumaNodeSetBits>;

// The slot of a node, or none for a node at kNumaNodeSetBits or above.
[[nodiscard]] constexpr core::Option<NumaNodeSlot> numa_node_slot(NumaNodeId node) noexcept {
    return NumaNodeSlot::check(node.raw());
}

class NumaNodeSet final {
public:
    // The empty set.
    constexpr NumaNodeSet() noexcept = default;

    // This set and the node of a slot.
    [[nodiscard]] constexpr NumaNodeSet with(NumaNodeSlot node) const noexcept {
        NumaNodeSet set = *this;
        set.bits_ |= std::uint64_t{1} << node.raw();
        return set;
    }

    [[nodiscard]] constexpr bool contains(NumaNodeId node) const noexcept {
        return node.raw() < kNumaNodeSetBits && ((bits_ >> node.raw()) & 1U) != 0;
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept { return bits_ == 0; }

    // The word of the set, bit k for node k, as mbind reads its mask.
    [[nodiscard]] constexpr std::uint64_t mask_word() const noexcept { return bits_; }

    friend constexpr bool operator==(NumaNodeSet, NumaNodeSet) noexcept = default;

private:
    std::uint64_t bits_ = 0;
};

// The kind of a placement.
enum class PlacementKind : std::uint8_t {
    // No policy of the door: the pages take the policy of the thread that
    // touches them first.
    unknown,
    // The pages come from one node, and from another node when that node is
    // low on free memory (MPOL_PREFERRED).
    node,
    // The pages come from the nodes of a set in turn (MPOL_INTERLEAVE).
    interleaved,
};

// The NUMA placement of a range: unknown, one node, or the nodes of a set.
// A placement is a plain value, and the default one is the unknown
// placement.
class Placement final {
public:
    constexpr Placement() noexcept = default;

    [[nodiscard]] static constexpr Placement unknown() noexcept { return Placement{}; }

    [[nodiscard]] static constexpr Placement on_node(NumaNodeId node) noexcept {
        Placement placement{};
        placement.kind_ = PlacementKind::node;
        placement.node_ = node;
        return placement;
    }

    // The interleave over the nodes of a set.  An empty set gives the
    // unknown placement.
    [[nodiscard]] static constexpr Placement interleaved(NumaNodeSet nodes) noexcept {
        Placement placement{};
        if (nodes.is_empty()) return placement;
        placement.kind_ = PlacementKind::interleaved;
        placement.nodes_ = nodes;
        return placement;
    }

    [[nodiscard]] constexpr PlacementKind kind() const noexcept { return kind_; }

    // The node of a placement of the kind node, and node 0 for each other
    // kind.
    [[nodiscard]] constexpr NumaNodeId node() const noexcept { return node_; }

    // The nodes of a placement of the kind interleaved, and the empty set for
    // each other kind.
    [[nodiscard]] constexpr NumaNodeSet nodes() const noexcept { return nodes_; }

    friend constexpr bool operator==(Placement const&, Placement const&) noexcept = default;

private:
    PlacementKind kind_ = PlacementKind::unknown;
    NumaNodeId node_{0};
    NumaNodeSet nodes_{};
};

namespace side {

// The role of the offset of a range from the first byte of its reservation.
struct RangeStart final {};

}  // namespace side

using RangeStart = core::Side<side::RangeStart, ByteCount>;

namespace detail {

// The one builder of a reservation over a map.  src/foundation/os/PageSource.cpp
// defines it, so only the door makes a reservation.
struct PageSourceDoor;

}  // namespace detail

// A part of one reservation (THE RANGES).  Only the door makes a range, so
// each copy is user-provided: a range is then neither trivially copyable
// nor an implicit-lifetime type, and std::bit_cast and
// std::start_lifetime_as build none from bytes.
class ReservedRange final {
public:
    ReservedRange(ReservedRange const& other) noexcept
        : origin_{other.origin_}, offset_{other.offset_}, bytes_{other.bytes_} {}
    ReservedRange& operator=(ReservedRange const& other) noexcept {
        origin_ = other.origin_;
        offset_ = other.offset_;
        bytes_ = other.bytes_;
        return *this;
    }
    ~ReservedRange() = default;

    [[nodiscard]] ByteCount size() const noexcept { return ByteCount{bytes_}; }
    [[nodiscard]] RangeStart start() const noexcept { return core::side_as<side::RangeStart>(ByteCount{offset_}); }

private:
    friend class Reservation;
    friend class CommittedRange;
    friend struct detail::PageSourceDoor;

    ReservedRange(std::uintptr_t origin, std::size_t offset, std::size_t bytes) noexcept
        : origin_{origin}, offset_{offset}, bytes_{bytes} {}

    std::uintptr_t origin_ = 0;
    std::size_t offset_ = 0;
    std::size_t bytes_ = 0;
};

// A part of one reservation that commit() gave read and write access (THE
// RANGES).  Each copy is user-provided, as for a ReservedRange.
class CommittedRange final {
public:
    CommittedRange(CommittedRange const& other) noexcept
        : origin_{other.origin_}, offset_{other.offset_}, bytes_{other.bytes_} {}
    CommittedRange& operator=(CommittedRange const& other) noexcept {
        origin_ = other.origin_;
        offset_ = other.offset_;
        bytes_ = other.bytes_;
        return *this;
    }
    ~CommittedRange() = default;

    [[nodiscard]] ByteCount size() const noexcept { return ByteCount{bytes_}; }
    [[nodiscard]] RangeStart start() const noexcept { return core::side_as<side::RangeStart>(ByteCount{offset_}); }

    // The reserved range of the same bytes.
    [[nodiscard]] ReservedRange reserved() const noexcept { return ReservedRange{origin_, offset_, bytes_}; }

private:
    friend class Reservation;
    friend struct detail::PageSourceDoor;

    CommittedRange(std::uintptr_t origin, std::size_t offset, std::size_t bytes) noexcept
        : origin_{origin}, offset_{offset}, bytes_{bytes} {}

    std::uintptr_t origin_ = 0;
    std::size_t offset_ = 0;
    std::size_t bytes_ = 0;
};

// The owner of one range of address space (THE RESERVATION).
class [[nodiscard]] Reservation final {
public:
    Reservation(Reservation&& other) noexcept
        : first_{other.first_}, bytes_{other.bytes_}, page_bytes_{other.page_bytes_}, alignment_{other.alignment_} {
        other.first_ = nullptr;
        other.bytes_ = 0;
    }

    Reservation(Reservation const&) = delete("two owners of one reservation would unmap it two times");
    Reservation& operator=(Reservation const&) = delete("two owners of one reservation would unmap it two times");
    Reservation&
    operator=(Reservation&&) = delete("a reservation owns one range for its life and takes no other range");

    // Unmaps the whole range, which resets its shadow in a build with a
    // sanitizer (THE SANITIZERS).  A reservation
    // that moved holds no range and unmaps nothing.
    ~Reservation();

    // The bytes of the range, a whole count of pages.
    [[nodiscard]] ByteCount size() const noexcept { return ByteCount{bytes_}; }

    // The unit of the ranges of the reservation: the bytes of one page of the
    // host.  It is 4 KiB on x86_64.  On aarch64 the auxiliary vector gives it,
    // and a process that cannot read the vector takes 64 KiB, a whole count
    // of each page size of aarch64.
    [[nodiscard]] ByteCount page_size() const noexcept { return ByteCount{page_bytes_}; }

    // The part of bytes at start, or none when start or bytes is not a whole
    // count of pages, when bytes is zero, or when the part does not lie inside
    // the range.  Complexity: O(1).
    [[nodiscard]] core::Option<ReservedRange> range(RangeStart start, ByteCount bytes) const noexcept;

    // The whole range.
    [[nodiscard]] ReservedRange whole() const noexcept;

    // The address of the first byte of a range, as a number: for a probe of
    // the kernel, such as a census of the nodes of the pages, and for the
    // address window of an allocator.  A range of a different reservation
    // ends the process.
    [[nodiscard]] std::uintptr_t address_of(ReservedRange const& range) const noexcept;

    // The bytes of a committed range.  Each byte reads zero until the owner
    // writes it, and again after a decommit with MADV_DONTNEED.  The View
    // borrows this reservation.  A range of a different reservation ends the
    // process.
    [[nodiscard]] core::View<std::uint8_t> bytes_of(CommittedRange const& range) & noexcept;
    void bytes_of(CommittedRange const&) && = delete("a View of a temporary reservation points into an unmapped range");

    // Gives the range read and write access (THE CALLS).
    [[nodiscard]] core::Result<CommittedRange, Errno> commit(ReservedRange const& range) noexcept;

    // Gives the frames of the range back to the kernel, and gives the kind
    // that ran (THE FALLBACKS).
    [[nodiscard]] PurgeKind decommit(CommittedRange const& range, PurgeKind kind) noexcept;

    // Places the pages of the range, and gives the placement that the range
    // got (THE FALLBACKS).  The unknown placement issues no call.
    [[nodiscard]] Placement bind(ReservedRange const& range, Placement placement) noexcept;

    // Asks for transparent huge pages in the range.
    [[nodiscard]] HugePageAdvice advise_huge(ReservedRange const& range) noexcept;

    // Makes huge pages of each whole huge page of the range before the call
    // returns.  A range with no whole huge page gets the base pages and
    // issues no call.
    [[nodiscard]] PageKind collapse(CommittedRange const& range) noexcept;

    // Faults in each page of the range for a write, and changes no byte.
    [[nodiscard]] PopulateKind populate(CommittedRange const& range) noexcept;

    // The node of the first page of the range, by move_pages with no target
    // node, which moves no page.  A page with no frame gives the error of the
    // kernel for that page, such as enoent.
    [[nodiscard]] core::Result<NumaNodeId, Errno> node_of_first_page(CommittedRange const& range) noexcept;

    // Grows the whole reservation to bytes, rounded up to a whole count of
    // pages, and keeps the bytes and the access of each page.  The new pages
    // take the access of the map: read and write when each page of the
    // reservation is committed, and no access when none is.  A reservation of
    // two kinds of access gives efault, and a smaller count gives einval.
    // The first byte can move, and a reservation of the alignment of a huge
    // page keeps its alignment.  Complexity: one move of the page table
    // entries, and no copy of a byte.
    [[nodiscard]] core::Result<core::Unit, Errno> grow(ByteCount bytes) noexcept;

private:
    friend struct detail::PageSourceDoor;

    Reservation(std::uint8_t* first, std::size_t bytes, std::size_t page_bytes, ReserveAlignment alignment) noexcept
        : first_{first}, bytes_{bytes}, page_bytes_{page_bytes}, alignment_{alignment} {}

    // The first byte of a range of this reservation.  A range of a different
    // reservation, or a part that does not lie inside this one, ends the
    // process.
    [[nodiscard]] std::uint8_t* first_of_(ReservedRange const& range) const noexcept;
    [[nodiscard]] std::uint8_t* first_of_(CommittedRange const& range) const noexcept;

    std::uint8_t* first_ = nullptr;
    std::size_t bytes_ = 0;
    std::size_t page_bytes_ = 0;
    ReserveAlignment alignment_ = ReserveAlignment::page;
};

// Reserves bytes of address space at the alignment (THE RESERVATION).
[[nodiscard]] core::Result<Reservation, Errno> reserve(ByteCount bytes, ReserveAlignment alignment) noexcept;

// The same reserve, refused with enomem and with no call for a count of
// bytes above the cap.
[[nodiscard]] core::Result<Reservation, Errno> reserve(ByteCount bytes, ReserveAlignment alignment,
                                                       ReserveCap cap) noexcept;

// The node of the CPU of the calling thread at the call.  The thread can
// move to another CPU at once after the call.
[[nodiscard]] core::Result<NumaNodeId, Errno> current_node() noexcept;

}  // namespace foundation::os
