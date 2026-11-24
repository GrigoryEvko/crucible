#pragma once

// The loader door of fixy: the load of a shared library, the functions of the
// library, and the close of the library.
//
// Loader door
//   with_library(ctx, path, diagnostic, body) loads the shared library at path
//   as the ctypes of Python loads one: dlopen with RTLD_NOW, so each reference
//   of the library binds at the load, and RTLD_LOCAL, so the names of the
//   library do not take part in the lookups of a later load.  The door calls
//   body with the Library, and it closes the library (dlclose) when the body
//   returns.  It gives the result of the body, or Unit for a body that gives
//   void.  A refused load calls no body.  The C library counts the loads of
//   each library, and it unloads the library at the last close.  The load runs
//   the constructors of the library, and the last close runs its destructors.
//
//   find_function<Signature>(ctx, library, name, diagnostic) gives the
//   function name of the library as a LibraryFunction, and a call of it runs
//   the function.  The symbol table of a library holds the names of its
//   functions and no types.  So the door cannot check the signature, and the
//   caller gives the type of the declaration of the function, such as
//   decltype(entry_point) of a header of the library.
//
// Scope of a library
//   A LibraryFunction names code of its library, and the close of the library
//   unmaps that code.  So no function outlives the scope of its library.
//   with_library is a scope mint: its last template parameter is the scope
//   brand CRUCIBLE_SCOPE_BRAND of foundation/Brand.h, fresh for each call site.
//   The Library of the body and each function of it carry that brand:
//     - The concept of the door refuses a result of the body that names the
//       brand (neg_library_function_returned_from_scope).
//     - The rule brand-escape refuses a result that holds the brand in a
//       capture (neg_library_function_called_after_close), and each object of
//       static or thread storage duration that holds the brand.
//     - The store check of the door refuses a body that stores a borrow, such
//       as a text that a function of the library gives, in a variable of the
//       caller (neg_library_body_keeps_a_text_of_the_library).
//   No code outside the scope can name the brand, so no variable outside the
//   scope holds a function of it.  The body gets the Library by a const
//   reference, and a Library cannot copy or move.  Its destructor is private
//   to the door, so no body ends the library before the end of the scope
//   (neg_library_destructor_in_body).
//
//   A holder that erases the type of a function hides the brand from the rule.
//   The base has one such holder, CallRef of foundation/core/CallRef.h, and it
//   refuses each callable that holds a scope brand
//   (neg_library_function_erased_into_static).  The quarantine counts each
//   holder of the standard library that erases a type, such as std::function,
//   outside the base, and no file of the base uses one.
//
//   The door is the one maker of a Library and of a LibraryFunction.  Each
//   part of LoaderDoor is private, and its friends are the doors of this
//   header.
//
// Diagnostic
//   An error of a Result of the base is small and never allocates (ErrorValue
//   of foundation/core/Choice.h), but the text of the loader holds up to
//   kLoaderDiagnosticBytes characters.  So the Result of a door holds only a
//   LoaderFailure, and a caller keeps expect and CRUCIBLE_TRY on it.  The door
//   copies the text of dlerror into a LoaderDiagnostic that the caller owns,
//   before it returns.  Thus no text stays in the state of the C library for
//   the thread, and no later call of the loader replaces it.
//
//   Each call of a door empties the diagnostic first, so a call that succeeds
//   leaves it empty.  A text longer than kLoaderDiagnosticBytes characters keeps
//   its first characters.
//
// Context
//   The context of a load and of a lookup admits IO and Block: the load opens
//   and maps the files of the library and of each library that it needs.
//
// This header includes no system header.  The door file src/fixy/os/Loader.cpp
// is the one includer of <dlfcn.h>, and it calls dlopen, dlsym, dlclose and
// dlerror of the C library.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/NoObject.h>
#include <foundation/core/CaptureStore.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Ctx.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace fixy::os {

