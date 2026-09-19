// Sentinel TU: compiles the header under the project warning flags so its
// self-test block runs.

#include <crucible/fixy/grant/_Dispatch.h>

#include <type_traits>

namespace gr = crucible::fixy::grant;
namespace disp = crucible::fixy::grant::dispatch;
using D = crucible::fixy::dim::DimensionAxis;

namespace {

struct CallbackFamily final {};
struct OtherFamily final {};
struct VtableBase final {};

static_assert(gr::IsGrantTag<disp::indirect_call<CallbackFamily>>);
static_assert(gr::IsGrantTag<disp::virtual_call<VtableBase>>);
static_assert(gr::IsGrantTag<disp::recurses<32>>);
static_assert(gr::IsGrantTag<disp::recurses<0>>);
static_assert(gr::IsGrantTag<disp::tail_call>);

static_assert(gr::which_dim_v<disp::indirect_call<CallbackFamily>> == D::CallShape);
static_assert(gr::which_dim_v<disp::virtual_call<VtableBase>> == D::CallShape);
static_assert(gr::which_dim_v<disp::recurses<32>> == D::CallShape);
static_assert(gr::which_dim_v<disp::tail_call> == D::CallShape);
static_assert(gr::which_dim_v<gr::accept_default_strict_for_CallShape> == D::CallShape);

static_assert(sizeof(disp::indirect_call<CallbackFamily>) == 1);
static_assert(sizeof(disp::virtual_call<VtableBase>) == 1);
static_assert(sizeof(disp::recurses<32>) == 1);
static_assert(sizeof(disp::tail_call) == 1);
static_assert(sizeof(gr::accept_default_strict_for_CallShape) == 1);

// Two recursion sites with different proven depths are distinct types, so they
// land in distinct federation cache slots.
static_assert(!std::is_same_v<disp::recurses<32>, disp::recurses<16>>);
static_assert(std::is_same_v<disp::recurses<8>, disp::recurses<8>>);

static_assert(!std::is_same_v<disp::indirect_call<CallbackFamily>, disp::indirect_call<OtherFamily>>);
static_assert(!std::is_same_v<disp::indirect_call<CallbackFamily>, disp::virtual_call<VtableBase>>);

}  // namespace

int main() { return 0; }
