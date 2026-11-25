// The compile cost of the keyed frame door with_mut of a FixedArray, against a
// raw loop on a C array of the same elements.  check_door_cost.py compiles
// this unit with the flags of the Scan check of foundation/core/Scan.h, and
// gives DOOR_COST_FORM the name of one of the four writers below.  With
// DOOR_COST_COUNT 32, the unit instantiates the writer for 32 seeds, so each
// door gets a body of its own and a fresh brand of its own, as each
// instantiated call of a door does.
//
//   RawWriter      a loop on a C array writes each element: the floor
//   DoorWriter     the body of with_mut writes each element of its run
//   RawSummer      the loop of RawWriter, which gives a small class
//   DoorSummer     the body of DoorWriter, which gives the same class
//
// The summers read the result checks of the door (FrameResult of
// foundation/core/Choice.h), which a body that gives void does not reach.
// The raw forms stand in an opt-out region: a C array is the floor that the
// check compares with, and it does not depend on the doors of an owner.

#include <foundation/Quarantine.h>
#include <foundation/core/Array.h>

#include <cstdint>

namespace door_cost {

// The brand of the keyed array of the doors.
struct WordsBrand final {};
using Words = ::foundation::core::FixedArray<std::uint64_t, 16, WordsBrand>;
using WordsKey = ::foundation::core::LoanKey<WordsBrand>;

// The result of a summer: a plain value that leaves the frame.
struct Sum final {
    std::uint64_t written = 0;
    std::uint64_t count = 0;
};

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: a C array of 16 words is the raw floor of the compile cost of one door call")

struct RawWords final {
    std::uint64_t words[16]{};
};

template <std::uint64_t Seed>
struct RawWriter final {
    static void write(RawWords& raw) noexcept {
        for (std::uint64_t& word : raw.words) {
            word = Seed;
        }
    }
};

template <std::uint64_t Seed>
struct RawSummer final {
    static Sum write(RawWords& raw) noexcept {
        Sum sum{};
        for (std::uint64_t& word : raw.words) {
            word = Seed;
            sum.written += word;
            sum.count += 1;
        }
        return sum;
    }
};

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

template <std::uint64_t Seed>
struct DoorWriter final {
    static void write(Words& words, WordsKey& key) noexcept {
        words.with_mut(key, [](auto run) noexcept {
            for (std::uint64_t& word : run) {
                word = Seed;
            }
        });
    }
};

template <std::uint64_t Seed>
struct DoorSummer final {
    static Sum write(Words& words, WordsKey& key) noexcept {
        return words.with_mut(key, [](auto run) noexcept {
            Sum sum{};
            for (std::uint64_t& word : run) {
                word = Seed;
                sum.written += word;
                sum.count += 1;
            }
            return sum;
        });
    }
};

#if DOOR_COST_COUNT == 32
template struct DOOR_COST_FORM<0>;
template struct DOOR_COST_FORM<1>;
template struct DOOR_COST_FORM<2>;
template struct DOOR_COST_FORM<3>;
template struct DOOR_COST_FORM<4>;
template struct DOOR_COST_FORM<5>;
template struct DOOR_COST_FORM<6>;
template struct DOOR_COST_FORM<7>;
template struct DOOR_COST_FORM<8>;
template struct DOOR_COST_FORM<9>;
template struct DOOR_COST_FORM<10>;
template struct DOOR_COST_FORM<11>;
template struct DOOR_COST_FORM<12>;
template struct DOOR_COST_FORM<13>;
template struct DOOR_COST_FORM<14>;
template struct DOOR_COST_FORM<15>;
template struct DOOR_COST_FORM<16>;
template struct DOOR_COST_FORM<17>;
template struct DOOR_COST_FORM<18>;
template struct DOOR_COST_FORM<19>;
template struct DOOR_COST_FORM<20>;
template struct DOOR_COST_FORM<21>;
template struct DOOR_COST_FORM<22>;
template struct DOOR_COST_FORM<23>;
template struct DOOR_COST_FORM<24>;
template struct DOOR_COST_FORM<25>;
template struct DOOR_COST_FORM<26>;
template struct DOOR_COST_FORM<27>;
template struct DOOR_COST_FORM<28>;
template struct DOOR_COST_FORM<29>;
template struct DOOR_COST_FORM<30>;
template struct DOOR_COST_FORM<31>;
#endif

}  // namespace door_cost
