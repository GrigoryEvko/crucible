// The records of an op are the inputs and then the outputs of its entry.
// mint_validated_op is the one door to the op that Vigil::record_op takes, and
// it refuses a count of records that disagrees with the entry.  So no count
// that disagrees reaches the metadata log or the trace ring.

#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>
#include <crucible/ValidatedOp.h>
#include <crucible/Vigil.h>
#include <fixy/Core.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include "test_harness.h"

#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::MetaLog;
using crucible::TraceRing;
using crucible::Vigil;

namespace {

template <std::size_t N>
[[nodiscard]] bool is_refused(const TraceRing::Entry& entry, crucible::SealedRecords<N> const& records) {
    return crucible::mint_validated_op(crucible::test::certify_synthetic_entry(entry), records).is_none();
}
[[nodiscard]] bool is_refused(const TraceRing::Entry& entry) {
    return crucible::mint_validated_op(crucible::test::certify_synthetic_entry(entry)).is_none();
}

void test_count_that_disagrees_with_the_entry_is_refused() {
    Vigil vigil;
    TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{0x51};
    entry.shape_hash = crucible::ShapeHash{0x52};
    entry.num_inputs = 1;
    entry.num_outputs = 1;
    // The records are the default records.  No region comes from two ops,
    // so no build reads them.
    ::fixy::FixedArray<crucible::TensorMeta, 3> metas{};

    // A count that agrees records the op, and claims this thread.
    CRUCIBLE_TEST_ASSERT(kTest, crucible::test::record_synthetic(vigil, entry, metas, 2));
    const uint64_t head_before = vigil.meta_log_total_produced().raw();
    const uint64_t ring_before = vigil.ring_total_produced();

    // One record too many, one too few, and no record for an op with two
    // tensors.  The door gives no op, so no call of record_op exists.
    const crucible::SealedRecords<3> three_records{crucible::test::record_list_of(metas, 3)};
    const crucible::SealedRecords<3> one_record{crucible::test::record_list_of(metas, 1)};
    CRUCIBLE_TEST_ASSERT(kTest, is_refused(entry, three_records));
    CRUCIBLE_TEST_ASSERT(kTest, is_refused(entry, one_record));
    CRUCIBLE_TEST_ASSERT(kTest, is_refused(entry));
    CRUCIBLE_TEST_ASSERT(kTest, vigil.meta_log_total_produced().raw() == head_before
                                    && "a refused op put records in the metadata log");
    CRUCIBLE_TEST_ASSERT(kTest, vigil.ring_total_produced() == ring_before && "a refused op reached the trace ring");

    // The op with a count that agrees records again.
    CRUCIBLE_TEST_ASSERT(kTest, crucible::test::record_synthetic(vigil, entry, metas, 2));
    CRUCIBLE_TEST_ASSERT(kTest, vigil.meta_log_total_produced().raw() == head_before + 2);
}

void test_op_with_no_tensors_takes_no_records() {
    TraceRing::Entry entry{};
    entry.schema_hash = crucible::SchemaHash{0x53};
    const ::fixy::FixedArray<crucible::TensorMeta, 1> metas{};
    const crucible::SealedRecords<1> one_record{crucible::test::record_list_of(metas, 1)};

    CRUCIBLE_TEST_ASSERT(kTest, !is_refused(entry));
    CRUCIBLE_TEST_ASSERT(kTest, is_refused(entry, one_record) && "an op with no tensors took a record");
}

}  // namespace

int main() {
    test_count_that_disagrees_with_the_entry_is_refused();
    test_op_with_no_tensors_takes_no_records();
    return 0;
}
