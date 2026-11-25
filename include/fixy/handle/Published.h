#pragma once

// Published<T, Brand>: an object that the stage that writes it gave out after
// its last write, with the brand of the store of the object.
//
// A Published<T, Brand> refers to one object of T, and it gives the object
// only as const.  Only mint_published makes one, and the mint takes the proof
// of the stage that writes such objects and a Ref of the object.  That stage
// mints a Published after its last write of the object, so no code writes the
// object while a Published names it, and two threads can read it.
//
// The brand is the brand of the Ref, such as the arena family of a graph
// (foundation/core/BumpArena.h).  It is a fresh brand and never the erased
// brand DefaultBrand, and the mint keeps it.  So a Published of one family, or
// of one mint site, does not stand in for a Published of another one.  Two
// stores of one family, or two runs of one mint site, give one type (fact 2 of
// foundation/Brand.h).  No door takes a Published as the key of other memory:
// it gives only its own object.
//
// A Published is a reference to a const T, and nothing more.  Its class
// template takes the const type, and it states refers{} (foundation/Transfer.h),
// so the walk reads it as a reference to its const argument: a Published moves
// to another thread, and two threads read one, when two threads can read a T.
// A class template that only held a Ref of const would ask each property of
// its argument too, because the walk counts the type arguments of a
// specialization.  The mint asks that two threads can read a const T, so each
// Published holds each property.
//
// A proof names the type that its stage publishes with the member alias
// published_type.  A Published owns nothing: the owner of the object keeps
// it alive, and a Published dangles when the owner ends the object.

#include <foundation/Brand.h>
#include <foundation/Transfer.h>
#include <foundation/core/Ref.h>

#include <type_traits>

namespace fixy::handle {

// The proof of the stage publishes objects of T, and two threads can read a
// const T.
template <class Proof, class T>
concept CanPublish = requires { typename Proof::published_type; }
                  && std::is_same_v<typename Proof::published_type, T> && ::foundation::transfer::Shareable<T const>;

template <class Const, ::foundation::brand::IsFreshBrand Brand>
    requires std::is_const_v<Const>
class PublishedObject;

template <class T, ::foundation::brand::IsFreshBrand Brand>
using Published = PublishedObject<T const, Brand>;

template <class Proof, class T, ::foundation::brand::IsFreshBrand Brand>
    requires CanPublish<Proof, std::remove_const_t<T>>
[[nodiscard]] constexpr Published<std::remove_const_t<T>, Brand>
mint_published(Proof const& proof, ::foundation::core::Ref<T, Brand> object) noexcept;

// The key of the constructor of a Published.  Its constructor is private,
// and only mint_published builds one.
class published_key final {
    constexpr published_key() noexcept {}

    template <class Proof, class T, ::foundation::brand::IsFreshBrand Brand>
        requires CanPublish<Proof, std::remove_const_t<T>>
    friend constexpr Published<std::remove_const_t<T>, Brand>
    fixy::handle::mint_published(Proof const& proof, ::foundation::core::Ref<T, Brand> object) noexcept;

public:
    published_key(published_key const&) = delete("a key lives in the call of the mint");
    published_key(published_key&&) = delete("a key lives in the call of the mint");
    published_key& operator=(published_key const&) = delete("a key lives in the call of the mint");
    published_key& operator=(published_key&&) = delete("a key lives in the call of the mint");
    // User-provided, because GCC counts a class whose copy and move are all
    // deleted as trivially copyable, and a bit_cast could then forge a key.
    constexpr ~published_key() {}
};

// The class of a Published<T, Brand>, over the const type T const.
template <class Const, ::foundation::brand::IsFreshBrand Brand>
    requires std::is_const_v<Const>
class[[= ::foundation::transfer::refers{}]] PublishedObject final {
public:
    constexpr PublishedObject(published_key const& /*key*/, ::foundation::core::Ref<Const, Brand> target) noexcept
        : target_{target} {}

    // The object, as const.
    [[nodiscard]] constexpr Const& get() const noexcept { return *target_; }
    [[nodiscard]] constexpr ::foundation::core::Ref<Const, Brand> ref() const noexcept { return target_; }

    // Two Published values are equal when they name one object.
    [[nodiscard]] friend constexpr bool operator==(PublishedObject const& left, PublishedObject const& right) noexcept {
        return ::foundation::core::same_object(left.target_, right.target_);
    }

private:
    ::foundation::core::Ref<Const, Brand> target_;
};

// The Published of an object that the stage of the proof wrote for the last
// time.  The caller calls it after the last write of the object.  The
// Published keeps the brand of the Ref.
template <class Proof, class T, ::foundation::brand::IsFreshBrand Brand>
    requires CanPublish<Proof, std::remove_const_t<T>>
[[nodiscard]] constexpr Published<std::remove_const_t<T>, Brand>
mint_published(Proof const& /*proof*/, ::foundation::core::Ref<T, Brand> object) noexcept {
    ::foundation::core::Ref<std::remove_const_t<T> const, Brand> const target = object;
    return Published<std::remove_const_t<T>, Brand>{published_key{}, target};
}

}  // namespace fixy::handle
