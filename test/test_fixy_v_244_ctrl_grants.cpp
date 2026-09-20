// A header-only surface is only checked where a translation unit pulls it
// in. Compiling this file runs the included headers' own static_asserts
// under the project warning flags, and adds the cross-header assertions
// neither header can make about itself.

#include <crucible/fixy/grant/_Ctrl.h>
#include <crucible/fixy/ctrl/_Throws.h>

#include <type_traits>

namespace gr = crucible::fixy::grant;
namespace ctrl = crucible::fixy::grant::ctrl;
using D = crucible::fixy::dim::DimensionAxis;

namespace {

// The alias must be type-identical to the default specialization, because
// a consumer's type-tree search targets the un-parametrized form.
static_assert(std::is_same_v<crucible::fixy::ctrl::throws, ctrl::throws<>>,
              "fixy::ctrl::throws must re-home to grant::ctrl::throws<>");
static_assert(gr::IsGrantTag<crucible::fixy::ctrl::throws>);

struct CustomException final {};
static_assert(gr::IsGrantTag<ctrl::throws<CustomException>>);
static_assert(gr::IsGrantTag<ctrl::abort<"PoolAllocator OOM is unrecoverable">>);
static_assert(gr::IsGrantTag<ctrl::longjmp_unsafe<"third-party setjmp island">>);
static_assert(gr::IsGrantTag<ctrl::exit<ctrl::at_exit>>);
static_assert(gr::IsGrantTag<ctrl::exit<ctrl::no_cleanup>>);
static_assert(gr::IsGrantTag<ctrl::exit<ctrl::exit_immediate>>);
static_assert(gr::IsGrantTag<ctrl::builtin_trap_ok>);
static_assert(gr::IsGrantTag<ctrl::unreachable_ok>);
static_assert(gr::IsGrantTag<ctrl::coroutine<ctrl::co_await_only>>);
static_assert(gr::IsGrantTag<ctrl::coroutine<ctrl::generator>>);
static_assert(gr::IsGrantTag<ctrl::coroutine<ctrl::async_task>>);

// Policy tags are not grants. A bare policy tag in a grant pack is rejected.
static_assert(!gr::IsGrantTag<ctrl::any_exception>);
static_assert(!gr::IsGrantTag<ctrl::at_exit>);
static_assert(!gr::IsGrantTag<ctrl::no_cleanup>);
static_assert(!gr::IsGrantTag<ctrl::exit_immediate>);
static_assert(!gr::IsGrantTag<ctrl::co_await_only>);
static_assert(!gr::IsGrantTag<ctrl::generator>);
static_assert(!gr::IsGrantTag<ctrl::async_task>);

static_assert(gr::which_dim_v<ctrl::throws<>> == D::ControlFlow);
static_assert(gr::which_dim_v<ctrl::throws<CustomException>> == D::ControlFlow);
static_assert(gr::which_dim_v<ctrl::abort<"x">> == D::ControlFlow);
static_assert(gr::which_dim_v<ctrl::longjmp_unsafe<"x">> == D::ControlFlow);
static_assert(gr::which_dim_v<ctrl::exit<ctrl::at_exit>> == D::ControlFlow);
static_assert(gr::which_dim_v<ctrl::exit<ctrl::exit_immediate>> == D::ControlFlow);
static_assert(gr::which_dim_v<ctrl::builtin_trap_ok> == D::ControlFlow);
static_assert(gr::which_dim_v<ctrl::unreachable_ok> == D::ControlFlow);
static_assert(gr::which_dim_v<ctrl::coroutine<ctrl::generator>> == D::ControlFlow);
static_assert(gr::which_dim_v<gr::accept_default_strict_for_ControlFlow> == D::ControlFlow);

static_assert(sizeof(ctrl::throws<>) == 1);
static_assert(sizeof(ctrl::abort<"x">) == 1);
static_assert(sizeof(ctrl::longjmp_unsafe<"x">) == 1);
static_assert(sizeof(ctrl::exit<ctrl::at_exit>) == 1);
static_assert(sizeof(ctrl::builtin_trap_ok) == 1);
static_assert(sizeof(ctrl::unreachable_ok) == 1);
static_assert(sizeof(ctrl::coroutine<ctrl::async_task>) == 1);
static_assert(sizeof(gr::accept_default_strict_for_ControlFlow) == 1);

// The rationale string participates in type identity, so two abort sites
// with different reasons occupy different federation-cache slots.
static_assert(!std::is_same_v<ctrl::abort<"OOM in PoolAllocator">, ctrl::abort<"CKernel table overflow">>);
static_assert(std::is_same_v<ctrl::abort<"contract violated">, ctrl::abort<"contract violated">>);
static_assert(!std::is_same_v<ctrl::abort<"ab">, ctrl::abort<"abc">>);
static_assert(ctrl::rationale{"unrecoverable"}.size() == 14);  // 13 + NUL

static_assert(!std::is_same_v<ctrl::exit<ctrl::at_exit>, ctrl::exit<ctrl::exit_immediate>>);
static_assert(!std::is_same_v<ctrl::coroutine<ctrl::generator>, ctrl::coroutine<ctrl::async_task>>);

}  // namespace

int main() { return 0; }
