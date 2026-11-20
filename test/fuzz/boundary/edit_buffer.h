#pragma once

// The bytes of one input that a runner edits: the mutations of the runner of
// runner_main.h, and the edits of the property test prop_boundary_harnesses.
//
// EditBuffer holds the bytes of a seed with room for the bytes that the edits
// put in.  An edit at a position flips the bits of a byte, sets a byte,
// inserts bytes, erases a byte or cuts the tail.  An insert and an erase write
// the result into a second buffer and then swap the two, so each edit copies
// each byte one time and no copy overlaps its source.

#include "entry.h"

#include <fixy/Core.h>

#include <cstddef>
#include <utility>

namespace crucible::fuzz::boundary {

// A count of bytes, or the position of a byte in an input.
using ByteCount = ::fixy::Quantity<::fixy::unit::Byte, std::size_t>;

class EditBuffer {
public:
    // A buffer that holds the bytes of seed, with room for room more bytes.
    EditBuffer(::fixy::View<std::byte const> seed, ByteCount room) noexcept
        : front_{::fixy::mint_heap_array_zeroed<std::byte>(test_alloc(), seed.size() + room.raw())},
          back_{::fixy::mint_heap_array_zeroed<std::byte>(test_alloc(), seed.size() + room.raw())},
          count_{seed.size()} {
        ::fixy::copy(front_.view().window(0, count_).expect("the buffer holds the seed"), seed)
            .expect("the window holds the count of the seed");
    }

    // The bytes of the input.
    [[nodiscard]] ::fixy::View<std::byte const> view() const& noexcept {
        return ::fixy::View<std::byte const>{front_.view()}.window(0, count_).expect("the count fits the buffer");
    }
    void view() const&& = delete("the view of a temporary buffer dangles at the end of the full expression");

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] bool is_empty() const noexcept { return count_ == 0; }

    // The byte at a position below size(), xor mask.
    void xor_at(ByteCount at, std::byte mask) noexcept { *byte_at_(at) ^= mask; }

    // The byte at a position below size() becomes value.
    void set_at(ByteCount at, std::byte value) noexcept { *byte_at_(at) = value; }

    // Puts the bytes of part before the byte at a position of at most size().
    // Complexity: one copy of each byte.
    void insert_at(ByteCount at, ::fixy::View<std::byte const> part) noexcept {
        std::size_t const position = at.raw();
        std::size_t const tail = count_ - position;
        ::fixy::View<std::byte> const target = back_.view();
        ::fixy::View<std::byte const> const source = view();
        ::fixy::copy(target.window(0, position).expect("the head fits the buffer"),
                     source.window(0, position).expect("an insert is at a position of the input"))
            .expect("the two heads hold one count");
        ::fixy::copy(target.window(position, part.size()).expect("the buffer has room for the part"), part)
            .expect("the window holds the count of the part");
        ::fixy::copy(target.window(position + part.size(), tail).expect("the tail fits the buffer"),
                     source.window(position, tail).expect("the tail is in the input"))
            .expect("the two tails hold one count");
        swap_();
        count_ += part.size();
    }

    // Removes the byte at a position below size().  Complexity: one copy of
    // each byte.
    void erase_at(ByteCount at) noexcept {
        std::size_t const position = at.raw();
        std::size_t const tail = count_ - position - 1;
        ::fixy::View<std::byte> const target = back_.view();
        ::fixy::View<std::byte const> const source = view();
        ::fixy::copy(target.window(0, position).expect("the head fits the buffer"),
                     source.window(0, position).expect("an erase is at a position of the input"))
            .expect("the two heads hold one count");
        ::fixy::copy(target.window(position, tail).expect("the tail fits the buffer"),
                     source.window(position + 1, tail).expect("the tail is in the input"))
            .expect("the two tails hold one count");
        swap_();
        count_ -= 1;
    }

    // Keeps the bytes before a position of at most size().
    void truncate_to(ByteCount length) noexcept {
        if (length.raw() > count_) ::fixy::fatal("a truncate keeps at most the bytes of the input");
        count_ = length.raw();
    }

private:
    [[nodiscard]] ::fixy::Ref<std::byte> byte_at_(ByteCount at) noexcept {
        if (at.raw() >= count_) ::fixy::fatal("an edit of a byte is at a position of the input");
        return front_.view().get(at.raw()).expect("a position of the input is in the buffer");
    }

    void swap_() noexcept {
        ::fixy::HeapArray<std::byte> held = std::move(front_);
        front_ = std::move(back_);
        back_ = std::move(held);
    }

    ::fixy::HeapArray<std::byte> front_;
    ::fixy::HeapArray<std::byte> back_;
    std::size_t count_ = 0;
};

}  // namespace crucible::fuzz::boundary
