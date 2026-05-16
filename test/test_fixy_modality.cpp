// ── test_fixy_modality — FIXY-G10 positive test ───────────────────────
//
// Pins the modality classification + R017/R018 surface across the
// grant catalog + grant_traits projection + default_witness_for_class
// bridge to G9.

#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Modality.h>
#include <crucible/fixy/Rules.h>
#include <crucible/algebra/Modality.h>
#include <crucible/safety/witness/Witness.h>

#include <cstdio>
#include <type_traits>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace cr = crucible::fixy::rule;
namespace alg = crucible::algebra;
namespace sw = crucible::safety::witness;

namespace {

// ─── ModalityClass enum surface ─────────────────────────────────────

static_assert(cf::modality_class_name(cf::ModalityClass::Frame)    == "Frame");
static_assert(cf::modality_class_name(cf::ModalityClass::Declares) == "Declares");
static_assert(cf::modality_class_name(cf::ModalityClass::Requires) == "Requires");
static_assert(cf::modality_class_name(cf::ModalityClass::Linear)   == "Linear");
static_assert(cf::modality_class_name(cf::ModalityClass::Quotient) == "Quotient");

// ─── classify_modality_v — algebra → fixy ──────────────────────────

static_assert(cf::classify_modality_v<alg::modality::Absolute_t>
              == cf::ModalityClass::Frame);
static_assert(cf::classify_modality_v<alg::modality::Comonad_t>
              == cf::ModalityClass::Declares);
static_assert(cf::classify_modality_v<alg::modality::RelativeMonad_t>
              == cf::ModalityClass::Requires);
static_assert(cf::classify_modality_v<alg::modality::Relative_t>
              == cf::ModalityClass::Linear);
static_assert(cf::classify_modality_v<alg::modality::Quotient_t>
              == cf::ModalityClass::Quotient);

// ─── grant_traits per-grant classification ─────────────────────────

// Frame-modality bases.
static_assert(cf::grant_traits<cg::reentrant>::modality_class_v
              == cf::ModalityClass::Frame);
static_assert(cf::grant_traits<cg::copy>::modality_class_v
              == cf::ModalityClass::Frame);
static_assert(cf::grant_traits<cg::complexity_constant>::modality_class_v
              == cf::ModalityClass::Frame);
static_assert(cf::grant_traits<cg::sized<8>>::modality_class_v
              == cf::ModalityClass::Frame);
static_assert(cf::grant_traits<cg::repr_packed>::modality_class_v
              == cf::ModalityClass::Frame);

// Declares-modality bases.
static_assert(cf::grant_traits<cg::mutable_in_place>::modality_class_v
              == cf::ModalityClass::Declares);
static_assert(cf::grant_traits<cg::append_only>::modality_class_v
              == cf::ModalityClass::Declares);
static_assert(cf::grant_traits<cg::precision_f32>::modality_class_v
              == cf::ModalityClass::Declares);
static_assert(cf::grant_traits<cg::declassify<int>>::modality_class_v
              == cf::ModalityClass::Declares);
static_assert(cf::grant_traits<cg::from_source<int>>::modality_class_v
              == cf::ModalityClass::Declares);
static_assert(cf::grant_traits<cg::trust_assumed_for<int>>::modality_class_v
              == cf::ModalityClass::Declares);
static_assert(cf::grant_traits<cg::observability_visible>::modality_class_v
              == cf::ModalityClass::Declares);

// Requires-modality bases.
static_assert(cf::grant_traits<cg::refined_with<int>>::modality_class_v
              == cf::ModalityClass::Requires);
static_assert(cf::grant_traits<cg::with<crucible::effects::Effect::IO>>::modality_class_v
              == cf::ModalityClass::Requires);
static_assert(cf::grant_traits<cg::overflow_wrap>::modality_class_v
              == cf::ModalityClass::Requires);
static_assert(cf::grant_traits<cg::overflow_saturate>::modality_class_v
              == cf::ModalityClass::Requires);

// Linear-modality bases.
static_assert(cf::grant_traits<cg::lifetime_region<0>>::modality_class_v
              == cf::ModalityClass::Linear);

// Quotient-modality bases.
static_assert(cf::grant_traits<cg::version<3>>::modality_class_v
              == cf::ModalityClass::Quotient);
static_assert(cf::grant_traits<cg::vendor_nv>::modality_class_v
              == cf::ModalityClass::Quotient);
static_assert(cf::grant_traits<cg::recipe_tier<cg::Tolerance::BITEXACT>>::modality_class_v
              == cf::ModalityClass::Quotient);
static_assert(cf::grant_traits<cg::forge_phase<cg::ForgePhase::Ingest>>::modality_class_v
              == cf::ModalityClass::Quotient);
static_assert(cf::grant_traits<cg::transport_tier<cg::TransportTier::Tcp>>::modality_class_v
              == cf::ModalityClass::Quotient);

// ─── Evidenced variants preserve modality class ────────────────────

using T1 = sw::Tested<1>;
static_assert(cf::grant_traits<cg::reentrant_e<T1>>::modality_class_v
              == cf::ModalityClass::Frame);
static_assert(cf::grant_traits<cg::mutable_in_place_e<T1>>::modality_class_v
              == cf::ModalityClass::Declares);
static_assert(cf::grant_traits<cg::overflow_wrap_e<T1>>::modality_class_v
              == cf::ModalityClass::Requires);
static_assert(cf::grant_traits<cg::lifetime_region_e<0, T1>>::modality_class_v
              == cf::ModalityClass::Linear);
static_assert(cf::grant_traits<cg::vendor_backend_e<cg::VendorBackend::NV, T1>>::modality_class_v
              == cf::ModalityClass::Quotient);

// ─── default_witness_for_class bridge to G9 ────────────────────────

static_assert(std::is_same_v<cf::default_witness_for_class<cf::ModalityClass::Frame>,
                             sw::Asserted<sw::UnnamedRationale>>);
static_assert(std::is_same_v<cf::default_witness_for_class<cf::ModalityClass::Declares>,
                             sw::Tested<0>>);
static_assert(std::is_same_v<cf::default_witness_for_class<cf::ModalityClass::Requires>,
                             sw::Asserted<sw::UnnamedRationale>>);
static_assert(std::is_same_v<cf::default_witness_for_class<cf::ModalityClass::Linear>,
                             sw::CrossValidated<0>>);
static_assert(std::is_same_v<cf::default_witness_for_class<cf::ModalityClass::Quotient>,
                             sw::Tested<0>>);

// ─── R017 / R018 ──────────────────────────────────────────────────

// Clean binding — one engagement per dim.
using OneLinearFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cg::lifetime_region<0>,                            // Linear #1
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>
>;

static_assert(cr::R017_no_linear_alias_v<OneLinearFn>,
    "Single Linear on Lifetime must pass R017.");
static_assert(cr::R018_frame_declares_consistency_v<OneLinearFn>,
    "Single-engagement-per-dim must pass R018.");

}  // namespace

