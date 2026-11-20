// The compile-time checks of foundation/alloc/Layout.h.

#include <foundation/alloc/Layout.h>

#include <foundation/Lifetime.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::alloc {

namespace layout_check {

struct alignas(64) LineSized {
    std::uint8_t first = 0;
};

struct Incomplete;

template <class T>
concept HasLayout = requires { Layout::of<T>(); };

}  // namespace layout_check

// The layout of each scalar type is its size and its alignment.
static_assert(Layout::of<std::uint8_t>().bytes().raw() == 1);
static_assert(Layout::of<std::uint8_t>().alignment() == Alignment{1});
static_assert(Layout::of<std::uint16_t>().bytes().raw() == 2);
static_assert(Layout::of<std::uint32_t>().bytes().raw() == 4);
static_assert(Layout::of<std::uint64_t>().bytes().raw() == 8);
static_assert(Layout::of<std::uint64_t>().alignment() == Alignment{8});
static_assert(Layout::of<double>().alignment() == alignment_of<double>());
static_assert(Layout::of<void*>().bytes().raw() == sizeof(void*));

// An over-aligned type has the layout of its alignment.
static_assert(Layout::of<layout_check::LineSized>().bytes().raw() == 64);
static_assert(Layout::of<layout_check::LineSized>().alignment() == Alignment{64});
static_assert(bytes_of(alignment_of<layout_check::LineSized>()).raw() == 64);

// An incomplete type, a reference, a function and void have no layout.
static_assert(!layout_check::HasLayout<layout_check::Incomplete>);
static_assert(!layout_check::HasLayout<int&>);
static_assert(!layout_check::HasLayout<void>);
static_assert(!layout_check::HasLayout<void()>);

// The array door counts the bytes of each element, and refuses no element
// and a count past the largest object.
static_assert(Layout::of_array<std::uint32_t>(core::ElementCount{3}).is_ok());
static_assert(Layout::of_array<std::uint32_t>(core::ElementCount{3}).value_or(Layout::of<std::uint8_t>()).bytes().raw()
              == 12);
static_assert(Layout::of_array<std::uint64_t>(core::ElementCount{0}).is_err());
static_assert(Layout::of_array<std::uint64_t>(core::ElementCount{kMaxLayoutBytes / 8 + 1}).is_err());
static_assert(Layout::of_array<std::uint64_t>(core::ElementCount{kMaxLayoutBytes / 8}).is_ok());

// The alignment door takes only a power of two.
static_assert(mint_alignment(core::ByteCount{16}).is_ok());
static_assert(mint_alignment(core::ByteCount{0}).is_err());
static_assert(mint_alignment(core::ByteCount{24}).is_err());

// The run-time door refuses zero bytes, an alignment that is not a power of
// two, and bytes that round up past the largest object.
static_assert(mint_layout(core::ByteCount{24}, Alignment{8}).is_ok());
static_assert(mint_layout(core::ByteCount{0}, Alignment{8}).is_err());
static_assert(mint_layout(core::ByteCount{24}, Alignment{12}).is_err());
static_assert(mint_layout(core::ByteCount{24}, Alignment{0}).is_err());
static_assert(mint_layout(core::ByteCount{kMaxLayoutBytes}, Alignment{1}).is_ok());
static_assert(mint_layout(core::ByteCount{kMaxLayoutBytes}, Alignment{64}).is_err());

// The last fact of THE LAYOUT counts the alignment - 1 bytes of the unaligned
// storage, so the largest layout at 64 bytes leaves room for them.
static_assert(mint_layout(core::ByteCount{kMaxLayoutBytes - 126}, Alignment{64}).is_ok());
static_assert(mint_layout(core::ByteCount{kMaxLayoutBytes - 62}, Alignment{64}).is_err());
static_assert(Layout::of_array<std::uint64_t>(core::ElementCount{(kMaxLayoutBytes - 7) / 8}).is_ok());
static_assert(Layout::of_array<std::uint64_t>(core::ElementCount{(kMaxLayoutBytes - 7) / 8 + 1}).is_err());

// The unaligned storage of a layout holds its bytes rounded up to the
// alignment and alignment - 1 bytes more, at an alignment of one.
static_assert(mint_layout(core::ByteCount{1000}, Alignment{256})
                  .expect("a check gives a layout")
                  .unaligned_storage()
                  .bytes()
                  .raw()
              == 1024 + 255);
static_assert(Layout::of<std::uint64_t>().unaligned_storage().bytes().raw() == 15);
static_assert(Layout::of<std::uint64_t>().unaligned_storage().alignment() == Alignment{1});
static_assert(Layout::of<std::uint8_t>().unaligned_storage() == Layout::of<std::uint8_t>());

// No number converts to an Alignment or to a Layout, and only the doors make
// a Layout.  A Layout holds a byte_seal: its copy and move constructors and
// its destructor are trivial, so a call passes it in registers, and its
// assignments are not, so std::bit_cast builds none and the checked lifetime
// start refuses it.
static_assert(!std::is_convertible_v<std::size_t, Alignment>);
static_assert(!std::is_constructible_v<Layout, std::size_t, std::size_t>);
static_assert(!std::is_constructible_v<Layout, core::ByteCount, Alignment>);
static_assert(!std::is_default_constructible_v<Layout>);
static_assert(sizeof(Layout) == 2 * sizeof(std::size_t));
static_assert(std::is_trivially_copy_constructible_v<Layout>);
static_assert(std::is_trivially_move_constructible_v<Layout>);
static_assert(std::is_trivially_destructible_v<Layout>);
static_assert(!std::is_trivially_copyable_v<Layout>);
static_assert(!::foundation::lifetime::ImplicitLifetimeThroughout<Layout>);

