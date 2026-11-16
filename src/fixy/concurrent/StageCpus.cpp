// The table of claimed CPUs of fixy/concurrent/StageCpus.h, and the claim of
// the candidates of one run.  No function of this file has a contract, so
// the object of libfixy does not call std::terminate.

#include <fixy/concurrent/StageCpus.h>

#include <foundation/Platform.h>
#include <foundation/core/Index.h>

#include <cstddef>
#include <cstdint>

namespace fixy::concurrent {

namespace detail {

// THE TABLE OF THE PROCESS of fixy/concurrent/StageCpus.h.
CRUCIBLE_PROCESS_WIDE inline constinit ProcessStageCpuClaims process_stage_cpu_claims_{};

}  // namespace detail

namespace {

using ::foundation::algebra::lattices::AffinityMask;
using ::foundation::core::Index;

// The bit of a CPU in its word.
[[nodiscard]] std::uint64_t bit_of(std::uint16_t cpu) noexcept { return std::uint64_t{1} << (cpu % 64); }

// The position of the word of a CPU.
[[nodiscard]] Index<StageCpuClaims::word_count> word_position_of(std::uint16_t cpu) noexcept {
    return Index<StageCpuClaims::word_count>::check(cpu / 64).expect(
        "a CPU id at most AffinityMask::kMaxCore has a word in the table of claimed CPUs");
}

}  // namespace

// The bound of a CPU id is a fatal invariant, not a contract precondition.  A
// precondition in a source file makes the object of libfixy call
// std::terminate, and the rule artifact-symbols refuses that symbol in libfixy.
bool StageCpuClaims::try_claim(std::uint16_t cpu) noexcept {
    CRUCIBLE_FATAL_INVARIANT(cpu <= AffinityMask::kMaxCore);
    std::uint64_t const bit = bit_of(cpu);
    return (words_.at(word_position_of(cpu)).fetch_or_acq_rel(bit) & bit) == 0;
}

void StageCpuClaims::give_back(std::uint16_t cpu) noexcept {
    CRUCIBLE_FATAL_INVARIANT(cpu <= AffinityMask::kMaxCore);
    std::uint64_t const bit = bit_of(cpu);
    std::uint64_t const before = words_.at(word_position_of(cpu)).fetch_and_acq_rel(~bit);
    CRUCIBLE_FATAL_INVARIANT((before & bit) != 0);
}

bool StageCpuClaims::is_claimed(std::uint16_t cpu) const noexcept {
    CRUCIBLE_FATAL_INVARIANT(cpu <= AffinityMask::kMaxCore);
    return (words_.at(word_position_of(cpu)).load_acquire() & bit_of(cpu)) != 0;
}

::foundation::core::Ref<ProcessStageCpuClaims, ProcessStageCpuClaims::brand_type> process_stage_cpu_claims() noexcept {
    return ::foundation::core::to(detail::process_stage_cpu_claims_);
}

namespace detail {

void StageCpuOrder::claim_into(StageCpuClaims& claims, ::foundation::core::View<int> cpus) const noexcept {
    for (int& cpu : cpus) {
        cpu = -1;
    }
    std::size_t filled = 0;
    for (std::uint16_t const cpu : order_) {
        if (filled == cpus.size()) break;
        if (!claims.try_claim(cpu)) continue;
        for (auto const& slot : cpus.get(filled)) {
            *slot = static_cast<int>(cpu);
        }
        ++filled;
    }
}

}  // namespace detail

void give_back_stage_cpus(StageCpuClaims& claims, ::foundation::core::View<int> cpus) noexcept {
    for (int& cpu : cpus) {
        if (cpu >= 0) claims.give_back(static_cast<std::uint16_t>(cpu));
        cpu = -1;
    }
}

}  // namespace fixy::concurrent
