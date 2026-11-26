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

// A copy of the first count records of an array of a test, as the list that
// a test seals (SealedRecords of crucible/ValidatedOp.h).  The process ends
// when the count does not fit the array, as for each other broken fixture.
// O(count).
template <auto N>
[[nodiscard]] ::fixy::FixedList<TensorMeta, N> record_list_of(::fixy::FixedArray<TensorMeta, N> const& metas,
                                                              uint32_t count) noexcept {
    ::fixy::FixedList<TensorMeta, N> list{};
    for (TensorMeta const& meta : records_of(metas, count))
        (void)list.append(meta).expect("the count of the records fits the array of the test");
    return list;
}

// Certifies an Entry that a test built, and pairs it with its sealed
// records, so it can reach record_op and dispatch_op.  The process ends when
// the count of the records disagrees with the entry, as for each other
// broken fixture.  The op points into the entry and holds a View of the
// list, so it must not outlive the entry or the list.
template <std::size_t N>
[[nodiscard]] ValidatedOp certify_synthetic_op(const TraceRing::Entry& entry CRUCIBLE_LIFETIMEBOUND,
                                               SealedRecords<N> const& records CRUCIBLE_LIFETIMEBOUND) noexcept {
    return mint_validated_op(certify_synthetic_entry(entry), records)
        .expect("the count of the records of a test agrees with its entry");
}
template <std::size_t N>
ValidatedOp certify_synthetic_op(const TraceRing::Entry&&, SealedRecords<N> const&) =
    delete("the op points into the entry, and a temporary entry ends before the op");
template <std::size_t N>
ValidatedOp certify_synthetic_op(const TraceRing::Entry&, SealedRecords<N> const&&) =
    delete("the op keeps a View of the sealed list, and a temporary list ends before the op");

// The same for an entry with no records.
[[nodiscard]] inline ValidatedOp certify_synthetic_op(const TraceRing::Entry& entry CRUCIBLE_LIFETIMEBOUND) noexcept {
    return mint_validated_op(certify_synthetic_entry(entry))
        .expect("the entry of a test with no records counts no tensor");
}
ValidatedOp certify_synthetic_op(const TraceRing::Entry&&) =
    delete("the op points into the entry, and a temporary entry ends before the op");

// Runs body with the op of an entry that a test built and of the first count
// records of an array of the test, and gives the result of body.  The
// records are a sealed copy that lives in the call, so a later write to the
// array changes no op, and the result of body holds no borrow of the op.
// O(count), and the cost of body.
template <auto N, class Body>
    requires std::is_invocable_v<Body&&, ValidatedOp const&>
          && ::foundation::core::HoldsNoBorrow<std::invoke_result_t<Body&&, ValidatedOp const&>>
decltype(auto) with_synthetic_op(const TraceRing::Entry& entry, ::fixy::FixedArray<TensorMeta, N> const& metas,
                                 uint32_t count, Body&& body) {
    SealedRecords<N> const records{record_list_of(metas, count)};
    return static_cast<Body&&>(body)(certify_synthetic_op(entry, records));
}

// Gives an op of a test to dispatch_op and to dispatch_op_pure.
//
// Vigil::dispatch_op is always inlined, so each call site copies the whole
// hot path into the test function.  These helpers are never inlined, so a
// test translation unit compiles that copy one time, and its back end stays
// short.  The behaviour is that of a direct call.
[[nodiscard, gnu::noinline]] inline DispatchResult dispatch_certified(Vigil& vigil, ValidatedOp const& op,
                                                                      ScopeHash scope_hash, CallsiteHash callsite_hash,
                                                                      OpDepth depth) {
    return vigil.dispatch_op(op, scope_hash, callsite_hash, depth);
}
[[nodiscard, gnu::noinline]] inline DispatchResult dispatch_pure_certified(Vigil& vigil, VigilFgCtx const& fg,
                                                                           ValidatedOp const& op) {
    return vigil.dispatch_op_pure(fg, op);
}

// Certifies an Entry that a test built with the first count records of an
// array of the test, and gives the op to record_op, dispatch_op or
// dispatch_op_pure.
template <auto N>
[[nodiscard]] bool record_synthetic(Vigil& vigil, const TraceRing::Entry& entry,
                                    ::fixy::FixedArray<TensorMeta, N> const& metas, uint32_t count,
                                    ScopeHash scope_hash = {}, CallsiteHash callsite_hash = {}, OpDepth depth = {}) {
    return with_synthetic_op(entry, metas, count, [&](ValidatedOp const& op) {
        return vigil.record_op(op, scope_hash, callsite_hash, depth);
    });
}

template <auto N>
[[nodiscard]] DispatchResult
dispatch_synthetic(Vigil& vigil, const TraceRing::Entry& entry, ::fixy::FixedArray<TensorMeta, N> const& metas,
                   uint32_t count, ScopeHash scope_hash = {}, CallsiteHash callsite_hash = {}, OpDepth depth = {}) {
    return with_synthetic_op(entry, metas, count, [&](ValidatedOp const& op) {
        return dispatch_certified(vigil, op, scope_hash, callsite_hash, depth);
    });
}

template <auto N>
[[nodiscard]] DispatchResult dispatch_pure_synthetic(Vigil& vigil, VigilFgCtx const& fg, const TraceRing::Entry& entry,
                                                     ::fixy::FixedArray<TensorMeta, N> const& metas, uint32_t count) {
    return with_synthetic_op(entry, metas, count,
                             [&](ValidatedOp const& op) { return dispatch_pure_certified(vigil, fg, op); });
}

}  // namespace crucible::test
