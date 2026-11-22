// The probes of the shape check of the core families and of the linear
// values.
//
// The rule codegen-shapes (utils/assay/guard/engines/elf/codegen_shapes.py) reads the disassembly of this unit.  Each
// probe is a pair of functions.  The first function does one hot operation of a
// family of include/foundation/core, of fixy::Linear, of a counter of
// fixy/Mutation.h or of a permission token.
// The second function does the same work in the raw form that the family
// replaces: a std::atomic operation, a raw pointer loop, memcpy or memset, a
// std::unique_ptr or std::optional access, std::unreachable, a plain value,
// an empty class or a contract_assert.  The check compares the instructions of the two functions
// of each pair, and the probe of the pair in the rule declares the
// differences that the family function can add.
//
// test/shape/CMakeLists.txt compiles this unit with the flags of the
// foundation library.  Each build then compares the pairs with its own flags.
// Each function takes its operands as parameters, and the optimizer cannot
// fold an operand into the code.  Each function is [[gnu::used]] in an
// unnamed namespace.  The compiler then emits it with the signature that it
// declares, and its name stays in this unit.
//
// The raw forms are in one opt-out region of the class ORACLE.  Each raw form
// is the reference for the shape of its family operation, so it must stay raw.
// The one exception is a combinator of Option or Result: its reference is the
// hand match that it replaces, beside it, and the match has its own pair.
//
// A test build arms the consume tracker of fixy/Qtt.h on each unit, as a test
// oracle of the rule must-consume.  The probes read the Release form of a
// Linear in each build, so this unit keeps the tracker off.  The unit is an
// object of its own, and no target links it.
#undef CRUCIBLE_QTT_TRACK_CONSUME
#define CRUCIBLE_QTT_TRACK_CONSUME 0

#include <fixy/Core.h>
#include <fixy/Mutation.h>
#include <fixy/Protocol.h>
#include <fixy/Qtt.h>
#include <foundation/AlignedBytes.h>
#include <foundation/Quarantine.h>
#include <foundation/alloc/HeapState.h>
#include <foundation/alloc/Layout.h>
#include <foundation/contracts/Post.h>
#include <foundation/core/CyclicCursor.h>
#include <foundation/core/Immortal.h>
#include <foundation/core/Seat.h>
#include <foundation/core/WindowStack.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>
#include <foundation/heap/HeapScope.h>
#include <foundation/permissions/Permission.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <compare>
#include <expected>
#include <limits>
#include <memory>
#include <numeric>
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

// The brand of the Ref of a probe.  A brand is an empty class.
struct RefProbeBrand final {};

// A word that only moves.  An Option of it takes the owning form, and it has
// no linear marker, so the end of an Option of it drops it.
struct MovedWord final {
    std::uint64_t value = 0;

    constexpr explicit MovedWord(std::uint64_t word) noexcept : value{word} {}
    MovedWord(MovedWord&&) noexcept = default;
    MovedWord(MovedWord const&) = delete("a moved word only moves, so an Option of it takes the owning form");
    MovedWord& operator=(MovedWord&&) noexcept = default;
    MovedWord& operator=(MovedWord const&) = delete("a moved word only moves");
};

// A payload of 64 bytes.  An Option of it goes through memory.
struct Block64 final {
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    std::uint64_t third = 0;
    std::uint64_t fourth = 0;
    std::uint64_t fifth = 0;
    std::uint64_t sixth = 0;
    std::uint64_t seventh = 0;
    std::uint64_t eighth = 0;
};

enum class Kind : std::uint8_t {
    first,
    second,
    third,
    fourth
};

// An error of four bytes.
enum class Code32 : std::uint32_t {
    gone = 1,
    late = 2
};

// An error of one byte, and the error of a caller that takes it through an
// implicit conversion.
enum class Code8 : std::uint8_t {
    lost = 1
};

struct NetError8 final {
    Code8 code = Code8::lost;
    constexpr NetError8(Code8 from) noexcept : code{from} {}
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

[[gnu::used]] bool atomic_cas_weak(fixy::Atomic<std::uint64_t>& cell, std::uint64_t expected,
                                   std::uint64_t desired) noexcept {
    return cell.cas_weak_acq_rel(expected, desired).is_ok();
}

[[gnu::used]] std::uint64_t atomic_fetch_xor(fixy::Atomic<std::uint64_t>& cell, std::uint64_t bits) noexcept {
    return cell.fetch_xor_acq_rel(bits);
}

[[gnu::used]] std::uint64_t count_fetch_add(fixy::Atomic<std::uint64_t, fixy::Counter>& count,
                                            std::uint64_t delta) noexcept {
    return count.fetch_add_relaxed(delta);
}

[[gnu::used]] std::uint64_t count_load(fixy::Atomic<std::uint64_t, fixy::Counter> const& count) noexcept {
    return count.load_acquire();
}

[[gnu::used]] void tally_add(fixy::Tally& tally, std::uint64_t amount) noexcept { tally.add(amount); }

[[gnu::used]] std::uint64_t tally_read(fixy::Tally const& tally) noexcept { return tally.read(); }

// The bump of a WideMonotonic holds no compare: its type bounds the start and
// the step, so the bump cannot wrap the count.
[[gnu::used]] void wide_monotonic_bump(fixy::WideMonotonic<1>& counter) noexcept { counter.bump(); }

// A read of a cyclic cursor holds no test of the end, and its step holds one
// compare: the test of the wrap.
[[gnu::used]] std::uint64_t
cyclic_cursor_read_step(foundation::core::CyclicCursor<std::uint64_t const>& cursor) noexcept {
    std::uint64_t const value = *cursor;
    static_cast<void>(cursor.step());
    return value;
}

[[gnu::used]] std::uint64_t cache_line_load(fixy::CacheLine<fixy::Atomic<std::uint64_t>> const& line) noexcept {
    return line->load_acquire();
}

// The cell of one writer.  The proof is a token of one word, its run-time id,
// and the read and the write of the writer compare it with the id that the
// cell keeps.  A fresh brand stays in the scope that made it, so no alias
// names the writer.
// Each probe names its cell in its first parameter, and the second parameter
// takes the writer of that cell.
struct ShapeHeadRole final {};

template <class CellReference>
struct writer_of;

template <class T, class Writer>
struct writer_of<fixy::Atomic<T, Writer> const&> {
    using type = Writer;
};

template <class T, class Writer>
struct writer_of<fixy::Atomic<T, Writer>&> {
    using type = Writer;
};

[[gnu::used]] std::uint64_t
sole_load_owned(fixy::Atomic<std::uint64_t, decltype(fixy::mint_sole_writer<ShapeHeadRole>())> const& cell,
                typename writer_of<decltype(cell)>::type const& writer) noexcept {
    return cell.load_owned(writer);
}

[[gnu::used]] void sole_store(fixy::Atomic<std::uint64_t, decltype(fixy::mint_sole_writer<ShapeHeadRole>())>& cell,
                              typename writer_of<decltype(cell)>::type const& writer, std::uint64_t value) noexcept {
    cell.store_release(writer, value);
}

[[gnu::used]] std::uint64_t
sole_load(fixy::Atomic<std::uint64_t, decltype(fixy::mint_sole_writer<ShapeHeadRole>())> const& cell) noexcept {
    return cell.load_acquire();
}

// A seat number that is never zero, a value that keeps an invariant.  Its
// word is the number, and zero is its empty word and the empty value of its
// niche, so the cell of an Option of a seat holds one 32-bit word.  The
// optimizer then removes the check of the niche on a load and the check of
// the empty word on a store.
class ShapeSeat final {
    std::uint32_t number_ = 1;
    constexpr explicit ShapeSeat(std::uint32_t number) noexcept : number_{number} {}
    friend struct ::foundation::core::atomic_word<ShapeSeat>;

public:
    constexpr ShapeSeat(ShapeSeat const&) noexcept = default;
    constexpr ShapeSeat& operator=(ShapeSeat const& other) noexcept {
        number_ = other.number_;
        return *this;
    }
    [[nodiscard]] static constexpr ShapeSeat niche_empty(fixy::niche_key<ShapeSeat> const&) noexcept {
        return ShapeSeat{0};
    }
    [[nodiscard]] constexpr bool is_niche_empty(fixy::niche_key<ShapeSeat> const&) const noexcept {
        return number_ == 0;
    }
};

}  // namespace

template <>
struct foundation::core::atomic_word<ShapeSeat> {
    using word_type = std::uint32_t;
    static constexpr word_type empty_word = 0;
    [[nodiscard]] static constexpr word_type to_word(ShapeSeat const& seat) noexcept { return seat.number_; }
    [[nodiscard]] static constexpr ShapeSeat from_word(atomic_word_key<ShapeSeat> const&, word_type word) noexcept {
        return ShapeSeat{word};
    }
};

