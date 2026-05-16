// ════════════════════════════════════════════════════════════════════
// example_fixy_modality_aware — FIXY-G10 worked example
//
// THE PATTERN: A Mimic NV emit binding RENDERED AS A MODALITY-CLASSIFIED
// GRANT PACK.  Each grant's modality class is annotated; downstream
// readers can answer "does this binding declare its precision?  or
// require it from the caller?" by inspection.
//
// Three small per-modality summary tables follow the binding,
// demonstrating that the production fixy::fn shape carries the
// same modality information available to category-pair rules R017
// and R018.
// ════════════════════════════════════════════════════════════════════

#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Modality.h>
#include <crucible/fixy/Rules.h>

#include <cstdint>
#include <cstdio>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace cr = crucible::fixy::rule;

namespace {

// ── Mimic NV emit binding — modality-annotated ──────────────────────

using MimicEmitFn = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,           //  Frame
    cf::accept_default_strict_for<cd::Refinement>,     //  Frame
    cg::copy,                                          //  Frame (Usage relaxed)
    cg::with<crucible::effects::Effect::Bg,
             crucible::effects::Effect::Alloc>,        //  Requires (Effect)
    cf::accept_default_strict_for<cd::Security>,       //  Frame
    cf::accept_default_strict_for<cd::Protocol>,       //  Frame
    cf::accept_default_strict_for<cd::Lifetime>,       //  Frame
    cf::accept_default_strict_for<cd::Provenance>,     //  Frame
    cf::accept_default_strict_for<cd::Trust>,          //  Frame
    cg::vendor_nv,                                     //  Quotient (Representation)
    cf::accept_default_strict_for<cd::Observability>,  //  Frame
    cf::accept_default_strict_for<cd::Complexity>,     //  Frame
    cg::precision_f32,                                 //  Declares (Precision)
    cf::accept_default_strict_for<cd::Space>,          //  Frame
    cf::accept_default_strict_for<cd::Overflow>,       //  Frame
    cg::mutable_in_place,                              //  Declares (Mutation)
    cg::reentrant,                                     //  Frame (Reentrancy)
    cf::accept_default_strict_for<cd::Size>,           //  Frame
    cf::accept_default_strict_for<cd::Version>,        //  Frame
    cf::accept_default_strict_for<cd::Staleness>       //  Frame
>;

// ── Modality-class distribution (constexpr-asserted) ────────────────
//
// The Mimic NV emit shape has:
//   * 17 Frame   (the bulk: strict-default acks + copy + reentrant)
//   * 2  Declares (Precision via precision_f32, Mutation via mutable_in_place)
//   * 1  Requires (Effect via with<Bg, Alloc>)
//   * 0  Linear   (no Permission discipline at this layer — that's
//                   the lifetime_region<Tag> case)
//   * 1  Quotient (Representation via vendor_nv)

// Spot-check via grant_traits.
static_assert(cf::grant_traits<cg::copy>::modality_class_v
              == cf::ModalityClass::Frame);
static_assert(cf::grant_traits<cg::reentrant>::modality_class_v
              == cf::ModalityClass::Frame);
static_assert(cf::grant_traits<cg::precision_f32>::modality_class_v
              == cf::ModalityClass::Declares);
static_assert(cf::grant_traits<cg::mutable_in_place>::modality_class_v
              == cf::ModalityClass::Declares);
static_assert(cf::grant_traits<cg::with<crucible::effects::Effect::Bg,
                                        crucible::effects::Effect::Alloc>
                              >::modality_class_v
              == cf::ModalityClass::Requires);
static_assert(cf::grant_traits<cg::vendor_nv>::modality_class_v
              == cf::ModalityClass::Quotient);

// R017 / R018 — well-formed binding passes.
static_assert(cr::R017_no_linear_alias_v<MimicEmitFn>);
static_assert(cr::R018_frame_declares_consistency_v<MimicEmitFn>);

}  // namespace

int main() {
    // Runtime smoke — print the per-modality class names for the
    // representative grants in the binding.
    constexpr cf::ModalityClass mc_copy =
        cf::grant_traits<cg::copy>::modality_class_v;
    constexpr cf::ModalityClass mc_precision =
        cf::grant_traits<cg::precision_f32>::modality_class_v;
    constexpr cf::ModalityClass mc_effect =
        cf::grant_traits<cg::with<crucible::effects::Effect::Bg,
                                  crucible::effects::Effect::Alloc>
                        >::modality_class_v;
    constexpr cf::ModalityClass mc_vendor =
        cf::grant_traits<cg::vendor_nv>::modality_class_v;

    if (mc_copy != cf::ModalityClass::Frame) return 1;
    if (mc_precision != cf::ModalityClass::Declares) return 2;
    if (mc_effect != cf::ModalityClass::Requires) return 3;
    if (mc_vendor != cf::ModalityClass::Quotient) return 4;

    if (!cr::R017_no_linear_alias_v<MimicEmitFn>) return 5;
    if (!cr::R018_frame_declares_consistency_v<MimicEmitFn>) return 6;

    std::fputs("example_fixy_modality_aware: OK\n", stdout);
    return 0;
}
