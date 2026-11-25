// Negative-compile test.  This file must fail to compile.
//
// The consumer side of the trace ring opens only with the proof of the drain
// stage.  The publish stage holds the proof of the consumer side of the
// metadata log, and that proof does not open the ring: a job of the publish
// stage would drain the ring beside the drain stage.  The function takes a
// proof of the publish stage as the publish stage gives it to a job, so the
// fixture builds no proof.

#include <crucible/BackgroundThread.h>

uint32_t drain_with_the_log_proof(crucible::BackgroundThread::TraceRingSide& side,
                                  crucible::BackgroundThread::PublishStage const& stage) noexcept;

uint32_t drain_with_the_log_proof(crucible::BackgroundThread::TraceRingSide& side,
                                  crucible::BackgroundThread::PublishStage const& stage) noexcept {
    return side.with_end(stage, [](crucible::TraceRing::Consumer& ring) noexcept {
        auto [drained, drained_key] =
            ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<crucible::TraceRing::Entry, 4>);
        return drained.with_mut(
            drained_key, [&ring](auto run) noexcept { return crucible::TraceRing::drain(ring, std::move(run)); });
    });
}

int main() { return 0; }
