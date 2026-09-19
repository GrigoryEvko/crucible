// Which instruction-set extension a compiled kernel needs is a separate
// question from who compiled it.  Two kernels can come from the same
// vendor and still not be interchangeable, because one issues
// instructions the other host cannot decode.
//
// That requirement is two trunks, an x86 one and an ARM one, meeting
// only at the scalar bottom and the portable top.  A richer extension
// within a trunk subsumes a poorer one; across trunks nothing subsumes
// anything, because neither host can execute the other's encoding.
// Every admission rule built on this lattice reduces to that
// incomparability, so if the two trunks were ever collapsed into one
// chain the rules would keep compiling and start admitting binaries
// that fault on the host they are handed to.  The cross-trunk negatives
// and the non-distributivity witness below are what stop that: a single
// chain would be distributive, and this lattice must not be.
//
// The assertions are re-stated at translation-unit scope on purpose.  A
// static_assert that lives only in a header is never evaluated under
// the project warning flags until some translation unit includes it.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>
#include <crucible/algebra/lattices/SimdIsaLattice.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cal = ::crucible::algebra::lattices;
namespace si = ::crucible::algebra::lattices::simd_isa;

namespace {

using cal::SimdIsa;
using L = cal::SimdIsaLattice;

static_assert(crucible::algebra::Lattice<L>, "SimdIsaLattice must satisfy the Lattice concept "
                                             "(element_type + leq + join + meet).");
static_assert(crucible::algebra::BoundedLattice<L>, "SimdIsaLattice has both bottom() (Scalar) and top() "
                                                    "(Portable) — it is a bounded lattice.");
static_assert(!crucible::algebra::UnboundedLattice<L>);
static_assert(!crucible::algebra::Semiring<L>, "SimdIsaLattice is NOT a semiring — it carries no "
                                               "equality+add+mul algebra, only the order-theoretic operations.");

static_assert(cal::simd_isa_count == 15, "SimdIsa must have exactly 15 enumerators. Adding an ISA requires "
                                         "placing it inside the correct trunk numeric range so the trunk "
                                         "classifiers pick it up, extending every name switch, and "
                                         "revisiting each admission rule that reads this order.");

static_assert(std::is_same_v<std::underlying_type_t<SimdIsa>, std::uint8_t>,
              "SimdIsa must use uint8_t underlying type — the trunk is "
              "packed into the high nibble (x86=0x1_, ARM=0x2_) so within-trunk "
              "integer order equals capability rank.");

static_assert(std::to_underlying(SimdIsa::Scalar) == 0x00, "Scalar = bottom sentinel");
static_assert(std::to_underlying(SimdIsa::Sse2) == 0x10, "x86 trunk base");
static_assert(std::to_underlying(SimdIsa::Avx512Bw) == 0x17, "x86 trunk top");
static_assert(std::to_underlying(SimdIsa::Neon) == 0x20, "ARM trunk base");
static_assert(std::to_underlying(SimdIsa::Sve2) == 0x24, "ARM trunk top");
static_assert(std::to_underlying(SimdIsa::Portable) == 0xFF, "Portable = top sentinel");

static_assert(cal::simd_isa_is_x86(SimdIsa::Sse2));
static_assert(cal::simd_isa_is_x86(SimdIsa::Avx2));
static_assert(cal::simd_isa_is_x86(SimdIsa::Avx512Bw));
static_assert(!cal::simd_isa_is_x86(SimdIsa::Neon));
static_assert(!cal::simd_isa_is_x86(SimdIsa::Scalar), "Scalar belongs to NEITHER trunk — it is the shared bottom, so "
                                                      "same-trunk must be false for it and leq must special-case it.");
static_assert(!cal::simd_isa_is_x86(SimdIsa::Portable));
static_assert(cal::simd_isa_is_arm(SimdIsa::Neon));
static_assert(cal::simd_isa_is_arm(SimdIsa::Sve2));
static_assert(!cal::simd_isa_is_arm(SimdIsa::Avx2));
static_assert(!cal::simd_isa_is_arm(SimdIsa::Portable));
static_assert(cal::simd_isa_same_trunk(SimdIsa::Sse2, SimdIsa::Avx512Bw));
static_assert(cal::simd_isa_same_trunk(SimdIsa::Neon, SimdIsa::Sve2));
static_assert(!cal::simd_isa_same_trunk(SimdIsa::Avx2, SimdIsa::Sve),
              "An x86 extension and an ARM extension are the cross-trunk case, "
              "which is where the incomparability comes from.");

static_assert(L::bottom() == SimdIsa::Scalar);
static_assert(L::top() == SimdIsa::Portable);

static_assert(L::leq(SimdIsa::Sse2, SimdIsa::Sse3));
static_assert(L::leq(SimdIsa::Sse3, SimdIsa::Ssse3));
static_assert(L::leq(SimdIsa::Ssse3, SimdIsa::Sse41));
static_assert(L::leq(SimdIsa::Sse41, SimdIsa::Sse42));
static_assert(L::leq(SimdIsa::Sse42, SimdIsa::Avx2));
static_assert(L::leq(SimdIsa::Avx2, SimdIsa::Avx512F));
static_assert(L::leq(SimdIsa::Avx512F, SimdIsa::Avx512Bw));
static_assert(L::leq(SimdIsa::Sse2, SimdIsa::Avx512Bw), "transitive endpoints");
static_assert(L::leq(SimdIsa::Avx2, SimdIsa::Avx512F), "An AVX2-pinned binary IS satisfied by an AVX-512 host.");
static_assert(!L::leq(SimdIsa::Avx2, SimdIsa::Sse2), "An AVX2-pinned binary is NOT satisfied by an SSE2-only host. "
                                                     "Admitting it would raise an undefined-instruction fault.");

static_assert(L::leq(SimdIsa::Neon, SimdIsa::NeonFp16));
static_assert(L::leq(SimdIsa::NeonFp16, SimdIsa::NeonDotProduct));
static_assert(L::leq(SimdIsa::NeonDotProduct, SimdIsa::Sve));
static_assert(L::leq(SimdIsa::Sve, SimdIsa::Sve2));
static_assert(L::leq(SimdIsa::Neon, SimdIsa::Sve2), "transitive endpoints");
static_assert(!L::leq(SimdIsa::Sve2, SimdIsa::Neon), "descending is false");

static_assert(L::leq(SimdIsa::Scalar, SimdIsa::Avx2));
static_assert(L::leq(SimdIsa::Scalar, SimdIsa::Sve2));
static_assert(L::leq(SimdIsa::Scalar, SimdIsa::Portable));
static_assert(L::leq(SimdIsa::Avx512Bw, SimdIsa::Portable));
static_assert(L::leq(SimdIsa::Sve2, SimdIsa::Portable));

// Every x86 and ARM pair is incomparable in both directions.  One
// direction alone would not be enough: a binary must be refused on the
// other trunk's host whichever side it was built for.
static_assert(!L::leq(SimdIsa::Avx2, SimdIsa::Sve));
static_assert(!L::leq(SimdIsa::Sve, SimdIsa::Avx2));
static_assert(!L::leq(SimdIsa::Sse2, SimdIsa::Neon));
static_assert(!L::leq(SimdIsa::Neon, SimdIsa::Sse2));
static_assert(!L::leq(SimdIsa::Avx512Bw, SimdIsa::Sve2));
static_assert(!L::leq(SimdIsa::Sve2, SimdIsa::Avx512Bw));
static_assert(!L::leq(SimdIsa::Avx512F, SimdIsa::Neon));
static_assert(!L::leq(SimdIsa::Neon, SimdIsa::Avx512F));

static_assert(!L::leq(SimdIsa::Portable, SimdIsa::Avx2));
static_assert(!L::leq(SimdIsa::Portable, SimdIsa::Scalar));
static_assert(!L::leq(SimdIsa::Avx2, SimdIsa::Scalar));
static_assert(!L::leq(SimdIsa::Sve, SimdIsa::Scalar));

static_assert(L::join(SimdIsa::Sse2, SimdIsa::Avx2) == SimdIsa::Avx2);
static_assert(L::join(SimdIsa::Neon, SimdIsa::Sve) == SimdIsa::Sve);
static_assert(L::join(SimdIsa::Avx2, SimdIsa::Sve) == SimdIsa::Portable,
              "The only common upper bound of an x86 extension and an ARM "
              "extension is the portable kernel that needs neither.");
static_assert(L::join(SimdIsa::Sse2, SimdIsa::Neon) == SimdIsa::Portable);
static_assert(L::join(SimdIsa::Scalar, SimdIsa::Avx2) == SimdIsa::Avx2, "Scalar is the join identity");
static_assert(L::join(SimdIsa::Portable, SimdIsa::Sve) == SimdIsa::Portable, "Portable absorbs in join");

static_assert(L::meet(SimdIsa::Sse2, SimdIsa::Avx2) == SimdIsa::Sse2);
static_assert(L::meet(SimdIsa::Neon, SimdIsa::Sve) == SimdIsa::Neon);
static_assert(L::meet(SimdIsa::Avx2, SimdIsa::Sve) == SimdIsa::Scalar,
              "The only common lower bound of an x86 extension and an ARM "
              "extension is the scalar floor with no vector unit at all.");
static_assert(L::meet(SimdIsa::Sse2, SimdIsa::Neon) == SimdIsa::Scalar);
static_assert(L::meet(SimdIsa::Portable, SimdIsa::Sve) == SimdIsa::Sve, "Portable is the meet identity");
static_assert(L::meet(SimdIsa::Scalar, SimdIsa::Avx2) == SimdIsa::Scalar, "Scalar absorbs in meet");

// Working the two sides out by hand, with Avx2 on one trunk and Neon
// and Sve on the other:
//
//   (Avx2 join Neon) meet Sve = Portable meet Sve = Sve
//   (Avx2 meet Sve) join (Neon meet Sve) = Scalar join Neon = Neon
//
// The two sides differ, so the lattice is not distributive.  A single
// chain would be, which is what makes this assertion the guard against
// collapsing the trunks.
static_assert(L::meet(L::join(SimdIsa::Avx2, SimdIsa::Neon), SimdIsa::Sve) == SimdIsa::Sve,
              "The left side of the distributivity test must be Sve.");
static_assert(L::join(L::meet(SimdIsa::Avx2, SimdIsa::Sve), L::meet(SimdIsa::Neon, SimdIsa::Sve)) == SimdIsa::Neon,
              "The right side of the distributivity test must be Neon: Neon and "
              "Sve share a trunk, so their meet is Neon and not Scalar.");
static_assert(L::meet(L::join(SimdIsa::Avx2, SimdIsa::Neon), SimdIsa::Sve)
                  != L::join(L::meet(SimdIsa::Avx2, SimdIsa::Sve), L::meet(SimdIsa::Neon, SimdIsa::Sve)),
              "SimdIsaLattice MUST be non-distributive. If this fires, the two "
              "trunks have been collapsed into a single chain, and every admission "
              "rule that relies on cross-trunk incomparability now admits binaries "
              "it must refuse.");

static_assert(crucible::algebra::Lattice<si::ScalarIsa>);
static_assert(crucible::algebra::Lattice<si::Avx2Isa>);
static_assert(crucible::algebra::Lattice<si::SveIsa>);
static_assert(crucible::algebra::BoundedLattice<si::PortableIsa>);
static_assert(std::is_empty_v<si::ScalarIsa::element_type>,
              "At<Scalar>::element_type must be empty so that grading a payload "
              "with it collapses to sizeof(payload) — an ISA annotation that "
              "costs no bytes at any binding site.");
static_assert(std::is_empty_v<si::Avx2Isa::element_type>);
static_assert(std::is_empty_v<si::SveIsa::element_type>);
static_assert(std::is_empty_v<si::PortableIsa::element_type>);
static_assert(si::Avx2Isa::isa == SimdIsa::Avx2, "At<I>::isa must equal I at the type level, so a consumer reads the "
                                                 "pinned ISA without carrying any runtime data.");
static_assert(si::SveIsa::isa == SimdIsa::Sve);
static_assert(si::PortableIsa::isa == SimdIsa::Portable);

struct EightByteValue {
    unsigned long long v{0};
};
static_assert(sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute, si::Avx2Isa, EightByteValue>)
                  == sizeof(EightByteValue),
              "Pinning an AVX2 ISA grade must add zero bytes to an 8-byte "
              "payload.");
static_assert(sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute, si::PortableIsa, int>)
              == sizeof(int));

static_assert(L::name() == std::string_view{"SimdIsaLattice"});
static_assert(si::Avx2Isa::name() == std::string_view{"SimdIsaLattice::At<Avx2>"});
static_assert(si::NeonIsa::name() == std::string_view{"SimdIsaLattice::At<Neon>"});
static_assert(si::ScalarIsa::name() == std::string_view{"SimdIsaLattice::At<Scalar>"});
static_assert(cal::simd_isa_name(SimdIsa::Sve2) == std::string_view{"Sve2"});

}  // namespace

int main() {
    cal::detail::simd_isa_lattice_self_test::runtime_smoke_test();
    return 0;
}