namespace eff = ::foundation::effects;

// The largest count of the characters of the text of a LoaderDiagnostic.
inline constexpr std::size_t kLoaderDiagnosticBytes = 512;

// A context that can load a library and look up its functions.
template <typename Ctx>
concept CtxFitsLibraryLoad = eff::IsExecCtx<Ctx> && eff::CtxOwnsAllOf<Ctx, eff::Effect::IO, eff::Effect::Block>;

// What failed in a call of the loader door (the loader door).
enum class LoaderFailure : std::uint8_t {
    // dlopen refused the library.
    refused_open,
    // dlsym found no function of the name in the library.
    refused_symbol,
};

namespace detail {

class LoaderDoor;

// The bits of the handle of dlopen, and the bits of the address of a function
// of a library.  The handle names an object of the C library, and the address
// names code of the library, and no object of this process.  So a library and
// a function hold them as words, as a Handle holds a descriptor, and only the
// door file and a call make a pointer of them.
enum class LibraryWord : std::uintptr_t {
};
enum class SymbolWord : std::uintptr_t {
};

}  // namespace detail

// The text of the loader for a failed call (the diagnostic).
class LoaderDiagnostic final {
public:
    // The empty diagnostic.
    LoaderDiagnostic() noexcept = default;

    // The text of the last failed call, or the empty text after a call that
    // succeeded.
    [[nodiscard]] ::foundation::core::TextView text() const& noexcept { return text_.view(); }
    ::foundation::core::TextView
    text() const&& = delete("the text of a temporary diagnostic points into an object that the full expression ends");

private:
    friend class detail::LoaderDoor;

    ::foundation::core::FixedText<kLoaderDiagnosticBytes> text_{};
};

// A loaded library in the scope of with_library (the loader door).  The door
// builds it, gives the body a const reference to it, and closes it when the
// body returns.  Brand is the scope brand of the call of the door.
template <class Brand>
class [[nodiscard]] Library final {
public:
    using brand_type = Brand;

    Library(Library const&) =
        delete("a library stays in the scope of with_library, and the end of the scope closes it one time");
    Library& operator=(Library const&) =
        delete("a library stays in the scope of with_library, and the end of the scope closes it one time");

private:
    friend class detail::LoaderDoor;

    explicit Library(detail::LibraryWord handle) noexcept : handle_{handle} {}

    // The end of the scope closes the library.  Only the door ends a library,
    // so no body ends it before the end of its scope and then calls a
    // function of it.
    ~Library() noexcept;

    // The handle of dlopen, which is not zero.
    detail::LibraryWord handle_{};
};

template <class Signature, class Brand>
class LibraryFunction;

// A function of a loaded library, which a call runs (the loader door).  Brand
// is the scope brand of its library, so the function lives only in the scope
// of the library (scope of a library).
template <class Return, class... Arguments, class Brand>
class LibraryFunction<Return(Arguments...) noexcept, Brand> final {
public:
    using brand_type = Brand;

    // Runs the function with the arguments.
    Return operator()(Arguments... arguments) const noexcept {
        return __builtin_bit_cast(Pointer, std::to_underlying(address_))(arguments...);
    }

private:
    using Pointer = Return (*)(Arguments...) noexcept;

    friend class detail::LoaderDoor;

    explicit LibraryFunction(detail::SymbolWord address) noexcept : address_{address} {}

