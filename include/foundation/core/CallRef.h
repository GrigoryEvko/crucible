#pragma once

// A reference to a callable of one signature, with no type of the callable
// in its own type.
//
//   CallRef<R(Args...) noexcept>{callable}
//                       the callable object that a name holds
//   CallRef<R(Args...) noexcept>::of<&function>()
//                       a function
//   CallRef<R(Args...) noexcept>::bind<&Class::member>(object)
//                       a member function of an object that a name holds
//   call_ref(args...)   calls the callable with the arguments
//   mint_erased_address(object)
//                       the erased address of an object, which a thunk of
//                       its type reads back (the erased address)
//
// Shape
//     A CallRef is two words: the erased address of the callable, and a
//     thunk that calls the callable at that address.  It owns nothing, it
//     allocates nothing, and it has no virtual call.  The signature is
//     noexcept, so a callable that can throw does not fit.  A call is one
//     indirect call through the thunk, and the thunk inlines the call of the
//     callable.
//
// Borrow
//     A CallRef borrows its callable, as a Ref borrows its object.  It
//     refuses the one shape that always dangles: a callable or an object
//     that is a temporary.  The constructor and bind of an rvalue are
//     deleted.  A CallRef has no copy and no move, and no new-expression
//     builds one, so it lives where its initializer builds it: in a frame, or
//     in a member of an object that the frame holds.  A function, as of<>
//     gives it, has no object, so it never dangles.
//
//     What stays open, stated rather than implied: a member of an object
//     that outlives the callable can still hold a CallRef, and a call
//     through it then reads an object that ended.  The holder keeps the
//     callable alive while a CallRef of it can run.
//
// Scope brand
//     A CallRef erases the type of its callable, so its own type names no
//     brand of the callable.  The rule brand-escape reads the brands in the
//     type of an object, so it cannot see a scope brand behind a CallRef.  A
//     static CallRef of a function of a library would then call the function
//     after the scope of with_library closed the library.  So the
//     constructor and bind refuse a callable or an object that holds a scope
//     brand (foundation/Brand.h), as erase_brand of foundation/core/Ref.h
//     refuses a scope brand.
//
//     The walk of the gate reads the template arguments of each class, and
//     the members and the bases of each local class: a closure, or a class
//     that a function defines, also inside a class of that function.  A
//     class of a namespace cannot name a scope brand in a member, because
//     only the body of a scope mint can name its brand.  So the walk reads no
//     member of such a class, and a CallRef of a large object costs a read of
//     its template arguments only.  The walk goes through references,
//     pointers and arrays.  It has no depth limit, so no brand is too deep
//     for it.  It reads each class one time, so it ends on a local class
//     that points to itself.
//
// The gates of the shape are the builtins of the compiler.  The gate of the
// scope brand reads the type by reflection.

#include <foundation/ByteSeal.h>
#include <foundation/core/Ref.h>

#include <cstddef>
#include <meta>

namespace foundation::core {

namespace detail {

// The type with no reference, no cv-qualifier, no pointer and no array
// extent.  Complexity: linear in the pointers and extents of the type.
[[nodiscard]] consteval std::meta::info scope_brand_core_(std::meta::info type) {
    std::meta::info bare = std::meta::dealias(std::meta::remove_cvref(type));
    while (std::meta::is_pointer_type(bare) || std::meta::is_array_type(bare)) {
        std::meta::info const inner =
            std::meta::is_pointer_type(bare) ? std::meta::remove_pointer(bare) : std::meta::remove_all_extents(bare);
        bare = std::meta::dealias(std::meta::remove_cv(inner));
    }
    return bare;
}

// True when a function defines the class, directly or through the classes
// that enclose it (scope brand).  Complexity: linear in the classes that
// enclose the class.
[[nodiscard]] consteval bool is_local_class_(std::meta::info type) {
    std::meta::info scope = std::meta::parent_of(type);
    while (!std::meta::is_namespace(scope)) {
        if (std::meta::is_function(scope)) return true;
        scope = std::meta::parent_of(scope);
    }
    return false;
}

// True when type holds a scope brand at a step of the walk of the gate
// (scope brand).  A scope brand is the closure type of CRUCIBLE_SCOPE_BRAND,
// which has no name.  The walk keeps a list of the types that it must read,
// and a list of each class that it read, so it reads each class one time.
// Each loop reads its list of reflection one time and indexes that copy, as
// names_scope_brand does.  Complexity: the template arguments of each class,
// and the members and bases of each local class, times the scan of the list
// of read classes, at compile time only.
[[nodiscard]] consteval bool holds_scope_brand_(std::meta::info type) {
    std::meta::access_context const context = std::meta::access_context::unchecked();
    std::vector<std::meta::info> pending{type};
    std::vector<std::meta::info> read;
    while (!pending.empty()) {
        std::meta::info const bare = scope_brand_core_(pending.back());
        pending.pop_back();
        if (!std::meta::is_class_type(bare)) continue;
        bool is_read = false;
        for (std::size_t index = 0; index < read.size() && !is_read; ++index)
            is_read = read[index] == bare;
        if (is_read) continue;
        read.push_back(bare);
        if (!std::meta::has_identifier(bare) && names_scope_brand(bare)) return true;
        if (std::meta::has_template_arguments(bare)) {
            std::vector<std::meta::info> const arguments = std::meta::template_arguments_of(bare);
            for (std::size_t index = 0; index < arguments.size(); ++index) {
                if (std::meta::is_type(arguments[index])) pending.push_back(arguments[index]);
            }
        }
        if (!std::meta::is_complete_type(bare) || !is_local_class_(bare)) continue;
        std::vector<std::meta::info> const members = std::meta::nonstatic_data_members_of(bare, context);
        for (std::size_t index = 0; index < members.size(); ++index)
            pending.push_back(std::meta::type_of(members[index]));
        std::vector<std::meta::info> const bases = std::meta::bases_of(bare, context);
        for (std::size_t index = 0; index < bases.size(); ++index)
            pending.push_back(std::meta::type_of(bases[index]));
    }
    return false;
}

}  // namespace detail

// A type that holds no scope brand, so a CallRef can erase it (scope brand).
template <class T>
concept HoldsNoScopeBrand = !detail::holds_scope_brand_(^^T);

// A result of a call that the signature with the result R can give back:
// any result for void, and a result that converts to R otherwise.
template <class Result, class R>
concept CallResultFits = __is_same(R, void) || __is_convertible(Result, R);

// A callable object that a CallRef of R(Args...) noexcept can call: an
// object type that is not const and not volatile, whose call with the
// arguments does not throw and gives a result that fits.
template <class F, class R, class... Args>
concept CallRefTarget = __is_object(F) && !__is_const(F) && !__is_volatile(F) && requires(F& callable, Args&&... args) {
    { callable(static_cast<Args&&>(args)...) } noexcept -> CallResultFits<R>;
};

// A function that a CallRef of R(Args...) noexcept can call.
template <auto Function, class R, class... Args>
concept CallRefFunction = requires(Args&&... args) {
    { Function(static_cast<Args&&>(args)...) } noexcept -> CallResultFits<R>;
};

// A member function of T that a CallRef of R(Args...) noexcept can call on
// an object of T.
template <auto Member, class T, class R, class... Args>
concept CallRefMember = __is_object(T) && requires(T& object, Args&&... args) {
    { (object.*Member)(static_cast<Args&&>(args)...) } noexcept -> CallResultFits<R>;
};

// An object whose address an ErasedAddress can hold: an object type that is
// not an array, not const and not volatile, and no type of bytes.  The
// address of bytes erases no type, and a const object gives a pointer that
// only reads.
template <class T>
concept ErasableObject =
    __is_object(T) && !__is_array(T) && !__is_const(T) && !__is_volatile(T) && !__is_same(T, char)
    && !__is_same(T, signed char) && !__is_same(T, unsigned char) && !__is_same(T, char8_t) && !__is_same(T, std::byte);

class ErasedAddress;

template <class T>
    requires ErasableObject<T>
[[= erasure_door{}]] [[nodiscard]] constexpr ErasedAddress mint_erased_address(T& object) noexcept;

// Erased address
//   The address of an object whose type a holder erases: the callable of a
//   CallRef, or the frame of a task of a thread.  The address is private.
//   Only mint_erased_address builds one from an object, and only restore<T>()
//   reads it back, as a reference to an object of the type T.  So the holder
//   keeps no pointer to void that can write the object: a copy of bytes,
//   read() and recv() take no erased address.  The quarantine plugin reads
//   the conversion in the mint as no view of bytes (the mark erasure_door of
//   foundation/ByteSeal.h), and it refuses each other view of the bytes of a
//   type that holds the tag of a sum.
//
//   The holder calls restore<T>() only with the type that it erased, in the
//   thunk of that type, as a cast back of a pointer to void does.  A different
//   type, or a restore of the empty address that the default constructor
//   gives, is undefined.  The seal refuses std::bit_cast and a lifetime start
//   over bytes, so no address comes from bytes, and the ABI still passes the
//   address in one register.
class [[nodiscard]] ErasedAddress final {
    void* address_ = nullptr;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr explicit ErasedAddress(void* address) noexcept : address_{address} {}

    template <class T>
        requires ErasableObject<T>
    friend constexpr ErasedAddress mint_erased_address(T& object) noexcept;

public:
    // The empty address, of a holder that holds no object, such as a CallRef
    // of a function.
    constexpr ErasedAddress() noexcept = default;

