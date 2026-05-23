// FIXY-V-245 — fixy/grant/Dispatch.h positive sentinel.
//
// Forces the header's in-file `detail::dispatch_grant_self_test` block to
// be checked under the project warning flags, plus cross-cutting surface
// assertions: IsGrantTag participation, which_dim→CallShape routing, the
// recurses<MaxDepth> bound-carries-identity property, and the engagement
// alias.

#include <crucible/fixy/grant/Dispatch.h>

#include <type_traits>

namespace gr   = crucible::fixy::grant;
namespace disp = crucible::fixy::grant::dispatch;
using D        = crucible::fixy::dim::DimensionAxis;

namespace {

struct CallbackFamily final {};
struct OtherFamily    final {};
struct VtableBase     final {};

// ── (1) Every grant tag participates in IsGrantTag ────────────────────
static_assert(gr::IsGrantTag<disp::indirect_call<CallbackFamily>>);
static_assert(gr::IsGrantTag<disp::virtual_call<VtableBase>>);
static_assert(gr::IsGrantTag<disp::recurses<32>>);
static_assert(gr::IsGrantTag<disp::recurses<0>>);
static_assert(gr::IsGrantTag<disp::tail_call>);

// ── (2) which_dim routes every tag (and the alias) to CallShape ───────
static_assert(gr::which_dim_v<disp::indirect_call<CallbackFamily>> == D::CallShape);
static_assert(gr::which_dim_v<disp::virtual_call<VtableBase>>      == D::CallShape);
static_assert(gr::which_dim_v<disp::recurses<32>>                  == D::CallShape);
static_assert(gr::which_dim_v<disp::tail_call>                     == D::CallShape);
static_assert(gr::which_dim_v<gr::accept_default_strict_for_CallShape> == D::CallShape);

// ── (3) Zero-cost — every grant tag is sizeof == 1 standalone ─────────
static_assert(sizeof(disp::indirect_call<CallbackFamily>)             == 1);
static_assert(sizeof(disp::virtual_call<VtableBase>)                  == 1);
static_assert(sizeof(disp::recurses<32>)                              == 1);
static_assert(sizeof(disp::tail_call)                                 == 1);
static_assert(sizeof(gr::accept_default_strict_for_CallShape)         == 1);

// ── (4) The LOAD-BEARING recurses bound carries type identity ─────────
// Two recursion sites with different proven depths are distinct types
// (distinct federation cache slots); the same depth is the same type.
static_assert(!std::is_same_v<disp::recurses<32>, disp::recurses<16>>);
static_assert(std::is_same_v<disp::recurses<8>, disp::recurses<8>>);

// ── (5) Callback-family / base tags carry identity ────────────────────
static_assert(!std::is_same_v<disp::indirect_call<CallbackFamily>,
                              disp::indirect_call<OtherFamily>>);
static_assert(!std::is_same_v<disp::indirect_call<CallbackFamily>,
                              disp::virtual_call<VtableBase>>);

}  // namespace

int main() { return 0; }