    // The address of the function in the library, which is not zero.
    detail::SymbolWord address_{};
    // A copy of bytes builds no function, so each function comes from a lookup
    // of find_function (foundation/ByteSeal.h).
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

namespace detail {

// The value of with_library for a body that gives Given: Unit for void, and
// the value with no const for each other result.
template <class Given>
struct LibraryScopeValue {
    using type = std::remove_cv_t<Given>;
};

template <>
struct LibraryScopeValue<void> {
    using type = ::foundation::core::Unit;
};

template <class Body, class Brand>
using library_scope_value_t = typename LibraryScopeValue<std::invoke_result_t<Body&&, Library<Brand> const&>>::type;

}  // namespace detail

// A result of the body of with_library: void, or a value that holds no
// borrow, names no brand of the scope and fits a Result.  The value leaves
// the scope, and the end of the scope closes the library, so a function of
// the library in it would name unmapped code (scope of a library).
template <class Given, class Brand>
concept LibraryScopeResult =
    std::is_void_v<Given>
    || (::foundation::core::HoldsNoBorrow<Given> && !::foundation::core::detail::names_brand(^^Given, ^^Brand)
        && ::foundation::core::ChoicePayload<std::remove_cv_t<Given>>);

// The body of with_library: it takes the Library of the scope by a const
// reference with no throw, its result leaves the scope, and it stores no
// borrow outside the scope (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h).  A function of the library can give the
// address of a text or a table in the memory of the library, and the last
// close unmaps that memory, so such an address in a capture would name
// unmapped memory after the scope.
template <class Body, class Brand>
concept LibraryScopeBody = std::is_nothrow_invocable_v<Body&&, Library<Brand> const&>
                        && LibraryScopeResult<std::invoke_result_t<Body&&, Library<Brand> const&>, Brand>
                        && ::foundation::core::StoresNoBorrowOutside<Body>;

// A body whose result does not leave the scope: the refused twin of the door
// binds to it.
template <class Body, class Brand>
concept RefusedLibraryScopeBody = std::is_nothrow_invocable_v<Body&&, Library<Brand> const&>
                               && (!LibraryScopeResult<std::invoke_result_t<Body&&, Library<Brand> const&>, Brand>);

// A body whose result leaves the scope and that stores a borrow outside the
// scope: the store twin of the door binds to it.
template <class Body, class Brand>
concept StoringLibraryScopeBody = std::is_nothrow_invocable_v<Body&&, Library<Brand> const&>
                               && LibraryScopeResult<std::invoke_result_t<Body&&, Library<Brand> const&>, Brand>
                               && (!::foundation::core::StoresNoBorrowOutside<Body>);

// The scope door of a library.  LoaderDoor names it as a friend, and this
// header defines it after the door.
template <class... Refused, eff::IsExecCtx Ctx, class DiagnosticBrand, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && CtxFitsLibraryLoad<Ctx> && LibraryScopeBody<Body, Fresh>
[[nodiscard]] auto with_library(Ctx const& ctx, ::foundation::core::CText path,
                                ::foundation::core::Ref<LoaderDiagnostic, DiagnosticBrand> diagnostic,
                                Body&& body) noexcept
    -> ::foundation::core::Result<detail::library_scope_value_t<Body, Fresh>, LoaderFailure>;

// The lookup door of a function, declared here for the same reason.
template <class Signature, eff::IsExecCtx Ctx, class Brand, class DiagnosticBrand>
    requires CtxFitsLibraryLoad<Ctx> && std::is_function_v<Signature>
[[nodiscard]] auto find_function(Ctx const& ctx, Library<Brand> const& library, ::foundation::core::CText name,
                                 ::foundation::core::Ref<LoaderDiagnostic, DiagnosticBrand> diagnostic) noexcept
    -> ::foundation::core::Result<LibraryFunction<Signature, Brand>, LoaderFailure>;

namespace detail {

// The door to the parts of a Library, a LibraryFunction and a
// LoaderDiagnostic, and to the calls of the door file.  Each part is private.
// The friends are with_library, find_function and Library, whose destructor
// closes the library, so no other code builds a library or a function from a
// word, ends a library, or closes a handle.
class LoaderDoor final : ::foundation::NoObject<LoaderDoor> {
    template <class... Refused, eff::IsExecCtx Ctx, class DiagnosticBrand, class Body,
              ::foundation::brand::IsBrand Fresh>
        requires(sizeof...(Refused) == 0) && CtxFitsLibraryLoad<Ctx> && LibraryScopeBody<Body, Fresh>
    friend auto ::fixy::os::with_library(Ctx const& ctx, ::foundation::core::CText path,
                                         ::foundation::core::Ref<LoaderDiagnostic, DiagnosticBrand> diagnostic,
                                         Body&& body) noexcept
        -> ::foundation::core::Result<detail::library_scope_value_t<Body, Fresh>, LoaderFailure>;