int main() {
    // Runtime smoke — exercise classify + grant_traits at runtime.
    auto frame_class = cf::grant_traits<cg::reentrant>::modality_class_v;
    auto declares_class = cf::grant_traits<cg::mutable_in_place>::modality_class_v;
    auto requires_class = cf::grant_traits<cg::overflow_wrap>::modality_class_v;
    auto linear_class = cf::grant_traits<cg::lifetime_region<0>>::modality_class_v;
    auto quotient_class = cf::grant_traits<cg::vendor_nv>::modality_class_v;

    if (frame_class != cf::ModalityClass::Frame) {
        std::fprintf(stderr, "reentrant should be Frame\n");
        return 1;
    }
    if (declares_class != cf::ModalityClass::Declares) {
        std::fprintf(stderr, "mutable_in_place should be Declares\n");
        return 2;
    }
    if (requires_class != cf::ModalityClass::Requires) {
        std::fprintf(stderr, "overflow_wrap should be Requires\n");
        return 3;
    }
    if (linear_class != cf::ModalityClass::Linear) {
        std::fprintf(stderr, "lifetime_region should be Linear\n");
        return 4;
    }
    if (quotient_class != cf::ModalityClass::Quotient) {
        std::fprintf(stderr, "vendor_nv should be Quotient\n");
        return 5;
    }

    bool one_linear_r017 = cr::R017_no_linear_alias_v<OneLinearFn>;
    if (!one_linear_r017) {
        std::fprintf(stderr, "OneLinearFn should pass R017\n");
        return 6;
    }

    std::fputs("test_fixy_modality: OK\n", stdout);
    return 0;
}
