#pragma once

#include <crucible/Platform.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

template <typename T, std::size_t Alignment = alignof(T)>
class [[nodiscard]] AlignedBuffer {
    static_assert(Alignment >= alignof(T), "AlignedBuffer Alignment must be >= alignof(T)");
    static_assert((Alignment & (Alignment - 1)) == 0, "AlignedBuffer Alignment must be a power of two");

public:
    using value_type = T;
    using size_type = std::size_t;

    static constexpr size_type alignment = Alignment;

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::AlignedBuffer"; }

    constexpr AlignedBuffer() noexcept = default;

    // Both byte computations wrap silently on a large count, and each wrap
    // hands back a buffer smaller than the caller asked for, which the caller
    // then overruns.  Neither can happen for a real count of T, so both abort.
    // Exhaustion aborts as well: this runs where a failed allocation has no
    // recovery.
    [[nodiscard]] static AlignedBuffer allocate(size_type count) {
        if (count == 0) [[unlikely]]
            return AlignedBuffer{};
        size_type bytes_raw = 0;
        if (__builtin_mul_overflow(count, sizeof(T), &bytes_raw)) [[unlikely]]
            std::abort();
        // aligned_alloc requires size to be a multiple of alignment.
        size_type bytes = 0;
        if (__builtin_add_overflow(bytes_raw, Alignment - 1, &bytes)) [[unlikely]]
            std::abort();
        bytes &= ~(Alignment - 1);
        void* raw = std::aligned_alloc(Alignment, bytes);
        if (!raw) [[unlikely]]
            std::abort();
        return AlignedBuffer{static_cast<T*>(raw), count};
    }

    // Value-initializes each element rather than zeroing the bytes, so a T with
    // member initializers gets the values it declares.  Zeroing a type like that
    // is also a compiler diagnostic.
    [[nodiscard]] static AlignedBuffer allocate_zeroed(size_type count) {
        AlignedBuffer buf = allocate(count);
        if (buf.data_ != nullptr) {
            for (size_type i = 0; i < count; ++i) {
                ::new(static_cast<void*>(buf.data_ + i)) T{};
            }
        }
        return buf;
    }

    AlignedBuffer(const AlignedBuffer&) = delete("AlignedBuffer is move-only");
    AlignedBuffer& operator=(const AlignedBuffer&) = delete("AlignedBuffer is move-only");

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
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] explicit operator bool() const noexcept { return data_ != nullptr; }

    [[nodiscard]] std::span<T> span() noexcept { return std::span<T>{data_, size_}; }
    [[nodiscard]] std::span<const T> span() const noexcept { return std::span<const T>{data_, size_}; }

    [[nodiscard]] T& operator[](size_type i) noexcept { return data_[i]; }
    [[nodiscard]] const T& operator[](size_type i) const noexcept { return data_[i]; }

private:
    explicit AlignedBuffer(T* p, size_type n) noexcept : data_{p}, size_{n} {}

    T* data_ = nullptr;
    size_type size_ = 0;
};

static_assert(sizeof(AlignedBuffer<int>) == sizeof(void*) + sizeof(std::size_t));
static_assert(!std::is_copy_constructible_v<AlignedBuffer<int>>);
static_assert(std::is_nothrow_move_constructible_v<AlignedBuffer<int>>);

}  // namespace crucible::safety