    template <class Signature, eff::IsExecCtx Ctx, class Brand, class DiagnosticBrand>
        requires CtxFitsLibraryLoad<Ctx> && std::is_function_v<Signature>
    friend auto ::fixy::os::find_function(
        Ctx const& ctx, Library<Brand> const& library, ::foundation::core::CText name,
        ::foundation::core::Ref<LoaderDiagnostic, DiagnosticBrand> diagnostic) noexcept
        -> ::foundation::core::Result<LibraryFunction<Signature, Brand>, LoaderFailure>;

    template <class Brand>
    friend class ::fixy::os::Library;

    // The calls of the door file.  Each call takes a path or a name that a
    // zero ends.  A failed call writes the text of dlerror into the
    // diagnostic.  The door file makes the pointer of a word, and the word of
    // a pointer.
    [[nodiscard]] static ::foundation::core::Result<LibraryWord, LoaderFailure>
    open_library_(::foundation::core::CText path, LoaderDiagnostic& diagnostic) noexcept;
    [[nodiscard]] static ::foundation::core::Result<SymbolWord, LoaderFailure>
    find_symbol_(LibraryWord handle, ::foundation::core::CText name, LoaderDiagnostic& diagnostic) noexcept;
    static void close_library_(LibraryWord handle) noexcept;

    // Builds the Library of handle on Brand in this frame, calls body with it,
    // and gives the value of body.  The end of the frame ends the Library,
    // which closes the library.
    template <class Brand, class Body>
    [[nodiscard]] static library_scope_value_t<Body, Brand> run_scope_(LibraryWord handle, Body&& body) noexcept {
        Library<Brand> const library{handle};
        if constexpr (std::is_void_v<std::invoke_result_t<Body&&, Library<Brand> const&>>) {
            static_cast<Body&&>(body)(library);
            return ::foundation::core::Unit{};
        } else {
            return static_cast<Body&&>(body)(library);
        }
    }

    template <class Brand>
    [[nodiscard]] static LibraryWord handle_of_(Library<Brand> const& library) noexcept {
        return library.handle_;
    }

    template <class Signature, class Brand>
    [[nodiscard]] static LibraryFunction<Signature, Brand> make_function_(SymbolWord address) noexcept {
        return LibraryFunction<Signature, Brand>{address};
    }

    // Empties the diagnostic.
    static void clear_(LoaderDiagnostic& diagnostic) noexcept {
        diagnostic.text_ = ::foundation::core::FixedText<kLoaderDiagnosticBytes>{};
    }

