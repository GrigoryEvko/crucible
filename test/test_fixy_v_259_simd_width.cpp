// Sentinel TU: compiles the header under the project warning flags so its
// static_asserts run.

#include <crucible/fixy/Simd.h>
#include <crucible/fixy/Vendor.h>

#include <meta>
#include <type_traits>
#include <utility>

namespace {

namespace fs = ::crucible::fixy::simd;
namespace gs = ::crucible::fixy::grant::simd;
namespace fv = ::crucible::fixy::vendor;
namespace gv = ::crucible::fixy::grant::vendor;
namespace gr = ::crucible::fixy::grant;
using D = ::crucible::fixy::dim::DimensionAxis;

static_assert(std::meta::enumerators_of(^^fs::WidthBits).size() == 6,
              "WidthBits diverged from {Scalar, 128, 256, 512, 1024, 2048}.  Append "
              "wider classes at the next free bit-width.  Never renumber an existing "
              "class, because a stored row hash keys on the value.");

// The enumerator value is the bit count, and that is the wire contract.
static_assert(std::to_underlying(fs::WidthBits::Scalar) == 0);
static_assert(std::to_underlying(fs::WidthBits::Bits128) == 128);
static_assert(std::to_underlying(fs::WidthBits::Bits256) == 256);
static_assert(std::to_underlying(fs::WidthBits::Bits512) == 512);
static_assert(std::to_underlying(fs::WidthBits::Bits1024) == 1024);
static_assert(std::to_underlying(fs::WidthBits::Bits2048) == 2048);

static_assert(fs::is_known_width_v<fs::WidthBits::Bits256>);
static_assert(fs::is_known_width_v<fs::WidthBits::Bits2048>);  // SVE forward-compat
static_assert(!fs::is_known_width(static_cast<fs::WidthBits>(64)));
static_assert(!fs::is_known_width(static_cast<fs::WidthBits>(777)));

static_assert(gr::which_dim_v<gs::width<fs::WidthBits::Bits256>> == D::SimdIsa);
static_assert(gr::IsGrantTag<fs::width_scalar>);
static_assert(gr::IsGrantTag<fs::width_128>);
static_assert(gr::IsGrantTag<fs::width_256>);
static_assert(gr::IsGrantTag<fs::width_512>);

// An AVX-512 width and an AVX2 intrinsic are each well-formed grants on their
// own.  Rejecting the pair is the job of the composition rule, so neither grant
// may reject the other standalone.
static_assert(gr::IsGrantTag<fs::width_512>);
static_assert(gr::IsGrantTag<fv::avx2_intrinsic>);
static_assert(gr::which_dim_v<fs::width_512> == D::SimdIsa);
static_assert(gr::which_dim_v<fv::avx2_intrinsic> == D::HwInstruction);
// The two grants sit on different axes, so one binding can carry both.  The
// composition rule reasons about their values, not about an axis collision.
static_assert(gr::which_dim_v<fs::width_512> != gr::which_dim_v<fv::avx2_intrinsic>);

}  // namespace

int main() {
    ::crucible::fixy::simd::detail::v259_self_test::runtime_smoke_test();
    return 0;
}
