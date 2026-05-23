// FIXY-V-259 sentinel TU: fixy/Simd.h — the WidthBits enum (6 register-
// width classes incl. forward-compat SVE 1024/2048) + the strong-typed
// `grant::simd::width<W>` grant routing onto DimensionAxis::SimdIsa, plus
// the 4 canonical aliases (width_scalar/128/256/512).
//
// Forces every header-embedded static_assert under the project warning
// flags (header-only static_asserts are otherwise unverified —
// feedback_header_only_static_assert_blind_spot) and adds cross-cutting
// checks: WidthBits cardinality, the is_known_width gate's totality, the
// V-258 composition witness (width + vendor::intrinsic individually
// well-formed; the S001 reject of their composition is V-260), and the
// runtime smoke test.
//
// HS14 structural coverage lives in two fixtures in test/fixy_neg/
// (out-of-enum + plausible-but-unlisted widths).  The width<512>-on-AVX2
// COMPOSITION fixture 4.2 ships with V-260's S001 collision rule.

#include <crucible/fixy/Simd.h>
#include <crucible/fixy/Vendor.h>          // composition witness with V-258

#include <meta>
#include <type_traits>
#include <utility>

namespace {

namespace fs  = ::crucible::fixy::simd;
namespace gs  = ::crucible::fixy::grant::simd;
namespace fv  = ::crucible::fixy::vendor;
namespace gv  = ::crucible::fixy::grant::vendor;
namespace gr  = ::crucible::fixy::grant;
using D = ::crucible::fixy::dim::DimensionAxis;

// ── WidthBits cardinality — 6 width classes (reflection-derived) ──────
static_assert(std::meta::enumerators_of(^^fs::WidthBits).size() == 6,
    "WidthBits diverged from {Scalar, 128, 256, 512, 1024, 2048}.  Append "
    "wider classes at the next free bit-width; never renumber an existing "
    "class (FOUND-I04 append-only — a stored row_hash keys on the value).");

// ── Width values are the actual bit counts (the wire contract) ────────
static_assert(std::to_underlying(fs::WidthBits::Scalar)   == 0);
static_assert(std::to_underlying(fs::WidthBits::Bits128)  == 128);
static_assert(std::to_underlying(fs::WidthBits::Bits256)  == 256);
static_assert(std::to_underlying(fs::WidthBits::Bits512)  == 512);
static_assert(std::to_underlying(fs::WidthBits::Bits1024) == 1024);
static_assert(std::to_underlying(fs::WidthBits::Bits2048) == 2048);

// ── is_known_width gate — accepts the 6, rejects out-of-enum ──────────
static_assert( fs::is_known_width_v<fs::WidthBits::Bits256>);
static_assert( fs::is_known_width_v<fs::WidthBits::Bits2048>);  // SVE forward-compat
static_assert(!fs::is_known_width(static_cast<fs::WidthBits>(64)));
static_assert(!fs::is_known_width(static_cast<fs::WidthBits>(777)));

// ── width<W> routes to SimdIsa (the V-253 axis), all 4 aliases valid ──
static_assert(gr::which_dim_v<gs::width<fs::WidthBits::Bits256>> == D::SimdIsa);
static_assert(gr::IsGrantTag<fs::width_scalar>);
static_assert(gr::IsGrantTag<fs::width_128>);
static_assert(gr::IsGrantTag<fs::width_256>);
static_assert(gr::IsGrantTag<fs::width_512>);

// ── V-258 composition witness — width_512 AND vendor::avx2_intrinsic
//    are EACH well-formed grants today.  The S001 collision rule that
//    rejects their COMPOSITION (AVX-512 width on AVX2 family) is V-260;
//    until then both are individually valid (fixture 4.2 ships with
//    V-260).  This pins the forward-compat contract: V-259 must not
//    pre-empt S001 by rejecting either grant standalone.
static_assert(gr::IsGrantTag<fs::width_512>);
static_assert(gr::IsGrantTag<fv::avx2_intrinsic>);
static_assert(gr::which_dim_v<fs::width_512>     == D::SimdIsa);
static_assert(gr::which_dim_v<fv::avx2_intrinsic> == D::HwInstruction);
// The two grants engage DIFFERENT axes (SimdIsa vs HwInstruction), which
// is exactly why a single binding can carry both — S001 reasons about
// their VALUE compatibility, not axis collision.
static_assert(gr::which_dim_v<fs::width_512> != gr::which_dim_v<fv::avx2_intrinsic>);

}  // namespace

int main() {
    ::crucible::fixy::simd::detail::v259_self_test::runtime_smoke_test();
    return 0;
}
