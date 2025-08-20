#pragma once
// Synthetic cite fixture for --self-test.
namespace crucible::selftest {
inline void planted_macro_forms(int n) {
    CRUCIBLE_PRE(n > 0);
    CRUCIBLE_PRE_FAST(n > 0);
    CRUCIBLE_PRE_MSG(n > 0, "synthetic");
    CRUCIBLE_POST(r, r > 0);
    CRUCIBLE_POST_FAST(r, r > 0);
    CRUCIBLE_POST_MSG(r, r > 0, "synthetic");
    contract_assert(n > 0);
}
inline int planted_p2900_forms(int const n)
    pre (n > 0)
    post (r: r > 0)
{ return n; }
inline void planted_decide_cites(int n) {
    decide::is_non_zero(n);
    decide::in_range(n);
    decide::all_in_range(n);
    decide::all_in_range(n);
    decide::aligned_in_range(n);
    decide::aligned_in_range(n);
    decide::no_overflow_mul(n);
    decide::no_overflow_mul(n);
    decide::no_overflow_sum(n);
    decide::no_overflow_sum(n);
    decide::no_overflow_pow2_shift(n);
    decide::no_overflow_pow2_shift(n);
    decide::is_power_of_two_le(n);
    decide::is_power_of_two_le(n);
    decide::factorization_eq(n);
    decide::factorization_eq(n);
    decide::coprime(n);
    decide::coprime(n);
    decide::intervals_pairwise_disjoint(n);
    decide::intervals_pairwise_disjoint(n);
    decide::intervals_cover_unit(n);
    decide::intervals_cover_unit(n);
    decide::tier_replaces(n);
    decide::tier_replaces(n);
    decide::row_subset(n);
    decide::row_subset(n);
    decide::fmix_preserves_non_zero(n);
    decide::fmix_preserves_non_zero(n);
    decide::strictly_increasing(n);
    decide::strictly_increasing(n);
    decide::weakly_increasing(n);
    decide::weakly_increasing(n);
    decide::conjunction(n);
    decide::conjunction(n);
    decide::disjunction(n);
    decide::disjunction(n);
    decide::implies(n);
    decide::implies(n);
    decide::positive(n);
    decide::positive(n);
    decide::non_negative(n);
    decide::non_negative(n);
    decide::valid_span(n);
    decide::valid_span(n);
}
}  // namespace crucible::selftest
