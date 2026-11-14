#pragma once

// An owned allocation of `count` elements of T at an alignment of the
// caller's choice.  The buffer owns the storage alone: a move hands it on and
// leaves the source empty, and the destructor frees it.  T is trivially
// destructible, so freeing the storage ends the elements.
//
// Each element starts its lifetime in one of two ways, and each allocation
// route states what it needs:
//
//   allocate                     the checked start over the bytes, so T is
//                                an implicit-lifetime type throughout
//   allocate_value_initialized   a value initialization of each element, so
//                                T has a default constructor that does not
//                                throw
//
// A proof type, or a class that holds one, has neither.  A class that
// refuses a start over bytes, such as a provenance tag, can still hold a
// value that its own default constructor built.
//
// A huge-page buffer is this buffer at huge_page_bytes alignment.  The
// alignment is necessary for the kernel to back the region with huge pages
// and not sufficient: nothing here advises the kernel.  A caller that wants
// huge pages registers the region and advises it separately.
//
// AlignedBytes of foundation/AlignedBytes.h owns a block of bytes at an
// alignment that the run gives, and its mint writes no byte.

#include <foundation/AlignedBytes.h>
#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Region.h>

#include <bit>
#include <cstddef>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>

namespace foundation {

namespace core::detail::owner {

// The fields of an AlignedBuffer: the first element and the count, or null
// and zero when it holds no storage.
template <typename T, std::size_t Alignment>
struct AlignedBufferState {
    T* data = nullptr;
    std::size_t size = 0;

    [[nodiscard]] static constexpr AlignedBufferState emptied(AlignedBufferState const& /*state*/) noexcept {
        return AlignedBufferState{};
    }

    // Gives the storage back.  T is trivially destructible, so the release
    // ends the elements.
    static constexpr void release(AlignedBufferState const& state) noexcept {
        if (state.data != nullptr) {
            heap_release_(state.data,
                          ::foundation::detail::aligned_allocation_bytes<Alignment>(0, state.size, sizeof(T)),
                          Alignment);
        }
    }
};

}  // namespace core::detail::owner

// The core of foundation/core/Owner.h gives the moves and the end of a
// buffer.
template <typename T, std::size_t Alignment = alignof(T)>
    requires(std::is_object_v<T> && std::is_trivially_destructible_v<T> && std::has_single_bit(Alignment)
             && Alignment >= alignof(T))
class [[nodiscard]] AlignedBuffer final
    : public core::detail::OwnerCore<core::detail::owner::AlignedBufferState<T, Alignment>> {
    using State = core::detail::owner::AlignedBufferState<T, Alignment>;
    using Core = core::detail::OwnerCore<State>;

public:
    using value_type = T;
    using size_type = std::size_t;

    static constexpr size_type alignment = Alignment;

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::AlignedBuffer"; }

    constexpr AlignedBuffer() noexcept = default;

    // The bytes the allocation of `count` elements occupies: the element
    // bytes rounded up to the alignment.  A wrap ends the process through
    // the fatal exit, and the text names the catalog entry
    // AllocationSizeOverflow.
    [[nodiscard]] static constexpr size_type allocation_bytes(size_type count) noexcept {
        return detail::aligned_allocation_bytes<Alignment>(0, count, sizeof(T));
    }

    // Starts the lifetime of `count` elements over fresh storage.  An
    // element holds an erroneous value until it is written.  Exhaustion
    // aborts: this runs where a failed allocation has no recovery.
    [[nodiscard]] static AlignedBuffer allocate(size_type count)
        requires lifetime::ImplicitLifetimeThroughout<T>
    {
        if (count == 0) [[unlikely]]
            return AlignedBuffer{};
        void* const raw = allocate_storage_(count);
        return AlignedBuffer{lifetime::start_as_array<T>(raw, count).data(), count};
    }

    // Value-initializes each element, so a T with member initializers gets
    // the values it declares, and every other T reads zero.  Complexity:
    // linear in count.
    [[nodiscard]] static AlignedBuffer allocate_value_initialized(size_type count)
        requires std::is_nothrow_default_constructible_v<T>
    {
        if (count == 0) [[unlikely]]
            return AlignedBuffer{};
        T* const first = static_cast<T*>(allocate_storage_(count));
        for (size_type index = 0; index < count; ++index)
            ::new(static_cast<void*>(first + index)) T();
        return AlignedBuffer{first, count};
    }

    AlignedBuffer(const AlignedBuffer&) = delete("AlignedBuffer owns its storage alone and is move-only");
    AlignedBuffer& operator=(const AlignedBuffer&) = delete("AlignedBuffer owns its storage alone and is move-only");

    AlignedBuffer(AlignedBuffer&&) noexcept = default;
    AlignedBuffer& operator=(AlignedBuffer&&) noexcept = default;

    // The end of the buffer and reset() are constexpr, so a class with an
    // empty buffer member stays a literal type.  No constant evaluation
    // allocates, because allocate() is not constexpr, so it never reaches the
    // free.  The buffer is empty before the release starts.
    constexpr void reset() noexcept { this->reset_(); }

    [[nodiscard]] T* data() noexcept { return this->state_.data; }
    [[nodiscard]] const T* data() const noexcept { return this->state_.data; }

    [[nodiscard]] size_type size() const noexcept { return this->state_.size; }
    [[nodiscard]] size_type bytes() const noexcept { return size() == 0 ? 0 : allocation_bytes(size()); }
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return this->state_.data != nullptr; }

    [[nodiscard]] std::span<T> span() noexcept { return std::span<T>{this->state_.data, this->state_.size}; }
    [[nodiscard]] std::span<const T> span() const noexcept {
        return std::span<const T>{this->state_.data, this->state_.size};
    }

    // The View of the elements.  It borrows this buffer.  The storage is not
    // null for each count above zero, and the allocation door gives no block
    // of more than __PTRDIFF_MAX__ bytes, so the count is at most the largest
    // count of a View.  The View then comes through the door of the base
    // layer, with no check.  The return type is deduced, so a unit that
    // only names the class, such as a concept of a gate, does not read View.
    [[nodiscard]] auto view() & noexcept {
        return core::View<T>{core::detail::ViewDoor::key(), this->state_.data, this->state_.size};
    }
    [[nodiscard]] auto view() const& noexcept {
        return core::View<T const>{core::detail::ViewDoor::key(), this->state_.data, this->state_.size};
    }
    void view() && = delete("a view of a temporary AlignedBuffer points into storage that the full expression frees");
    void
    view() const&& = delete("a view of a temporary AlignedBuffer points into storage that the full expression frees");

    [[nodiscard]] T& operator[](size_type i) noexcept {
        CRUCIBLE_PRE(i < this->state_.size);
        return this->state_.data[i];
    }
    [[nodiscard]] const T& operator[](size_type i) const noexcept {
        CRUCIBLE_PRE(i < this->state_.size);
        return this->state_.data[i];
    }

private:
    explicit AlignedBuffer(T* data, size_type size) noexcept : Core{State{data, size}} {}

    // The allocation door ends the process when the memory is exhausted:
    // this runs where a failed allocation has no recovery.
    [[nodiscard]] static void* allocate_storage_(size_type count) {
        return core::detail::heap_allocate_(allocation_bytes(count), Alignment);
    }
};

}  // namespace foundation