namespace {

[[gnu::used]] fixy::Option<ShapeSeat> word_option_load(fixy::Atomic<fixy::Option<ShapeSeat>> const& cell) noexcept {
    return cell.load_acquire();
}

[[gnu::used]] void word_option_store(fixy::Atomic<fixy::Option<ShapeSeat>>& cell,
                                     fixy::Option<ShapeSeat> value) noexcept {
    cell.store_release(value);
}

// The cell of an Option of a Ref holds the pointer of the object, and the
// null pointer is the empty Option.
using ShapeRefOption = fixy::Option<fixy::Ref<Payload, RefProbeBrand>>;

[[gnu::used]] ShapeRefOption ref_option_load(fixy::Atomic<ShapeRefOption> const& cell) noexcept {
    return cell.load_acquire();
}

[[gnu::used]] void ref_option_store(fixy::Atomic<ShapeRefOption>& cell, ShapeRefOption value) noexcept {
    cell.store_release(value);
}

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

[[gnu::used]] std::uint32_t fixed_array_at(::foundation::core::FixedArray<std::uint32_t, 16> const& array,
                                           fixy::Index<16> position) noexcept {
    return array.at(position);
}

// The or of a bit into one word of a FixedArray through the element door, as
// the insert of a CPU into a CpuSet does.
[[gnu::used]] void fixed_array_with_element(::foundation::core::FixedArray<std::uint64_t, 128>& words,
                                            fixy::Index<128> position, std::uint64_t bit) noexcept {
    words.with_element(position, [bit](std::uint64_t& word) noexcept { word |= bit; });
}

// The or of a bit into one element of a HeapArray through the element door.
// A position at or past the count takes the fatal exit, as the write through
// get() and expect did.
[[gnu::used]] void heap_array_with_element(::foundation::core::HeapArray<std::uint32_t>& array, std::size_t position,
                                           std::uint32_t bit) noexcept {
    array.with_element(
        position, [bit](std::uint32_t& element) noexcept { element |= bit; },
        [] noexcept { ::foundation::core::fatal("the probe gets a position below the count"); });
}

// The or of a bit into the element of a masked key of a MaskedArray through
// the element door.  An array that holds no storage takes the fatal exit, as
// a read through at_masked does.
[[gnu::used]] void masked_array_with_masked(::foundation::core::MaskedArray<std::uint32_t>& array, std::uint64_t key,
                                            std::uint32_t bit) noexcept {
    array.with_masked(key, [bit](std::uint32_t& element) noexcept { element |= bit; });
}

// Two arrays of a batch, as the batch of the drain stage holds them.
struct ShapeBatch final {
    ::foundation::core::FixedArray<std::uint64_t, 64> words{};
    ::foundation::core::FixedArray<std::uint32_t, 64> halves{};
};

// The pop of a batch: a copy of a run into each array of the batch, each
// through its member frame door in place, as the drain stage pops the ring.
[[gnu::used]] void fixed_array_frame_pop(ShapeBatch& batch, fixy::View<std::uint64_t const, 64> words,
                                         fixy::View<std::uint32_t const, 64> halves) noexcept {
    batch.words.with_mut([&batch, words, halves](auto word_run) noexcept {
        batch.halves.with_mut([&word_run, words, halves](auto half_run) noexcept {
            fixy::copy(word_run.view(), words);
            fixy::copy(half_run.view(), halves);
        });
    });
}

// The brands of the keyed arrays of the probes, one for each array.
struct ShapeWordsBrand final {};
struct ShapeHalvesBrand final {};
struct ShapeSourceBrand final {};
struct ShapeTargetBrand final {};

struct KeyedShapeBatch final {
    ::foundation::core::FixedArray<std::uint64_t, 64, ShapeWordsBrand> words{};
    ::foundation::core::FixedArray<std::uint32_t, 64, ShapeHalvesBrand> halves{};
};

// The pop of a batch of keyed arrays: the pop of fixed_array_frame_pop, with
// the two keyed frame doors in the borrow form.  A key is an empty class with
// a trivial move, so it takes no register as an argument, and the code is the
// code of the doors with no key.
[[gnu::used]] void keyed_array_frame_pop(KeyedShapeBatch& batch, ::foundation::core::LoanKey<ShapeWordsBrand> words_key,
                                         ::foundation::core::LoanKey<ShapeHalvesBrand> halves_key,
                                         fixy::View<std::uint64_t const, 64> words,
                                         fixy::View<std::uint32_t const, 64> halves) noexcept {
    batch.words.with_mut(words_key, [&batch, &halves_key, words, halves](auto word_run) noexcept {
        batch.halves.with_mut(halves_key, [&word_run, words, halves](auto half_run) noexcept {
            fixy::copy(word_run.view(), words);
            fixy::copy(half_run.view(), halves);
        });
    });
}

// A copy from the frame of one keyed array into the frame of another: the
// two frames have two brands, so the copy is one memcpy (THE ALIAS PROOF of
// foundation/core/Frame.h).
[[gnu::used]] void keyed_frame_copy(::foundation::core::FixedArray<int, 64, ShapeSourceBrand>& source,
                                    ::foundation::core::LoanKey<ShapeSourceBrand>& source_key,
                                    ::foundation::core::FixedArray<int, 64, ShapeTargetBrand>& target,
                                    ::foundation::core::LoanKey<ShapeTargetBrand>& target_key) noexcept {
    source.with_mut(source_key, [&target, &target_key](auto source_run) noexcept {
        target.with_mut(target_key, [&source_run](auto target_run) noexcept {
            static_cast<void>(::foundation::core::copy(std::move(target_run), source_run).is_ok());
        });
    });
}

// An op that doubles its input, for write_each.
struct ShapeTwice final {
    int operator()(int const& value) const noexcept { return value * 2; }
};

// write_each from the frame of one keyed array into the frame of another: the
// two brands give the loop of restrict pointers.
[[gnu::used]] void keyed_frame_write_each(::foundation::core::FixedArray<int, 64, ShapeSourceBrand>& source,
                                          ::foundation::core::LoanKey<ShapeSourceBrand>& source_key,
                                          ::foundation::core::FixedArray<int, 64, ShapeTargetBrand>& target,
                                          ::foundation::core::LoanKey<ShapeTargetBrand>& target_key) noexcept {
    source.with_mut(source_key, [&target, &target_key](auto source_run) noexcept {
        target.with_mut(target_key, [&source_run](auto target_run) noexcept {
            static_cast<void>(::foundation::core::write_each(std::move(target_run), ShapeTwice{}, source_run).is_ok());
        });
    });
}

// The brand of the keyed heap array of the probe.
struct ShapeHeapBrand final {};

// A copy of a run into a keyed FixedHeapArray through its keyed frame door in
// the borrow form, as the metadata log writes its ring.  The array holds a
// pointer and a count, so the code loads the pointer and copies the run.  A
// key is an empty class, so as an argument by value it takes no register.
[[gnu::used]] void keyed_heap_array_frame_copy(
    ::foundation::core::FixedHeapArray<std::uint64_t, 64, alignof(std::uint64_t), ShapeHeapBrand>& target,
    ::foundation::core::LoanKey<ShapeHeapBrand> key, fixy::View<std::uint64_t const, 64> words) noexcept {
    target.with_mut(key, [words](auto run) noexcept { fixy::copy(run.view(), words); });
}

// The open of a loan of count elements, a write of its first element, and
// its end, as the record of one op opens its frame of records.
[[gnu::used]] std::uint64_t window_loan_open(::foundation::core::WindowStack<std::uint64_t, 64>& stack,
                                             ::foundation::core::WindowCount count) noexcept {
    ::foundation::core::WindowLoan<std::uint64_t, 64> const loan{stack, count};
    std::uint64_t held = 0;
    for (fixy::View<std::uint64_t> const window : loan.window()) {
        held = window.size();
    }
    return held;
}

[[gnu::used]] fixy::Option<fixy::Index<16>> index_check(std::size_t position) noexcept {
    return fixy::Index<16>::check(position);
}

[[gnu::used]] fixy::Option<fixy::TextView> text_window(fixy::TextView text, std::size_t offset,
                                                       std::size_t count) noexcept {
    return text.window(offset, count);
}

// The query of a window: the two address ranges compared as integers.
[[gnu::used]] bool text_is_window_of(fixy::TextView part, fixy::TextView whole) noexcept {
    return fixy::is_window_of(fixy::WindowPartText{part}, fixy::WindowWholeText{whole});
}

// The value of the digits of a run of a text: a loop over the Option of the
// window of the run, and a loop over the characters of the window.
[[gnu::used]] std::uint32_t text_window_digits(fixy::TextView text, std::size_t offset, std::size_t count) noexcept {
    std::uint32_t value = 0;
    for (fixy::TextView run : text.window(offset, count)) {
        for (char const character : run) {
            value = value * 10u + static_cast<std::uint32_t>(character - '0');
        }
    }
    return value;
}

// The sum of a run of a View: a loop over the Option of the window of the
// run, and a loop over the elements of the window.
[[gnu::used]] std::uint32_t view_window_sum(fixy::View<std::uint32_t const> view, std::size_t offset,
                                            std::size_t count) noexcept {
    std::uint32_t sum = 0;
    for (fixy::View<std::uint32_t const> run : view.window(offset, count)) {
        for (std::uint32_t const element : run) {
            sum += element;
        }
    }
    return sum;
}

// The first and the last element of a run of four elements: a loop over the
// Option of the window<4>, and two reads at constant indexes of the run.
[[gnu::used]] std::uint32_t view_window_fixed(fixy::View<std::uint32_t const> view, std::size_t offset) noexcept {
    std::uint32_t value = 0;
    for (fixy::View<std::uint32_t const, 4> run : view.window<4>(offset)) {
        value = run.at(fixy::index<0>()) ^ run.at(fixy::index<3>());
    }
    return value;
}

[[gnu::used]] std::uint32_t view_reverse_sum(fixy::View<std::uint32_t const> view) noexcept {
    std::uint32_t sum = 0;
    for (std::uint32_t element : view.reverse()) {
        sum += element;
    }
    return sum;
}

// Each run of two consecutive elements, with a step of one element.
[[gnu::used]] std::uint32_t view_windows_sum(fixy::View<std::uint32_t const> view) noexcept {
    std::uint32_t sum = 0;
    for (fixy::View<std::uint32_t const, 2> run : view.windows<2>()) {
        sum += run.at(fixy::index<0>()) ^ run.at(fixy::index<1>());
    }
    return sum;
}

// Each run of four elements, with a step of four elements.
[[gnu::used]] std::uint32_t view_chunks_sum(fixy::View<std::uint32_t const> view) noexcept {
    std::uint32_t sum = 0;
    for (fixy::View<std::uint32_t const, 4> run : view.chunks<4>()) {
        sum += run.at(fixy::index<0>()) ^ run.at(fixy::index<3>());
    }
    return sum;
}

// Each element of a View of a fixed extent with its proven index.
[[gnu::used]] std::uint32_t view_enumerate_sum(fixy::View<std::uint32_t const, 64> view) noexcept {
    std::uint32_t sum = 0;
    for (auto const [position, element] : view.enumerate()) {
        sum += *element * static_cast<std::uint32_t>(position.raw());
    }
    return sum;
}

// The pairs of two walks of one fixed extent: the type proves the counts
// equal, so the loop has no compare before its first step.
[[gnu::used]] std::uint32_t view_zip_fixed_sum(fixy::View<std::uint32_t const, 64> left,
                                               fixy::View<std::uint32_t const, 64> right) noexcept {
    std::uint32_t sum = 0;
    for (auto const [first, second] : left.walk().zip(right.walk())) {
        sum += *first * *second;
    }
    return sum;
}

// The pairs of two walks of a dynamic extent: the compare of the counts, and
// the fold over the pairs.  The fold pushes each pair through the walk.  The
// cursor loop of a zip has the pair view_zip_fixed_sum.
[[gnu::used]] std::uint32_t view_zip_sum(fixy::View<std::uint32_t const> left,
                                         fixy::View<std::uint32_t const> right) noexcept {
    return left.walk()
        .try_zip(right.walk())
        .match(
            [](auto pairs) noexcept {
                return static_cast<decltype(pairs)&&>(pairs).fold(std::uint32_t{0},
                                                                  [](std::uint32_t sum, auto const& pair) noexcept {
                                                                      auto const& [first, second] = pair;
                                                                      return sum + *first * *second;
                                                                  });
            },
            [](fixy::LengthMismatch) noexcept { return std::uint32_t{0}; });
}

// ── The walks ───────────────────────────────────────────────────────────────
//
// Each terminal of a walk pushes each element through the nest of its stages,
// so a chain compiles to the loop of its raw form.  A float fold stays left
// to right: the raw loop and the chain multiply in lanes and add the products
// in order.

// The sum of the squares of a run of floats, left to right.
[[gnu::used]] float walk_map_fold_f32(fixy::View<float const> view) noexcept {
    return view.walk()
        .map([](float const& value) noexcept { return value * value; })
        .fold(0.0f, [](float sum, float square) noexcept { return sum + square; });
}

// The same over 32-bit words, whose add the optimizer can reorder.
[[gnu::used]] std::uint32_t walk_map_fold_u32(fixy::View<std::uint32_t const> view) noexcept {
    return view.walk()
        .map([](std::uint32_t const& value) noexcept { return value * value; })
        .fold(std::uint32_t{0}, [](std::uint32_t sum, std::uint32_t square) noexcept { return sum + square; });
}

// The count of the floats above a limit.
[[gnu::used]] std::size_t walk_filter_count_f32(fixy::View<float const> view) noexcept {
    return view.walk().filter([](float const& value) noexcept { return value > 0.5f; }).count();
}

// The count of the words that three divides.
[[gnu::used]] std::size_t walk_filter_count_u32(fixy::View<std::uint32_t const> view) noexcept {
    return view.walk().filter([](std::uint32_t const& value) noexcept { return value % 3 == 0; }).count();
}

// The dot product of two runs of one fixed extent, left to right.
[[gnu::used]] float walk_zip_fold_f32(fixy::View<float const, 64> left, fixy::View<float const, 64> right) noexcept {
    return left.walk()
        .zip(right.walk())
        .map([](auto const& pair) noexcept {
            auto const& [first, second] = pair;
            return *first * *second;
        })
        .fold(0.0f, [](float sum, float product) noexcept { return sum + product; });
}

// The dot product of two runs of a dynamic extent: the compare of the counts,
// and the loop over the pairs.
[[gnu::used]] float walk_try_zip_fold_f32(fixy::View<float const> left, fixy::View<float const> right) noexcept {
    return left.walk()
        .try_zip(right.walk())
        .match(
            [](auto pairs) noexcept {
                return static_cast<decltype(pairs)&&>(pairs)
                    .map([](auto const& pair) noexcept {
                        auto const& [first, second] = pair;
                        return *first * *second;
                    })
                    .fold(0.0f, [](float sum, float product) noexcept { return sum + product; });
            },
            [](fixy::LengthMismatch) noexcept { return -1.0f; });
}

// The hash of the first rank extents of a fixed array of 8: the shape of the
// loops of the record path over the extents of a tensor.
[[gnu::used]] std::uint64_t walk_limit_fold_u64(fixy::View<std::uint64_t const, 8> extents,
                                                std::uint8_t rank) noexcept {
    return extents.walk()
        .limit(fixy::ElementCount{rank})
        .fold(std::uint64_t{0xcbf29ce484222325ULL}, [](std::uint64_t hash, std::uint64_t const& extent) noexcept {
            return (hash ^ extent) * 0x100000001b3ULL;
        });
}

// A long exact chain: two maps on one side, a map on the other side, a zip, a
// map of the pair and a fold.
[[gnu::used]] std::uint32_t walk_long_chain_u32(fixy::View<std::uint32_t const, 64> left,
                                                fixy::View<std::uint32_t const, 64> right) noexcept {
    return left.walk()
        .map([](std::uint32_t const& value) noexcept { return value + 3; })
        .map([](std::uint32_t value) noexcept { return value * 5; })
        .zip(right.walk().map([](std::uint32_t const& value) noexcept { return value ^ 0x55u; }))
        .map([](auto const& pair) noexcept {
            auto const& [first, second] = pair;
            return first * second + 1;
        })
        .fold(std::uint32_t{0}, [](std::uint32_t sum, std::uint32_t term) noexcept { return sum + term; });
}

// A map, a filter, a limit and a fold: the stop of a limit of a walk of
// unknown count, inside a fold.
[[gnu::used]] std::uint32_t walk_filter_limit_fold_u32(fixy::View<std::uint32_t const> view,
                                                       std::uint32_t limit) noexcept {
    return view.walk()
        .map([](std::uint32_t const& value) noexcept { return value * 3; })
        .filter([](std::uint32_t const& value) noexcept { return value % 7 == 0; })
        .limit(fixy::ElementCount{limit})
        .fold(std::uint32_t{0}, [](std::uint32_t sum, std::uint32_t value) noexcept { return sum + value; });
}

// any_of after a map, over a run of a fixed extent: the stop of the push.
[[gnu::used]] bool walk_map_any_u32(fixy::View<std::uint32_t const, 64> view) noexcept {
    return view.walk()
        .map([](std::uint32_t const& value) noexcept { return value >> 3; })
        .any_of([](std::uint32_t const& shifted) noexcept { return shifted == 17; });
}

// all_of: the stop at the first refusal.
[[gnu::used]] bool walk_all_u32(fixy::View<std::uint32_t const> view) noexcept {
    return view.walk().all_of([](std::uint32_t const& value) noexcept { return value < 1000; });
}

// The sum of the words of each run of a run of runs, one run after the other.
[[gnu::used]] std::uint32_t walk_flat_map_fold_u32(fixy::View<fixy::View<std::uint32_t const> const> runs) noexcept {
    return runs.walk()
        .flat_map([](fixy::View<std::uint32_t const> const& run) noexcept { return run.walk(); })
        .fold(std::uint32_t{0}, [](std::uint32_t sum, std::uint32_t const& value) noexcept { return sum + value; });
}

// The cursor loop over a map: the counted loop of the View.
[[gnu::used]] float walk_map_fold_cursor_f32(fixy::View<float const> view) noexcept {
    float sum = 0.0f;
    for (float const square : view.walk().map([](float const& value) noexcept { return value * value; })) {
        sum += square;
    }
    return sum;
}

// The cursor loop over a filter: a scalar loop nest (THE CURSOR LOOP of
// foundation/core/Walk.h).
[[gnu::used]] std::size_t walk_filter_count_cursor_f32(fixy::View<float const> view) noexcept {
    std::size_t kept = 0;
    for ([[maybe_unused]] float const& value :
         view.walk().filter([](float const& candidate) noexcept { return candidate > 0.5f; })) {
        ++kept;
    }
    return kept;
}

// The parallel columns of one branded scope, as a helper of the scope takes
// them: each column by value.  A probe function is compiled and never
// called, so its brand is a named empty class.
struct BrandedProbeBrand final {};
using BrandedColumn = fixy::BrandedView<std::uint32_t const, BrandedProbeBrand>;

// A loop over three parallel columns, with a release store of the sum of
// each row.  Each read keeps the base pointer of its column in a register,
// so the loop loads and stores no column on the stack.  The release store
// is one plain store on x86-64, and the vectorizer leaves the loop as it is
// on each side.
[[gnu::used]] void branded_columns(fixy::Atomic<std::uint32_t>& cell, BrandedColumn first, BrandedColumn second,
                                   BrandedColumn third) noexcept {
    for (auto const index : first.indices()) {
        cell.store_release(first.at(index) + second.at(index) + third.at(index));
    }
}

// An append into a FixedList: one compare of the count, the store of the
// element, and the step of the count.
[[gnu::used]] bool fixed_list_append(fixy::FixedList<std::uint32_t, 64>& list, std::uint32_t value) noexcept {
    return list.append(value).is_ok();
}

// The last element of a FixedList, or zero for an empty list.
[[gnu::used]] std::uint32_t fixed_list_take_last(fixy::FixedList<std::uint32_t, 64>& list) noexcept {
    return list.take_last().value_or(0u);
}

// The sum of the elements of a FixedArray: the loop of a raw pointer over the
// elements, with no test of the address of the array.
[[gnu::used]] std::uint32_t fixed_array_sum(::foundation::core::FixedArray<std::uint32_t, 16> const& array) noexcept {
    std::uint32_t sum = 0;
    for (std::uint32_t const element : array) {
        sum += element;
    }
    return sum;
}

// The sum of the elements of a HeapArray: the loop of a raw pointer up to the
// count, with no test of the pointer.
[[gnu::used]] std::uint32_t heap_array_sum(::foundation::core::HeapArray<std::uint32_t> const& array) noexcept {
    std::uint32_t sum = 0;
    for (std::uint32_t const element : array) {
        sum += element;
    }
    return sum;
}

// The sum of the elements of a FixedList: the loop of a raw pointer up to the
// count, with no test of the address of the list.
[[gnu::used]] std::uint32_t fixed_list_sum(fixy::FixedList<std::uint32_t, 64> const& list) noexcept {
    std::uint32_t sum = 0;
    for (std::uint32_t const element : list) {
        sum += element;
    }
    return sum;
}

// ── The bit sets and the slot map ──────────────────────────────────────────

// The domains of the ids of a heap set and of the keys of a slot map.
struct ShapeSlotTag final {};
struct ShapeHandleTag final {};

// test and set of a BitSet with an Index: the load of the word and the test
// or the write of the bit, with no bounds test.
[[gnu::used]] bool bit_set_test(fixy::BitSet<256> const& set, fixy::Index<256> index) noexcept {
    return set.test(index);
}

[[gnu::used]] void bit_set_set(fixy::BitSet<256>& set, fixy::Index<256> index) noexcept { set.set(index); }

// The lanes of a packed word of 16 bits: a domain key of a scoped enum.
enum class ShapeLane : std::uint8_t {
    first,
    last = 15
};

// test of a BitSet of 16 positions with a key of a scoped enum: the compare
// of the key with the count, and the shift and the mask of the word of 16
// bits.
[[gnu::used]] bool bit_set_lane_test(fixy::BitSet<16, ShapeLane> const& set, ShapeLane lane) noexcept {
    return set.test(lane);
}

// test of a HeapBitSet with an id: the compare of the id with the count, and
// the load of the word and the test of the bit.
[[gnu::used]] bool heap_bit_set_test(fixy::HeapBitSet<ShapeSlotTag> const& set, fixy::BitId<ShapeSlotTag> id) noexcept {
    return set.test(id);
}

// The packed word of the kinds of the scalar slots of an op: ten bits of tags
// and five bits of data slots.
struct ShapeTagsField final {};
struct ShapeDataField final {};
using ShapeKinds =
    fixy::PackedWord<std::uint16_t, fixy::BitField<ShapeTagsField, 10>, fixy::BitField<ShapeDataField, 5>>;

// get of a PackedWord: the shift and the mask of the word.
[[gnu::used]] std::uint8_t packed_word_get(ShapeKinds kinds) noexcept { return kinds.get<ShapeDataField>(); }

// with of a PackedWord: the compare of the value with the width of the field,
// and the mask and the or of the word.  A refused value gives zero.
[[gnu::used]] std::uint16_t packed_word_with(ShapeKinds kinds, std::uint8_t data) noexcept {
    std::uint16_t word = 0;
    for (ShapeKinds const changed : kinds.with<ShapeDataField>(data)) {
        word = changed.bits();
    }
    return word;
}

// get of a SlotMap: the compare of the index with the used slots, the test of
// the low bit of the generation of the key, the load of the generation of the
// slot, its compare with the key, and the load of the value.
[[gnu::used]] std::uint32_t slot_map_get(fixy::SlotMap<std::uint32_t, ShapeHandleTag> const& map,
                                         fixy::SlotKey<ShapeHandleTag> key) noexcept {
    std::uint32_t value = 0;
    for (auto const found : map.get(key)) {
        value = *found;
    }
    return value;
}

// ── The owner core ──────────────────────────────────────────────────────────

// The release of a probe owner adds two words of its state to this cell, so
// the optimizer keeps each read of the state.
constinit fixy::Atomic<std::uint64_t> released_words{std::uint64_t{0}};

// The state of a probe owner on the core: three words, as the state of
// AlignedBytes.
struct ThreeWordState final {
    using owner_state_mark = fixy::OwnerStateMark;

