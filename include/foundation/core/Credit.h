#pragma once

// The credit of the Ref family: Hole<Size, Align>, the heap storage of one
// object with no object in it.
//
// A Hole owns Size bytes of heap storage at the alignment Align.  It comes
// from two mints:
//
//   mint_hole<Size, Align>(alloc)   takes the storage from the allocation
//                                   door of foundation/core/Heap.h.  The
//                                   argument is the allocation capability.
//   mint_hole(std::move(box))       ends the object of a Box and keeps its
//                                   storage, with no call of the door.
//
// mint_box<T>(std::move(hole), args...) of foundation/core/Ref.h builds a T
// in the storage of a Hole, with no call of the door.  The Hole must have
// the exact size and alignment of T, so the Box gives back the same storage
// that the Hole holds.  consume() gives the storage back to the door.  So N
// steps from a Box to a Hole and back call the door one time.
//
// A Hole is linear: each path uses a Hole exactly one time, and the rule
// must-consume of the quarantine plugin reports a path that does not.
// consume() is its drop door.  The destructor also gives the storage back,
// so a Hole that a path drops does not leak.
//
// A Hole is move-only.  A move leaves the source empty.  C++ keeps a
// moved-from object alive, so each spend of a Hole tests for the storage
// in each build, and an empty Hole ends the process through fatal().  The
// test is one compare with a branch to a cold call.  The annotation
// no_start_over_bytes refuses a Hole to the checked lifetime start, so no
// bytes become a Hole that frees storage that it does not own.
//
// The constructors of a Hole take the key that only HoleDoor builds, and
// only the two mints call the door.  The door and the key hold no template.
// No constructor of a Hole takes a pointer: one allocates the storage, and
// one takes the storage of a Box.  Box and Hole are the friends of each
// other, and neither has a different friend.  An explicit specialization of
// Box or of Hole, or of a member of one, is a specialization outside its
// home, and the rule specialization-home refuses it in each build.

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/core/Heap.h>
#include <foundation/core/Report.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace foundation::core {

// The object of a Box: a complete object type that is not an array, not
// cv-qualified and not abstract, with a destructor that does not throw.
template <class T>
concept BoxPayload = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>
                  && !std::is_abstract_v<T> && std::is_nothrow_destructible_v<T>;

// Box is defined in foundation/core/Ref.h, which includes this header.
template <BoxPayload T>
class Box;

// The size and the alignment of a Hole: a size of at least one byte, and an
// alignment that is a power of two.
template <std::size_t Size, std::size_t Align>
concept HoleShape = Size > 0 && Align > 0 && (Align & (Align - 1)) == 0;

namespace detail {

// The door of the two mints of a Hole, and the key of the constructors of a
// Hole.  HoleDoor is the one friend of the key.  The door and the key hold
// no template, so no explicit specialization of a template reaches the key.
// A use of namespace detail outside the base gets only a Hole of new
// storage, or a Hole of the storage of a Box that it gives up.
struct HoleDoor;

class [[nodiscard]] HoleKey final {
    friend struct HoleDoor;

    constexpr HoleKey() noexcept {}
    constexpr HoleKey(HoleKey const&) noexcept {}

public:
    HoleKey& operator=(HoleKey const&) = delete("a key of a Hole lives for one construction");
};

struct HoleDoor {
    [[nodiscard]] static constexpr HoleKey key() noexcept { return HoleKey{}; }
};

}  // namespace detail

template <std::size_t Size, std::size_t Align>
    requires HoleShape<Size, Align>
class [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] Hole {
    void* storage_ = nullptr;

    template <BoxPayload T>
    friend class Box;

    // The storage of a Hole that a path spends.  A moved-from or spent Hole
    // holds none, and each build tests it here.
    [[nodiscard]] void* held_() const noexcept {
        if (storage_ == nullptr) [[unlikely]] {
            fatal("a spend of a Hole that holds no storage: the Hole was moved from or spent");
        }
        return storage_;
    }

    // Gives the storage to a Box, which builds its object in it.  The Hole
    // is then empty.
    [[nodiscard]][[= ::foundation::linear::detach_door{}]] void* detach_() && noexcept {
        void* const storage = held_();
        storage_ = nullptr;
        return storage;
    }

public:
    // Takes Size bytes at the alignment Align from the allocation door.
    // Only mint_hole holds the key.
    explicit Hole(detail::HoleKey const& /*key*/) noexcept : storage_{detail::heap_allocate_(Size, Align)} {}

    // Ends the object of a Box of the exact size and alignment, and takes
    // its storage.  The Box is then empty.  Only mint_hole holds the key.
    template <BoxPayload T>
        requires(sizeof(T) == Size && alignof(T) == Align)
    explicit Hole(detail::HoleKey const& /*key*/, Box<T>&& box) noexcept : storage_{box.vacate_()} {}

    Hole(Hole&& other) noexcept : storage_{other.storage_} { other.storage_ = nullptr; }

    Hole& operator=(Hole&& other) noexcept {
        if (this != &other) {
            if (storage_ != nullptr) {
                detail::heap_release_(storage_, Size, Align);
            }
            storage_ = other.storage_;
            other.storage_ = nullptr;
        }
        return *this;
    }

    Hole(Hole const&) = delete("a Hole owns its storage alone: a copy would free the storage two times");
    Hole& operator=(Hole const&) = delete("a Hole owns its storage alone: a copy would free the storage two times");

    ~Hole() {
        if (storage_ != nullptr) {
            detail::heap_release_(storage_, Size, Align);
        }
    }

    // Gives the storage back to the allocation door.  The Hole is then
    // empty.
    [[= ::foundation::linear::drop_door{}]] void consume() && noexcept {
        detail::heap_release_(held_(), Size, Align);
        storage_ = nullptr;
    }
};

// The gate of the mint of a Hole from the allocation capability.
template <std::size_t Size, std::size_t Align, class Marker>
concept CanMintHole = HoleShape<Size, Align> && AllocationCapability<Marker>;

// A Hole of new storage from the allocation door.  The door never gives a
// null pointer.
template <std::size_t Size, std::size_t Align, class Marker>
    requires CanMintHole<Size, Align, Marker>
[[nodiscard]] Hole<Size, Align> mint_hole(Marker /*allocation*/) noexcept {
    return Hole<Size, Align>{detail::HoleDoor::key()};
}

// A Hole of the storage of a Box that the caller gives up.  The object of
// the Box ends, and its storage stays with no call of the door.  A Box that
// holds no object ends the process through fatal().
template <BoxPayload T>
[[nodiscard]] Hole<sizeof(T), alignof(T)> mint_hole(Box<T>&& box) noexcept {
    return Hole<sizeof(T), alignof(T)>{detail::HoleDoor::key(), std::move(box)};
}

}  // namespace foundation::core
