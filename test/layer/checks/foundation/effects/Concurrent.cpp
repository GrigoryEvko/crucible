// The compile-time checks of foundation/effects/Concurrent.h.

#include <foundation/effects/Concurrent.h>

namespace foundation::effects {

namespace detail::concurrent_row_self_test {

using resource::CarbonGramsPerKwh;
using resource::HbmBytes;
using resource::NicQp;
using resource::SmBudget;

// A pack that the row refuses, and a pair that the sum refuses, answer
// false here instead of stopping the build.
template <typename... Tags>
concept row_is_formable = requires { typename ConcurrentRow<Tags...>; };

template <typename R1, typename R2>
concept sum_is_formable = requires { typename concurrent_row_sum_t<R1, R2>; };

static_assert(std::is_same_v<concurrent_row_sum_t<ConcurrentRow<>, ConcurrentRow<>>, ConcurrentRow<>>);
static_assert(
    std::is_same_v<concurrent_row_sum_t<ConcurrentRow<SmBudget<32>>, ConcurrentRow<>>, ConcurrentRow<SmBudget<32>>>);
static_assert(
    std::is_same_v<concurrent_row_sum_t<ConcurrentRow<>, ConcurrentRow<SmBudget<32>>>, ConcurrentRow<SmBudget<32>>>);
static_assert(std::is_same_v<concurrent_row_sum_t<ConcurrentRow<SmBudget<32>>, ConcurrentRow<SmBudget<64>>>,
                             ConcurrentRow<SmBudget<96>>>);

// The two axes come out in catalog order, which puts the compute axis
// before the network one, whichever side names which.
static_assert(std::is_same_v<concurrent_row_sum_t<ConcurrentRow<SmBudget<32>>, ConcurrentRow<NicQp<4>>>,
                             ConcurrentRow<SmBudget<32>, NicQp<4>>>);
static_assert(std::is_same_v<concurrent_row_sum_t<ConcurrentRow<NicQp<4>>, ConcurrentRow<SmBudget<32>>>,
                             ConcurrentRow<SmBudget<32>, NicQp<4>>>);
static_assert(
    std::is_same_v<concurrent_row_sum_t<ConcurrentRow<SmBudget<32>, NicQp<4>>, ConcurrentRow<SmBudget<64>, NicQp<2>>>,
                   ConcurrentRow<SmBudget<96>, NicQp<6>>>);

// The last axis of the catalog joins the sum, so the walk reaches the
// end of the catalog.
static_assert(std::is_same_v<concurrent_row_sum_t<ConcurrentRow<CarbonGramsPerKwh<3>>, ConcurrentRow<SmBudget<1>>>,
                             ConcurrentRow<SmBudget<1>, CarbonGramsPerKwh<3>>>);

static_assert(std::is_same_v<concurrent_row_n_t<ConcurrentRow<SmBudget<10>>, ConcurrentRow<SmBudget<20>>,
                                                ConcurrentRow<SmBudget<30>>, ConcurrentRow<SmBudget<40>>>,
                             ConcurrentRow<SmBudget<100>>>);
static_assert(std::is_same_v<concurrent_row_n_t<>, ConcurrentRow<>>);
static_assert(std::is_same_v<concurrent_row_n_t<ConcurrentRow<SmBudget<32>>>, ConcurrentRow<SmBudget<32>>>);

// A sum that wraps on one axis, at any position of the fold, names no
// type, and neither does an argument that is not a row.  A sum that
// reaches the top of uint64_t exactly still exists.
template <typename... Rs>
concept HasConcurrentSum = requires { typename concurrent_row_n_t<Rs...>; };

static_assert(HasConcurrentSum<ConcurrentRow<SmBudget<UINT64_MAX - 1>>, ConcurrentRow<SmBudget<1>>, ConcurrentRow<>>);
static_assert(!HasConcurrentSum<ConcurrentRow<SmBudget<UINT64_MAX>>, ConcurrentRow<SmBudget<1>>, ConcurrentRow<>>);
static_assert(!HasConcurrentSum<ConcurrentRow<>, ConcurrentRow<SmBudget<UINT64_MAX>>, ConcurrentRow<SmBudget<1>>>);
static_assert(!HasConcurrentSum<ConcurrentRow<SmBudget<UINT64_MAX / 2 + 1>>, ConcurrentRow<HbmBytes<1>>,
                                ConcurrentRow<SmBudget<UINT64_MAX / 2 + 1>>>);
static_assert(!HasConcurrentSum<int>);
static_assert(!HasConcurrentSum<ConcurrentRow<>, SmBudget<1>>);

static_assert(concurrent_row_value_v<ResourceKind::Sm, ConcurrentRow<SmBudget<32>>> == 32);
static_assert(concurrent_row_value_v<ResourceKind::NicQp, ConcurrentRow<SmBudget<32>>> == 0);
static_assert(concurrent_row_value_v<ResourceKind::Sm, ConcurrentRow<>> == 0);
static_assert(concurrent_row_value_v<ResourceKind::Sm, ConcurrentRow<SmBudget<10>, SmBudget<20>, SmBudget<30>>> == 60);
static_assert(
    std::is_same_v<concurrent_row_sum_t<ConcurrentRow<SmBudget<10>, SmBudget<20>>, ConcurrentRow<SmBudget<30>>>,
                   ConcurrentRow<SmBudget<60>>>);

// A row naming an axis once cannot wrap, whatever the value, because
// the fold starts from zero.
static_assert(row_is_formable<SmBudget<UINT64_MAX>>);
static_assert(row_is_formable<SmBudget<UINT64_MAX - 1>, SmBudget<1>>);

// This row would otherwise report a demand of zero on the SM axis.
static_assert(!row_is_formable<SmBudget<UINT64_MAX>, SmBudget<1>>);

// One wrapping axis is enough to refuse the row, the last axis of the
// catalog is walked, and a wrap that a third value would bring back down
// is still refused.
static_assert(!row_is_formable<SmBudget<32>, HbmBytes<UINT64_MAX>, HbmBytes<1>>);
static_assert(!row_is_formable<CarbonGramsPerKwh<UINT64_MAX>, CarbonGramsPerKwh<1>>);
static_assert(!row_is_formable<SmBudget<UINT64_MAX - 10>, SmBudget<20>, SmBudget<1>>);
static_assert(!row_is_formable<int>);

static_assert(ConcurrentlySchedulable<ConcurrentRow<SmBudget<32>>, ConcurrentRow<SmBudget<64>>>);
static_assert(
    ConcurrentlySchedulable<ConcurrentRow<HbmBytes<40000000000ULL>>, ConcurrentRow<HbmBytes<40000000000ULL>>>);
static_assert(ConcurrentlySchedulable<ConcurrentRow<>, ConcurrentRow<>>);
static_assert(ConcurrentlySchedulable<ConcurrentRow<SmBudget<1>>, ConcurrentRow<>>);
static_assert(ConcurrentlySchedulable<ConcurrentRow<SmBudget<UINT64_MAX>>, ConcurrentRow<>>);

static_assert(!ConcurrentlySchedulable<ConcurrentRow<HbmBytes<UINT64_MAX>>, ConcurrentRow<HbmBytes<1>>>);
static_assert(!ConcurrentlySchedulable<ConcurrentRow<SmBudget<32>, HbmBytes<UINT64_MAX>>,
                                       ConcurrentRow<SmBudget<64>, HbmBytes<1>>>);
static_assert(
    !ConcurrentlySchedulable<ConcurrentRow<CarbonGramsPerKwh<UINT64_MAX>>, ConcurrentRow<CarbonGramsPerKwh<1>>>);
static_assert(!ConcurrentlySchedulable<int, ConcurrentRow<>>);
static_assert(!ConcurrentlySchedulable<ConcurrentRow<>, SmBudget<1>>);

// A pair that wraps has no sum, so the wrapped value never becomes a
// type.
static_assert(sum_is_formable<ConcurrentRow<HbmBytes<UINT64_MAX - 1>>, ConcurrentRow<HbmBytes<1>>>);
static_assert(!sum_is_formable<ConcurrentRow<HbmBytes<UINT64_MAX>>, ConcurrentRow<HbmBytes<2>>>);
static_assert(!sum_is_formable<ConcurrentRow<>, int>);

static_assert(IsCanonicalConcurrentRow<ConcurrentRow<>>);
static_assert(IsCanonicalConcurrentRow<ConcurrentRow<SmBudget<32>>>);
static_assert(IsCanonicalConcurrentRow<ConcurrentRow<SmBudget<32>, NicQp<4>>>);

// Order does not bear on it.  Naming an axis twice does.
static_assert(IsCanonicalConcurrentRow<ConcurrentRow<NicQp<4>, SmBudget<32>, HbmBytes<1024>>>);
static_assert(!IsCanonicalConcurrentRow<ConcurrentRow<SmBudget<10>, SmBudget<20>>>);
static_assert(!IsCanonicalConcurrentRow<ConcurrentRow<SmBudget<10>, SmBudget<20>, SmBudget<30>>>);
static_assert(!IsCanonicalConcurrentRow<ConcurrentRow<SmBudget<32>, NicQp<4>, SmBudget<64>>>);
static_assert(!IsCanonicalConcurrentRow<ConcurrentRow<CarbonGramsPerKwh<10>, CarbonGramsPerKwh<20>>>);
static_assert(!IsCanonicalConcurrentRow<int>);

static_assert(IsCanonicalConcurrentRow<
              concurrent_row_sum_t<ConcurrentRow<SmBudget<10>, SmBudget<20>>, ConcurrentRow<SmBudget<30>>>>);
static_assert(IsCanonicalConcurrentRow<
              concurrent_row_sum_t<ConcurrentRow<SmBudget<32>, NicQp<4>>, ConcurrentRow<SmBudget<64>, NicQp<2>>>>);

static_assert(IsConcurrentRow<ConcurrentRow<>>);
static_assert(IsConcurrentRow<ConcurrentRow<SmBudget<32>>>);
static_assert(!IsConcurrentRow<int>);
static_assert(!IsConcurrentRow<SmBudget<32>>);

static_assert(concurrent_row_descriptors_v<ConcurrentRow<>>.size() == 0);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<SmBudget<32>>>.size() == 1);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<SmBudget<32>>>[0].kind == ResourceKind::Sm);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<SmBudget<32>>>[0].value == 32);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<SmBudget<32>, NicQp<4>>>.size() == 2);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<SmBudget<32>, NicQp<4>>>[0].kind == ResourceKind::Sm);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<SmBudget<32>, NicQp<4>>>[1].kind == ResourceKind::NicQp);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<SmBudget<32>, NicQp<4>>>[1].value == 4);