    std::uint64_t first = 0;
    std::uint64_t second = 0;
    std::uint64_t third = 0;

    [[nodiscard]] static constexpr ThreeWordState emptied(ThreeWordState const& /*state*/) noexcept {
        return ThreeWordState{};
    }

    static void release(ThreeWordState const& state) noexcept {
        if (state.first != 0) static_cast<void>(released_words.fetch_add_acq_rel(state.second + state.third));
    }
};

class ThreeWordOwner final : public fixy::OwnerCore<ThreeWordState> {
public:
    ThreeWordOwner(ThreeWordOwner&&) noexcept = default;
    ThreeWordOwner& operator=(ThreeWordOwner&&) noexcept = default;

    [[nodiscard]] std::uint64_t second() const noexcept { return state_.second; }
};

// The take of an owner out of its place, a read of one field, and the end of
// the owner through a const local.  The core moves the state through a local
// that is not const, so the state stays in registers, and no copy of it goes
// to the stack (THE COPY OF A STATE of foundation/core/Owner.h).
[[gnu::used]] std::uint64_t owner_take_and_end(ThreeWordOwner& source) noexcept {
    ThreeWordOwner taken{std::move(source)};
    std::uint64_t const second = taken.second();
    ThreeWordOwner const ended{std::move(taken)};
    return second;
}

// Two mints of a block whose bytes hold no value, and their ends.  The mint,
// the constructor of the block and the take of its storage are always
// inline, so the fields stay in registers.  A function that is not always
// inline and has two calls in a unit stays out of line at -O1, as it does in
// a caller at its inline budget at -O3.  Its result then goes through memory
// one word at a time, and a caller that reads two fields with one 16-byte
// load waits for a store forward that cannot occur.  The two layouts can
// swap with no change of the result.
[[gnu::used]] std::size_t aligned_bytes_mint_two(foundation::core::Alloc alloc, fixy::Layout first,
                                                 fixy::Layout second) noexcept {
    foundation::UnwrittenBytes const one = foundation::mint_aligned_bytes(alloc, first);
    foundation::UnwrittenBytes const two = foundation::mint_aligned_bytes(alloc, second);
    return one.size() + two.size();
}

// The output windows of one op with two outputs.  The probe builds the list
// of the two windows, so its count is two at compile time.  The door checks
// each window against the block and compares the pair, and the body writes
// value into the first element of each window that holds one.  The two
// windows can swap with no change of the result.
[[gnu::used]] bool aligned_bytes_two_windows(foundation::AlignedBytes& block, foundation::ByteWindow first,
                                             foundation::ByteWindow second, float value) noexcept {
    fixy::FixedList<foundation::ByteWindow, 2> windows{};
    static_cast<void>(windows.append(first).is_ok());
    static_cast<void>(windows.append(second).is_ok());
    auto const write_first = [value](auto& views) noexcept {
        for (auto& view : views) {
            for (auto element : view.get(0)) {
                *element = value;
            }
        }
    };
    return foundation::with_mut_windows<float>(block, windows, write_first).is_ok();
}

// The same two output windows through the door of a static count.  The
// FixedArray holds the two windows, so the door tests each window and the
// one pair at a fixed position, and the body gets one ViewMut for each
// window as an argument.  The body writes value into the first element of
// each window that holds one.  The two windows can swap with no change of
// the result.
[[gnu::used]] bool aligned_bytes_two_windows_static(foundation::AlignedBytes& block,
                                                    fixy::FixedArray<foundation::ByteWindow, 2> const& windows,
                                                    float value) noexcept {
    auto const write_first = [value](auto first_view, auto second_view) noexcept {
        for (auto element : first_view.get(0)) {
            *element = value;
        }
        for (auto element : second_view.get(0)) {
            *element = value;
        }
    };
    return foundation::with_mut_windows<float>(block, windows, write_first).is_ok();
}

// ── Ref, Choice and Report ──────────────────────────────────────────────────

[[gnu::used]] std::uint32_t box_read(fixy::Box<Payload>& box) noexcept { return box->value; }

// A read through an Immortal: one load of the address of its object, one test
// with a branch to a cold call, and the read.
[[gnu::used]] std::uint32_t immortal_read(foundation::core::Immortal<Payload>& holder) noexcept {
    return holder.get()->value;
}

// The fast paths of a bin of the heap state of the allocator: the load of
// head, one compare with a branch to the cold refill or flush, and the
// load or store of the block and of head.
[[gnu::used]] foundation::alloc::AddressWord heap_allocate(foundation::alloc::HeapState& heap) noexcept {
    return heap.allocate(foundation::alloc::SizeClass{1});
}

[[gnu::used]] void heap_release(foundation::alloc::HeapState& heap, foundation::alloc::AddressWord block) noexcept {
    heap.release(foundation::alloc::SizeClass{1}, block);
}

// A loop of pairs in the body of a scope of the heap of the thread: the read
// of the cell, one compare with the sentinel and its branch to the cold
// claim, then the two fast paths for each pair, with the heap in a register.
[[gnu::used]] std::uint64_t heap_scope_pairs(std::uint32_t pairs) noexcept {
    return foundation::heap::mint_heap_scope(foundation::effects::testing::test().alloc, [pairs](auto& scope) noexcept {
        std::uint64_t sum = 0;
        for (std::uint32_t index = 0; index < pairs; ++index) {
            auto block = scope.template allocate<foundation::alloc::SizeClass{1}>();
            sum += static_cast<std::uint64_t>(block.address());
            scope.release(std::move(block));
        }
        return sum;
    });
}

// A linear handle of one word, whose send gives the next handle, as the send
// of a session handle gives the handle of the next step.
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] StepHandle final {
    std::uint64_t state = 0;

    explicit StepHandle(std::uint64_t value) noexcept : state{value} {}
    StepHandle(StepHandle&&) noexcept = default;
    StepHandle& operator=(StepHandle&& source) noexcept {
        state = source.state;
        return *this;
    }
    StepHandle(StepHandle const&) = delete("a handle of a step is linear");
    StepHandle& operator=(StepHandle const&) = delete("a handle of a step is linear");
    ~StepHandle() = default;

    [[nodiscard]] StepHandle send(std::uint64_t item) && noexcept { return StepHandle{state + item}; }
};

// One turn of a send loop over a Seat: the send on the payload in place, and
// the next handle in the same storage.  The raw form is the emplace of a
// std::optional, which tests and clears its flag first.
[[gnu::used]] void seat_step_send(foundation::core::Seat<StepHandle>& seat, std::uint64_t item) noexcept {
    seat.step([item](StepHandle&& handle) noexcept { return static_cast<StepHandle&&>(handle).send(item); });
}

// A Box built in a Hole: one test of the storage of the Hole with a branch
// to a cold call, the clear of the Hole, and the build of the object in the
// storage.  No call of the allocation door.
[[gnu::used]] fixy::Box<Payload> box_in_hole(fixy::Hole<sizeof(Payload), alignof(Payload)>&& hole,
                                             std::uint32_t value) noexcept {
    return fixy::mint_box<Payload>(std::move(hole), value);
}

// A read through a Ref is the load of a raw pointer.  A Ref has no empty
// state, so the read has no test.
[[gnu::used]] std::uint32_t ref_read(fixy::Ref<Payload const, RefProbeBrand> ref) noexcept { return ref->value; }

// An Option of a plain payload, as a function gives it back: in a register.
// expect adds one test and one branch to a cold call.  For a payload of four
// bytes, the payload is the low word of the register and the flag is bit 32,
// as in std::optional, so the test of the flag takes a shift.
[[gnu::used]] std::uint32_t option_expect(fixy::Option<std::uint32_t> option) noexcept {
    return std::move(option).expect("the probe gets a full Option");
}

// An Option whose payload has a niche, so the Option has no flag.  expect
// adds one compare and one branch to a cold call.
[[gnu::used]] fixy::View<std::uint32_t const>
option_expect_niche(fixy::Option<fixy::View<std::uint32_t const>> option) noexcept {
    return std::move(option).expect("the probe gets a full Option");
}

// The match of an Option is one test of the tag.
[[gnu::used]] std::uint32_t option_match(fixy::Option<std::uint32_t> option) noexcept {
    return std::move(option).match([](std::uint32_t value) noexcept { return value * 3u; },
                                   []() noexcept { return 0u; });
}

// A loop over an Option makes zero turns or one turn.
[[gnu::used]] std::uint32_t option_loop(fixy::Option<std::uint32_t> option, std::uint32_t total) noexcept {
    for (std::uint32_t const& value : option) {
        total += value;
    }
    return total;
}

[[gnu::used]] std::uint32_t option_value_or(fixy::Option<std::uint32_t> option) noexcept {
    return std::move(option).value_or(7u);
}

// A take of an Option of a word through a reference moves the payload out,
// and the Option is empty after it.
[[gnu::used]] std::uint64_t option_take(fixy::Option<std::uint64_t>& held) noexcept {
    return std::move(held).value_or(0u);
}

// is_some of an Option of a word that the caller keeps in memory reads the
// tag word after the payload.
[[gnu::used]] bool option_is_some(fixy::Option<std::uint64_t> const& option) noexcept { return option.is_some(); }

// A clear of an Option of a payload of 64 bytes writes only its tag.  Its sum
// goes through memory, so its empty marker writes no zero word.
[[gnu::used]] void option_clear_block(fixy::Option<Block64>& held) noexcept { held = {}; }

// A fill of an empty Option of a linear word builds the word in place.  The
// word is linear, so the Option takes the owning form, with a flag byte as
// std::optional has.
[[gnu::used]] void option_fill_linear(fixy::Option<fixy::Linear<std::uint64_t>>& held, std::uint64_t value) noexcept {
    held.fill(fixy::mint_linear<std::uint64_t>(value));
}

// expect_none ends an empty Option of a linear word.  It tests the flag, and
// a full Option ends the process, so no word drops.
[[gnu::used]] void option_end_linear(fixy::Option<fixy::Linear<std::uint64_t>>&& held) noexcept {
    std::move(held).expect_none("the caller gives an empty Option");
}

// The twin of option_end_linear with a droppable payload: a word that only
// moves takes the owning form too, and its end drops it.
[[gnu::used]] void option_end_owned(fixy::Option<MovedWord>&& held) noexcept {
    fixy::Option<MovedWord> const ended{std::move(held)};
}

// An empty Option of a word, and an Option that an inlined function gives,
// come back in two registers with no store to the stack.  The tag fills the
// word after the storage, so the cells hold no padding byte that a copy
// carries.
namespace {

[[nodiscard]] constexpr fixy::Option<std::size_t> word_below(std::size_t value, std::size_t limit) noexcept {
    if (value >= limit) return fixy::none;
    return fixy::Option<std::size_t>::some(value);
}

}  // namespace

[[gnu::used]] fixy::Option<std::size_t> option_empty_word() noexcept { return fixy::none; }

[[gnu::used]] fixy::Option<std::size_t> option_word_below(std::size_t value, std::size_t limit) noexcept {
    return word_below(value, limit);
}

// A sealed word, as an Address or a Tagged pointer of an earned tag is.  The
// seal makes its assignment user-provided, so it is not trivially copyable.
// Its copy, move and end are those of its bytes, so an Option of it keeps the
// plain slot.  The Option comes back in two registers, and a match of it is
// one test of the tag, as for an Option of a plain word.
struct SealedWord final {
    std::size_t raw = 0;
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal{};
};

namespace {

[[nodiscard]] constexpr fixy::Option<SealedWord> sealed_word_below(std::size_t value, std::size_t limit) noexcept {
    if (value >= limit) return fixy::none;
    return fixy::Option<SealedWord>::some(SealedWord{.raw = value});
}

}  // namespace

[[gnu::used]] fixy::Option<SealedWord> option_sealed_word_below(std::size_t value, std::size_t limit) noexcept {
    return sealed_word_below(value, limit);
}

[[gnu::used]] std::size_t option_sealed_match(fixy::Option<SealedWord> option) noexcept {
    return std::move(option).match([](SealedWord word) noexcept { return word.raw * 3u; },
                                   []() noexcept { return std::size_t{0}; });
}

// A compare of two Options tests the two states, and compares the payloads
// only when both hold one.
[[gnu::used]] bool option_equal(fixy::Option<std::uint32_t> left, fixy::Option<std::uint32_t> right) noexcept {
    return left == right;
}

