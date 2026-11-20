#pragma once

// The loader door of fixy: the load of a shared library, the functions of the
// library, and the close of the library.
//
// THE LOADER DOOR
//   mint_library(ctx, path, diagnostic) loads the shared library at path as
//   the ctypes of Python loads one: dlopen with RTLD_NOW, so each reference of
//   the library binds at the load, and RTLD_LOCAL, so the names of the library
//   do not take part in the lookups of a later load.  The owner of the library
//   comes in a Linear.  The end of the owner closes the library (dlclose).  The
//   C library counts the loads of each library, and it unloads the library at
//   the last close.  The load runs the constructors of the library, and the
//   last close runs its destructors.
//
//   find_function<Signature>(ctx, library, name, diagnostic) gives the
//   function name of the library as a LibraryFunction<Signature>, and a call of
//   it runs the function.  The symbol table of a library holds the names of its
//   functions and no types.  So the door cannot check the signature, and the
//   caller gives the type of the declaration of the function, such as
//   decltype(entry_point) of a header of the library.  A LibraryFunction
//   borrows its library: it is valid only while the library is open.
//
// THE DIAGNOSTIC
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
// THE CONTEXT
//   The context of a load and of a lookup admits IO and Block: the load opens
//   and maps the files of the library and of each library that it needs.
//
// This header includes no system header.  The door file src/fixy/os/Loader.cpp
// is the one includer of <dlfcn.h>, and it calls dlopen, dlsym, dlclose and
// dlerror of the C library.

#include <fixy/Qtt.h>
#include <foundation/ByteSeal.h>
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

// What failed in a call of the loader door (THE LOADER DOOR).
enum class LoaderFailure : std::uint8_t {
    // dlopen refused the library.
    refused_open,
    // dlsym found no function of the name in the library.
    refused_symbol,
    // The owner holds no library, so the door made no lookup.
    not_open,
};

namespace detail {

struct LoaderDoor;

// The bits of the handle of dlopen, and the bits of the address of a function
// of a library.  The handle names an object of the C library, and the address
// names code of the library, and no object of this process.  So an owner and a
// function hold them as words, as a Handle holds a descriptor, and only the
// door file and a call make a pointer of them.  Zero is the word of no library.
enum class LibraryWord : std::uintptr_t {
};
enum class SymbolWord : std::uintptr_t {
};

}  // namespace detail

// The text of the loader for a failed call (THE DIAGNOSTIC).
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
    friend struct detail::LoaderDoor;

    ::foundation::core::FixedText<kLoaderDiagnosticBytes> text_{};
};

// The owner of a loaded library (THE LOADER DOOR).
class [[nodiscard]] Library final {
public:
    // The empty owner holds no library, and its end closes nothing.
    Library() noexcept = default;

    Library(Library const&) = delete("a library has one owner, and its end closes the library one time");
    Library& operator=(Library const&) = delete("a library has one owner, and its end closes the library one time");

    // A move takes the library, and the source holds none.
    Library(Library&& other) noexcept : handle_{other.handle_} { other.handle_ = detail::LibraryWord{}; }

    // A move assignment closes the library that this owner holds, and then
    // takes the library of the source.
    Library& operator=(Library&& other) noexcept {
        if (this != &other) {
            close_();
            handle_ = other.handle_;
            other.handle_ = detail::LibraryWord{};
        }
        return *this;
    }

    ~Library() noexcept { close_(); }

    // False for the empty owner, such as one that a move emptied.
    [[nodiscard]] bool is_open() const noexcept { return handle_ != detail::LibraryWord{}; }

private:
    friend struct detail::LoaderDoor;

    explicit Library(detail::LibraryWord handle) noexcept : handle_{handle} {}

    void close_() noexcept;

    // The handle of dlopen, or zero for the empty owner.
    detail::LibraryWord handle_{};
};

template <class Signature>
class LibraryFunction;

// A function of a loaded library, which a call runs (THE LOADER DOOR).  The
// function borrows its library, so it is valid only while the library is open.
template <class Return, class... Arguments>
class LibraryFunction<Return(Arguments...) noexcept> final {
public:
    // Runs the function with the arguments.
    Return operator()(Arguments... arguments) const noexcept {
        return __builtin_bit_cast(Pointer, std::to_underlying(address_))(arguments...);
    }

private:
    using Pointer = Return (*)(Arguments...) noexcept;

    friend struct detail::LoaderDoor;

    explicit LibraryFunction(detail::SymbolWord address) noexcept : address_{address} {}

