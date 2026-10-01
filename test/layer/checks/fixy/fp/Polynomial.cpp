// The compile-time checks of fixy/fp/Polynomial.h.

#include <fixy/fp/Polynomial.h>

namespace fixy::fp::detail::polynomial_self_test {

// Bit patterns, never a float compare: a float equality compare is a
// build error here, and the pattern is what a content hash folds anyway.
[[nodiscard]] consteval std::uint32_t bits_of(float value) noexcept { return std::bit_cast<std::uint32_t>(value); }

// ── The identities ──────────────────────────────────────────────────
//
// log(1) is zero, sin(0) is zero and cos(0) is one exactly, in every
// rounding mode, so these are the cells that would catch a transposed
// coefficient or a sign error in the series rather than a last-ulp
// difference.

static_assert(bits_of(log_poly(1.0f)) == bits_of(0.0f), "log_poly(1) must be exactly +0.");
static_assert(bits_of(sin_poly(0.0f)) == bits_of(0.0f), "sin_poly(0) must be exactly +0.");
static_assert(bits_of(sin_poly(-0.0f)) == bits_of(-0.0f), "sin_poly(-0) must keep the sign of the zero.");
static_assert(bits_of(cos_poly(0.0f)) == bits_of(1.0f), "cos_poly(0) must be exactly 1.");

// log(2) is the coefficient the reduction multiplies the exponent by, so
// this cell binds the series to that constant.
static_assert(bits_of(log_poly(2.0f)) == bits_of(0x1.62e43p-1f), "log_poly(2) must be the pinned log(2).");

// The quadrant walk: sin at pi/2 and cos at pi land on the poles, which
// is what says the four switch arms are wired to the right expressions.
static_assert(bits_of(sin_poly(0x1.921FB6p+0f)) == bits_of(1.0f), "sin_poly(pi/2) must be exactly 1.");
static_assert(bits_of(cos_poly(0x1.921FB6p+1f)) == bits_of(-1.0f), "cos_poly(pi) must be exactly -1.");

// ── The pinned pair ─────────────────────────────────────────────────
//
// fixy/fp/Polynomial.h holds the two pinned words, kBoxMullerZ1 and
// kBoxMullerZ2, and says how they were measured.

static_assert(bits_of(0x1.2056aep-3f) == kBoxMullerZ1, "the pinned word and the hex float it documents disagree.");
static_assert(bits_of(-0x1.049aa2p-1f) == kBoxMullerZ2, "the pinned word and the hex float it documents disagree.");

static_assert(bits_of(box_muller_polynomial(0xDEADBEEFu, 0xCAFEBABEu).first) == kBoxMullerZ1,
              "box_muller_polynomial(0xDEADBEEF, 0xCAFEBABE).first moved. The pair is pinned because a "
              "Philox-seeded normal sample reaches a content hash: if this changed on purpose, re-measure "
              "on the development host and say so in the commit.");
static_assert(bits_of(box_muller_polynomial(0xDEADBEEFu, 0xCAFEBABEu).second) == kBoxMullerZ2,
              "box_muller_polynomial(0xDEADBEEF, 0xCAFEBABE).second moved. Same rule as .first.");

// The two outputs of one call are different samples, which is the whole
// point of the transform.
static_assert(kBoxMullerZ1 != kBoxMullerZ2);

// ── The reduction ───────────────────────────────────────────────────

static_assert(reduce_quarter_pi(0.0f).quadrant == 0);
static_assert(reduce_quarter_pi(0x1.921FB6p+0f).quadrant == 1, "pi/2 lands in quadrant 1.");
static_assert(reduce_quarter_pi(0x1.921FB6p+1f).quadrant == 2, "pi lands in quadrant 2.");

// The mask is what makes the four switch arms exhaustive, so the cell
// that says so reads the mask rather than trusting the comment: a
// negative multiple still reports a quadrant in range.
static_assert(reduce_quarter_pi(-0x1.921FB6p+0f).quadrant == 3,
              "-pi/2 is the multiple -1, and the mask must map it to quadrant 3.");

// One turn is exactly four float pi/2, so it reduces to zero in quadrant
// 0 at both signs.
static_assert(reduce_quarter_pi(kOneTurn).quadrant == 0 && bits_of(reduce_quarter_pi(kOneTurn).reduced) == 0u);
static_assert(reduce_quarter_pi(-kOneTurn).quadrant == 0 && bits_of(reduce_quarter_pi(-kOneTurn).reduced) == 0u);

// ── The domains ─────────────────────────────────────────────────────
//
// One turn is admitted at both signs, and the next float up is refused,
// so the bound is exact.  test/fixy/neg/neg_fp_* reach each precondition
// in a constant evaluation.

static_assert(bits_of(kOneTurn) == bits_of(4.0f * 0x1.921FB6p+0f), "one turn must be exactly four float pi/2.");
static_assert(is_within_one_turn(kOneTurn) && is_within_one_turn(-kOneTurn));
static_assert(!is_within_one_turn(0x1.921FB8p+2f) && !is_within_one_turn(-0x1.921FB8p+2f),
              "the first float past one turn must be refused.");
static_assert(!is_within_one_turn(std::numeric_limits<float>::infinity()));
static_assert(!is_within_one_turn(std::numeric_limits<float>::quiet_NaN()));

// The extreme raw words keep box_muller_polynomial inside both domains:
// each call below would stop at a precondition otherwise.  The largest
// word gives a uniform of exactly 1 and an angle of exactly one turn, so
// the radius is -0 and the pair is two negative zeros.
static_assert(std::isfinite(box_muller_polynomial(0u, 0u).first)
              && std::isfinite(box_muller_polynomial(0u, 0u).second));
static_assert(bits_of(box_muller_polynomial(0xFFFFFFFFu, 0xFFFFFFFFu).first) == bits_of(-0.0f));
static_assert(bits_of(box_muller_polynomial(0xFFFFFFFFu, 0xFFFFFFFFu).second) == bits_of(-0.0f));

static_assert(is_positive_normal(std::numeric_limits<float>::min()));
static_assert(is_positive_normal(std::numeric_limits<float>::max()));
static_assert(!is_positive_normal(0.0f) && !is_positive_normal(-0.0f));
static_assert(!is_positive_normal(-1.0f));
static_assert(!is_positive_normal(std::numeric_limits<float>::denorm_min()),
              "a subnormal is refused: the reduction reads its exponent field as a normal exponent.");
static_assert(!is_positive_normal(std::numeric_limits<float>::infinity()));
static_assert(!is_positive_normal(std::numeric_limits<float>::quiet_NaN()));

}  // namespace fixy::fp::detail::polynomial_self_test