// The row below names its axes out of catalog order, and the
// descriptors keep that order.
static_assert(concurrent_row_descriptors_v<ConcurrentRow<NicQp<4>, SmBudget<32>>>[0].kind == ResourceKind::NicQp);
static_assert(concurrent_row_descriptors_v<ConcurrentRow<NicQp<4>, SmBudget<32>>>[1].kind == ResourceKind::Sm);

static_assert(std::is_empty_v<ConcurrentRow<>>);
static_assert(std::is_empty_v<ConcurrentRow<SmBudget<32>>>);
static_assert(std::is_empty_v<ConcurrentRow<SmBudget<32>, NicQp<4>>>);

// The run-time half of these checks lives in
// test/foundation/test_effects_resources.cpp, which builds the rows and
// walks their descriptors with values the optimizer cannot fold.

}  // namespace detail::concurrent_row_self_test

}  // namespace foundation::effects

namespace foundation::effects::detail::row_hash_concurrent_row_self_test {

using ::foundation::diag::row_hash_contribution_v;
using ::foundation::effects::ConcurrentRow;
using ::foundation::effects::EmptyConcurrentRow;
using ::foundation::effects::resource::HbmBytes;
using ::foundation::effects::resource::NicQp;
using ::foundation::effects::resource::SmBudget;

static_assert(row_hash_contribution_v<EmptyConcurrentRow> != 0);
static_assert(row_hash_contribution_v<EmptyConcurrentRow> != row_hash_contribution_v<ConcurrentRow<SmBudget<32>>>);

static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>>> != row_hash_contribution_v<ConcurrentRow<NicQp<4>>>);
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>>>
              != row_hash_contribution_v<ConcurrentRow<HbmBytes<32>>>);

