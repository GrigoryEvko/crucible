// The loader door of fixy (fixy/os/Loader.h).  This file is the one includer
// of <dlfcn.h>.  It calls dlopen, dlsym, dlclose and dlerror of the C library,
// because the C library owns the list of the loaded libraries, their
// constructors and destructors, and the lookup of their names (decision D12 of
// misc/01_10_2026_quarantine.md).

#include <fixy/os/Loader.h>

#include <foundation/core/Choice.h>
#include <foundation/core/Text.h>

#include <dlfcn.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace fixy::os::detail {

namespace {

namespace core = ::foundation::core;

// Writes the text of the last failed call of the loader on the calling thread
// into the diagnostic (THE DIAGNOSTIC of fixy/os/Loader.h).  The text of
// dlerror ends with a zero, and the door copies it up to the zero or up to
// kLoaderDiagnosticBytes characters.  No text writes the text FALLBACK.
void write_last_error(LoaderDiagnostic& diagnostic, core::TextView fallback) noexcept {
    char const* const text = ::dlerror();
    if (text == nullptr) {
        LoaderDoor::write(diagnostic, fallback);
        return;
    }
    std::size_t count = 0;
    while (count < kLoaderDiagnosticBytes && text[count] != '\0')
        ++count;
    LoaderDoor::write(diagnostic, core::detail::text_over_(text, count));
}

}  // namespace

core::Result<Library, LoaderFailure> open_library_(core::CText path, LoaderDiagnostic& diagnostic) noexcept {
    void* const handle = ::dlopen(core::detail::c_text_first_(path), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        write_last_error(diagnostic, "the loader refused the library and gave no text");
        return core::err(LoaderFailure::refused_open);
    }
    return LoaderDoor::make_library(LibraryWord{__builtin_bit_cast(std::uintptr_t, handle)});
}

// A symbol can have the address zero with no error, so the door clears the
// text of the last error before the lookup.  The address of a function is not
// zero, so the door gives refused_symbol for an address zero too.
core::Result<SymbolWord, LoaderFailure> find_symbol_(LibraryWord handle, core::CText name,
                                                     LoaderDiagnostic& diagnostic) noexcept {
    static_cast<void>(::dlerror());
    void* const address =
        ::dlsym(__builtin_bit_cast(void*, std::to_underlying(handle)), core::detail::c_text_first_(name));
    if (address == nullptr) {
        write_last_error(diagnostic, "the name has the address zero");
        return core::err(LoaderFailure::refused_symbol);
    }
    return SymbolWord{__builtin_bit_cast(std::uintptr_t, address)};
}

// The end of an owner reports no error, as the end of a handle does.
void close_library_(LibraryWord handle) noexcept {
    static_cast<void>(::dlclose(__builtin_bit_cast(void*, std::to_underlying(handle))));
}

}  // namespace fixy::os::detail