[[gnu::used]] bool option_equal_word(fixy::Option<std::uint64_t> left, fixy::Option<std::uint64_t> right) noexcept {
    return left == right;
}

// A full Option of a Ref is the address in one register, with no test of
// the null pointer.
[[gnu::used]] fixy::Option<fixy::Ref<std::uint32_t>> option_ref_some(std::uint32_t& slot) noexcept {
    return fixy::some(fixy::erase_brand(fixy::to(slot)));
}

// ok_or gives the payload of an Option as a Result, in the same register.
[[gnu::used]] fixy::Result<std::uint32_t, Code32> option_ok_or(fixy::Option<std::uint32_t> option) noexcept {
    return std::move(option).ok_or(Code32::gone);
}

// CRUCIBLE_TRY is one test of the tag.  The value takes a second test of
// the tag, which the optimizer removes.
[[gnu::used]] fixy::Option<std::uint32_t> option_try(fixy::Option<std::uint32_t> option) noexcept {
    CRUCIBLE_TRY(value, std::move(option));
    return fixy::some(value + 1u);
}

[[gnu::used]] fixy::Result<std::uint32_t, Code32> result_try(fixy::Result<std::uint32_t, Code32> result) noexcept {
    CRUCIBLE_TRY(value, std::move(result));
    return value + 1u;
}

// The error of the callee converts to the error of the caller.
[[gnu::used]] fixy::Result<std::uint32_t, NetError8>
result_try_convert(fixy::Result<std::uint32_t, Code8> result) noexcept {
    CRUCIBLE_TRY(value, std::move(result));
    return value + 1u;
}

// A Result of a value of four bytes and an error of four bytes, as a
// function gives it back: in one register.  The value is the low word and
// the tag is bit 32, as in std::expected.  expect adds one test and one
// branch to a cold call.
[[gnu::used]] std::uint32_t result_expect(fixy::Result<std::uint32_t, Code32> result) noexcept {
    return std::move(result).expect("the probe gets a value");
}

// The match of a Result is one test of the tag.
[[gnu::used]] std::uint32_t result_match(fixy::Result<std::uint32_t, Code32> result) noexcept {
    return std::move(result).match([](std::uint32_t value) noexcept { return value * 3u; },
                                   [](Code32 code) noexcept { return static_cast<std::uint32_t>(code); });
}

// ── The combinators of Option and Result ─────────────────────────────────
//
// Each pair compares a combinator with the hand match that it replaces, so
// the function NAME_raw of these pairs is a match of the family and not raw
// code.  It needs no opt-out region.  The match pairs with its raw form in
// option_match and result_match.

namespace {

[[nodiscard]] constexpr fixy::Option<std::uint32_t> half_when_even(std::uint32_t value) noexcept {
    if (value % 2u != 0u) return fixy::none;
    return fixy::Option<std::uint32_t>::some(value / 2u);
}

[[nodiscard]] constexpr fixy::Result<std::uint32_t, NetError8> checked_half(std::uint32_t value) noexcept {
    if (value % 2u != 0u) return fixy::err(Code8::lost);
    return value / 2u;
}

}  // namespace

// map of an Option of a plain payload.
[[gnu::used]] fixy::Option<std::uint64_t> option_map(fixy::Option<std::uint32_t> option) noexcept {
    return std::move(option).map([](std::uint32_t slot) noexcept { return std::uint64_t{slot} * 24u; });
}

[[gnu::used]] fixy::Option<std::uint64_t> option_map_raw(fixy::Option<std::uint32_t> option) noexcept {
    return std::move(option).match([](std::uint32_t slot) noexcept { return fixy::some(std::uint64_t{slot} * 24u); },
                                   []() noexcept { return fixy::Option<std::uint64_t>{fixy::none}; });
}

// map of an Option of a niche payload, an Index.
[[gnu::used]] fixy::Option<std::uint32_t> option_map_niche(fixy::Option<fixy::Index<1024>> option) noexcept {
    return std::move(option).map(
        [](fixy::Index<1024> index) noexcept { return static_cast<std::uint32_t>(index.raw() * 3u); });
}

[[gnu::used]] fixy::Option<std::uint32_t> option_map_niche_raw(fixy::Option<fixy::Index<1024>> option) noexcept {
    return std::move(option).match(
        [](fixy::Index<1024> index) noexcept { return fixy::some(static_cast<std::uint32_t>(index.raw() * 3u)); },
        []() noexcept { return fixy::Option<std::uint32_t>{fixy::none}; });
}

// map and then value_or, the form of a map with a default.
[[gnu::used]] std::uint64_t option_map_value_or(fixy::Option<std::uint32_t> option) noexcept {
    return std::move(option).map([](std::uint32_t slot) noexcept { return std::uint64_t{slot} * 24u; }).value_or(7u);
}

[[gnu::used]] std::uint64_t option_map_value_or_raw(fixy::Option<std::uint32_t> option) noexcept {
    return std::move(option).match([](std::uint32_t slot) noexcept { return std::uint64_t{slot} * 24u; },
                                   []() noexcept { return std::uint64_t{7}; });
}

// and_then of an Option: a second step that can miss.
[[gnu::used]] fixy::Option<std::uint32_t> option_and_then(fixy::Option<std::uint32_t> option) noexcept {
    return std::move(option).and_then([](std::uint32_t value) noexcept { return half_when_even(value); });
}

[[gnu::used]] fixy::Option<std::uint32_t> option_and_then_raw(fixy::Option<std::uint32_t> option) noexcept {
    return std::move(option).match([](std::uint32_t value) noexcept { return half_when_even(value); },
                                   []() noexcept { return fixy::Option<std::uint32_t>{fixy::none}; });
}

// filter of an Option.
[[gnu::used]] fixy::Option<std::uint32_t> option_filter(fixy::Option<std::uint32_t> option) noexcept {
    return std::move(option).filter([](std::uint32_t const& slot) noexcept { return slot % 2u == 0u; });
}

[[gnu::used]] fixy::Option<std::uint32_t> option_filter_raw(fixy::Option<std::uint32_t> option) noexcept {
    return std::move(option).match(
        [](std::uint32_t slot) noexcept {
            if (slot % 2u != 0u) return fixy::Option<std::uint32_t>{fixy::none};
            return fixy::some(slot);
        },
        []() noexcept { return fixy::Option<std::uint32_t>{fixy::none}; });
}

// is_some_and and is_none_or of an Option that the caller keeps in memory.
[[gnu::used]] bool option_is_some_and(fixy::Option<std::uint32_t> const& option, std::uint32_t limit) noexcept {
    return option.is_some_and([limit](std::uint32_t const& slot) noexcept { return slot < limit; });
}

[[gnu::used]] bool option_is_some_and_raw(fixy::Option<std::uint32_t> const& option, std::uint32_t limit) noexcept {
    return option.match([limit](std::uint32_t const& slot) noexcept { return slot < limit; },
                        []() noexcept { return false; });
}

[[gnu::used]] bool option_is_none_or(fixy::Option<std::uint32_t> const& option, std::uint32_t limit) noexcept {
    return option.is_none_or([limit](std::uint32_t const& slot) noexcept { return slot < limit; });
}

[[gnu::used]] bool option_is_none_or_raw(fixy::Option<std::uint32_t> const& option, std::uint32_t limit) noexcept {
    return option.match([limit](std::uint32_t const& slot) noexcept { return slot < limit; },
                        []() noexcept { return true; });
}

// A read through as_ref against the same read through the borrow match.
// The Ref of the payload has an address that is not null, so the Option of
// the Ref takes no test of the null pointer.
[[gnu::used]] bool option_as_ref(fixy::Option<std::uint32_t> const& option, std::uint32_t limit) noexcept {
    return fixy::as_ref(option).is_some_and([limit](auto const& slot) noexcept { return *slot < limit; });
}

[[gnu::used]] bool option_as_ref_raw(fixy::Option<std::uint32_t> const& option, std::uint32_t limit) noexcept {
    return option.match([limit](std::uint32_t const& slot) noexcept { return slot < limit; },
                        []() noexcept { return false; });
}

// map of a Result: the error passes.
[[gnu::used]] fixy::Result<std::uint32_t, Code32> result_map(fixy::Result<std::uint64_t, Code32> result) noexcept {
    return std::move(result).map([](std::uint64_t value) noexcept { return static_cast<std::uint32_t>(value >> 3u); });
}

[[gnu::used]] fixy::Result<std::uint32_t, Code32> result_map_raw(fixy::Result<std::uint64_t, Code32> result) noexcept {
    using Mapped = fixy::Result<std::uint32_t, Code32>;
    return std::move(result).match(
        [](std::uint64_t value) noexcept -> Mapped { return static_cast<std::uint32_t>(value >> 3u); },
        [](Code32 code) noexcept -> Mapped { return fixy::err(code); });
}

// map_err of a Result: the value passes.
[[gnu::used]] fixy::Result<std::uint64_t, NetError8>
result_map_err(fixy::Result<std::uint64_t, Code32> result) noexcept {
    return std::move(result).map_err(
        [](Code32 code) noexcept { return NetError8{code == Code32::gone ? Code8::lost : Code8{2}}; });
}

[[gnu::used]] fixy::Result<std::uint64_t, NetError8>
result_map_err_raw(fixy::Result<std::uint64_t, Code32> result) noexcept {
    using Converted = fixy::Result<std::uint64_t, NetError8>;
    return std::move(result).match([](std::uint64_t value) noexcept -> Converted { return value; },
                                   [](Code32 code) noexcept -> Converted {
                                       return fixy::err(NetError8{code == Code32::gone ? Code8::lost : Code8{2}});
                                   });
}

// CRUCIBLE_TRY over map_err, against the hand test of the error and the
// return of a converted error.
[[gnu::used]] fixy::Result<std::uint64_t, Code8>
result_try_map_err(fixy::Result<std::uint32_t, Code32> result) noexcept {
    CRUCIBLE_TRY(value, std::move(result).map_err([](Code32) noexcept { return Code8::lost; }));
    return std::uint64_t{value} + 1u;
}

[[gnu::used]] fixy::Result<std::uint64_t, Code8>
result_try_map_err_raw(fixy::Result<std::uint32_t, Code32> result) noexcept {
    if (result.is_err()) return fixy::err(Code8::lost);
    return std::uint64_t{std::move(result).expect("the test above found a value")} + 1u;
}

// and_then of a Result, whose error converts as CRUCIBLE_TRY converts it.
[[gnu::used]] fixy::Result<std::uint32_t, NetError8>
result_and_then(fixy::Result<std::uint32_t, Code8> result) noexcept {
    return std::move(result).and_then([](std::uint32_t value) noexcept { return checked_half(value); });
}

[[gnu::used]] fixy::Result<std::uint32_t, NetError8>
result_and_then_raw(fixy::Result<std::uint32_t, Code8> result) noexcept {
    using Chained = fixy::Result<std::uint32_t, NetError8>;
    return std::move(result).match([](std::uint32_t value) noexcept -> Chained { return checked_half(value); },
                                   [](Code8 code) noexcept -> Chained { return fixy::err(code); });
}

// or_else of a Result: a recovery from the error.
[[gnu::used]] fixy::Result<std::uint32_t, NetError8>
result_or_else(fixy::Result<std::uint32_t, Code32> result) noexcept {
    return std::move(result).or_else(
        [](Code32 code) noexcept { return checked_half(static_cast<std::uint32_t>(code)); });
}

[[gnu::used]] fixy::Result<std::uint32_t, NetError8>
result_or_else_raw(fixy::Result<std::uint32_t, Code32> result) noexcept {
    using Recovered = fixy::Result<std::uint32_t, NetError8>;
    return std::move(result).match(
        [](std::uint32_t value) noexcept -> Recovered { return value; },
        [](Code32 code) noexcept -> Recovered { return checked_half(static_cast<std::uint32_t>(code)); });
}

// is_ok_and and is_err_and of a Result that the caller keeps in memory.
[[gnu::used]] bool result_is_ok_and(fixy::Result<std::uint32_t, Code32> const& result, std::uint32_t limit) noexcept {
    return result.is_ok_and([limit](std::uint32_t const& value) noexcept { return value < limit; });
}

[[gnu::used]] bool result_is_ok_and_raw(fixy::Result<std::uint32_t, Code32> const& result,
                                        std::uint32_t limit) noexcept {
    return result.match([limit](std::uint32_t const& value) noexcept { return value < limit; },
                        [](Code32 const&) noexcept { return false; });
}

[[gnu::used]] bool result_is_err_and(fixy::Result<std::uint32_t, Code32> const& result) noexcept {
    return result.is_err_and([](Code32 const& code) noexcept { return code == Code32::late; });
}

[[gnu::used]] bool result_is_err_and_raw(fixy::Result<std::uint32_t, Code32> const& result) noexcept {
    return result.match([](std::uint32_t const&) noexcept { return false; },
                        [](Code32 const& code) noexcept { return code == Code32::late; });
}

// The match of a OneOf of three alternatives compares the tag with each of
// the three indices, and a tag after the last index ends the process.
[[gnu::used]] std::uint32_t one_of_match(fixy::OneOf<std::uint32_t, Code32, std::uint16_t> reading) noexcept {
    return std::move(reading).match([](std::uint32_t word) noexcept { return word * 3u; },
                                    [](Code32 code) noexcept { return static_cast<std::uint32_t>(code); },
                                    [](std::uint16_t half) noexcept { return std::uint32_t{half} + 5u; });
}

// which() of a OneOf is the load of its tag.
[[gnu::used]] std::size_t one_of_which(fixy::OneOf<std::uint32_t, Code32, std::uint16_t> const& reading) noexcept {
    return reading.which().raw();
}

