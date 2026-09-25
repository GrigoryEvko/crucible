#pragma once

// Four-tier chain over the console-I/O surface a function touches.
// Declaring a tier asserts that the actual surface is contained in the
// set that tier allows.
//
// The tiers climb by what each one adds: a format parse, then a forced
// flush and its write syscall, then an unbounded wait on whoever is at
// the terminal.
//
// Disruptiveness grows upward, which inverts the usual reading of the
// operators.  A join returns the more disruptive of two tiers, which is
// right for propagation.  It is wrong for admission.  A gate that wants
// the no-stdio floor must call meet, because join hands back the most
// permissive party's surface.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>
#include <crucible/algebra/lattices/_ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

enum class Stdio : std::uint8_t {
    NoStdio = 0,  // bottom — no console I/O
    BufferedWrite = 1,  // write to a buffered stream (deferred flush; format cost)
    UnbufferedWrite = 2,  // write to an unbuffered / flushed stream (syscall per call)
    InteractiveRead = 3,  // top — reads stdin / blocks on interactive input
};

[[nodiscard]] consteval std::string_view stdio_name(Stdio t) noexcept {
    switch (t) {
        case Stdio::NoStdio:
            return "NoStdio";
        case Stdio::BufferedWrite:
            return "BufferedWrite";
        case Stdio::UnbufferedWrite:
            return "UnbufferedWrite";
        case Stdio::InteractiveRead:
            return "InteractiveRead";
        default:
            return std::string_view{"<unknown Stdio>"};
    }
}

struct StdioLattice : ChainLatticeOps<Stdio> {
    [[nodiscard]] static constexpr Stdio bottom() noexcept { return Stdio::NoStdio; }
    [[nodiscard]] static constexpr Stdio top() noexcept { return Stdio::InteractiveRead; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "StdioLattice"; }

    template <Stdio T>
    struct At {
        struct element_type {
            using stdio_value_type = Stdio;
            [[nodiscard]] constexpr operator stdio_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr Stdio tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case Stdio::NoStdio:
                    return "StdioLattice::At<NoStdio>";
                case Stdio::BufferedWrite:
                    return "StdioLattice::At<BufferedWrite>";
                case Stdio::UnbufferedWrite:
                    return "StdioLattice::At<UnbufferedWrite>";
                case Stdio::InteractiveRead:
                    return "StdioLattice::At<InteractiveRead>";
                default:
                    return "StdioLattice::At<?>";
            }
        }
    };
};

namespace detail::stdio_lattice_self_test {

inline constexpr std::size_t stdio_count = std::meta::enumerators_of(^^Stdio).size();

static_assert(stdio_count == 4, "Stdio must hold exactly the four tiers NoStdio, BufferedWrite, "
                                "UnbufferedWrite and InteractiveRead.  A new tier appends at the next free "
                                "ordinal and needs an arm in stdio_name and in At<T>'s name().");

static_assert(std::to_underlying(Stdio::NoStdio) == 0);
static_assert(std::to_underlying(Stdio::InteractiveRead) == 3);
static_assert(std::is_same_v<std::underlying_type_t<Stdio>, std::uint8_t>);

[[nodiscard]] consteval bool every_stdio_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Stdio));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = stdio_name([:en:]);
        if (n == std::string_view{"<unknown Stdio>"}) return false;
        if (n.empty()) return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_stdio_has_name(), "stdio_name() has no arm for at least one tier, so that tier reports "
                                      "the '<unknown Stdio>' sentinel.");

static_assert(::crucible::algebra::Lattice<StdioLattice>);
static_assert(::crucible::algebra::BoundedLattice<StdioLattice>);
static_assert(!::crucible::algebra::Semiring<StdioLattice>);

static_assert(verify_chain_lattice_exhaustive<StdioLattice>(),
              "StdioLattice's chain-order lattice axioms must hold at every "
              "(Stdio)³ triple.");
static_assert(verify_chain_lattice_distributive_exhaustive<StdioLattice>(),
              "StdioLattice's chain order must satisfy distributivity at every "
              "(Stdio)³ triple.");

static_assert(StdioLattice::bottom() == Stdio::NoStdio);
static_assert(StdioLattice::top() == Stdio::InteractiveRead);
static_assert(StdioLattice::name() == std::string_view{"StdioLattice"});

static_assert(StdioLattice::leq(Stdio::NoStdio, Stdio::InteractiveRead));
static_assert(!StdioLattice::leq(Stdio::InteractiveRead, Stdio::NoStdio));

static_assert(StdioLattice::leq(Stdio::NoStdio, Stdio::BufferedWrite));
static_assert(StdioLattice::leq(Stdio::BufferedWrite, Stdio::UnbufferedWrite));
static_assert(StdioLattice::leq(Stdio::UnbufferedWrite, Stdio::InteractiveRead));

static_assert(!StdioLattice::leq(Stdio::BufferedWrite, Stdio::NoStdio));
static_assert(!StdioLattice::leq(Stdio::InteractiveRead, Stdio::UnbufferedWrite));

static_assert(StdioLattice::join(Stdio::BufferedWrite, Stdio::UnbufferedWrite) == Stdio::UnbufferedWrite);
static_assert(StdioLattice::join(Stdio::NoStdio, Stdio::BufferedWrite) == Stdio::BufferedWrite);
static_assert(StdioLattice::meet(Stdio::InteractiveRead, Stdio::BufferedWrite) == Stdio::BufferedWrite);

// The two assertions below pin the polarity.  Inverting the chain reds
// them in lockstep and stops a gate from being written against the wrong
// operator.
static_assert(StdioLattice::join(Stdio::NoStdio, Stdio::InteractiveRead) == Stdio::InteractiveRead,
              "StdioLattice's join returns the more disruptive stdio surface of "
              "the two operands.  A gate that treats composition as "
              "strictest-wins would silently admit InteractiveRead.  Use meet "
              "for the no-stdio floor.");
static_assert(StdioLattice::meet(Stdio::NoStdio, Stdio::InteractiveRead) == Stdio::NoStdio,
              "StdioLattice's meet returns the least disruptive stdio surface of "
              "the two operands.  A hot-path admission gate must call meet, not "
              "join.");

static_assert(std::is_empty_v<StdioLattice::At<Stdio::NoStdio>::element_type>);
static_assert(std::is_empty_v<StdioLattice::At<Stdio::BufferedWrite>::element_type>);
static_assert(std::is_empty_v<StdioLattice::At<Stdio::UnbufferedWrite>::element_type>);
static_assert(std::is_empty_v<StdioLattice::At<Stdio::InteractiveRead>::element_type>);
static_assert(StdioLattice::At<Stdio::UnbufferedWrite>::tier == Stdio::UnbufferedWrite);

inline void stdio_lattice_runtime_smoke_test() {
    Stdio a = Stdio::NoStdio;
    Stdio b = Stdio::InteractiveRead;
    [[maybe_unused]] bool rl = StdioLattice::leq(a, b);
    [[maybe_unused]] Stdio rj = StdioLattice::join(a, b);
    [[maybe_unused]] Stdio rm = StdioLattice::meet(a, b);

    Stdio c = Stdio::BufferedWrite;
    Stdio d = Stdio::UnbufferedWrite;
    [[maybe_unused]] Stdio rj2 = StdioLattice::join(c, d);
    [[maybe_unused]] Stdio rm2 = StdioLattice::meet(c, d);

    StdioLattice::At<Stdio::BufferedWrite>::element_type bw_pin{};
    [[maybe_unused]] Stdio bw_recovered = bw_pin;
}

}  // namespace detail::stdio_lattice_self_test

}  // namespace crucible::algebra::lattices
