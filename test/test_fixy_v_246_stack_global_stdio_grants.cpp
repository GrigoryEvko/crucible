// Sentinel TU: compiles the three grant headers under the project warning flags
// so their self-test blocks run.

#include <crucible/fixy/grant/Stack.h>
#include <crucible/fixy/grant/Global.h>
#include <crucible/fixy/grant/Stdio.h>

#include <type_traits>

namespace gr = crucible::fixy::grant;
using D = crucible::fixy::dim::DimensionAxis;

namespace {

static_assert(gr::IsGrantTag<gr::stack::alloc<64>>);
static_assert(gr::IsGrantTag<gr::stack::vla_ok>);
static_assert(gr::IsGrantTag<gr::stack::alloca_ok>);
static_assert(gr::which_dim_v<gr::stack::alloc<64>> == D::StackUse);
static_assert(gr::which_dim_v<gr::stack::vla_ok> == D::StackUse);
static_assert(gr::which_dim_v<gr::stack::alloca_ok> == D::StackUse);
static_assert(gr::which_dim_v<gr::accept_default_strict_for_StackUse> == D::StackUse);
static_assert(sizeof(gr::stack::alloc<64>) == 1);
static_assert(!std::is_same_v<gr::stack::alloc<64>, gr::stack::alloc<4096>>);

struct CKernelTableTag final {};
struct SchemaCacheTag final {};
struct ConfigTag final {};

static_assert(gr::IsGrantTag<gr::global::singleton<CKernelTableTag>>);
static_assert(gr::IsGrantTag<gr::global::thread_local_<SchemaCacheTag>>);
static_assert(gr::IsGrantTag<gr::global::namespace_static<ConfigTag>>);
static_assert(gr::IsGrantTag<gr::global::atexit_handler>);
static_assert(gr::which_dim_v<gr::global::singleton<CKernelTableTag>> == D::GlobalState);
static_assert(gr::which_dim_v<gr::global::thread_local_<SchemaCacheTag>> == D::GlobalState);
static_assert(gr::which_dim_v<gr::global::namespace_static<ConfigTag>> == D::GlobalState);
static_assert(gr::which_dim_v<gr::global::atexit_handler> == D::GlobalState);
static_assert(gr::which_dim_v<gr::accept_default_strict_for_GlobalState> == D::GlobalState);
static_assert(sizeof(gr::global::singleton<CKernelTableTag>) == 1);
static_assert(!std::is_same_v<gr::global::singleton<CKernelTableTag>, gr::global::singleton<SchemaCacheTag>>);

namespace st = gr::stdio::streams;
static_assert(gr::IsGrantTag<gr::stdio::write<st::Stderr>>);
static_assert(gr::IsGrantTag<gr::stdio::write<st::Stdout>>);
static_assert(gr::IsGrantTag<gr::stdio::write<st::Debug>>);
static_assert(!gr::IsGrantTag<st::Stderr>);  // policy tag is not a grant
static_assert(gr::which_dim_v<gr::stdio::write<st::Stderr>> == D::Stdio);
static_assert(gr::which_dim_v<gr::stdio::write<st::Debug>> == D::Stdio);
static_assert(gr::which_dim_v<gr::accept_default_strict_for_Stdio> == D::Stdio);
static_assert(sizeof(gr::stdio::write<st::Stderr>) == 1);
static_assert(!std::is_same_v<gr::stdio::write<st::Stderr>, gr::stdio::write<st::Stdout>>);

static_assert(D::StackUse != D::GlobalState);
static_assert(D::GlobalState != D::Stdio);
static_assert(D::StackUse != D::Stdio);

}  // namespace

int main() { return 0; }