static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>>>
              != row_hash_contribution_v<ConcurrentRow<SmBudget<64>>>);

// The largest demand on one axis separates from the empty row.  A row
// that wraps to zero on that axis cannot be spelled.
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<UINT64_MAX>>>
              != row_hash_contribution_v<EmptyConcurrentRow>);

// The two rows below declare the same demand in two spellings, so
// they must land in the same cache slot.
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<10>, SmBudget<20>>>
              == row_hash_contribution_v<ConcurrentRow<SmBudget<30>>>);

static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<10>, SmBudget<20>, SmBudget<30>>>
              == row_hash_contribution_v<ConcurrentRow<SmBudget<60>>>);

// Naming the axes in the other order is a third spelling of one
// demand.
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>, NicQp<4>>>
              == row_hash_contribution_v<ConcurrentRow<NicQp<4>, SmBudget<32>>>);

// A row holding one tag and that tag on its own are different things,
// and the row identity keeps them apart whatever the payload fold
// produces.
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<32>>> != row_hash_contribution_v<SmBudget<32>>);

// A row of zero budgets declares no demand on any axis, which is the
// demand the empty row declares.
static_assert(row_hash_contribution_v<ConcurrentRow<SmBudget<0>>> == row_hash_contribution_v<EmptyConcurrentRow>);

}  // namespace foundation::effects::detail::row_hash_concurrent_row_self_test

// A concurrent row is a ConcurrentRow over resource tags, empty or not.  A
// resource tag alone is no row.
static_assert(::foundation::effects::IsConcurrentRow<::foundation::effects::ConcurrentRow<>>
              && ::foundation::effects::IsConcurrentRow<
                  ::foundation::effects::ConcurrentRow<::foundation::effects::resource::SmBudget<32>>>);
static_assert(!::foundation::effects::IsConcurrentRow<int>
              && !::foundation::effects::IsConcurrentRow<::foundation::effects::resource::SmBudget<32>>);
