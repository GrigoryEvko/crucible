#pragma once

// The pin of the write section of a table (crucible/RegistrationSeal.h): two
// writers on two threads take the section of one table again and again, and
// no two sections of the table exist at once.
//
// Each writer has a context that owns Div, so the second writer spins while
// the first holds the section.  A writer counts itself in at the start of its
// section and out at its end.  A writer that finds another writer inside
// counts an overlap.  A table whose claim door gave a second section while
// the first lives gives overlaps.

#include <crucible/RegistrationSeal.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace crucible::test {

// The sections that each writer takes, and the pauses that a writer spins
// inside one section, so the other writer meets a held section.
inline constexpr std::uint32_t kSectionRounds = 256;
inline constexpr std::uint32_t kPausesInSection = 64;

// The sections that the two writers took, the sections that found another
// writer inside, and the writers inside after the join.
struct SectionOverlap {
    std::uint32_t taken = 0;
    std::uint32_t overlaps = 0;
    std::uint32_t inside = 0;
};

// Runs the two writers on table, and gives what they counted.  The table
// states synchronized{}, so the two writers borrow it on two threads at the
// same time.  Complexity: kSectionRounds sections on each thread.
template <class Table>
[[nodiscard]] SectionOverlap run_two_writers(Table& table) {
    ::fixy::Atomic<std::uint32_t> inside{0};
    ::fixy::Atomic<std::uint32_t> overlaps{0};
    const auto writer_of = [&table, &inside, &overlaps] noexcept {
        return [&table, &inside, &overlaps](::fixy::BgLoadCtx const& ctx) noexcept {
            std::uint32_t taken = 0;
            for (std::uint32_t round = 0; round < kSectionRounds; ++round) {
                taken += table.try_enter_write_section(ctx).match(
                    [&inside, &overlaps](WriteSection<Table> /*section*/) noexcept {
                        if (inside.fetch_add_acq_rel(1) != 0) static_cast<void>(overlaps.fetch_add_acq_rel(1));
                        for (std::uint32_t pause = 0; pause < kPausesInSection; ++pause)
                            CRUCIBLE_SPIN_PAUSE;
                        static_cast<void>(inside.fetch_sub_acq_rel(1));
                        return std::uint32_t{1};
                    },
                    [] noexcept { return std::uint32_t{0}; });
            }
            return taken;
        };
    };
    const ::fixy::BgLoadCtx bg{::foundation::effects::testing::bg()};
    auto const [first_taken, second_taken] =
        ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, writer_of(), writer_of())
            .expect("the thread door starts the two writers");
    return SectionOverlap{
        .taken = first_taken + second_taken, .overlaps = overlaps.load_acquire(), .inside = inside.load_acquire()};
}

}  // namespace crucible::test
