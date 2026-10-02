// The probes of the shape check of the core families.
//
// utils/scripts/check-core-shapes.py reads the disassembly of this unit.  Each
// probe is a pair of functions.  The first function does one hot operation of a
// family of include/foundation/core.  The second function does the same work in
// the raw form that the family replaces: a std::atomic operation, a raw pointer
// loop, memcpy or memset, a std::unique_ptr or std::optional access, or
// std::unreachable.  The check compares the instructions of the two functions
// of each pair with the expectation of the pair.
//
// test/shape/CMakeLists.txt compiles this unit with one fixed set of flags in
// each preset.  Each function takes its operands as parameters, so the
// optimizer cannot fold an operand into the code.  Each function is
// [[gnu::used]] in an unnamed namespace.  The compiler then emits it with the
// signature that it declares, and its name stays in this unit.
//
// The raw forms are in one opt-out region of the class ORACLE.  Each raw form
// is the reference for the shape of its family operation, so it must stay raw.

#include <fixy/Core.h>
#include <foundation/Quarantine.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <atomic>
#include <memory>
#include <optional>
#include <utility>

namespace {

// A value of two words.  The cell of an Atomic holds it as one 64-bit integer.
struct WordPair final {
    std::uint32_t low = 0;
    std::uint32_t high = 0;
};

struct Payload final {
    std::uint32_t value = 0;
};

enum class Kind : std::uint8_t {
    first,
    second,
    third,
    fourth
};

// ── Atomic ──────────────────────────────────────────────────────────────────

[[gnu::used]] std::uint64_t atomic_load(fixy::Atomic<std::uint64_t> const& cell) noexcept {
    return cell.load_acquire();
}

[[gnu::used]] void atomic_store(fixy::Atomic<std::uint64_t>& cell, std::uint64_t value) noexcept {
    cell.store_release(value);
}

[[gnu::used]] std::uint64_t atomic_exchange(fixy::Atomic<std::uint64_t>& cell, std::uint64_t value) noexcept {
    return cell.exchange_acq_rel(value);
}

[[gnu::used]] bool atomic_cas(fixy::Atomic<std::uint64_t>& cell, std::uint64_t expected,
                              std::uint64_t desired) noexcept {
    return cell.cas_acq_rel(expected, desired).is_ok();
}

[[gnu::used]] bool atomic_cas_pair(fixy::Atomic<WordPair>& cell, WordPair expected, WordPair desired) noexcept {
    return cell.cas_acq_rel(expected, desired).is_ok();
}

[[gnu::used]] std::uint64_t atomic_fetch_add(fixy::Atomic<std::uint64_t>& cell, std::uint64_t delta) noexcept {
    return cell.fetch_add_acq_rel(delta);
}

[[gnu::used]] std::uint64_t atomic_fetch_sub(fixy::Atomic<std::uint64_t>& cell, std::uint64_t delta) noexcept {
    return cell.fetch_sub_acq_rel(delta);
}

[[gnu::used]] std::uint64_t atomic_fetch_or(fixy::Atomic<std::uint64_t>& cell, std::uint64_t bits) noexcept {
    return cell.fetch_or_acq_rel(bits);
}

[[gnu::used]] std::uint64_t atomic_fetch_and(fixy::Atomic<std::uint64_t>& cell, std::uint64_t bits) noexcept {
    return cell.fetch_and_acq_rel(bits);
}

[[gnu::used]] std::uint64_t atomic_fetch_max(fixy::Atomic<std::uint64_t>& cell, std::uint64_t value) noexcept {
    return cell.fetch_max_acq_rel(value);
}

[[gnu::used]] std::uint64_t atomic_fetch_min(fixy::Atomic<std::uint64_t>& cell, std::uint64_t value) noexcept {
    return cell.fetch_min_acq_rel(value);
}

[[gnu::used]] void tally_add(fixy::Tally& tally, std::uint64_t amount) noexcept { tally.add(amount); }

[[gnu::used]] std::uint64_t tally_read(fixy::Tally const& tally) noexcept { return tally.read(); }

// ── Region ──────────────────────────────────────────────────────────────────

[[gnu::used]] std::uint32_t view_sum(fixy::View<std::uint32_t const> view) noexcept {
    std::uint32_t sum = 0;
    for (std::uint32_t element : view) {
        sum += element;
    }
    return sum;
}

[[gnu::used]] fixy::Option<fixy::View<std::uint32_t const>>
view_window(fixy::View<std::uint32_t const> view, std::size_t offset, std::size_t count) noexcept {
    return view.window(offset, count);
}

// The append path of a producer: the window of the run in the segment, then
// the copy of the run into the window.
[[gnu::used]] bool view_append(fixy::View<std::uint8_t> segment, std::size_t used,
                               fixy::View<std::uint8_t const> run) noexcept {
    bool is_appended = false;
    for (fixy::View<std::uint8_t> window : segment.window(used, run.size())) {
        is_appended = fixy::copy(window, run).is_ok();
    }
    return is_appended;
}

[[gnu::used]] void view_fill(fixy::View<std::uint8_t> view) noexcept { fixy::fill(view, std::uint8_t{0}); }

// ── Ref, Choice and Report ──────────────────────────────────────────────────

[[gnu::used]] std::uint32_t box_get(fixy::Box<Payload>& box) noexcept { return box.get().value; }

// An Option of a plain payload, as a function gives it back: in a register.
// expect adds one test and one branch to a cold call.  The payload fills the
// low word of the register, so the flag is at bit 32, and the code also uses a
// move and a shift to read the flag.  With the flag at byte 0, the shift reads
// the payload, and the hot part has one instruction less.
[[gnu::used]] std::uint32_t option_expect(fixy::Option<std::uint32_t> option) noexcept {
    return std::move(option).expect("the probe gets a full Option");
}

// An Option whose payload has a niche, so the Option has no flag.  expect
// adds one compare and one branch to a cold call.
[[gnu::used]] fixy::View<std::uint32_t const>
option_expect_niche(fixy::Option<fixy::View<std::uint32_t const>> option) noexcept {
    return std::move(option).expect("the probe gets a full Option");
}

[[gnu::used]] std::uint32_t switch_unreachable(Kind kind) noexcept {
    switch (kind) {
        case Kind::first:
            return 11;
        case Kind::second:
            return 23;
        case Kind::third:
            return 37;
        case Kind::fourth:
            return 41;
        default:
            fixy::unreachable();
    }
}

// ── The raw forms ───────────────────────────────────────────────────────────

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: the raw form that the shape of each family probe is compared with")

struct RawWindow final {
    std::uint32_t const* first;
    std::size_t count;
};

[[gnu::used]] std::uint64_t atomic_load_raw(std::atomic<std::uint64_t> const& cell) noexcept {
    return cell.load(std::memory_order_acquire);
}

[[gnu::used]] void atomic_store_raw(std::atomic<std::uint64_t>& cell, std::uint64_t value) noexcept {
    cell.store(value, std::memory_order_release);
}

[[gnu::used]] std::uint64_t atomic_exchange_raw(std::atomic<std::uint64_t>& cell, std::uint64_t value) noexcept {
    return cell.exchange(value, std::memory_order_acq_rel);
}

[[gnu::used]] bool atomic_cas_raw(std::atomic<std::uint64_t>& cell, std::uint64_t expected,
                                  std::uint64_t desired) noexcept {
    return cell.compare_exchange_strong(expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
}

[[gnu::used]] bool atomic_cas_pair_raw(std::atomic<WordPair>& cell, WordPair expected, WordPair desired) noexcept {
    return cell.compare_exchange_strong(expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
}

[[gnu::used]] std::uint64_t atomic_fetch_add_raw(std::atomic<std::uint64_t>& cell, std::uint64_t delta) noexcept {
    return cell.fetch_add(delta, std::memory_order_acq_rel);
}

[[gnu::used]] std::uint64_t atomic_fetch_sub_raw(std::atomic<std::uint64_t>& cell, std::uint64_t delta) noexcept {
    return cell.fetch_sub(delta, std::memory_order_acq_rel);
}

[[gnu::used]] std::uint64_t atomic_fetch_or_raw(std::atomic<std::uint64_t>& cell, std::uint64_t bits) noexcept {
    return cell.fetch_or(bits, std::memory_order_acq_rel);
}

[[gnu::used]] std::uint64_t atomic_fetch_and_raw(std::atomic<std::uint64_t>& cell, std::uint64_t bits) noexcept {
    return cell.fetch_and(bits, std::memory_order_acq_rel);
}

[[gnu::used]] std::uint64_t atomic_fetch_max_raw(std::atomic<std::uint64_t>& cell, std::uint64_t value) noexcept {
    return cell.fetch_max(value, std::memory_order_acq_rel);
}

[[gnu::used]] std::uint64_t atomic_fetch_min_raw(std::atomic<std::uint64_t>& cell, std::uint64_t value) noexcept {
    return cell.fetch_min(value, std::memory_order_acq_rel);
}

// A statistics counter that a Tally replaces is a relaxed counter.
[[gnu::used]] void tally_add_raw(std::atomic<std::uint64_t>& counter, std::uint64_t amount) noexcept {
    counter.fetch_add(amount, std::memory_order_relaxed);
}

[[gnu::used]] std::uint64_t tally_read_raw(std::atomic<std::uint64_t> const& counter) noexcept {
    return counter.load(std::memory_order_relaxed);
}

[[gnu::used]] std::uint32_t view_sum_raw(std::uint32_t const* first, std::size_t count) noexcept {
    std::uint32_t sum = 0;
    for (std::uint32_t const* cursor = first; cursor != first + count; ++cursor) {
        sum += *cursor;
    }
    return sum;
}

[[gnu::used]] RawWindow view_window_raw(std::uint32_t const* first, std::size_t size, std::size_t offset,
                                        std::size_t count) noexcept {
    if (offset > size || count > size - offset) [[unlikely]] {
        return RawWindow{nullptr, ~std::size_t{0}};
    }
    return RawWindow{first + offset, count};
}

[[gnu::used]] bool view_append_raw(std::uint8_t* segment, std::size_t size, std::size_t used, std::uint8_t const* run,
                                   std::size_t run_size) noexcept {
    if (used > size || run_size > size - used) [[unlikely]] {
        return false;
    }
    std::memcpy(segment + used, run, run_size);
    return true;
}

[[gnu::used]] void view_fill_raw(std::uint8_t* first, std::size_t count) noexcept { std::memset(first, 0, count); }

[[gnu::used]] std::uint32_t box_get_raw(std::unique_ptr<Payload>& box) noexcept { return box->value; }

[[gnu::used]] std::uint32_t option_expect_raw(std::optional<std::uint32_t> option) noexcept { return *option; }

[[gnu::used]] RawWindow option_expect_niche_raw(RawWindow window) noexcept { return window; }

[[gnu::used]] std::uint32_t switch_unreachable_raw(Kind kind) noexcept {
    switch (kind) {
        case Kind::first:
            return 11;
        case Kind::second:
            return 23;
        case Kind::third:
            return 37;
        case Kind::fourth:
            return 41;
        default:
            std::unreachable();
    }
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace
