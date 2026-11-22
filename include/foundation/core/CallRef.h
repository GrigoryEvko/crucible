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
//
// Shape
//     A CallRef is two words: the address of the callable, and a thunk that
//     calls the callable at that address.  It owns nothing, it allocates
//     nothing, and it has no virtual call.  The signature is noexcept, so a
//     callable that can throw does not fit.  A call is one indirect call
//     through the thunk, and the thunk inlines the call of the callable.
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
// The gates are the builtins of the compiler, so this header needs no
// header of the library.

namespace foundation::core {

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

template <class Signature>
class CallRef;

template <class R, class... Args>
class [[nodiscard]] CallRef<R(Args...) noexcept> final {
public:
    // The callable that a name holds.  The address of the callable is the
    // address of the reference.
    template <class F>
        requires CallRefTarget<F, R, Args...> && (!__is_same(F, CallRef))
    constexpr CallRef(F& callable) noexcept
        : object_{static_cast<void*>(__builtin_addressof(callable))}, thunk_{&call_object_<F>} {}

    // A callable that is a temporary ends at the end of the full expression.
    template <class F>
        requires(!__is_reference(F))
    CallRef(F&&) = delete("a CallRef of a temporary callable dangles at the end of the full expression.  Give the "
                          "CallRef a callable that a name holds");

    // A function, which has no object.
    template <auto Function>
        requires CallRefFunction<Function, R, Args...>
    [[nodiscard]] static constexpr CallRef of() noexcept {
        return CallRef{nullptr, &call_function_<Function>};
    }

    // A member function of an object that a name holds.
    template <auto Member, class T>
        requires CallRefMember<Member, T, R, Args...>
    [[nodiscard]] static constexpr CallRef bind(T& object) noexcept {
        return CallRef{static_cast<void*>(__builtin_addressof(object)), &call_member_<Member, T>};
    }

    template <auto Member, class T>
        requires(!__is_reference(T))
    static void bind(T&&) = delete("a CallRef of a member of a temporary object dangles at the end of the full "
                                   "expression.  Give bind an object that a name holds");

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
    using Thunk = R (*)(void*, Args&&...) noexcept;

    constexpr CallRef(void* object, Thunk thunk) noexcept : object_{object}, thunk_{thunk} {}

    // A signature with the result void drops the result of the callable.
    template <class F>
    static constexpr R call_object_(void* object, Args&&... args) noexcept {
        if constexpr (__is_same(R, void)) {
            static_cast<void>((*static_cast<F*>(object))(static_cast<Args&&>(args)...));
        } else {
            return (*static_cast<F*>(object))(static_cast<Args&&>(args)...);
        }
    }

    template <auto Function>
    static constexpr R call_function_(void*, Args&&... args) noexcept {
        if constexpr (__is_same(R, void)) {
            static_cast<void>(Function(static_cast<Args&&>(args)...));
        } else {
            return Function(static_cast<Args&&>(args)...);
        }
    }

    template <auto Member, class T>
    static constexpr R call_member_(void* object, Args&&... args) noexcept {
        if constexpr (__is_same(R, void)) {
            static_cast<void>((static_cast<T*>(object)->*Member)(static_cast<Args&&>(args)...));
        } else {
            return (static_cast<T*>(object)->*Member)(static_cast<Args&&>(args)...);
        }
    }

    void* object_ = nullptr;
    Thunk thunk_ = nullptr;
};

}  // namespace foundation::core