// ── The size classes ───────────────────────────────────────────────────

namespace layout_check {

[[nodiscard]] consteval SizeClass class_at(std::uint8_t value) { return SizeClass{value}; }

[[nodiscard]] consteval std::size_t slot_at(std::uint8_t value) { return slot_bytes(class_at(value)).raw(); }

// The class of a layout of bytes at the alignment, or 0 for none.
[[nodiscard]] consteval std::uint8_t class_value_for(std::size_t bytes, std::size_t alignment) {
    core::Option<SizeClass> found =
        class_for(mint_layout(core::ByteCount{bytes}, Alignment{alignment}).expect("a check gives a layout"));
    return static_cast<std::uint8_t>(static_cast<core::Option<SizeClass>&&>(found).value_or(SizeClass{0}));
}

// Each class holds its slot and not one byte more, and the next class takes
// the next byte.  The slots rise, each is a whole count of 16 bytes, and the
// slot of a class above 128 bytes is less than 5/4 of the smallest request
// of the class.  Complexity: O(64).
[[nodiscard]] consteval bool classes_hold_their_bounds() {
    for (std::uint8_t value = 1; value <= kSizeClassCount; ++value) {
        std::size_t const slot = slot_at(value);
        if (slot % kSlotStepBytes != 0) return false;
        if (class_value_for(slot, 1) != value) return false;
        if (value < kSizeClassCount && class_value_for(slot + 1, 1) != value + 1) return false;
        if (value == 1) continue;
        std::size_t const smallest = slot_at(static_cast<std::uint8_t>(value - 1)) + 1;
        if (class_value_for(smallest, 1) != value) return false;
        if (slot <= kSmallClassBytes) continue;
        if (slot * 4 >= smallest * 5) return false;
    }
    return true;
}

// For each alignment from 1 byte to 2 MiB and each class bound, the slot of
// class_for holds the bytes and is a whole count of the alignment, and the
// class below it does not hold both.  Complexity: O(22 * 64).
[[nodiscard]] consteval bool classes_hold_each_alignment() {
    for (std::size_t alignment = 1; alignment <= kLargestSlotBytes; alignment *= 2) {
        for (std::uint8_t value = 1; value <= kSizeClassCount; ++value) {
            std::size_t const bytes = slot_at(value);
            std::uint8_t const found = class_value_for(bytes, alignment);
            if (found == 0) return false;
            std::size_t const slot = slot_at(found);
            if (slot < bytes || slot % alignment != 0) return false;
            if (found == 1) continue;
            std::size_t const below = slot_at(static_cast<std::uint8_t>(found - 1));
            if (below >= bytes && below % alignment == 0) return false;
        }
    }
    return true;
}

}  // namespace layout_check

static_assert(slot_bytes(SizeClass{1}).raw() == 16);
static_assert(slot_bytes(SizeClass{8}).raw() == 128);
static_assert(slot_bytes(SizeClass{9}).raw() == 160);
static_assert(slot_bytes(SizeClass{12}).raw() == 256);
static_assert(slot_bytes(SizeClass{32}).raw() == 8192);
static_assert(slot_bytes(SizeClass{52}).raw() == 256 * 1024);
static_assert(slot_bytes(SizeClass{64}).raw() == kLargestSlotBytes);
static_assert(layout_check::classes_hold_their_bounds());
static_assert(layout_check::classes_hold_each_alignment());

// A request past the largest class, or at an alignment past it, has no class.
static_assert(layout_check::class_value_for(kLargestSlotBytes + 1, 1) == 0);
static_assert(layout_check::class_value_for(16, kLargestSlotBytes * 2) == 0);
static_assert(layout_check::class_value_for(1, 1) == 1);
static_assert(layout_check::class_value_for(1, 64) == 4);

// The slabs and the bins of the classes at the bounds of each kind.
static_assert(!is_size_class(SizeClass{0}));
static_assert(!is_size_class(SizeClass{65}));
static_assert(slab_kind_of(SizeClass{32}) == SlabKind::small);
static_assert(slab_kind_of(SizeClass{33}) == SlabKind::medium);
static_assert(slab_kind_of(SizeClass{52}) == SlabKind::medium);
static_assert(slab_kind_of(SizeClass{53}) == SlabKind::large);
static_assert(slab_slots(SizeClass{1}) == 4096);
static_assert(slab_slots(SizeClass{32}) == 8);
static_assert(slab_slots(SizeClass{33}) == 204);
static_assert(slab_slots(SizeClass{52}) == 8);
static_assert(slab_slots(SizeClass{64}) == 1);
static_assert(bin_capacity(SizeClass{8}) == 128);
static_assert(bin_capacity(SizeClass{12}) == 64);
static_assert(bin_capacity(SizeClass{32}) == 2);
static_assert(bin_capacity(SizeClass{64}) == 2);

}  // namespace foundation::alloc
