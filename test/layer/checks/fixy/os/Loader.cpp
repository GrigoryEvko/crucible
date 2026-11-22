// The compile-time checks of fixy/os/Loader.h.

#include <fixy/os/Loader.h>
#include <foundation/Brand.h>
#include <foundation/core/Choice.h>

#include <type_traits>

namespace fixy::os::detail::loader_invariants {

struct ScopeBrand final {};
struct OtherScopeBrand final {};

using ReadId = int() noexcept;
using ScopeLibrary = Library<ScopeBrand>;
using ScopeFunction = LibraryFunction<ReadId, ScopeBrand>;
using OtherFunction = LibraryFunction<ReadId, OtherScopeBrand>;

// The library: the door builds it, and it stays in the scope of the door.
static_assert(!std::is_copy_constructible_v<ScopeLibrary> && !std::is_move_constructible_v<ScopeLibrary>,
              "a library stays in the scope of with_library");
static_assert(!std::is_copy_assignable_v<ScopeLibrary> && !std::is_move_assignable_v<ScopeLibrary>);
static_assert(!std::is_constructible_v<ScopeLibrary, LibraryWord>, "only the door builds a library");
static_assert(!std::is_default_constructible_v<ScopeLibrary>, "a library has no empty state");

// The function: the brand of its library, and the call ABI of a pointer.
static_assert(::foundation::brand::SameBrand<ScopeLibrary, ScopeFunction>,
              "a function carries the brand of its library");
static_assert(!::foundation::brand::SameBrand<ScopeFunction, OtherFunction>);
static_assert(!std::is_convertible_v<ScopeFunction, OtherFunction>
                  && !std::is_constructible_v<OtherFunction, ScopeFunction>,
              "a function of one scope does not stand for a function of another scope");
static_assert(!std::is_constructible_v<ScopeFunction, SymbolWord>, "only a lookup builds a function");
static_assert(sizeof(ScopeFunction) == sizeof(SymbolWord), "the brand and the seal add no byte");
static_assert(std::is_trivially_copy_constructible_v<ScopeFunction> && std::is_trivially_destructible_v<ScopeFunction>,
              "the ABI passes a function in a register");
static_assert(!std::is_trivially_copyable_v<ScopeFunction>, "a copy of bytes builds no function");

// The result of a body leaves the scope: no function, no address and no brand
// of the scope.
static_assert(LibraryScopeResult<void, ScopeBrand> && LibraryScopeResult<int, ScopeBrand>);
static_assert(LibraryScopeResult<OtherFunction, ScopeBrand>, "a function of an outer scope outlives the inner scope");
static_assert(!LibraryScopeResult<ScopeFunction, ScopeBrand>, "a function leaves no scope of its library");
static_assert(!LibraryScopeResult<::foundation::core::Option<ScopeFunction>, ScopeBrand>);
static_assert(!LibraryScopeResult<ScopeLibrary const*, ScopeBrand>, "an address of the library leaves no scope");

}  // namespace fixy::os::detail::loader_invariants
