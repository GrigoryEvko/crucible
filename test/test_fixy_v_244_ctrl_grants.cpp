// FIXY-V-244 — fixy/grant/Ctrl.h positive sentinel.
//
// The header ships an in-file `detail::ctrl_grant_self_test` static_assert
// block; this TU forces that block to be checked under the project warning
// flags (header-only static_asserts are otherwise never verified — see the
// header-only-static_assert-blind-spot discipline) AND adds the cross-header
// assertions the header itself cannot make: the V-087 `fixy::ctrl::throws`
// re-home identity, and the engagement-alias / IsGrantTag surface.

#include <crucible/fixy/grant/Ctrl.h>   // grant::ctrl::* family
#include <crucible/fixy/ctrl/Throws.h>  // V-087 fixy::ctrl::throws re-home

#include <type_traits>

namespace gr   = crucible::fixy::grant;
namespace ctrl = crucible::fixy::grant::ctrl;
using D        = crucible::fixy::dim::DimensionAxis;

namespace {

// ── (1) V-087 re-home identity ────────────────────────────────────────
// `fixy::ctrl::throws` must be type-identical to the canonical default
// specialization `grant::ctrl::throws<>` so PermissionFork's type-tree
// search (which targets the un-parametrized form) keeps working unchanged.
static_assert(std::is_same_v<crucible::fixy::ctrl::throws, ctrl::throws<>>,
              "V-087 fixy::ctrl::throws must re-home to grant::ctrl::throws<>");
static_assert(gr::IsGrantTag<crucible::fixy::ctrl::throws>);

// ── (2) Every grant tag participates in IsGrantTag ────────────────────
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

// Policy tags are NOT grants (a bare use in a pack must be rejected).
static_assert(!gr::IsGrantTag<ctrl::any_exception>);
static_assert(!gr::IsGrantTag<ctrl::at_exit>);
static_assert(!gr::IsGrantTag<ctrl::no_cleanup>);
static_assert(!gr::IsGrantTag<ctrl::exit_immediate>);
static_assert(!gr::IsGrantTag<ctrl::co_await_only>);
static_assert(!gr::IsGrantTag<ctrl::generator>);
static_assert(!gr::IsGrantTag<ctrl::async_task>);

// ── (3) which_dim routes every tag (and the alias) to ControlFlow ─────
static_assert(gr::which_dim_v<ctrl::throws<>>                    == D::ControlFlow);
static_assert(gr::which_dim_v<ctrl::throws<CustomException>>     == D::ControlFlow);
static_assert(gr::which_dim_v<ctrl::abort<"x">>                  == D::ControlFlow);
static_assert(gr::which_dim_v<ctrl::longjmp_unsafe<"x">>         == D::ControlFlow);
static_assert(gr::which_dim_v<ctrl::exit<ctrl::at_exit>>         == D::ControlFlow);
static_assert(gr::which_dim_v<ctrl::exit<ctrl::exit_immediate>>  == D::ControlFlow);
static_assert(gr::which_dim_v<ctrl::builtin_trap_ok>             == D::ControlFlow);
static_assert(gr::which_dim_v<ctrl::unreachable_ok>              == D::ControlFlow);
static_assert(gr::which_dim_v<ctrl::coroutine<ctrl::generator>>  == D::ControlFlow);
static_assert(gr::which_dim_v<gr::accept_default_strict_for_ControlFlow>
              == D::ControlFlow);

// ── (4) Zero-cost — every grant tag is sizeof == 1 standalone ─────────
static_assert(sizeof(ctrl::throws<>)                    == 1);
static_assert(sizeof(ctrl::abort<"x">)                  == 1);
static_assert(sizeof(ctrl::longjmp_unsafe<"x">)         == 1);
static_assert(sizeof(ctrl::exit<ctrl::at_exit>)         == 1);
static_assert(sizeof(ctrl::builtin_trap_ok)             == 1);
static_assert(sizeof(ctrl::unreachable_ok)              == 1);
static_assert(sizeof(ctrl::coroutine<ctrl::async_task>) == 1);
static_assert(sizeof(gr::accept_default_strict_for_ControlFlow) == 1);

// ── (5) The LOAD-BEARING abort rationale carries type identity ────────
// Two abort sites with different reasons are distinct types (distinct
// federation cache slots); the same reason is the same type.
static_assert(!std::is_same_v<ctrl::abort<"OOM in PoolAllocator">,
                              ctrl::abort<"CKernel table overflow">>);
static_assert(std::is_same_v<ctrl::abort<"contract violated">,
                             ctrl::abort<"contract violated">>);
static_assert(!std::is_same_v<ctrl::abort<"ab">, ctrl::abort<"abc">>);
static_assert(ctrl::rationale{"unrecoverable"}.size() == 14);  // 13 + NUL

// ── (6) exit / coroutine policy variants are distinct types ───────────
static_assert(!std::is_same_v<ctrl::exit<ctrl::at_exit>,
                              ctrl::exit<ctrl::exit_immediate>>);
static_assert(!std::is_same_v<ctrl::coroutine<ctrl::generator>,
                              ctrl::coroutine<ctrl::async_task>>);

}  // namespace

int main() { return 0; }
