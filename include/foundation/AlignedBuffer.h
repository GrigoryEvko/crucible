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

#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>
#include <foundation/diag/Catalog.h>

#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>

namespace foundation {

// The size of one huge page on the supported targets: x86-64, and aarch64
// with 4 KiB base pages.
inline constexpr std::size_t huge_page_bytes = std::size_t{2} << 20;

namespace detail {

[[noreturn]] CRUCIBLE_COLD inline void aligned_allocation_failed_abort_(std::size_t alloc_bytes,
                                                                        std::size_t alignment) noexcept {
    std::fprintf(stderr, "crucible: fatal: aligned_alloc(alignment=%zu, bytes=%zu) returned nullptr\n", alignment,
                 alloc_bytes);
    if (alignment >= huge_page_bytes) {
        using Tag = ::foundation::diag::HugePageAllocationFailed;
        std::fprintf(stderr, "  %.*s: %.*s\n  remediation: %.*s\n", static_cast<int>(Tag::name.size()),
                     Tag::name.data(), static_cast<int>(Tag::description.size()), Tag::description.data(),
                     static_cast<int>(Tag::remediation.size()), Tag::remediation.data());
    }
    std::abort();
}

}  // namespace detail

template <typename T, std::size_t Alignment = alignof(T)>
    requires(std::is_object_v<T> && std::is_trivially_destructible_v<T> && std::has_single_bit(Alignment)
             && Alignment >= alignof(T))
class [[nodiscard]] AlignedBuffer {
public:
    using value_type = T;
    using size_type = std::size_t;

    static constexpr size_type alignment = Alignment;

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::AlignedBuffer"; }

    constexpr AlignedBuffer() noexcept = default;

    // The bytes the allocation of `count` elements occupies: the element
    // bytes rounded up to the alignment, which aligned_alloc requires.  Both
    // steps wrap on a count no real buffer has, and a wrap would hand back a
    // buffer smaller than asked for, so a wrap aborts.
    [[nodiscard]] static constexpr size_type allocation_bytes(size_type count) noexcept {
        size_type raw_bytes = 0;
        if (__builtin_mul_overflow(count, sizeof(T), &raw_bytes)) [[unlikely]]
            std::abort();
        size_type padded = 0;
        if (__builtin_add_overflow(raw_bytes, Alignment - 1, &padded)) [[unlikely]]
            std::abort();
        return padded & ~(Alignment - 1);
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
    // the values it declares, and every other T reads zero.
    [[nodiscard]] static AlignedBuffer allocate_value_initialized(size_type count)
        requires std::is_nothrow_default_constructible_v<T>
    {
        if (count == 0) [[unlikely]]
            return AlignedBuffer{};
        T* const first = static_cast<T*>(allocate_storage_(count));
        std::uninitialized_value_construct_n(first, count);
        return AlignedBuffer{first, count};
    }

    AlignedBuffer(const AlignedBuffer&) = delete("AlignedBuffer owns its storage alone and is move-only");
    AlignedBuffer& operator=(const AlignedBuffer&) = delete("AlignedBuffer owns its storage alone and is move-only");

    AlignedBuffer(AlignedBuffer&& other) noexcept : data_{other.data_}, size_{other.size_} {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept {
        if (this != &other) {
            reset();
            data_ = other.data_;
            size_ = other.size_;
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    ~AlignedBuffer() noexcept { reset(); }

    void reset() noexcept {
        if (data_ != nullptr) {
            std::free(data_);
            data_ = nullptr;
            size_ = 0;
        }
    }

    [[nodiscard]] T* data() noexcept { return data_; }
    [[nodiscard]] const T* data() const noexcept { return data_; }

    [[nodiscard]] size_type size() const noexcept { return size_; }
    [[nodiscard]] size_type bytes() const noexcept { return size_ == 0 ? 0 : allocation_bytes(size_); }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return data_ != nullptr; }

    [[nodiscard]] std::span<T> span() noexcept { return std::span<T>{data_, size_}; }
    [[nodiscard]] std::span<const T> span() const noexcept { return std::span<const T>{data_, size_}; }

    [[nodiscard]] T& operator[](size_type i) noexcept {
        CRUCIBLE_PRE(i < size_);
        return data_[i];
    }
    [[nodiscard]] const T& operator[](size_type i) const noexcept {
        CRUCIBLE_PRE(i < size_);
        return data_[i];
    }

private:
    explicit AlignedBuffer(T* data, size_type size) noexcept : data_{data}, size_{size} {}

    [[nodiscard]] static void* allocate_storage_(size_type count) {
        const size_type bytes = allocation_bytes(count);
        void* const raw = std::aligned_alloc(Alignment, bytes);
        if (raw == nullptr) [[unlikely]]
            detail::aligned_allocation_failed_abort_(bytes, Alignment);
        return raw;
    }

    T* data_ = nullptr;
    size_type size_ = 0;
};

namespace detail::aligned_buffer_self_test {

// The shape of a proof: every constructor is user-provided, so the class is
// not an implicit-lifetime type.
class ProofShape {
public:
    ProofShape(const ProofShape&) noexcept {}

private:
    ProofShape() noexcept {}
};
struct HoldsProof {
    ProofShape proof;
};

// A count that refuses a start over bytes and builds from its own default
// constructor, the shape of a provenance tag.
struct [[=::foundation::lifetime::no_start_over_bytes{}]] MarkedCount {
    unsigned long long count = 0;
};

template <typename T, std::size_t Alignment = alignof(T)>
concept can_buffer = requires { typename AlignedBuffer<T, Alignment>; };

template <typename T>
concept can_allocate = requires { AlignedBuffer<T>::allocate(std::size_t{1}); };

template <typename T>
concept can_value_initialize = requires { AlignedBuffer<T>::allocate_value_initialized(std::size_t{1}); };

static_assert(sizeof(AlignedBuffer<int>) == sizeof(void*) + sizeof(std::size_t));
static_assert(!std::is_copy_constructible_v<AlignedBuffer<int>> && !std::is_copy_assignable_v<AlignedBuffer<int>>);
static_assert(std::is_nothrow_move_constructible_v<AlignedBuffer<int>>);
static_assert(std::is_nothrow_move_assignable_v<AlignedBuffer<int>>);
static_assert(can_buffer<int> && can_buffer<unsigned char, 4096> && can_buffer<double, huge_page_bytes>);
static_assert(can_allocate<int> && can_value_initialize<int>);
static_assert(!can_allocate<HoldsProof> && !can_value_initialize<HoldsProof>,
              "a proof element can start its lifetime by neither route");
static_assert(!can_allocate<MarkedCount> && can_value_initialize<MarkedCount>,
              "a class that refuses a start over bytes builds from its own default constructor");
static_assert(!can_buffer<int, 3> && !can_buffer<double, 4>, "the alignment is a power of two, at least alignof(T)");
static_assert(!can_buffer<int&>, "an element is an object type");
static_assert(AlignedBuffer<int>::allocation_bytes(3) == 12);
static_assert(AlignedBuffer<int, 64>::allocation_bytes(3) == 64);
static_assert(AlignedBuffer<unsigned char, huge_page_bytes>::allocation_bytes(1) == huge_page_bytes);

}  // namespace detail::aligned_buffer_self_test

}  // namespace foundation