// get<T>() of a OneOf compares the tag with the index of T one time.  The
// Option of the Ref is the address of the storage or null, and the read
// through it is the read of the storage when the tag agrees.
[[gnu::used]] std::uint32_t one_of_get(fixy::OneOf<std::uint32_t, Code32, std::uint16_t> const& reading) noexcept {
    return reading.get<std::uint32_t>().match([](auto word) noexcept { return *word; },
                                              [] noexcept { return std::uint32_t{0}; });
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

// ── Record ──────────────────────────────────────────────────────────────────

// Two empty parts of different types.
struct TokenA final {};
struct TokenB final {};

// A Record of two words, as a function gives it back: in two registers.
[[gnu::used]] fixy::Record<std::uint64_t, std::uint64_t> record_return(std::uint64_t first,
                                                                       std::uint64_t second) noexcept {
    return {first, second};
}

// A binding reads the parts in place.
[[gnu::used]] std::uint64_t record_binding(fixy::Record<std::uint64_t, std::uint64_t> record) noexcept {
    auto const& [low, high] = record;
    return low + high;
}

// == compares each part in order.
[[gnu::used]] bool record_equal(fixy::Record<std::uint32_t, std::uint32_t> const& left,
                                fixy::Record<std::uint32_t, std::uint32_t> const& right) noexcept {
    return left == right;
}

// <=> compares the parts in order, and it stops at the first part that is not
// equivalent.
[[gnu::used]] bool record_order(fixy::Record<std::uint32_t, std::uint32_t> left,
                                fixy::Record<std::uint32_t, std::uint32_t> right) noexcept {
    return std::is_lt(left <=> right);
}

// Two empty parts take one byte, and a pass moves no byte.
[[gnu::used]] fixy::Record<TokenA, TokenB> record_pass_tokens(fixy::Record<TokenA, TokenB> tokens) noexcept {
    return tokens;
}

// ── Protocol ────────────────────────────────────────────────────────────────

// A record that derives its operations from its parts: 16 bytes, in the
// order of their sizes, with no padding byte.
struct[[= fixy::derive_protocol{}]] ProtocolRecord final {
    std::uint64_t key = 0;
    std::uint32_t count = 0;
    std::uint16_t lane = 0;
    Kind kind = Kind::first;
    bool flag = false;
};

// equal compares each part in declaration order.
[[gnu::used]] bool protocol_equal(ProtocolRecord const& left, ProtocolRecord const& right) noexcept {
    return fixy::equal(left, right);
}

// order compares the parts in declaration order, and it stops at the first
// part that differs.
[[gnu::used]] bool protocol_order(ProtocolRecord const& left, ProtocolRecord const& right) noexcept {
    return std::is_lt(fixy::order(left, right));
}

// hash puts the 16 bytes of the image into one block and folds it with one
// multiply.
[[gnu::used]] std::uint64_t protocol_hash(ProtocolRecord const& value) noexcept { return fixy::hash(value).raw(); }

// encode writes each part as its bytes.
[[gnu::used]] void protocol_encode(ProtocolRecord const& value, fixy::View<std::byte, 16> out) noexcept {
    fixy::encode(value, out);
}

// decode reads each part, and it refuses an enumeration value that names no
// enumerator and a bool byte other than 0 and 1.
[[gnu::used]] fixy::Result<ProtocolRecord, fixy::DecodeFault>
protocol_decode(fixy::View<std::byte const, 16> image) noexcept {
    return fixy::decode<ProtocolRecord>(image);
}

// format of the Protocol family has no probe.  Each append of a text stores
// a byte at an address with an index register, which the rule refuses with
// no declaration, and format is no part of a hot path.  test_core_protocol
// checks the text that it writes.

// equal of a double compares its bits.
[[gnu::used]] bool protocol_equal_double(double left, double right) noexcept { return fixy::equal(left, right); }

// <=> of a FixedArray is lexicographic over its elements.
[[gnu::used]] bool fixed_array_order(fixy::FixedArray<std::uint32_t, 4> const& left,
                                     fixy::FixedArray<std::uint32_t, 4> const& right) noexcept {
    return std::is_lt(left <=> right);
}

// ── Scalar ──────────────────────────────────────────────────────────────────

[[gnu::used]] fixy::Option<std::uint32_t> scalar_checked_add(std::uint32_t left, std::uint32_t right) noexcept {
    return fixy::checked_add(left, right);
}

// An Option of a word has 16 bytes, and it comes back in two registers.
[[gnu::used]] fixy::Option<std::uint64_t> scalar_checked_add_wide(std::uint64_t left, std::uint64_t right) noexcept {
    return fixy::checked_add(left, right);
}

[[gnu::used]] fixy::Option<std::int32_t> scalar_checked_div(std::int32_t dividend, std::int32_t divisor) noexcept {
    return fixy::checked_div(dividend, divisor);
}

[[gnu::used]] std::uint64_t scalar_wrapping_shl(std::uint64_t value, int count) noexcept {
    return fixy::wrapping_shl(value, count);
}

[[gnu::used]] std::int64_t scalar_saturating_add(std::int64_t left, std::int64_t right) noexcept {
    return fixy::saturating_add(left, right);
}

[[gnu::used]] ::foundation::core::WideProduct<std::uint64_t> scalar_widening_mul(std::uint64_t left,
                                                                                 std::uint64_t right) noexcept {
    return ::foundation::core::widening_mul(left, right);
}

[[gnu::used]] double scalar_min_float(double left, double right) noexcept { return fixy::min(left, right); }

[[gnu::used]] std::int32_t scalar_clamp_bounds(std::int32_t value, fixy::Bounds<std::int32_t> range) noexcept {
    return fixy::clamp(value, range);
}

[[gnu::used]] bool scalar_compare_mixed(std::int64_t left, std::uint64_t right) noexcept {
    return std::is_lt(fixy::compare(left, right));
}

[[gnu::used]] bool scalar_fits(std::int32_t value) noexcept { return fixy::fits<std::uint8_t>(value); }

[[gnu::used]] std::uint32_t scalar_unsigned_abs(std::int32_t value) noexcept { return fixy::unsigned_abs(value); }

[[gnu::used]] fixy::Option<std::uint32_t> scalar_narrow_checked(std::uint64_t value) noexcept {
    return fixy::narrow<std::uint32_t>(value);
}

[[gnu::used]] fixy::Option<std::int32_t> scalar_narrow_float(double value) noexcept {
    return fixy::narrow<std::int32_t>(value);
}

[[gnu::used]] std::int16_t scalar_narrow_saturating(std::int64_t value) noexcept {
    return fixy::saturating_narrow<std::int16_t>(value);
}

[[gnu::used]] std::uint64_t scalar_widen(std::uint32_t value) noexcept { return fixy::widen<std::uint64_t>(value); }

[[gnu::used]] int scalar_popcount(std::uint64_t value) noexcept { return fixy::popcount(value); }

[[gnu::used]] int scalar_countl_zero(std::uint64_t value) noexcept { return fixy::countl_zero(value); }

[[gnu::used]] std::uint64_t scalar_rotl(std::uint64_t value, int count) noexcept { return fixy::rotl(value, count); }

[[gnu::used]] std::uint32_t scalar_byteswap(std::uint32_t value) noexcept { return fixy::byteswap(value); }

[[gnu::used]] fixy::Option<std::uint32_t> scalar_bit_ceil(std::uint32_t value) noexcept {
    return fixy::bit_ceil(value);
}

[[gnu::used]] std::uint64_t scalar_low_mask(int count) noexcept { return fixy::low_mask<std::uint64_t>(count); }

[[gnu::used]] std::uint64_t scalar_bit_cast(double value) noexcept { return fixy::bit_cast<std::uint64_t>(value); }

[[gnu::used]] fixy::Option<bool> scalar_decode_bool(std::uint8_t byte) noexcept { return fixy::decode<bool>(byte); }

using ProbeSlotId = fixy::Id<struct ProbeSlotTag, std::uint32_t>;

[[gnu::used]] fixy::Option<ProbeSlotId> scalar_id_decode(std::uint32_t word) noexcept {
    return fixy::decode<ProbeSlotId>(word);
}

[[gnu::used]] bool scalar_fp_is_nan(double value) noexcept { return fixy::fp::is_nan(value); }

[[gnu::used]] double scalar_fp_abs(double value) noexcept { return fixy::fp::abs(value); }

[[gnu::used]] double scalar_fp_sqrt(double value) noexcept { return fixy::fp::sqrt(value); }

[[gnu::used]] double scalar_fp_fma(double left, double right, double addend) noexcept {
    return fixy::fp::fma(left, right, addend);
}

[[gnu::used]] std::uint64_t scalar_fp_total_key(double value) noexcept { return fixy::fp::total_key(value); }

[[gnu::used]] std::uint64_t scalar_exchange(std::uint64_t& slot, std::uint64_t value) noexcept {
    return fixy::exchange(slot, value);
}

[[gnu::used]] void scalar_swap(std::uint64_t& left, std::uint64_t& right) noexcept { fixy::swap(left, right); }

using ProbeBytes = fixy::Quantity<fixy::unit::Byte, std::uint32_t>;

[[gnu::used]] fixy::Option<ProbeBytes> scalar_quantity_add(ProbeBytes left, ProbeBytes right) noexcept {
    return fixy::checked_add(left, right);
}

[[gnu::used]] fixy::Option<fixy::Quantity<fixy::unit::Nanosecond, std::uint32_t>>
scalar_quantity_to_finer(fixy::Quantity<fixy::unit::Microsecond, std::uint32_t> value) noexcept {
    return fixy::convert<fixy::unit::Nanosecond>(value);
}

[[gnu::used]] fixy::Option<fixy::Quantity<fixy::unit::Second, std::uint32_t>>
scalar_quantity_to_coarser(fixy::Quantity<fixy::unit::Millisecond, std::uint32_t> value) noexcept {
    return fixy::convert<fixy::unit::Second>(value);
}

[[gnu::used]] fixy::Option<ProbeBytes>
scalar_quantity_product(fixy::Quantity<fixy::unit::BytePerSecond, std::uint32_t> rate,
                        fixy::Quantity<fixy::unit::Second, std::uint32_t> seconds) noexcept {
    return checked_mul(rate, seconds);
}

[[gnu::used]] fixy::Quantity<fixy::unit::FlopPerByte, double>
scalar_quantity_float_quotient(fixy::Quantity<fixy::unit::FlopPerSecond, double> peak,
                               fixy::Quantity<fixy::unit::BytePerSecond, double> bandwidth) noexcept {
    return divide(peak, bandwidth);
}

[[gnu::used]] std::uint32_t scalar_philox_generate(std::uint64_t offset, std::uint64_t key) noexcept {
    fixy::Philox4x32::Counter const block = fixy::Philox4x32::generate(offset, key);
    return block.at(fixy::index<0>()) ^ block.at(fixy::index<1>()) ^ block.at(fixy::index<2>())
         ^ block.at(fixy::index<3>());
}

// ── Linear and the permission tokens ────────────────────────────────────────

struct ProbeRegion final {
    using permission_row = ::foundation::effects::Row<>;
};
struct ProbeBrand final {};
using ProbeToken = ::foundation::permissions::Permission<ProbeRegion, ProbeBrand>;

// The move constructor and the destructor of a Linear are trivial, so the
// ABI passes a Linear in a register, as it passes its value.
[[gnu::used]] int linear_consume(fixy::Linear<int> value) noexcept { return std::move(value).consume(); }

// A Linear of a word has the size of the word, so the ABI passes it in one
// register, and consume is the move of the register.
[[gnu::used]] std::uint64_t linear_consume_word(fixy::Linear<std::uint64_t> value) noexcept {
    return std::move(value).consume();
}

// The move assignment of a Linear is user-provided, so the class is not
// trivially copyable.  It stores the value, as the assignment of the value
// does.
[[gnu::used]] void linear_assign(fixy::Linear<int>& target, fixy::Linear<int>&& source) noexcept {
    target = std::move(source);
}

// A token is empty, and its move constructor is trivial, so a pass and a
// return move no byte.
[[gnu::used]] ProbeToken token_pass(ProbeToken token) noexcept { return token; }

// The move assignment of a token is user-provided, and it moves no byte.
[[gnu::used]] void token_assign(ProbeToken& target, ProbeToken&& source) noexcept { target = std::move(source); }

// ── The contract checks ─────────────────────────────────────────────────────

// A precondition evaluates its condition one time.  Its runtime arm is the
// contract_assert of the raw form, under the same contract semantic.
[[gnu::used]] std::uint64_t contract_pre(std::uint64_t index, std::uint64_t count) noexcept {
    CRUCIBLE_PRE(index < count);
    return index * 3;
}

// A postcondition is a precondition before the return, and it also
// evaluates its condition one time.
[[gnu::used]] std::uint64_t contract_post(std::uint64_t index, std::uint64_t count) noexcept {
    std::uint64_t const tripled = index * 3;
    CRUCIBLE_POST(tripled, tripled != count);
    return tripled;
}

// The message form is a precondition whose condition carries the message.
// The compiler folds the message out of the condition, so the message is
// only a constant of the report.
[[gnu::used]] std::uint64_t contract_pre_msg(std::uint64_t index, std::uint64_t count) noexcept {
    CRUCIBLE_PRE_MSG(index < count, "the index is less than the count");
    return index * 3;
}

// ── The raw forms ───────────────────────────────────────────────────────────

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: the raw form that the shape of each family probe is compared with")

struct RawWindow final {
    std::uint32_t const* first;
    std::size_t count;
};

struct RawText final {
    char const* first;
    std::size_t count;
};

struct RawList final {
    std::uint32_t slots[64];
    std::uint32_t count;
};

struct RawHeap final {
    std::uint32_t* first;
    std::size_t count;
};

struct RawMasked final {
    std::uint32_t* first;
    std::size_t count;
    std::size_t mask;
};

// The two words of a BrandedView: the base pointer and the count of 4 bytes.
struct RawColumn final {
    std::uint32_t const* first;
    std::uint32_t count;
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

[[gnu::used]] bool atomic_cas_weak_raw(std::atomic<std::uint64_t>& cell, std::uint64_t expected,
                                       std::uint64_t desired) noexcept {
    return cell.compare_exchange_weak(expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
}

[[gnu::used]] std::uint64_t atomic_fetch_xor_raw(std::atomic<std::uint64_t>& cell, std::uint64_t bits) noexcept {
    return cell.fetch_xor(bits, std::memory_order_acq_rel);
}

// A count replaces a relaxed counter of std::atomic.
[[gnu::used]] std::uint64_t count_fetch_add_raw(std::atomic<std::uint64_t>& counter, std::uint64_t delta) noexcept {
    return counter.fetch_add(delta, std::memory_order_relaxed);
}

[[gnu::used]] std::uint64_t count_load_raw(std::atomic<std::uint64_t> const& counter) noexcept {
    return counter.load(std::memory_order_relaxed);
}

// A statistics counter that a Tally replaces is a relaxed counter.
[[gnu::used]] void tally_add_raw(std::atomic<std::uint64_t>& counter, std::uint64_t amount) noexcept {
    counter.fetch_add(amount, std::memory_order_relaxed);
}

[[gnu::used]] std::uint64_t tally_read_raw(std::atomic<std::uint64_t> const& counter) noexcept {
    return counter.load(std::memory_order_relaxed);
}

// The count that a WideMonotonic replaces is a plain 64-bit integer that the
// code adds one to.
[[gnu::used]] void wide_monotonic_bump_raw(std::uint64_t& counter) noexcept { ++counter; }

// The loop that a cyclic cursor replaces: three pointers, a read, a step and
// the wrap to the first element at the end.
struct RawCycle {
    std::uint64_t const* first;
    std::uint64_t const* at;
    std::uint64_t const* end;
};

[[gnu::used]] std::uint64_t cyclic_cursor_read_step_raw(RawCycle& cursor) noexcept {
    std::uint64_t const value = *cursor.at;
    ++cursor.at;
    if (cursor.at == cursor.end) cursor.at = cursor.first;
    return value;
}

// A CacheLine replaces a cell that the code aligns by hand, so the raw form
// reads the cell with no CacheLine around it.
[[gnu::used]] std::uint64_t cache_line_load_raw(fixy::Atomic<std::uint64_t> const& cell) noexcept {
    return cell.load_acquire();
}

// The read of its own variable by the one thread that writes it is a
// relaxed load.
[[gnu::used]] std::uint64_t sole_load_owned_raw(std::atomic<std::uint64_t> const& cell) noexcept {
    return cell.load(std::memory_order_relaxed);
}

[[gnu::used]] void sole_store_raw(std::atomic<std::uint64_t>& cell, std::uint64_t value) noexcept {
    cell.store(value, std::memory_order_release);
}

[[gnu::used]] std::uint64_t sole_load_raw(std::atomic<std::uint64_t> const& cell) noexcept {
    return cell.load(std::memory_order_acquire);
}

// The raw form of a cell of an Option of a seat is a cell of the number, with
// zero for no seat.
[[gnu::used]] std::uint32_t word_option_load_raw(std::atomic<std::uint32_t> const& cell) noexcept {
    return cell.load(std::memory_order_acquire);
}

[[gnu::used]] void word_option_store_raw(std::atomic<std::uint32_t>& cell, std::uint32_t value) noexcept {
    cell.store(value, std::memory_order_release);
}

// The raw form of a cell of an Option of a Ref is a cell of a pointer, with
// the null pointer for no object.
[[gnu::used]] Payload* ref_option_load_raw(std::atomic<Payload*> const& cell) noexcept {
    return cell.load(std::memory_order_acquire);
}

[[gnu::used]] void ref_option_store_raw(std::atomic<Payload*>& cell, Payload* value) noexcept {
    cell.store(value, std::memory_order_release);
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

[[gnu::used]] std::uint32_t fixed_array_at_raw(std::uint32_t const* first, std::uint32_t position) noexcept {
    return first[position];
}

[[gnu::used]] void fixed_array_with_element_raw(std::uint64_t* first, std::uint32_t position,
                                                std::uint64_t bit) noexcept {
    first[position] |= bit;
}

[[gnu::used]] void heap_array_with_element_raw(RawHeap& array, std::size_t position, std::uint32_t bit) noexcept {
    array.first[position] |= bit;
}

[[gnu::used]] void masked_array_with_masked_raw(RawMasked& array, std::uint64_t key, std::uint32_t bit) noexcept {
    array.first[key & array.mask] |= bit;
}

struct RawShapeBatch final {
    std::uint64_t words[64];
    std::uint32_t halves[64];
};

[[gnu::used]] void fixed_array_frame_pop_raw(RawShapeBatch& batch, std::uint64_t const* words,
                                             std::uint32_t const* halves) noexcept {
    std::memmove(batch.words, words, sizeof batch.words);
    std::memmove(batch.halves, halves, sizeof batch.halves);
}

// The pop of a batch of keyed arrays before the keys: the same two copies.
[[gnu::used]] void keyed_array_frame_pop_raw(RawShapeBatch& batch, std::uint64_t const* words,
                                             std::uint32_t const* halves) noexcept {
    std::memmove(batch.words, words, sizeof batch.words);
    std::memmove(batch.halves, halves, sizeof batch.halves);
}

// A copy between two arrays that do not overlap: one memcpy.
[[gnu::used]] void keyed_frame_copy_raw(int* target, int const* source) noexcept {
    std::memcpy(target, source, 64 * sizeof(int));
}

// The loop of write_each over two runs that do not overlap, with restrict
// pointers.
[[gnu::used]] void keyed_frame_write_each_raw(int* __restrict target, int const* __restrict source) noexcept {
    for (std::size_t position = 0; position != 64; ++position) {
        target[position] = source[position] * 2;
    }
}

// The pointer and the count of a heap array of words.
struct RawHeapWords final {
    std::uint64_t* first;
    std::size_t count;
};

// The copy into a keyed heap array before the key: the load of the pointer
// and one copy.
[[gnu::used]] void keyed_heap_array_frame_copy_raw(RawHeapWords& target, std::uint64_t const* words) noexcept {
    std::memmove(target.first, words, 64 * sizeof(std::uint64_t));
}

// The stack and the frame of the records of an op before the window stack:
// the window test, the step of the top and its check at the end.
struct RawWindowStack final {
    std::uint64_t slots[64];
    std::size_t top;
};

[[gnu::used]] std::uint64_t window_loan_open_raw(RawWindowStack& stack, std::uint32_t count) noexcept {
    std::size_t const base = stack.top;
    [[assume(base <= 64)]];
    if (count > 64 - base) return 0;
    stack.top = base + count;
    if (stack.top != base + count) __builtin_trap();
    stack.top = base;
    return count;
}

// An Option of an Index has the niche of the count: 16 is the empty value.
[[gnu::used]] std::uint32_t index_check_raw(std::size_t position) noexcept {
    return position < 16 ? static_cast<std::uint32_t>(position) : 16u;
}

[[gnu::used]] RawText text_window_raw(char const* first, std::size_t size, std::size_t offset,
                                      std::size_t count) noexcept {
    if (offset > size || count > size - offset) [[unlikely]] {
        return RawText{nullptr, ~std::size_t{0}};
    }
    return RawText{first + offset, count};
}

// The raw form compares the two pointers, as the code that the query replaces
// did.  A bitwise or of the two compares has no branch, as the family side has
// none.  The hardening of a compare then tests the same compares on the two
// sides.
[[gnu::used]] bool text_is_window_of_raw(RawText part, RawText whole) noexcept {
    return !((part.first < whole.first) | (part.first + part.count > whole.first + whole.count));
}

[[gnu::used]] std::uint32_t text_window_digits_raw(char const* first, std::size_t size, std::size_t offset,
                                                   std::size_t count) noexcept {
    std::uint32_t value = 0;
    if (offset > size || count > size - offset) [[unlikely]] {
        return value;
    }
    for (char const* cursor = first + offset; cursor != first + offset + count; ++cursor) {
        value = value * 10u + static_cast<std::uint32_t>(*cursor - '0');
    }
    return value;
}

[[gnu::used]] std::uint32_t view_window_sum_raw(std::uint32_t const* first, std::size_t size, std::size_t offset,
                                                std::size_t count) noexcept {
    std::uint32_t sum = 0;
    if (offset > size || count > size - offset) [[unlikely]] {
        return sum;
    }
    for (std::uint32_t const* cursor = first + offset; cursor != first + offset + count; ++cursor) {
        sum += *cursor;
    }
    return sum;
}

[[gnu::used]] std::uint32_t view_window_fixed_raw(std::uint32_t const* first, std::size_t size,
                                                  std::size_t offset) noexcept {
    if (offset > size || 4 > size - offset) [[unlikely]] {
        return 0;
    }
    return first[offset] ^ first[offset + 3];
}

[[gnu::used]] std::uint32_t view_reverse_sum_raw(std::uint32_t const* first, std::size_t count) noexcept {
    std::uint32_t sum = 0;
    for (std::uint32_t const* after = first + count; after != first; --after) {
        sum += after[-1];
    }
    return sum;
}

[[gnu::used]] std::uint32_t view_windows_sum_raw(std::uint32_t const* first, std::size_t count) noexcept {
    std::uint32_t sum = 0;
    std::uint32_t const* const end = count < 2 ? first : first + (count - 1);
    for (std::uint32_t const* cursor = first; cursor != end; ++cursor) {
        sum += cursor[0] ^ cursor[1];
    }
    return sum;
}

[[gnu::used]] std::uint32_t view_chunks_sum_raw(std::uint32_t const* first, std::size_t count) noexcept {
    std::uint32_t sum = 0;
    std::uint32_t const* const end = first + count / 4 * 4;
    for (std::uint32_t const* cursor = first; cursor != end; cursor += 4) {
        sum += cursor[0] ^ cursor[3];
    }
    return sum;
}

[[gnu::used]] std::uint32_t view_enumerate_sum_raw(std::uint32_t const* first) noexcept {
    std::uint32_t sum = 0;
    std::uint32_t position = 0;
    for (std::uint32_t const* cursor = first; cursor != first + 64; ++cursor) {
        sum += *cursor * position;
        ++position;
    }
    return sum;
}

[[gnu::used]] std::uint32_t view_zip_fixed_sum_raw(std::uint32_t const* left, std::uint32_t const* right) noexcept {
    std::uint32_t sum = 0;
    std::uint32_t const* other = right;
    for (std::uint32_t const* cursor = left; cursor != left + 64; ++cursor) {
        sum += *cursor * *other;
        ++other;
    }
    return sum;
}

[[gnu::used]] std::uint32_t view_zip_sum_raw(std::uint32_t const* left, std::size_t left_count,
                                             std::uint32_t const* right, std::size_t right_count) noexcept {
    std::uint32_t sum = 0;
    if (left_count != right_count) {
        return sum;
    }
    for (std::size_t index = 0; index < left_count; ++index) {
        sum += left[index] * right[index];
    }
    return sum;
}

[[gnu::used]] float walk_map_fold_f32_raw(float const* first, std::size_t count) noexcept {
    float sum = 0.0f;
    for (std::size_t index = 0; index < count; ++index) {
        sum += first[index] * first[index];
    }
    return sum;
}

[[gnu::used]] std::uint32_t walk_map_fold_u32_raw(std::uint32_t const* first, std::size_t count) noexcept {
    std::uint32_t sum = 0;
    for (std::size_t index = 0; index < count; ++index) {
        sum += first[index] * first[index];
    }
    return sum;
}

[[gnu::used]] std::size_t walk_filter_count_f32_raw(float const* first, std::size_t count) noexcept {
    std::size_t kept = 0;
    for (std::size_t index = 0; index < count; ++index) {
        if (first[index] > 0.5f) ++kept;
    }
    return kept;
}

[[gnu::used]] std::size_t walk_filter_count_u32_raw(std::uint32_t const* first, std::size_t count) noexcept {
    std::size_t kept = 0;
    for (std::size_t index = 0; index < count; ++index) {
        if (first[index] % 3 == 0) ++kept;
    }
    return kept;
}

[[gnu::used]] float walk_zip_fold_f32_raw(float const* left, float const* right) noexcept {
    float sum = 0.0f;
    for (std::size_t index = 0; index < 64; ++index) {
        sum += left[index] * right[index];
    }
    return sum;
}

[[gnu::used]] float walk_try_zip_fold_f32_raw(float const* left, std::size_t left_count, float const* right,
                                              std::size_t right_count) noexcept {
    if (left_count != right_count) return -1.0f;
    float sum = 0.0f;
    for (std::size_t index = 0; index < left_count; ++index) {
        sum += left[index] * right[index];
    }
    return sum;
}

[[gnu::used]] std::uint64_t walk_limit_fold_u64_raw(std::uint64_t const* extents, std::uint8_t rank) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (std::size_t index = 0; index < rank && index < 8; ++index) {
        hash = (hash ^ extents[index]) * 0x100000001b3ULL;
    }
    return hash;
}

[[gnu::used]] std::uint32_t walk_long_chain_u32_raw(std::uint32_t const* left, std::uint32_t const* right) noexcept {
    std::uint32_t sum = 0;
    for (std::size_t index = 0; index < 64; ++index) {
        std::uint32_t const first = (left[index] + 3) * 5;
        std::uint32_t const second = right[index] ^ 0x55u;
        sum += first * second + 1;
    }
    return sum;
}

[[gnu::used]] std::uint32_t walk_filter_limit_fold_u32_raw(std::uint32_t const* first, std::size_t count,
                                                           std::uint32_t limit) noexcept {
    std::size_t left = limit;
    std::uint32_t sum = 0;
    if (left == 0) return sum;
    for (std::uint32_t const* cursor = first; cursor != first + count; ++cursor) {
        std::uint32_t const value = *cursor * 3;
        if (value % 7 != 0) continue;
        sum += value;
        if (--left == 0) break;
    }
    return sum;
}

[[gnu::used]] bool walk_map_any_u32_raw(std::uint32_t const* first) noexcept {
    for (std::uint32_t const* cursor = first; cursor != first + 64; ++cursor) {
        if ((*cursor >> 3) == 17) return true;
    }
    return false;
}

[[gnu::used]] bool walk_all_u32_raw(std::uint32_t const* first, std::size_t count) noexcept {
    for (std::uint32_t const* cursor = first; cursor != first + count; ++cursor) {
        if (*cursor >= 1000) return false;
    }
    return true;
}

[[gnu::used]] std::uint32_t walk_flat_map_fold_u32_raw(RawWindow const* runs, std::size_t count) noexcept {
    std::uint32_t sum = 0;
    for (std::size_t run = 0; run < count; ++run) {
        for (std::size_t index = 0; index < runs[run].count; ++index) {
            sum += runs[run].first[index];
        }
    }
    return sum;
}

[[gnu::used]] float walk_map_fold_cursor_f32_raw(float const* first, std::size_t count) noexcept {
    float sum = 0.0f;
    for (std::size_t index = 0; index < count; ++index) {
        sum += first[index] * first[index];
    }
    return sum;
}

// The raw form of a cursor that skips the refused elements in its step: a
// scalar loop nest.
[[gnu::used]] std::size_t walk_filter_count_cursor_f32_raw(float const* first, std::size_t count) noexcept {
    float const* const end = first + count;
    float const* cursor = first;
    while (cursor != end && !(*cursor > 0.5f)) {
        ++cursor;
    }
    std::size_t kept = 0;
    while (cursor != end) {
        ++kept;
        ++cursor;
        while (cursor != end && !(*cursor > 0.5f)) {
            ++cursor;
        }
    }
    return kept;
}

[[gnu::used]] void branded_columns_raw(std::atomic<std::uint32_t>& cell, RawColumn first, RawColumn second,
                                       RawColumn third) noexcept {
    for (std::size_t index = 0; index < first.count; ++index) {
        cell.store(first.first[index] + second.first[index] + third.first[index], std::memory_order_release);
    }
}

[[gnu::used]] bool fixed_list_append_raw(RawList& list, std::uint32_t value) noexcept {
    if (list.count == 64) {
        return false;
    }
    list.slots[list.count] = value;
    ++list.count;
    return true;
}

[[gnu::used]] std::uint32_t fixed_list_take_last_raw(RawList& list) noexcept {
    if (list.count == 0) {
        return 0;
    }
    --list.count;
    return list.slots[list.count];
}

[[gnu::used]] std::uint32_t fixed_array_sum_raw(std::array<std::uint32_t, 16> const& array) noexcept {
    std::uint32_t sum = 0;
    for (std::uint32_t const element : array) {
        sum += element;
    }
    return sum;
}

[[gnu::used]] std::uint32_t heap_array_sum_raw(RawHeap const& array) noexcept {
    std::uint32_t sum = 0;
    for (std::uint32_t const* cursor = array.first; cursor != array.first + array.count; ++cursor) {
        sum += *cursor;
    }
    return sum;
}

[[gnu::used]] bool bit_set_test_raw(std::uint64_t const* words, std::uint32_t position) noexcept {
    return ((words[position / 64] >> (position % 64)) & 1u) != 0;
}

[[gnu::used]] void bit_set_set_raw(std::uint64_t* words, std::uint32_t position) noexcept {
    words[position / 64] |= std::uint64_t{1} << (position % 64);
}

[[gnu::used]] bool bit_set_lane_test_raw(std::uint16_t const& word, std::uint8_t lane) noexcept {
    return ((std::uint32_t{word} >> lane) & 1u) != 0;
}

[[gnu::used]] std::uint8_t packed_word_get_raw(std::uint16_t word) noexcept {
    return static_cast<std::uint8_t>((std::uint32_t{word} >> 10) & 0x1Fu);
}

[[gnu::used]] std::uint16_t packed_word_with_raw(std::uint16_t word, std::uint8_t data) noexcept {
    if ((std::uint32_t{data} & ~0x1Fu) != 0) {
        return 0;
    }
    return static_cast<std::uint16_t>((std::uint32_t{word} & ~0x7C00u) | (std::uint32_t{data} << 10));
}

// A bitmap on the heap with its count of bits.
struct RawBits final {
    std::uint64_t* words = nullptr;
    std::uint32_t count = 0;
};

[[gnu::used]] bool heap_bit_set_test_raw(RawBits const& set, std::uint32_t position) noexcept {
    return ((set.words[position / 64] >> (position % 64)) & 1u) != 0;
}

// A generational table: the values, the free indexes, the generations, and
// the counts of the slots.
struct RawSlots final {
    std::uint32_t* values = nullptr;
    std::uint32_t* free_stack = nullptr;
    std::uint32_t* generations = nullptr;
    std::uint32_t capacity = 0;
    std::uint32_t used = 0;
    std::uint32_t free_count = 0;
    std::uint32_t live = 0;
};

[[gnu::used]] std::uint32_t slot_map_get_raw(RawSlots const& map, std::uint64_t key) noexcept {
    std::uint32_t const index = static_cast<std::uint32_t>(key);
    if (index >= map.used) {
        return 0;
    }
    std::uint32_t const generation = map.generations[index];
    if ((key >> 32) != generation || (generation & 1u) == 0) {
        return 0;
    }
    return map.values[index];
}

[[gnu::used]] std::uint32_t fixed_list_sum_raw(RawList const& list) noexcept {
    std::uint32_t sum = 0;
    for (std::uint32_t const* cursor = list.slots; cursor != list.slots + list.count; ++cursor) {
        sum += *cursor;
    }
    return sum;
}

// The three words of a probe owner.  The release adds to the cell of the
// family side, so the two sides do the same atomic work.
struct RawOwnerWords final {
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    std::uint64_t third = 0;
};

[[gnu::used]] std::uint64_t owner_take_and_end_raw(RawOwnerWords& source) noexcept {
    std::uint64_t const first = source.first;
    std::uint64_t const second = source.second;
    std::uint64_t const third = source.third;
    source.first = 0;
    source.second = 0;
    source.third = 0;
    if (first != 0) static_cast<void>(released_words.fetch_add_acq_rel(second + third));
    return second;
}

// Takes the address of a block and does nothing.  The compiler cannot see
// the body, so the address escapes, and a malloc and a free of the block
// stay in the code of the caller.  The parameter is not const, so the
// compiler does not read the call as a read of the unwritten bytes.
[[gnu::noipa]] void hold_block(void* /*block*/) noexcept {}

// The storage of a block holds its bytes rounded up to the alignment, and
// alignment - 1 bytes more (THE STORAGE of foundation/AlignedBytes.h).  Each
// address goes to hold_block, so the compiler keeps the two malloc and free
// pairs.  With no use of the addresses, it removes each pair.
[[gnu::used]] std::size_t aligned_bytes_mint_two_raw(std::size_t first_bytes, std::size_t first_alignment,
                                                     std::size_t second_bytes, std::size_t second_alignment) noexcept {
    std::size_t const first_rounded = (first_bytes + (first_alignment - 1)) & ~(first_alignment - 1);
    std::size_t const second_rounded = (second_bytes + (second_alignment - 1)) & ~(second_alignment - 1);
    void* const one = __builtin_malloc(first_rounded + (first_alignment - 1));
    void* const two = __builtin_malloc(second_rounded + (second_alignment - 1));
    hold_block(one);
    hold_block(two);
    __builtin_free(two);
    __builtin_free(one);
    return first_bytes + second_bytes;
}

// The fields of a named block: its storage, its count of bytes and its
// alignment (THE STORAGE of foundation/AlignedBytes.h).
struct RawBlock final {
    std::uint8_t* storage;
    std::size_t bytes;
    std::size_t alignment;
};

// A window of a block: the offset of its first byte and its count of bytes.
struct RawByteWindow final {
    std::size_t start;
    std::size_t length;
};

// Two pointer windows of a block, with the tests of the door in its order:
// inside the block, a whole count of floats and the first byte aligned for
// each window, and then no byte that the two share.  Then value goes into
// the first element of each window that holds one.
[[gnu::used]] bool aligned_bytes_two_windows_raw(RawBlock& block, RawByteWindow first, RawByteWindow second,
                                                 float value) noexcept {
    std::uint8_t* const base =
        block.storage == nullptr
            ? nullptr
            : block.storage
                  + ((std::size_t{0} - __builtin_bit_cast(std::uintptr_t, block.storage)) & (block.alignment - 1));
    if (first.start > block.bytes || first.length > block.bytes - first.start) return false;
    if (first.length % sizeof(float) != 0) return false;
    if ((__builtin_bit_cast(std::uintptr_t, base + first.start) & (alignof(float) - 1)) != 0) return false;
    if (second.start > block.bytes || second.length > block.bytes - second.start) return false;
    if (second.length % sizeof(float) != 0) return false;
    if ((__builtin_bit_cast(std::uintptr_t, base + second.start) & (alignof(float) - 1)) != 0) return false;
    std::size_t const first_end = first.start + first.length;
    std::size_t const second_end = second.start + second.length;
    if (first.start < first_end && second.start < second_end && first.start < second_end && second.start < first_end) {
        return false;
    }
    if (first.length != 0) *static_cast<float*>(static_cast<void*>(base + first.start)) = value;
    if (second.length != 0) *static_cast<float*>(static_cast<void*>(base + second.start)) = value;
    return true;
}

// The raw form of the static door: the two pointer windows of the raw form
// above, read from an array of the caller, as the door reads them.
[[gnu::used]] bool aligned_bytes_two_windows_static_raw(RawBlock& block, RawByteWindow const (&windows)[2],
                                                        float value) noexcept {
    RawByteWindow const& first = windows[0];
    RawByteWindow const& second = windows[1];
    std::uint8_t* const base =
        block.storage == nullptr
            ? nullptr
            : block.storage
                  + ((std::size_t{0} - __builtin_bit_cast(std::uintptr_t, block.storage)) & (block.alignment - 1));
    if (first.start > block.bytes || first.length > block.bytes - first.start) return false;
    if (first.length % sizeof(float) != 0) return false;
    if ((__builtin_bit_cast(std::uintptr_t, base + first.start) & (alignof(float) - 1)) != 0) return false;
    if (second.start > block.bytes || second.length > block.bytes - second.start) return false;
    if (second.length % sizeof(float) != 0) return false;
    if ((__builtin_bit_cast(std::uintptr_t, base + second.start) & (alignof(float) - 1)) != 0) return false;
    std::size_t const first_end = first.start + first.length;
    std::size_t const second_end = second.start + second.length;
    if (first.start < first_end && second.start < second_end && first.start < second_end && second.start < first_end) {
        return false;
    }
    if (first.length != 0) *static_cast<float*>(static_cast<void*>(base + first.start)) = value;
    if (second.length != 0) *static_cast<float*>(static_cast<void*>(base + second.start)) = value;
    return true;
}

[[gnu::used]] std::uint32_t box_read_raw(std::unique_ptr<Payload>& box) noexcept { return box->value; }

// The raw holder of an object of the whole process: its storage and the
// address of the object in it.
struct ImmortalRaw final {
    alignas(Payload) unsigned char storage[sizeof(Payload)]{};
    Payload* object = nullptr;
};

[[gnu::used]] std::uint32_t immortal_read_raw(ImmortalRaw& holder) noexcept { return holder.object->value; }

// The raw form of the bins of a heap: three pointers into a stack of words,
// at the offsets of the bins of a HeapState, and the cold bodies of an empty
// and of a full bin.
struct RawBinStack final {
    std::uint64_t* head = nullptr;
    std::uint64_t* empty_end = nullptr;
    std::uint64_t* full_begin = nullptr;
};

struct RawHeapState final {
    unsigned char header[64]{};
    RawBinStack bins[65]{};
};

[[gnu::noipa, gnu::cold]] std::uint64_t raw_refill(RawHeapState& heap, std::uint32_t size_class) noexcept {
    return heap.bins[size_class].head == nullptr ? 0 : 1;
}

[[gnu::noipa, gnu::cold]] void raw_flush(RawHeapState& heap, std::uint32_t size_class, std::uint64_t block) noexcept {
    heap.bins[size_class].head = heap.bins[size_class].full_begin + (block & 1);
}

[[gnu::used]] std::uint64_t heap_allocate_raw(RawHeapState& heap) noexcept {
    RawBinStack& bin = heap.bins[1];
    std::uint64_t* const head = bin.head;
    if (head == bin.empty_end) [[unlikely]]
        return raw_refill(heap, 1);
    bin.head = head + 1;
    return *head;
}

[[gnu::used]] void heap_release_raw(RawHeapState& heap, std::uint64_t block) noexcept {
    RawBinStack& bin = heap.bins[1];
    std::uint64_t* head = bin.head;
    if (head == bin.full_begin) [[unlikely]] {
        raw_flush(heap, 1, block);
        return;
    }
    --head;
    *head = block;
    bin.head = head;
}

// The raw form of a loop of pairs on the heap of the thread: the cell read by
// hand, the cold claim for the sentinel, and the fast paths of the heap state
// with no scope, no brand and no block type.
[[gnu::used]] std::uint64_t heap_scope_pairs_raw(std::uint32_t pairs) noexcept {
    foundation::alloc::HeapState* heap = foundation::alloc::tls_heap_;
    if (heap == &foundation::alloc::sentinel_heap_) [[unlikely]] {
        heap = &foundation::alloc::claim_thread_heap();
    }
    std::uint64_t sum = 0;
    for (std::uint32_t index = 0; index < pairs; ++index) {
        foundation::alloc::AddressWord const block = heap->allocate(foundation::alloc::SizeClass{1});
        sum += static_cast<std::uint64_t>(block);
        heap->release(foundation::alloc::SizeClass{1}, block);
    }
    return sum;
}

[[gnu::used]] void seat_step_send_raw(std::optional<StepHandle>& slot, std::uint64_t item) noexcept {
    slot.emplace(std::move(*slot).send(item));
}

[[gnu::used]] Payload* box_in_hole_raw(void* storage, std::uint32_t value) noexcept {
    return ::new(storage) Payload{value};
}

[[gnu::used]] std::uint32_t ref_read_raw(Payload const* payload) noexcept { return payload->value; }

[[gnu::used]] std::uint32_t option_expect_raw(std::optional<std::uint32_t> option) noexcept { return *option; }

[[gnu::used]] RawWindow option_expect_niche_raw(RawWindow window) noexcept { return window; }

[[gnu::used]] std::uint32_t option_match_raw(std::optional<std::uint32_t> option) noexcept {
    return option ? *option * 3u : 0u;
}

[[gnu::used]] std::uint32_t option_loop_raw(std::optional<std::uint32_t> option, std::uint32_t total) noexcept {
    if (option) {
        total += *option;
    }
    return total;
}

[[gnu::used]] std::uint32_t option_value_or_raw(std::optional<std::uint32_t> option) noexcept {
    return option.value_or(7u);
}

[[gnu::used]] std::uint64_t option_take_raw(std::optional<std::uint64_t>& held) noexcept {
    std::uint64_t const value = held.value_or(0u);
    held.reset();
    return value;
}

[[gnu::used]] bool option_is_some_raw(std::optional<std::uint64_t> const& option) noexcept {
    return option.has_value();
}

[[gnu::used]] void option_clear_block_raw(std::optional<Block64>& held) noexcept { held = std::nullopt; }

[[gnu::used]] void option_fill_linear_raw(std::optional<std::uint64_t>& held, std::uint64_t value) noexcept {
    held.emplace(value);
}

// The raw end empties the optional with no test, and a full optional drops
// its payload.
[[gnu::used]] void option_end_linear_raw(std::optional<std::uint64_t>&& held) noexcept { held.reset(); }

[[gnu::used]] void option_end_owned_raw(std::optional<std::uint64_t>&& held) noexcept {
    std::optional<std::uint64_t> const ended{std::move(held)};
    held.reset();
    static_cast<void>(ended);
}

// The raw form of an Option of a word is a size and a flag word, which come
// back in two registers.
struct RawWord final {
    std::size_t value;
    std::size_t holds;
};

[[gnu::used]] RawWord option_empty_word_raw() noexcept { return RawWord{0, 0}; }

[[gnu::used]] RawWord option_word_below_raw(std::size_t value, std::size_t limit) noexcept {
    if (value >= limit) return RawWord{0, 0};
    return RawWord{value, 1};
}

[[gnu::used]] RawWord option_sealed_word_below_raw(std::size_t value, std::size_t limit) noexcept {
    if (value >= limit) return RawWord{0, 0};
    return RawWord{value, 1};
}

[[gnu::used]] std::size_t option_sealed_match_raw(RawWord option) noexcept {
    return option.holds != 0 ? option.value * 3u : 0u;
}

[[gnu::used]] bool option_equal_raw(std::optional<std::uint32_t> left, std::optional<std::uint32_t> right) noexcept {
    return left == right;
}

[[gnu::used]] bool option_equal_word_raw(std::optional<std::uint64_t> left,
                                         std::optional<std::uint64_t> right) noexcept {
    return left == right;
}

[[gnu::used]] std::uint32_t* option_ref_some_raw(std::uint32_t& slot) noexcept { return &slot; }

[[gnu::used]] std::expected<std::uint32_t, Code32> option_ok_or_raw(std::optional<std::uint32_t> option) noexcept {
    if (option) return *option;
    return std::unexpected(Code32::gone);
}

[[gnu::used]] std::optional<std::uint32_t> option_try_raw(std::optional<std::uint32_t> option) noexcept {
    if (!option) return std::nullopt;
    return *option + 1u;
}

[[gnu::used]] std::expected<std::uint32_t, Code32>
result_try_raw(std::expected<std::uint32_t, Code32> result) noexcept {
    if (!result) return std::unexpected(result.error());
    return *result + 1u;
}

[[gnu::used]] std::expected<std::uint32_t, NetError8>
result_try_convert_raw(std::expected<std::uint32_t, Code8> result) noexcept {
    if (!result) return std::unexpected(NetError8{result.error()});
    return *result + 1u;
}

[[gnu::used]] std::uint32_t result_expect_raw(std::expected<std::uint32_t, Code32> result) noexcept { return *result; }

[[gnu::used]] std::uint32_t result_match_raw(std::expected<std::uint32_t, Code32> result) noexcept {
    return result ? *result * 3u : static_cast<std::uint32_t>(result.error());
}

// The raw form of a OneOf is a storage word that holds each alternative and
// a tag of one byte after it.  A union gives the same layout, but the rule
// proof-routes refuses a union, because it can hold a proof type.
struct RawReading final {
    std::uint32_t storage;
    std::uint8_t tag;
};

[[gnu::used]] std::uint32_t one_of_match_raw(RawReading reading) noexcept {
    if (reading.tag == 0) return reading.storage * 3u;
    if (reading.tag == 1) return reading.storage;
    return std::uint32_t{static_cast<std::uint16_t>(reading.storage)} + 5u;
}

[[gnu::used]] std::size_t one_of_which_raw(RawReading const& reading) noexcept { return reading.tag; }

[[gnu::used]] std::uint32_t one_of_get_raw(RawReading const& reading) noexcept {
    return reading.tag == 0 ? reading.storage : 0u;
}

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

// The raw form of a Record is a struct with the same members.
struct RawWords final {
    std::uint64_t first;
    std::uint64_t second;
};

// A hand-written struct.  Its == is the compare of the members by hand, and
// its <=> is the defaulted <=> of the struct.
struct RawPair32 final {
    std::uint32_t first;
    std::uint32_t second;

    friend std::strong_ordering operator<=>(RawPair32 const&, RawPair32 const&) noexcept = default;
};

struct RawTokens final {
    [[no_unique_address]] TokenA first;
    [[no_unique_address]] TokenB second;
};

[[gnu::used]] RawWords record_return_raw(std::uint64_t first, std::uint64_t second) noexcept {
    return RawWords{first, second};
}

[[gnu::used]] std::uint64_t record_binding_raw(RawWords record) noexcept { return record.first + record.second; }

[[gnu::used]] bool record_equal_raw(RawPair32 const& left, RawPair32 const& right) noexcept {
    return left.first == right.first && left.second == right.second;
}

[[gnu::used]] bool record_order_raw(RawPair32 left, RawPair32 right) noexcept { return std::is_lt(left <=> right); }

[[gnu::used]] RawTokens record_pass_tokens_raw(RawTokens tokens) noexcept { return tokens; }

struct RawProtocolRecord final {
    std::uint64_t key = 0;
    std::uint32_t count = 0;
    std::uint16_t lane = 0;
    Kind kind = Kind::first;
    bool flag = false;

    friend std::strong_ordering operator<=>(RawProtocolRecord const&, RawProtocolRecord const&) noexcept = default;
};

// The reason of a refused image, as a hand reader gives it.
struct RawDecodeFault final {
    std::uint32_t offset = 0;
    std::uint8_t reason = 0;
};

[[gnu::used]] bool protocol_equal_raw(RawProtocolRecord const& left, RawProtocolRecord const& right) noexcept {
    return left.key == right.key && left.count == right.count && left.lane == right.lane && left.kind == right.kind
        && left.flag == right.flag;
}

[[gnu::used]] bool protocol_order_raw(RawProtocolRecord const& left, RawProtocolRecord const& right) noexcept {
    return std::is_lt(left <=> right);
}

[[gnu::used]] std::uint64_t protocol_hash_raw(RawProtocolRecord const& value) noexcept {
    std::uint64_t const high = std::uint64_t{value.count} | (std::uint64_t{value.lane} << 32)
                             | (static_cast<std::uint64_t>(value.kind) << 48)
                             | (static_cast<std::uint64_t>(value.flag ? 1U : 0U) << 56);
    __extension__ using Wide = unsigned __int128;
    Wide const product =
        static_cast<Wide>(value.key ^ 0x13198a2e03707344ULL) * static_cast<Wide>(high ^ 0x243f6a8885a308d3ULL);
    return static_cast<std::uint64_t>(product) ^ static_cast<std::uint64_t>(product >> 64);
}

[[gnu::used]] void protocol_encode_raw(RawProtocolRecord const& value, std::byte* out) noexcept {
    std::uint8_t const flag = value.flag ? 1 : 0;
    std::memcpy(out, &value.key, 8);
    std::memcpy(out + 8, &value.count, 4);
    std::memcpy(out + 12, &value.lane, 2);
    std::memcpy(out + 14, &value.kind, 1);
    std::memcpy(out + 15, &flag, 1);
}

[[gnu::used]] std::expected<RawProtocolRecord, RawDecodeFault> protocol_decode_raw(std::byte const* image) noexcept {
    RawProtocolRecord value{};
    std::uint8_t kind = 0;
    std::uint8_t flag = 0;
    std::memcpy(&value.key, image, 8);
    std::memcpy(&value.count, image + 8, 4);
    std::memcpy(&value.lane, image + 12, 2);
    std::memcpy(&kind, image + 14, 1);
    if (kind > 3) return std::unexpected(RawDecodeFault{14, 1});
    std::memcpy(&flag, image + 15, 1);
    if (flag > 1) return std::unexpected(RawDecodeFault{15, 0});
    value.kind = static_cast<Kind>(kind);
    value.flag = flag == 1;
    return value;
}

[[gnu::used]] bool protocol_equal_double_raw(double left, double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

[[gnu::used]] bool fixed_array_order_raw(std::array<std::uint32_t, 4> const& left,
                                         std::array<std::uint32_t, 4> const& right) noexcept {
    return std::is_lt(left <=> right);
}

[[gnu::used]] std::optional<std::uint32_t> scalar_checked_add_raw(std::uint32_t left, std::uint32_t right) noexcept {
    std::uint32_t sum = 0;
    if (__builtin_add_overflow(left, right, &sum)) return std::nullopt;
    return sum;
}

[[gnu::used]] std::optional<std::uint64_t> scalar_checked_add_wide_raw(std::uint64_t left,
                                                                       std::uint64_t right) noexcept {
    std::uint64_t sum = 0;
    if (__builtin_add_overflow(left, right, &sum)) return std::nullopt;
    return sum;
}

[[gnu::used]] std::optional<std::int32_t> scalar_checked_div_raw(std::int32_t dividend, std::int32_t divisor) noexcept {
    if (divisor == 0 || (dividend == std::numeric_limits<std::int32_t>::min() && divisor == -1)) return std::nullopt;
    return dividend / divisor;
}

[[gnu::used]] std::uint64_t scalar_wrapping_shl_raw(std::uint64_t value, int count) noexcept {
    return value << (static_cast<unsigned>(count) & 63u);
}

[[gnu::used]] std::int64_t scalar_saturating_add_raw(std::int64_t left, std::int64_t right) noexcept {
    return std::saturating_add(left, right);
}

struct RawWideProduct final {
    std::uint64_t low;
    std::uint64_t high;
};

[[gnu::used]] RawWideProduct scalar_widening_mul_raw(std::uint64_t left, std::uint64_t right) noexcept {
    __extension__ using Wide = unsigned __int128;
    Wide const product = static_cast<Wide>(left) * static_cast<Wide>(right);
    return RawWideProduct{static_cast<std::uint64_t>(product), static_cast<std::uint64_t>(product >> 64)};
}

[[gnu::used]] double scalar_min_float_raw(double left, double right) noexcept { return std::min(left, right); }

struct RawBounds final {
    std::int32_t lo;
    std::int32_t hi;
};

[[gnu::used]] std::int32_t scalar_clamp_bounds_raw(std::int32_t value, RawBounds range) noexcept {
    return std::clamp(value, range.lo, range.hi);
}

[[gnu::used]] bool scalar_compare_mixed_raw(std::int64_t left, std::uint64_t right) noexcept {
    return std::cmp_less(left, right);
}

[[gnu::used]] bool scalar_fits_raw(std::int32_t value) noexcept { return std::in_range<std::uint8_t>(value); }

[[gnu::used]] std::uint32_t scalar_unsigned_abs_raw(std::int32_t value) noexcept {
    return value < 0 ? 0u - static_cast<std::uint32_t>(value) : static_cast<std::uint32_t>(value);
}

[[gnu::used]] std::optional<std::uint32_t> scalar_narrow_checked_raw(std::uint64_t value) noexcept {
    if (value > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    return static_cast<std::uint32_t>(value);
}

[[gnu::used]] std::optional<std::int32_t> scalar_narrow_float_raw(double value) noexcept {
    if (!(value > -2147483649.0 && value < 2147483648.0)) return std::nullopt;
    return static_cast<std::int32_t>(value);
}

[[gnu::used]] std::int16_t scalar_narrow_saturating_raw(std::int64_t value) noexcept {
    if (value < std::numeric_limits<std::int16_t>::min()) return std::numeric_limits<std::int16_t>::min();
    if (value > std::numeric_limits<std::int16_t>::max()) return std::numeric_limits<std::int16_t>::max();
    return static_cast<std::int16_t>(value);
}

[[gnu::used]] std::uint64_t scalar_widen_raw(std::uint32_t value) noexcept { return value; }

[[gnu::used]] int scalar_popcount_raw(std::uint64_t value) noexcept { return std::popcount(value); }

[[gnu::used]] int scalar_countl_zero_raw(std::uint64_t value) noexcept { return std::countl_zero(value); }

[[gnu::used]] std::uint64_t scalar_rotl_raw(std::uint64_t value, int count) noexcept { return std::rotl(value, count); }

[[gnu::used]] std::uint32_t scalar_byteswap_raw(std::uint32_t value) noexcept { return std::byteswap(value); }

// std::bit_ceil is undefined past the top bit, so the raw form tests the
// range first.
[[gnu::used]] std::optional<std::uint32_t> scalar_bit_ceil_raw(std::uint32_t value) noexcept {
    if (value > (std::uint32_t{1} << 31)) return std::nullopt;
    return std::bit_ceil(value);
}

[[gnu::used]] std::uint64_t scalar_low_mask_raw(int count) noexcept {
    if (count <= 0) return 0;
    if (count >= 64) return ~std::uint64_t{0};
    return (std::uint64_t{1} << count) - 1;
}

[[gnu::used]] std::uint64_t scalar_bit_cast_raw(double value) noexcept { return std::bit_cast<std::uint64_t>(value); }

[[gnu::used]] std::optional<bool> scalar_decode_bool_raw(std::uint8_t byte) noexcept {
    if (byte > 1) return std::nullopt;
    return byte == 1;
}

[[gnu::used]] std::optional<std::uint32_t> scalar_id_decode_raw(std::uint32_t word) noexcept {
    if (word == std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    return word;
}

[[gnu::used]] bool scalar_fp_is_nan_raw(double value) noexcept { return std::isnan(value); }

[[gnu::used]] double scalar_fp_abs_raw(double value) noexcept { return std::fabs(value); }

[[gnu::used]] double scalar_fp_sqrt_raw(double value) noexcept { return std::sqrt(value); }

[[gnu::used]] double scalar_fp_fma_raw(double left, double right, double addend) noexcept {
    return std::fma(left, right, addend);
}

[[gnu::used]] std::uint64_t scalar_fp_total_key_raw(double value) noexcept {
    auto const bits = std::bit_cast<std::uint64_t>(value);
    auto const spread = static_cast<std::uint64_t>(static_cast<std::int64_t>(bits) >> 63);
    return bits ^ (spread | (std::uint64_t{1} << 63));
}

[[gnu::used]] std::uint64_t scalar_exchange_raw(std::uint64_t& slot, std::uint64_t value) noexcept {
    return std::exchange(slot, value);
}

[[gnu::used]] void scalar_swap_raw(std::uint64_t& left, std::uint64_t& right) noexcept { std::swap(left, right); }

[[gnu::used]] std::optional<std::uint32_t> scalar_quantity_add_raw(std::uint32_t left, std::uint32_t right) noexcept {
    std::uint32_t sum = 0;
    if (__builtin_add_overflow(left, right, &sum)) return std::nullopt;
    return sum;
}

[[gnu::used]] std::optional<std::uint32_t> scalar_quantity_to_finer_raw(std::uint32_t value) noexcept {
    std::uint32_t product = 0;
    if (__builtin_mul_overflow(value, 1000u, &product)) return std::nullopt;
    return product;
}

[[gnu::used]] std::optional<std::uint32_t> scalar_quantity_to_coarser_raw(std::uint32_t value) noexcept {
    if (value % 1000u != 0) return std::nullopt;
    return value / 1000u;
}

[[gnu::used]] std::optional<std::uint32_t> scalar_quantity_product_raw(std::uint32_t rate,
                                                                       std::uint32_t seconds) noexcept {
    std::uint32_t product = 0;
    if (__builtin_mul_overflow(rate, seconds, &product)) return std::nullopt;
    return product;
}

// The one division, and the canonical NaN of the mask of the sign bit.
[[gnu::used]] double scalar_quantity_float_quotient_raw(double peak, double bandwidth) noexcept {
    double const quotient = peak / bandwidth;
    auto const magnitude = std::bit_cast<std::uint64_t>(quotient) & ~(std::uint64_t{1} << 63);
    return magnitude > std::uint64_t{0x7FF0000000000000} ? std::numeric_limits<double>::quiet_NaN() : quotient;
}

// The ten rounds of Philox4x32-10 over a std::array counter and key, with
// one 64-bit product for the two halves of each multiplied word.
[[gnu::used]] std::uint32_t scalar_philox_generate_raw(std::uint64_t offset, std::uint64_t key) noexcept {
    std::array<std::uint32_t, 4> counter{static_cast<std::uint32_t>(offset), static_cast<std::uint32_t>(offset >> 32),
                                         0u, 0u};
    std::array<std::uint32_t, 2> words{static_cast<std::uint32_t>(key), static_cast<std::uint32_t>(key >> 32)};
    for (int round = 0; round < 10; round++) {
        std::uint64_t const product0 = std::uint64_t{0xD2511F53u} * counter[0];
        std::uint64_t const product1 = std::uint64_t{0xCD9E8D57u} * counter[2];
        counter = {
            static_cast<std::uint32_t>(product1 >> 32) ^ counter[1] ^ words[0], static_cast<std::uint32_t>(product1),
            static_cast<std::uint32_t>(product0 >> 32) ^ counter[3] ^ words[1], static_cast<std::uint32_t>(product0)};
        words[0] += 0x9E3779B9u;
        words[1] += 0xBB67AE85u;
    }
    return counter[0] ^ counter[1] ^ counter[2] ^ counter[3];
}

// The raw form of a token is an empty class that the ABI passes in no
// register.
struct RawToken final {};

[[gnu::used]] int linear_consume_raw(int value) noexcept { return value; }

[[gnu::used]] std::uint64_t linear_consume_word_raw(std::uint64_t value) noexcept { return value; }

[[gnu::used]] void linear_assign_raw(int& target, int& source) noexcept { target = source; }

[[gnu::used]] RawToken token_pass_raw(RawToken token) noexcept { return token; }

[[gnu::used]] void token_assign_raw(RawToken& target, RawToken& source) noexcept { target = source; }

// The raw form of a contract check is the language assertion, which the
// contract semantic of the unit controls.
[[gnu::used]] std::uint64_t contract_pre_raw(std::uint64_t index, std::uint64_t count) noexcept {
    contract_assert(index < count);
    return index * 3;
}

[[gnu::used]] std::uint64_t contract_post_raw(std::uint64_t index, std::uint64_t count) noexcept {
    std::uint64_t const tripled = index * 3;
    contract_assert(tripled != count);
    return tripled;
}

[[gnu::used]] std::uint64_t contract_pre_msg_raw(std::uint64_t index, std::uint64_t count) noexcept {
    contract_assert(index < count);
    return index * 3;
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace
