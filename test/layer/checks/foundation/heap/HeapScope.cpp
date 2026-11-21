// The compile-time checks of foundation/heap/HeapScope.h.

#include <foundation/heap/HeapScope.h>

#include <foundation/Brand.h>
#include <foundation/Linear.h>
#include <foundation/alloc/Layout.h>
#include <foundation/permissions/Permission.h>

#include <type_traits>

namespace foundation::heap {

namespace heap_scope_check {

struct Brand final {};

using Block = LocalBlock<alloc::SizeClass{1}, Brand>;
using Scope = HeapScope<Brand>;

}  // namespace heap_scope_check

// A block is one address word.  It is linear, it moves with no throw, and it
// does not copy.
static_assert(sizeof(heap_scope_check::Block) == 8);
static_assert(linear::is_linear_v<heap_scope_check::Block>);
static_assert(!std::is_copy_constructible_v<heap_scope_check::Block>);
static_assert(std::is_nothrow_move_constructible_v<heap_scope_check::Block>);

// A scope lives in the frame of its mint: it does not copy or move.
static_assert(!std::is_copy_constructible_v<heap_scope_check::Scope>);
static_assert(!std::is_move_constructible_v<heap_scope_check::Scope>);

// The doors take the classes with a slab, 1 to 52.
static_assert(SlabSizeClass<alloc::SizeClass{1}> && SlabSizeClass<alloc::SizeClass{52}>);
static_assert(!SlabSizeClass<alloc::SizeClass{0}> && !SlabSizeClass<alloc::SizeClass{53}>);

// The tag of the owner declares an empty row, so its mints take no context,
// and it names its scope door, so the scope mint gives its permission on the
// scope brand of a call of mint_heap_scope.
static_assert(permissions::has_permission_row(^^heap_tag::Owner));
static_assert(permissions::permission_row_empty(^^heap_tag::Owner));
static_assert(permissions::ScopeMintableTag<heap_tag::Owner>);
static_assert(permissions::ScopePermissionArgs<heap_tag::Owner, heap_scope_check::Brand>);
static_assert(!permissions::ScopePermissionArgs<heap_tag::Owner, brand::DefaultBrand>);

}  // namespace foundation::heap