    // Writes the text into the empty diagnostic, cut to kLoaderDiagnosticBytes
    // characters.
    static void write_(LoaderDiagnostic& diagnostic, ::foundation::core::TextView text) noexcept {
        std::size_t const kept = text.size() < kLoaderDiagnosticBytes ? text.size() : kLoaderDiagnosticBytes;
        static_cast<void>(diagnostic.text_.append(text.window(0, kept).value_or(::foundation::core::TextView{})));
    }
};

}  // namespace detail

template <class Brand>
Library<Brand>::~Library() noexcept {
    detail::LoaderDoor::close_library_(handle_);
}

// Loads the shared library at path, calls body with the Library on the scope
// brand of this call site, and closes the library when the body returns (the
// loader door).  A refused load gives refused_open and calls no body, and the
// diagnostic holds the text of the loader.  The pack refuses each explicit
// argument, so no caller names the brand.
template <class... Refused, eff::IsExecCtx Ctx, class DiagnosticBrand, class Body, ::foundation::brand::IsBrand Fresh>
    requires(sizeof...(Refused) == 0) && CtxFitsLibraryLoad<Ctx> && LibraryScopeBody<Body, Fresh>
auto with_library(Ctx const& /*ctx*/, ::foundation::core::CText path,
                  ::foundation::core::Ref<LoaderDiagnostic, DiagnosticBrand> diagnostic, Body&& body) noexcept
    -> ::foundation::core::Result<detail::library_scope_value_t<Body, Fresh>, LoaderFailure> {
    using Value = detail::library_scope_value_t<Body, Fresh>;
    detail::LoaderDoor::clear_(*diagnostic);
    return detail::LoaderDoor::open_library_(path, *diagnostic)
        .map([&body](detail::LibraryWord handle) noexcept -> Value {
            return detail::LoaderDoor::run_scope_<Fresh>(handle, static_cast<Body&&>(body));
        });
}

// A body whose result holds a borrow or names the brand of the scope binds to
// this overload, so the refusal gives its reason.
template <class... Refused, eff::IsExecCtx Ctx, class DiagnosticBrand, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && CtxFitsLibraryLoad<Ctx> && RefusedLibraryScopeBody<Body, Fresh>
void with_library(Ctx const& ctx, ::foundation::core::CText path,
                  ::foundation::core::Ref<LoaderDiagnostic, DiagnosticBrand> diagnostic, Body&& body) =
    delete("the end of the scope of with_library closes the library, so the result of the body holds no reference "
           "and no address, and it names no brand of the library: call each function in the body, and give a "
           "plain value");

// A body that stores a borrow outside the scope binds to this overload, so
// the refusal gives its reason.
template <class... Refused, eff::IsExecCtx Ctx, class DiagnosticBrand, class Body,
          ::foundation::brand::IsBrand Fresh = CRUCIBLE_SCOPE_BRAND>
    requires(sizeof...(Refused) == 0) && CtxFitsLibraryLoad<Ctx> && StoringLibraryScopeBody<Body, Fresh>
void with_library(Ctx const& ctx, ::foundation::core::CText path,
                  ::foundation::core::Ref<LoaderDiagnostic, DiagnosticBrand> diagnostic,
                  Body&& body) = delete(CRUCIBLE_STORING_BODY_REFUSAL);

// The function name of the library, as a LibraryFunction of the signature of
// its declaration (the loader door).  The signature is a function type with
// noexcept.  The function carries the brand of the library.  A name with no
// function gives refused_symbol, and the diagnostic holds the text of the
// loader.
template <class Signature, eff::IsExecCtx Ctx, class Brand, class DiagnosticBrand>
    requires CtxFitsLibraryLoad<Ctx> && std::is_function_v<Signature>
auto find_function(Ctx const& /*ctx*/, Library<Brand> const& library, ::foundation::core::CText name,
                   ::foundation::core::Ref<LoaderDiagnostic, DiagnosticBrand> diagnostic) noexcept
    -> ::foundation::core::Result<LibraryFunction<Signature, Brand>, LoaderFailure> {
    detail::LoaderDoor::clear_(*diagnostic);
    // No combinator builds the function: a closure type names no brand, so a
    // map would give a result of the brand from a scope that no template
    // argument names, and the rule brand-escape refuses that.
    ::foundation::core::Result<detail::SymbolWord, LoaderFailure> found =
        detail::LoaderDoor::find_symbol_(detail::LoaderDoor::handle_of_(library), name, *diagnostic);
    if (found.is_err()) return ::foundation::core::err(LoaderFailure::refused_symbol);
    return detail::LoaderDoor::make_function_<Signature, Brand>(std::move(found).value_or(detail::SymbolWord{}));
}

}  // namespace fixy::os
