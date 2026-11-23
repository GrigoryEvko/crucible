#pragma once

// The Immortal family: one object of the whole process, whose destructor
// never runs.
//
//   Immortal<T>          storage for one T.  A constant initializer gives an
//                        empty holder, and the destructor of the holder is
//                        trivial
//   holder.fill(args)    builds the T in the storage, one time
//   holder.get()         a Ref to the T, with the fresh brand of the call
//   holder.is_filled()   true after the fill
//
// Storage
//   An Immortal holds aligned storage for one T and the address of the T in
//   it.  An empty holder is zero bytes, so a constinit variable of it runs no
//   code at the start of the process.  The destructor of the holder is
//   trivial, and it never runs the destructor of T, so the storage of T is
//   never released.  So an Immortal is only for an object of the whole
//   process, such as the window of the root of the allocator: its
//   destructor would unmap the window while code at the end of the process
//   still frees blocks of it.  A process-wide variable with a destructor also
//   makes each unit that holds it register the destructor with the C library,
//   and a holder makes no such call.
//
// Rules
//   fill builds the T one time, and a second fill ends the process.  get() of
//   an empty holder ends the process.  Each check is one test of the address
//   and a branch to a cold call of the fatal exit, as each access of a Box
//   has, so get() is one load of the address and its test.  One thread fills
//   the holder, and each reader reads it after a publish that happens after
//   the fill, such as the state word of the root.  The holder does not copy
//   or move, because the address of the T is in its storage.

#include <foundation/Brand.h>
#include <foundation/NoObject.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Ref.h>

#include <new>
#include <type_traits>

namespace foundation::core {

// A type that an Immortal holds: an object type that is not an array, not
// const and not volatile.
template <class T>
concept ImmortalPayload = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>;

template <ImmortalPayload T>
class Immortal;

// The door of an Immortal to the door of a Ref (the doors of the owners above
// foundation/core/Ref.h).  Its one member is private, and Immortal is its one
// friend.  The invariant: get() lends the object only after the test of the
// fill, the object lives for the whole process, and the brand is the fresh
// brand of the call of get().  A home owner row of utils/rule-table.txt keeps
// this definition in this header.
class ImmortalRefDoor final : ::foundation::NoObject<ImmortalRefDoor> {
    template <ImmortalPayload T>
    friend class Immortal;

    // The object of a filled holder, as a Ref of the brand.
    template <::foundation::brand::IsBrand Brand, RefTarget T>
    [[nodiscard]] CRUCIBLE_INLINE static constexpr Ref<T, Brand> lend_(T& object) noexcept {
        return detail::RefDoor::lend<Brand>(object);
    }
};

template <ImmortalPayload T>
class Immortal final {
public:
    // The empty holder (the storage).
    constexpr Immortal() noexcept = default;

    Immortal(Immortal const&) = delete("the object of an Immortal lives in its storage for the whole process");
    Immortal&
    operator=(Immortal const&) = delete("the object of an Immortal lives in its storage for the whole process");

    // Trivial, so the destructor of T never runs (the storage).
    ~Immortal() = default;

    // Builds the T from the arguments in the storage.  A holder that holds a
    // T ends the process (the rules).
    template <class... Args>
        requires std::is_nothrow_constructible_v<T, Args&&...>
    void fill(Args&&... args) noexcept {
        if (object_ != nullptr) [[unlikely]] {
            fatal("an Immortal holds one object for the process, and a second fill would build another one over it");
        }
        object_ = ::new(static_cast<void*>(storage_)) T(static_cast<Args&&>(args)...);
    }

    [[nodiscard]] constexpr bool is_filled() const noexcept { return object_ != nullptr; }

    // The T as a Ref with the fresh brand of the call.  An empty holder ends
    // the process (the rules).  The pack refuses each explicit argument, so
    // no caller names the brand.
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] Ref<T, ::foundation::brand::undeducible_brand_t<Fresh>> get() noexcept {
        return ImmortalRefDoor::lend_<Fresh>(*held_());
    }
    template <class... Refused, ::foundation::brand::IsBrand Fresh = CRUCIBLE_FRESH_BRAND>
        requires(sizeof...(Refused) == 0)
    [[nodiscard]] Ref<T const, ::foundation::brand::undeducible_brand_t<Fresh>> get() const noexcept {
        T const& object = *held_();
        return ImmortalRefDoor::lend_<Fresh>(object);
    }

private:
    [[nodiscard, gnu::always_inline]] T* held_() const noexcept {
        if (object_ == nullptr) [[unlikely]] {
            fatal("a read of an Immortal came before its fill, so the holder has no object to give");
        }
        return object_;
    }

    alignas(T) unsigned char storage_[sizeof(T)]{};
    T* object_ = nullptr;
};

}  // namespace foundation::core