    // The object at the address, as the type T that the holder erased.
    template <class T>
        requires ErasableObject<T>
    [[nodiscard]] constexpr T& restore() const noexcept {
        return *static_cast<T*>(address_);
    }
};

// The erased address of an object that a name holds.
template <class T>
    requires ErasableObject<T>
[[= erasure_door{}]] [[nodiscard]] constexpr ErasedAddress mint_erased_address(T& object) noexcept {
    return ErasedAddress{static_cast<void*>(__builtin_addressof(object))};
}

template <class Signature>
class CallRef;

template <class R, class... Args>
class [[nodiscard]] CallRef<R(Args...) noexcept> final {
public:
    // The callable that a name holds, which holds no scope brand.  The address
    // of the callable is the address of the reference.
    template <class F>
        requires CallRefTarget<F, R, Args...> && HoldsNoScopeBrand<F> && (!__is_same(F, CallRef))
    constexpr CallRef(F& callable) noexcept : object_{mint_erased_address(callable)}, thunk_{&call_object_<F>} {}

    // A callable that is a temporary ends at the end of the full expression.
    template <class F>
        requires(!__is_reference(F))
    CallRef(F&&) = delete("a CallRef of a temporary callable dangles at the end of the full expression.  Give the "
                          "CallRef a callable that a name holds");

    // A callable that holds a scope brand (scope brand).
    template <class F>
        requires(__is_object(F) && !HoldsNoScopeBrand<F>)
    CallRef(F&) = delete("a CallRef erases the type of its callable, so the rule brand-escape cannot read a scope "
                         "brand behind it, and a static CallRef would keep a value of the scope past the end of the "
                         "scope.  Call the callable in the scope of its brand");

    // A function, which has no object.
    template <auto Function>
        requires CallRefFunction<Function, R, Args...>
    [[nodiscard]] static constexpr CallRef of() noexcept {
        return CallRef{ErasedAddress{}, &call_function_<Function>};
    }

    // A member function of an object that a name holds, which holds no scope
    // brand.
    template <auto Member, class T>
        requires CallRefMember<Member, T, R, Args...> && HoldsNoScopeBrand<T>
    [[nodiscard]] static constexpr CallRef bind(T& object) noexcept {
        return CallRef{mint_erased_address(object), &call_member_<Member, T>};
    }

    template <auto Member, class T>
        requires(!__is_reference(T))
    static void bind(T&&) = delete("a CallRef of a member of a temporary object dangles at the end of the full "
                                   "expression.  Give bind an object that a name holds");

    // An object that holds a scope brand (scope brand).
    template <auto Member, class T>
        requires(__is_object(T) && !HoldsNoScopeBrand<T>)
    static void bind(T&) = delete("a CallRef erases the type of the object of its member, so the rule brand-escape "
                                  "cannot read a scope brand behind it, and a static CallRef would keep a value of "
                                  "the scope past the end of the scope.  Call the member in the scope of its brand");

    CallRef(CallRef const&) = delete("a CallRef lives where its initializer builds it.  A copy could outlive the "
                                     "callable");
    CallRef(CallRef&&) = delete("a CallRef lives where its initializer builds it.  A move could carry it out of the "
                                "frame");
    CallRef& operator=(CallRef const&) = delete("a CallRef refers to one callable.  Assignment would rebind it");
    CallRef& operator=(CallRef&&) = delete("a CallRef refers to one callable.  Assignment would rebind it");

    static void* operator new(decltype(sizeof(0))) = delete("a CallRef lives in a frame, not on the heap");
    static void* operator new[](decltype(sizeof(0))) = delete("a CallRef lives in a frame, not on the heap");

    // Calls the callable with the arguments.
    constexpr R operator()(Args... args) const noexcept { return thunk_(object_, static_cast<Args&&>(args)...); }

private:
    // The thunk takes each argument by reference, so an argument of a type
    // that cannot move reaches the callable with no move.
    using Thunk = R (*)(ErasedAddress, Args&&...) noexcept;

    constexpr CallRef(ErasedAddress object, Thunk thunk) noexcept : object_{object}, thunk_{thunk} {}

    // A signature with the result void drops the result of the callable.
    template <class F>
    static constexpr R call_object_(ErasedAddress object, Args&&... args) noexcept {
        if constexpr (__is_same(R, void)) {
            static_cast<void>(object.restore<F>()(static_cast<Args&&>(args)...));
        } else {
            return object.restore<F>()(static_cast<Args&&>(args)...);
        }
    }

    template <auto Function>
    static constexpr R call_function_(ErasedAddress, Args&&... args) noexcept {
        if constexpr (__is_same(R, void)) {
            static_cast<void>(Function(static_cast<Args&&>(args)...));
        } else {
            return Function(static_cast<Args&&>(args)...);
        }
    }

    template <auto Member, class T>
    static constexpr R call_member_(ErasedAddress object, Args&&... args) noexcept {
        if constexpr (__is_same(R, void)) {
            static_cast<void>((object.restore<T>().*Member)(static_cast<Args&&>(args)...));
        } else {
            return (object.restore<T>().*Member)(static_cast<Args&&>(args)...);
        }
    }

    ErasedAddress object_{};
    Thunk thunk_ = nullptr;
};

}  // namespace foundation::core