    // The address of the function in the library, which is not zero.
    detail::SymbolWord address_{};
    // A copy of bytes builds no function, so each function comes from a lookup
    // of find_function (foundation/ByteSeal.h).
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};
};

namespace detail {

// The calls of the door file.  Each call takes a path or a name that a zero
// ends.  A failed call writes the text of dlerror into the diagnostic.  The
// door file makes the pointer of a word, and the word of a pointer.
[[nodiscard]] ::foundation::core::Result<Library, LoaderFailure> open_library_(::foundation::core::CText path,
                                                                               LoaderDiagnostic& diagnostic) noexcept;
[[nodiscard]] ::foundation::core::Result<SymbolWord, LoaderFailure>
find_symbol_(LibraryWord handle, ::foundation::core::CText name, LoaderDiagnostic& diagnostic) noexcept;
void close_library_(LibraryWord handle) noexcept;

// The door to the parts of a Library, a LibraryFunction and a
// LoaderDiagnostic.  Only the functions of this header and of
// src/fixy/os/Loader.cpp use it.
struct LoaderDoor {
    [[nodiscard]] static Library make_library(LibraryWord handle) noexcept { return Library{handle}; }

    [[nodiscard]] static LibraryWord handle_of(Library const& library) noexcept { return library.handle_; }

    template <class Signature>
    [[nodiscard]] static LibraryFunction<Signature> make_function(SymbolWord address) noexcept {
        return LibraryFunction<Signature>{address};
    }

    // Empties the diagnostic.
    static void clear(LoaderDiagnostic& diagnostic) noexcept {
        diagnostic.text_ = ::foundation::core::FixedText<kLoaderDiagnosticBytes>{};
    }

    // Writes the text into the empty diagnostic, cut to kLoaderDiagnosticBytes
    // characters.
    static void write(LoaderDiagnostic& diagnostic, ::foundation::core::TextView text) noexcept {
        std::size_t const kept = text.size() < kLoaderDiagnosticBytes ? text.size() : kLoaderDiagnosticBytes;
        static_cast<void>(diagnostic.text_.append(text.window(0, kept).value_or(::foundation::core::TextView{})));
    }
};

}  // namespace detail

inline void Library::close_() noexcept {
    if (handle_ == detail::LibraryWord{}) return;
    detail::close_library_(handle_);
    handle_ = detail::LibraryWord{};
}

// Loads the shared library at path (THE LOADER DOOR).  A refused load gives
// refused_open, and the diagnostic holds the text of the loader.
template <eff::IsExecCtx Ctx, class Brand>
    requires CtxFitsLibraryLoad<Ctx>
[[nodiscard]] ::foundation::core::Result<Linear<Library>, LoaderFailure>
mint_library(Ctx const& /*ctx*/, ::foundation::core::CText path,
             ::foundation::core::Ref<LoaderDiagnostic, Brand> diagnostic) noexcept {
    detail::LoaderDoor::clear(*diagnostic);
    return detail::open_library_(path, *diagnostic).map([](Library library) noexcept {
        return mint_linear<Library>(std::move(library));
    });
}

// The function name of the library, as a LibraryFunction of the signature of
// its declaration (THE LOADER DOOR).  The signature is a function type with
// noexcept.  A name with no function gives refused_symbol, and the diagnostic
// holds the text of the loader.  The empty owner gives not_open and makes no
// lookup.
template <class Signature, eff::IsExecCtx Ctx, class Brand>
    requires CtxFitsLibraryLoad<Ctx> && std::is_function_v<Signature>
[[nodiscard]] ::foundation::core::Result<LibraryFunction<Signature>, LoaderFailure>
find_function(Ctx const& /*ctx*/, Library const& library, ::foundation::core::CText name,
              ::foundation::core::Ref<LoaderDiagnostic, Brand> diagnostic) noexcept {
    detail::LoaderDoor::clear(*diagnostic);
    if (!library.is_open()) {
        detail::LoaderDoor::write(*diagnostic, "the library is not open");
        return ::foundation::core::err(LoaderFailure::not_open);
    }
    return detail::find_symbol_(detail::LoaderDoor::handle_of(library), name, *diagnostic)
        .map([](detail::SymbolWord address) noexcept { return detail::LoaderDoor::make_function<Signature>(address); });
}

template <class Signature, eff::IsExecCtx Ctx, class Brand>
void find_function(Ctx const&, Library const&&, ::foundation::core::CText,
                   ::foundation::core::Ref<LoaderDiagnostic, Brand>) =
    delete("a function of a temporary library dangles at the end of the full expression");

}  // namespace fixy::os
