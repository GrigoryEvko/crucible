// The page source door of the base (foundation/os/PageSource.h).  It issues
// the raw primitive of foundation/os/Syscall.h, and foundation/os/MapWords.h
// and foundation/os/MemoryPolicyWords.h give the words of its calls, so this
// file includes no system header.

#include <foundation/os/PageSource.h>

#include <foundation/core/Bits.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Report.h>
#include <foundation/os/MapWords.h>
#include <foundation/os/MemoryPolicyWords.h>
#include <foundation/os/Syscall.h>

#include <cstddef>
#include <cstdint>
#include <utility>

#if !defined(__x86_64__)
#include <foundation/os/Auxiliary.h>
#endif

namespace foundation::os {

namespace detail {

struct PageSourceDoor {
    [[nodiscard]] static Reservation build(std::uint8_t* first, std::size_t bytes, std::size_t page_bytes,
                                           ReserveAlignment alignment) noexcept {
        return Reservation{first, bytes, page_bytes, alignment};
    }

    [[nodiscard]] static CommittedRange committed(ReservedRange const& range) noexcept {
        return CommittedRange{range.origin_, range.offset_, range.bytes_};
    }
};

}  // namespace detail

namespace {

// The file descriptor of a map with no file.
constexpr int kNoFile = -1;

[[nodiscard]] std::uintptr_t word_of(void const* address) noexcept {
    return __builtin_bit_cast(std::uintptr_t, address);
}

[[nodiscard]] std::uint8_t* bytes_at(std::uint64_t word) noexcept { return __builtin_bit_cast(std::uint8_t*, word); }

// The unit of the pages of the door.  x86_64 has pages of 4 KiB only, so the
// door reads no file there.  An aarch64 kernel can use pages of 4 KiB,
// 16 KiB or 64 KiB, and the auxiliary vector gives the size.  A process that
// cannot read the vector, such as a process that is not dumpable, whose
// /proc/self/auxv only root reads, takes 64 KiB: a whole count of each of the
// three, so each range of the door is still a whole count of pages.
#if defined(__x86_64__)
[[nodiscard]] std::size_t read_page_bytes() noexcept { return std::size_t{4} << 10; }
#else
constexpr std::uint64_t kLargestPageBytes = std::uint64_t{64} << 10;

[[nodiscard]] std::size_t read_page_bytes() noexcept {
    return read_auxiliary(AuxiliaryEntry::page_size).value_or(kLargestPageBytes);
}
#endif

// The bytes of the alignment of a reservation.  A huge page of 2 MiB is a
// whole count of pages of each size that the two architectures use.
[[nodiscard]] std::size_t alignment_bytes(ReserveAlignment alignment, std::size_t page_bytes) noexcept {
    return alignment == ReserveAlignment::huge_page ? kPageSourceHugePageBytes : page_bytes;
}

// A count of bytes rounded up to a whole count of pages, or none when the
// rounding does not fit in a std::size_t.
[[nodiscard]] core::Option<std::size_t> whole_pages(std::size_t bytes, std::size_t page_bytes) noexcept {
    return core::align_up(bytes, page_bytes);
}

// Maps bytes of address space with no access and no reserve of swap.
[[nodiscard]] core::Result<std::uint8_t*, Errno> map_no_access(std::size_t bytes) noexcept {
    CRUCIBLE_TRY(address, result_of(raw_syscall(Syscall::mmap, Word{}, bytes, kProtectNone,
                                                kMapPrivateAnonymous | kMapNoReserve, kNoFile, 0)));
    return bytes_at(address);
}

// Unmaps bytes that the door mapped.  The kernel refuses an unmap only for an
// address or a count that no map of the door has, so a refusal is a defect
// of the door, and the process ends.
void unmap_mapped(std::uint8_t* first, std::size_t bytes) noexcept {
    core::Result<std::uint64_t, Errno> const unmapped = result_of(raw_syscall(Syscall::munmap, first, bytes));
    if (unmapped.is_err()) {
        core::fatal("PageSource: the unmap of {} bytes that the door mapped failed with the error {}.  The kernel "
                    "refuses an unmap only for an address or a count that no map of the door has",
                    bytes, static_cast<std::uint16_t>(unmapped.err().value_or(Errno::einval)));
    }
}

// The first byte of a map of bytes at the alignment: one map of the bytes
// and the alignment less one page, and an unmap of the head and of the tail
// outside the aligned bytes.  A map of the kernel starts at a page, so an
// alignment of one page takes no more bytes.
[[nodiscard]] core::Result<std::uint8_t*, Errno> map_aligned(std::size_t bytes, std::size_t alignment,
                                                             std::size_t page_bytes) noexcept {
    std::size_t const slack = alignment > page_bytes ? alignment - page_bytes : 0;
    std::size_t total = 0;
    if (__builtin_add_overflow(bytes, slack, &total)) return core::err(Errno::enomem);
    CRUCIBLE_TRY(mapped, map_no_access(total));
    CRUCIBLE_TRY(aligned, core::align_up(word_of(mapped), std::uintptr_t{alignment})
                              .ok_or(Errno::enomem)
                              .map_err([mapped, total](Errno error) noexcept {
                                  unmap_mapped(mapped, total);
                                  return error;
                              }));
    std::size_t const head = aligned - word_of(mapped);
    std::size_t const tail = total - head - bytes;
    if (head != 0) unmap_mapped(mapped, head);
    if (tail != 0) unmap_mapped(mapped + head + bytes, tail);
    return mapped + head;
}

}  // namespace

core::Result<Reservation, Errno> reserve(ByteCount bytes, ReserveAlignment alignment) noexcept {
    if (bytes.raw() == 0) return core::err(Errno::einval);
    std::size_t const page_bytes = read_page_bytes();
    CRUCIBLE_TRY(size, whole_pages(bytes.raw(), page_bytes).ok_or(Errno::enomem));
    CRUCIBLE_TRY(first, map_aligned(size, alignment_bytes(alignment, page_bytes), page_bytes));
    return detail::PageSourceDoor::build(first, size, page_bytes, alignment);
}

core::Result<Reservation, Errno> reserve(ByteCount bytes, ReserveAlignment alignment, ReserveCap cap) noexcept {
    if (bytes.raw() > cap.raw()) return core::err(Errno::enomem);
    return reserve(bytes, alignment);
}

core::Result<NumaNodeId, Errno> current_node() noexcept {
    unsigned cpu = 0;
    unsigned node = 0;
    CRUCIBLE_TRY(taken, result_of(raw_syscall(Syscall::getcpu, static_cast<void const*>(&cpu),
                                              static_cast<void const*>(&node), Word{})));
    static_cast<void>(taken);
    return NumaNodeId{node};
}

Reservation::~Reservation() {
    if (first_ == nullptr) return;
    unmap_mapped(first_, bytes_);
}

// The origin of a range of an ended reservation can be the first byte of a
// new reservation at the same address, so the part of the range must also
// lie inside this reservation.  Each compare is its own test, one for each
// branch.
std::uint8_t* Reservation::first_of_(ReservedRange const& range) const noexcept {
    if (range.origin_ != word_of(first_)) [[unlikely]] {
        core::fatal("PageSource: a range of the reservation at {} reached the reservation at {}.  A range applies "
                    "only to the reservation that made it, and a grow that moved the reservation ends each range "
                    "of the old place",
                    range.origin_, word_of(first_));
    }
    if (range.offset_ > bytes_) [[unlikely]] {
        core::fatal("PageSource: a range at the offset {} reached a reservation of {} bytes.  The range came from an "
                    "ended reservation at the same address",
                    range.offset_, bytes_);
    }
    if (range.bytes_ > bytes_ - range.offset_) [[unlikely]] {
        core::fatal("PageSource: a range of {} bytes at the offset {} reached a reservation of {} bytes.  The range "
                    "came from an ended reservation at the same address",
                    range.bytes_, range.offset_, bytes_);
    }
    return first_ + range.offset_;
}

std::uint8_t* Reservation::first_of_(CommittedRange const& range) const noexcept { return first_of_(range.reserved()); }

core::Option<ReservedRange> Reservation::range(RangeStart start, ByteCount bytes) const noexcept {
    std::size_t const offset = start.value().raw();
    std::size_t const count = bytes.raw();
    bool const is_whole_pages = offset % page_bytes_ == 0 && count % page_bytes_ == 0;
    bool const is_inside = count != 0 && offset <= bytes_ && count <= bytes_ - offset;
    if (!is_whole_pages || !is_inside) return core::none;
    return core::Option<ReservedRange>::some(ReservedRange{word_of(first_), offset, count});
}

ReservedRange Reservation::whole() const noexcept { return ReservedRange{word_of(first_), 0, bytes_}; }

std::uintptr_t Reservation::address_of(ReservedRange const& range) const noexcept { return word_of(first_of_(range)); }

core::View<std::uint8_t> Reservation::bytes_of(CommittedRange const& range) & noexcept {
    std::uint8_t* const first = first_of_(range);
    return core::CommittedRangeDoor::committed_bytes_(first, range.bytes_);
}

core::Result<CommittedRange, Errno> Reservation::commit(ReservedRange const& range) noexcept {
    std::uint8_t* const first = first_of_(range);
    CRUCIBLE_TRY(opened, result_of(raw_syscall(Syscall::mprotect, first, range.bytes_, kProtectReadWrite)));
    static_cast<void>(opened);
    return detail::PageSourceDoor::committed(range);
}

PurgeKind Reservation::decommit(CommittedRange const& range, PurgeKind kind) noexcept {
    std::uint8_t* const first = first_of_(range);
    if (kind == PurgeKind::lazy_free) {
        if (unit_of(raw_syscall(Syscall::madvise, first, range.bytes_, kAdviseFree)).is_ok()) {
            return PurgeKind::lazy_free;
        }
    }
    core::Result<core::Unit, Errno> const released =
        unit_of(raw_syscall(Syscall::madvise, first, range.bytes_, kAdviseDontNeed));
    if (released.is_err()) {
        core::fatal("PageSource: MADV_DONTNEED of {} committed bytes failed with the error {}.  The kernel refuses "
                    "it only for a range that no map holds or that a lock holds, and the door gives no such range",
                    range.bytes_, static_cast<std::uint16_t>(released.err().value_or(Errno::einval)));
    }
    return PurgeKind::dont_need;
}

Placement Reservation::bind(ReservedRange const& range, Placement placement) noexcept {
    std::uint8_t* const first = first_of_(range);
    if (placement.kind() == PlacementKind::unknown) return Placement::unknown();
    NumaNodeSet nodes = placement.nodes();
    int policy = kMemoryPolicyInterleave;
    if (placement.kind() == PlacementKind::node) {
        CRUCIBLE_LET_ELSE(slot, numa_node_slot(placement.node()), Placement::unknown());
        nodes = NumaNodeSet{}.with(slot);
        policy = kMemoryPolicyPreferred;
    }
    // mbind reads maxnode - 1 bits of the mask, so a mask of one word takes
    // kNumaNodeSetBits + 1.  No flag: the policy applies to each page at its
    // first touch, and no page of a new range has a frame to move.
    std::uint64_t const mask = nodes.mask_word();
    core::Result<core::Unit, Errno> const bound =
        unit_of(raw_syscall(Syscall::mbind, first, range.bytes_, policy, static_cast<void const*>(&mask),
                            std::uint64_t{kNumaNodeSetBits} + 1, 0));
    return bound.is_ok() ? placement : Placement::unknown();
}

HugePageAdvice Reservation::advise_huge(ReservedRange const& range) noexcept {
    std::uint8_t* const first = first_of_(range);
    return unit_of(raw_syscall(Syscall::madvise, first, range.bytes_, kAdviseHugePage)).is_ok()
             ? HugePageAdvice::advised
             : HugePageAdvice::refused;
}

PageKind Reservation::collapse(CommittedRange const& range) noexcept {
    std::uint8_t* const first = first_of_(range);
    std::uintptr_t const start = word_of(first);
    CRUCIBLE_LET_ELSE(aligned, core::align_up(start, std::uintptr_t{kPageSourceHugePageBytes}), PageKind::base);
    std::size_t const head = aligned - start;
    if (head >= range.bytes_) return PageKind::base;
    std::size_t const whole = (range.bytes_ - head) & ~(kPageSourceHugePageBytes - 1);
    if (whole == 0) return PageKind::base;
    return unit_of(raw_syscall(Syscall::madvise, aligned, whole, kAdviseCollapse)).is_ok() ? PageKind::huge
                                                                                           : PageKind::base;
}

PopulateKind Reservation::populate(CommittedRange const& range) noexcept {
    std::uint8_t* const first = first_of_(range);
    core::Result<core::Unit, Errno> const populated =
        unit_of(raw_syscall(Syscall::madvise, first, range.bytes_, kAdvisePopulateWrite));
    if (populated.is_ok()) return PopulateKind::kernel;
    if (populated.err().value_or(Errno::enomem) != Errno::einval) return PopulateKind::deferred;
    // An atomic or of zero faults in the page for a write, and a write of
    // another thread to the same byte at the same time is not lost.
    // Complexity: one write for each page of the range.
    for (std::size_t offset = 0; offset < range.bytes_; offset += page_bytes_) {
        static_cast<void>(__atomic_fetch_or(first + offset, std::uint8_t{0}, __ATOMIC_RELAXED));
    }
    return PopulateKind::touched;
}

// move_pages of the calling process with no target nodes writes the node of
// each page, or the negative error of a page with no frame, into its status.
core::Result<NumaNodeId, Errno> Reservation::node_of_first_page(CommittedRange const& range) noexcept {
    constexpr int kCallingProcess = 0;
    constexpr unsigned kNoFlags = 0;
    void const* const page = first_of_(range);
    int status = 0;
    CRUCIBLE_TRY(queried,
                 result_of(raw_syscall(Syscall::move_pages, kCallingProcess, 1, static_cast<void const*>(&page), Word{},
                                       static_cast<void const*>(&status), kNoFlags)));
    static_cast<void>(queried);
    CRUCIBLE_TRY(node, result_of(static_cast<long>(status)));
    return NumaNodeId{static_cast<std::uint32_t>(node)};
}

core::Result<core::Unit, Errno> Reservation::grow(ByteCount bytes) noexcept {
    CRUCIBLE_TRY(size, whole_pages(bytes.raw(), page_bytes_).ok_or(Errno::enomem));
    if (size < bytes_) return core::err(Errno::einval);
    if (size == bytes_) return core::Unit{};
    if (alignment_ == ReserveAlignment::page) {
        CRUCIBLE_TRY(moved, result_of(raw_syscall(Syscall::mremap, first_, bytes_, size, kRemapMayMove)));
        first_ = bytes_at(moved);
        bytes_ = size;
        return core::Unit{};
    }
    // A move of the kernel keeps only the alignment of a page.  So the door
    // maps an aligned place of the new size, and the move replaces that place
    // with the map of the reservation.  A refused move leaves the reservation
    // as it was, and the door unmaps the place.
    CRUCIBLE_TRY(place, map_aligned(size, alignment_bytes(alignment_, page_bytes_), page_bytes_));
    core::Result<std::uint64_t, Errno> moved =
        result_of(raw_syscall(Syscall::mremap, first_, bytes_, size, kRemapMayMove | kRemapFixed, place));
    if (moved.is_err()) {
        unmap_mapped(place, size);
        return core::err(moved.err().value_or(Errno::enomem));
    }
    first_ = place;
    bytes_ = size;
    return core::Unit{};
}

}  // namespace foundation::os
