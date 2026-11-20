#pragma once

// Synchronization in this header is a pure spin on an acquire load.  Do not
// add sleep_for, yield, futex or condition_variable.

#include <crucible/Vigil.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Tags.h>
#include <fixy/check/Check.h>
#include <cstdint>

namespace crucible::test {

// Waits for the background thread to publish a region.  Replay has not
// started at that point: the foreground aligns to the region on its next
// dispatches, and Vigil::is_compiled turns true only when it activates.
//
// After flush() the publication is already visible, because the release and
// acquire on total_processed order it.  This loop is a safety net.
inline void wait_region_published(::fixy::TestRunnerCtx const& test, Vigil& vigil) {
    uint64_t spins = 0;
    while (!vigil.has_pending_region()) {
        CRUCIBLE_TEST_ASSERT(test, ++spins < 100000000 && "the background thread did not publish a region");
        CRUCIBLE_SPIN_PAUSE;
    }
}

inline void flush_and_wait_region_published(::fixy::TestRunnerCtx const& test, Vigil& vigil) {
    CRUCIBLE_TEST_ASSERT(test, vigil.flush(test).is_ok() && "the background thread stopped before the flush");
    CRUCIBLE_TEST_ASSERT(test, vigil.flush_complete() && "flush() returned but bg thread did not finish processing");
    wait_region_published(test, vigil);
}

// Certifies an Entry a test built field by field, so it can reach
// record_op and dispatch_op, which take the second trust tag.
//
// This lives in the harness rather than beside TraceRing because it runs
// no check: it states that the fields are whatever the test wrote, which
// is true of a literal in a test and is not true of anything an adapter
// fills from a foreign runtime. An adapter reaching a recording entry
// point runs the checks in vessel_api_typed.h and crosses the retag
// edge, and it has nothing shorter to reach for, because this is not on
// its include path.
//
// A test that wants the adapter's checks exercised should call the
// adapter. This is for the tests that drive the Vigil directly.
[[nodiscard]] inline TraceRing::ValidatedEntryPtr certify_synthetic_entry(const TraceRing::Entry& entry
                                                                          CRUCIBLE_LIFETIMEBOUND) noexcept {
    return mint_ffi_entry(entry).retag<::fixy::tags::vessel_trust::Validated>();
}
TraceRing::ValidatedEntryPtr certify_synthetic_entry(const TraceRing::Entry&&) =
    delete("the result points into the entry, and a temporary entry ends before the result");

// The first count records of an array of a test: the records of one op.
template <auto N>
[[nodiscard]] ::fixy::View<const TensorMeta>
records_of(::fixy::FixedArray<TensorMeta, N> const& metas CRUCIBLE_LIFETIMEBOUND, uint32_t count) noexcept {
    return ::fixy::View<const TensorMeta>{metas.view()}.window(0, count).expect(
        "the count of the records fits the array of the test");
}
template <auto N>
::fixy::View<const TensorMeta> records_of(::fixy::FixedArray<TensorMeta, N> const&&, uint32_t) =
    delete("the result points into the array, and a temporary array ends before the result");

// Certifies an Entry that a test built, and pairs it with its records, so it
// can reach record_op and dispatch_op.  The process ends when the count of
// the records disagrees with the entry, as for each other broken fixture.
[[nodiscard]] inline ValidatedOp certify_synthetic_op(const TraceRing::Entry& entry CRUCIBLE_LIFETIMEBOUND,
                                                      ::fixy::View<const TensorMeta> metas) noexcept {
    return mint_validated_op(certify_synthetic_entry(entry), metas)
        .expect("the count of the records of a test agrees with its entry");
}
ValidatedOp certify_synthetic_op(const TraceRing::Entry&&, ::fixy::View<const TensorMeta>) =
    delete("the op points into the entry, and a temporary entry ends before the op");

// The same for the first count records of an array of a test.
template <auto N>
[[nodiscard]] ValidatedOp certify_synthetic_op(const TraceRing::Entry& entry CRUCIBLE_LIFETIMEBOUND,
                                               ::fixy::FixedArray<TensorMeta, N> const& metas CRUCIBLE_LIFETIMEBOUND,
                                               uint32_t count) noexcept {
    return certify_synthetic_op(entry, records_of(metas, count));
}
template <auto N>
ValidatedOp certify_synthetic_op(const TraceRing::Entry&&, ::fixy::FixedArray<TensorMeta, N> const&, uint32_t) =
    delete("the op points into the entry, and a temporary entry ends before the op");
template <auto N>
ValidatedOp certify_synthetic_op(const TraceRing::Entry&, ::fixy::FixedArray<TensorMeta, N> const&&, uint32_t) =
    delete("the op points into the array, and a temporary array ends before the op");

// The same for an entry with no records.
[[nodiscard]] inline ValidatedOp certify_synthetic_op(const TraceRing::Entry& entry CRUCIBLE_LIFETIMEBOUND) noexcept {
    return certify_synthetic_op(entry, ::fixy::View<const TensorMeta>{});
}
ValidatedOp certify_synthetic_op(const TraceRing::Entry&&) =
    delete("the op points into the entry, and a temporary entry ends before the op");

// Certifies an Entry that a test built, and gives it to dispatch_op.
//
// Vigil::dispatch_op is always inlined, so each call site copies the whole
// hot path into the test function.  This helper is never inlined, so a test
// translation unit compiles that copy one time, and its back end stays
// short.  The behaviour is that of a direct call.
[[nodiscard, gnu::noinline]] inline DispatchResult dispatch_synthetic(Vigil& vigil, const TraceRing::Entry& entry,
                                                                      ::fixy::View<const TensorMeta> metas) {
    return vigil.dispatch_op(certify_synthetic_op(entry, metas));
}

template <auto N>
[[nodiscard]] DispatchResult dispatch_synthetic(Vigil& vigil, const TraceRing::Entry& entry,
                                                ::fixy::FixedArray<TensorMeta, N> const& metas, uint32_t count) {
    return dispatch_synthetic(vigil, entry, records_of(metas, count));
}

// The same for dispatch_op_pure, which also takes the context of the
// producer claim of the Vigil.
[[nodiscard, gnu::noinline]] inline DispatchResult dispatch_pure_synthetic(Vigil& vigil, VigilFgCtx const& fg,
                                                                           const TraceRing::Entry& entry,
                                                                           ::fixy::View<const TensorMeta> metas) {
    return vigil.dispatch_op_pure(fg, certify_synthetic_op(entry, metas));
}

template <auto N>
[[nodiscard]] DispatchResult dispatch_pure_synthetic(Vigil& vigil, VigilFgCtx const& fg, const TraceRing::Entry& entry,
                                                     ::fixy::FixedArray<TensorMeta, N> const& metas, uint32_t count) {
    return dispatch_pure_synthetic(vigil, fg, entry, records_of(metas, count));
}

}  // namespace crucible::test
