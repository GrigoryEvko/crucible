// The compile-time checks of foundation/alloc/Layout.h.

#include <foundation/alloc/Layout.h>

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

// No number converts to an Alignment or to a Layout, and only the doors make
// a Layout.  The copy of a Layout is user-provided, so std::bit_cast and
// std::start_lifetime_as build none.
static_assert(!std::is_convertible_v<std::size_t, Alignment>);
static_assert(!std::is_constructible_v<Layout, std::size_t, std::size_t>);
static_assert(!std::is_constructible_v<Layout, core::ByteCount, Alignment>);
static_assert(!std::is_default_constructible_v<Layout>);
static_assert(std::is_nothrow_copy_constructible_v<Layout>);
static_assert(!std::is_trivially_copyable_v<Layout>);
static_assert(!std::is_implicit_lifetime_v<Layout>);

}  // namespace foundation::alloc
