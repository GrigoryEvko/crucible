// The compile-time checks of foundation/core/Atomic.h.

#include <foundation/core/Atomic.h>

#include <foundation/core/Verbs.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace foundation::core {

namespace detail::atomic_checks {

enum class Phase : std::uint8_t {
    idle,
    busy
};
enum class WidePhase : std::uint64_t {
    idle,
    busy
};

struct Wrapped {
    int value = 0;
};

// Two words with no padding: one bit pattern for each value.
struct Pair {
    std::uint32_t first = 0;
    std::uint32_t second = 0;
};

// Six bytes of members and two bytes of padding.
struct Padded {
    std::uint32_t key = 0;
    std::uint16_t tag = 0;
};

struct NotDefaultConstructible {
    explicit constexpr NotDefaultConstructible(std::uint32_t initial) noexcept : value{initial} {}
    std::uint32_t value;
};

// The size of the value, and its alignment.
static_assert(sizeof(Atomic<std::uint64_t>) == 8 && alignof(Atomic<std::uint64_t>) == 8);
static_assert(sizeof(Atomic<std::uint32_t>) == 4 && alignof(Atomic<std::uint32_t>) == 4);
static_assert(sizeof(Atomic<std::uint8_t>) == 1);
static_assert(sizeof(Atomic<bool>) == 1);
static_assert(sizeof(Atomic<Phase>) == 1);
static_assert(sizeof(Atomic<WidePhase>) == 8 && alignof(Atomic<WidePhase>) == 8);
static_assert(sizeof(Atomic<Pair>) == 8 && alignof(Atomic<Pair>) == 8);
static_assert(sizeof(Atomic<Wrapped>) == 4 && alignof(Atomic<Wrapped>) == 4);
static_assert(sizeof(Tally) == 8 && alignof(Tally) == 8);
// The result of a compare-and-swap is the read value and one flag, so the
// ABI gives it back in two registers.
static_assert(sizeof(Result<Unit, CasRefusal<std::uint64_t>>) == 16);
static_assert(std::is_trivially_copyable_v<Result<Unit, CasRefusal<std::uint64_t>>>);

// One cache line for a small cell, and whole lines for a larger one.
static_assert(sizeof(CacheLine<Atomic<std::uint64_t>>) == 64 && alignof(CacheLine<Atomic<std::uint64_t>>) == 64);
static_assert(sizeof(CacheLine<Tally>) == 64);
static_assert(sizeof(CacheLine<std::uint8_t>) == 64);
struct NinetySixBytes {
    std::uint64_t word_0 = 0, word_1 = 0, word_2 = 0, word_3 = 0, word_4 = 0, word_5 = 0;
    std::uint64_t word_6 = 0, word_7 = 0, word_8 = 0, word_9 = 0, word_10 = 0, word_11 = 0;
};
static_assert(sizeof(NinetySixBytes) == 96 && sizeof(CacheLine<NinetySixBytes>) == 128);

// The address of a cell is its identity, so a cell does not move.
static_assert(!std::is_copy_constructible_v<Atomic<int>> && !std::is_move_constructible_v<Atomic<int>>);
static_assert(!std::is_copy_assignable_v<Atomic<int>> && !std::is_move_assignable_v<Atomic<int>>);
static_assert(!std::is_copy_constructible_v<Tally> && !std::is_move_constructible_v<Tally>);
static_assert(!std::is_copy_constructible_v<CacheLine<Atomic<int>>>);

// No operator and no conversion: each operation names its order.
static_assert(!std::is_convertible_v<Atomic<int>&, int>);
static_assert(!std::is_assignable_v<Atomic<int>&, int>);
template <class A>
concept Increments = requires(A& cell) { ++cell; };
static_assert(!Increments<Atomic<int>>);

// The value gate.
static_assert(AtomicValue<int> && AtomicValue<std::uint64_t> && AtomicValue<bool> && AtomicValue<Phase>);
static_assert(AtomicValue<Wrapped> && AtomicValue<Pair> && AtomicValue<int*>);
static_assert(AtomicValue<NotDefaultConstructible>);
static_assert(!AtomicValue<int const>);
static_assert(!AtomicValue<int volatile>);
static_assert(!AtomicValue<Padded>);
static_assert(!AtomicValue<float>);
static_assert(!AtomicValue<double>);
static_assert(!AtomicValue<int[2]>);
static_assert(!AtomicValue<int&>);

// A niche Option has the bytes of its payload and one bit pattern for each
// value, so a cell holds it as one word.  A plain Option holds a tag and
// padding, and the gate refuses it.
struct NicheSlot {
    std::uint32_t raw = 0;
    [[nodiscard]] static constexpr NicheSlot niche_empty(niche_key<NicheSlot> const&) noexcept {
        return NicheSlot{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(niche_key<NicheSlot> const&) const noexcept {
        return raw == 0xFFFFFFFFu;
    }
};
static_assert(AtomicValue<Option<NicheSlot>>);
static_assert(sizeof(Atomic<Option<NicheSlot>>) == 4 && alignof(Atomic<Option<NicheSlot>>) == 4);
static_assert(!AtomicValue<Option<std::uint32_t>> && !AtomicValue<Option<std::uint64_t>>);

// A value with no default constructor gives a cell with no default
// constructor, and the cell never holds bits that no constructor made.
static_assert(!std::is_default_constructible_v<Atomic<NotDefaultConstructible>>);
static_assert(std::is_default_constructible_v<Atomic<Pair>>);

// A cell of a class value holds the bits of the value, and the constructor
// works in a constant evaluation, so a constinit cell needs no dynamic
// initialization.
[[nodiscard]] consteval bool builds_in_a_constant_evaluation() noexcept {
    Atomic<Pair> const pair_cell{Pair{3, 4}};
    Atomic<Pair> const default_cell{};
    Atomic<int*> const pointer_cell{nullptr};
    Atomic<Option<NicheSlot>> const empty_option_cell{};
    Atomic<Option<NicheSlot>> const full_option_cell{Option<NicheSlot>::some(NicheSlot{5})};
    static_cast<void>(pair_cell);
    static_cast<void>(default_cell);
    static_cast<void>(pointer_cell);
    static_cast<void>(empty_option_cell);
    static_cast<void>(full_option_cell);
    return true;
}
static_assert(builds_in_a_constant_evaluation());

// The arithmetic steps an integer, and not a bool or an enum.  A bitwise
// operation takes an unsigned integer.
template <class A>
concept Adds = requires(A& cell) { cell.fetch_add_acq_rel(typename A::value_type{}); };
template <class A>
concept Subtracts = requires(A& cell) { cell.fetch_sub_acq_rel(typename A::value_type{}); };
template <class A>
concept Bounds = requires(A& cell) {
    cell.fetch_max_acq_rel(typename A::value_type{});
    cell.fetch_min_acq_rel(typename A::value_type{});
};
template <class A>
concept SetsBits = requires(A& cell) {
    cell.fetch_or_acq_rel(typename A::value_type{});
    cell.fetch_and_acq_rel(typename A::value_type{});
};
template <class A>
concept Exchanges = requires(A& cell) { cell.exchange_acq_rel(typename A::value_type{}); };
static_assert(Adds<Atomic<int>> && Adds<Atomic<std::uint64_t>>);
static_assert(!Adds<Atomic<bool>> && !Adds<Atomic<Phase>> && !Adds<Atomic<Pair>>);
static_assert(Subtracts<Atomic<int>> && !Subtracts<Atomic<bool>> && !Subtracts<Atomic<Phase>>);
static_assert(Bounds<Atomic<std::int64_t>> && Bounds<Atomic<std::uint8_t>>);
static_assert(!Bounds<Atomic<bool>> && !Bounds<Atomic<Phase>> && !Bounds<Atomic<Pair>>);
static_assert(SetsBits<Atomic<std::uint32_t>> && SetsBits<Atomic<std::uint8_t>>);
static_assert(!SetsBits<Atomic<int>> && !SetsBits<Atomic<bool>> && !SetsBits<Atomic<Phase>>);
static_assert(Exchanges<Atomic<int>> && Exchanges<Atomic<bool>> && Exchanges<Atomic<Phase>> && Exchanges<Atomic<Pair>>);

// A CacheLine always holds its cell, so the dereference borrows it from an
// lvalue.  A borrow of a temporary CacheLine is refused.  No member named
// get borrows with no key.
template <class C>
concept DereferencesTemporary = requires(C&& line) { *static_cast<C&&>(line); };
template <class C>
concept ArrowsTemporary = requires(C&& line) { static_cast<C&&>(line).operator->(); };
template <class C>
concept HasGet = requires(C& line) { line.get(); };
static_assert(std::is_same_v<decltype(*std::declval<CacheLine<int>&>()), int&>);
static_assert(std::is_same_v<decltype(*std::declval<CacheLine<int> const&>()), int const&>);
static_assert(std::is_same_v<decltype(std::declval<CacheLine<int>&>().operator->()), int*>);
static_assert(std::is_same_v<decltype(std::declval<CacheLine<int> const&>().operator->()), int const*>);
static_assert(!DereferencesTemporary<CacheLine<int>> && !DereferencesTemporary<CacheLine<int> const>);
static_assert(!ArrowsTemporary<CacheLine<int>> && !ArrowsTemporary<CacheLine<int> const>);
static_assert(!HasGet<CacheLine<int>> && !HasGet<CacheLine<int> const>);

// The closed surface of the family (plan section 10.2, item 9).  The list
// holds the names of the family of plan section 11.6, value_type and
// observed of main, and brand_type, the member that foundation/Brand.h reads.
inline constexpr TextView atomic_names{
    "Atomic AtomicValue AtomicCount AtomicBits CasRefusal SoleWriter CacheLine Tally load_acquire store_release "
    "exchange_acq_rel cas_acq_rel fetch_add_acq_rel fetch_sub_acq_rel fetch_or_acq_rel fetch_and_acq_rel "
    "fetch_max_acq_rel fetch_min_acq_rel load_owned add read value_type observed brand_type"};
CRUCIBLE_CLOSED_SURFACE(atomic_names, Atomic<int>);
CRUCIBLE_CLOSED_SURFACE(atomic_names, Atomic<std::uint64_t>);
CRUCIBLE_CLOSED_SURFACE(atomic_names, Atomic<Pair>);
CRUCIBLE_CLOSED_SURFACE(atomic_names, Atomic<Phase>);
CRUCIBLE_CLOSED_SURFACE(atomic_names, Atomic<Option<NicheSlot>>);
CRUCIBLE_CLOSED_SURFACE(atomic_names, sole_writer_key);
CRUCIBLE_CLOSED_SURFACE(atomic_names, CasRefusal<int>);
CRUCIBLE_CLOSED_SURFACE(atomic_names, Tally);
CRUCIBLE_CLOSED_SURFACE(atomic_names, CacheLine<int>, DereferenceProtocol);
CRUCIBLE_CLOSED_SURFACE(atomic_names, CacheLine<Atomic<std::uint64_t>>, DereferenceProtocol);
CRUCIBLE_CLOSED_SURFACE(atomic_names, CacheLine<Tally>, DereferenceProtocol);

// The writer of a cell of one writer.  A role is an empty class, and the
// mint takes the role and no other template argument.
struct HeadRole {};
struct TailRole {};
struct NotEmptyRole {
    int field = 0;
};
static_assert(SoleWriterRole<HeadRole> && !SoleWriterRole<HeadRole const> && !SoleWriterRole<int>);
static_assert(!SoleWriterRole<NotEmptyRole>);
static_assert(SoleWriterMint<HeadRole> && !SoleWriterMint<HeadRole, TailRole> && !SoleWriterMint<int>);
static_assert(!std::is_default_constructible_v<sole_writer_key>);

// Each write takes the proof of the writer of the cell, and only that
// proof.  Each thread reads with acquire order, and only the writer reads
// with no order.  The cell has no read-modify-write operation.
template <class Cell, class Writer>
concept StoresWith =
    requires(Cell& cell, Writer const& writer) { cell.store_release(writer, typename Cell::value_type{}); };
template <class Cell, class Writer>
concept LoadsOwnedWith = requires(Cell const& cell, Writer const& writer) { cell.load_owned(writer); };
template <class Cell>
concept StoresWithoutProof = requires(Cell& cell) { cell.store_release(typename Cell::value_type{}); };
template <class Cell>
concept LoadsAcquire = requires(Cell const& cell) { cell.load_acquire(); };
template <class Cell>
concept Swaps = requires(Cell& cell) { cell.cas_acq_rel(typename Cell::value_type{}, typename Cell::value_type{}); };

// The checks of the cell of one writer.  A fresh brand stays in the scope
// that made it, so the aliases of the writers and of the cells are in the
// body of this function.
[[nodiscard]] consteval bool sole_writer_checks() noexcept {
    using HeadWriter = decltype(mint_sole_writer<HeadRole>());
    using TailWriter = decltype(mint_sole_writer<TailRole>());
    using HeadCell = Atomic<std::uint64_t, HeadWriter>;
    using TailPairCell = Atomic<Pair, TailWriter>;

    // Each call site of the mint gives a fresh brand, so two writers of one
    // role are two types.
    static_assert(!std::is_same_v<decltype(mint_sole_writer<HeadRole>()), HeadWriter>);
    static_assert(::foundation::brand::IsBranded<HeadWriter>);

    // A writer is a linear token with no bytes: it moves in a register, it
    // does not copy, and no default constructor, bit copy or key that a
    // caller makes builds one.
    static_assert(sizeof(HeadWriter) == 1 && std::is_empty_v<HeadWriter>);
    static_assert(::foundation::linear::is_linear_v<HeadWriter>);
    static_assert(!std::is_copy_constructible_v<HeadWriter> && !std::is_copy_assignable_v<HeadWriter>);
    static_assert(std::is_trivially_move_constructible_v<HeadWriter> && std::is_trivially_destructible_v<HeadWriter>);
    static_assert(!std::is_trivially_copyable_v<HeadWriter> && !std::is_default_constructible_v<HeadWriter>);

    // The cell of one writer has the layout of its value, and it does not
    // move.
    static_assert(sizeof(HeadCell) == 8 && alignof(HeadCell) == 8);
    static_assert(!std::is_copy_constructible_v<HeadCell> && !std::is_move_constructible_v<HeadCell>);
    static_assert(!std::is_convertible_v<HeadCell&, std::uint64_t>);

    // The operations of the writer take its proof, and the cell has no
    // read-modify-write operation.
    static_assert(StoresWith<HeadCell, HeadWriter> && LoadsOwnedWith<HeadCell, HeadWriter> && LoadsAcquire<HeadCell>);
    static_assert(!StoresWith<HeadCell, TailWriter> && !LoadsOwnedWith<HeadCell, TailWriter>);
    static_assert(!StoresWithoutProof<HeadCell> && StoresWithoutProof<Atomic<std::uint64_t>>);
    static_assert(!LoadsOwnedWith<Atomic<std::uint64_t>, HeadWriter>);
    static_assert(!Exchanges<HeadCell> && !Swaps<HeadCell> && !Adds<HeadCell> && !Subtracts<HeadCell>);
    static_assert(!SetsBits<HeadCell> && !Bounds<HeadCell> && Swaps<Atomic<std::uint64_t>>);

    CRUCIBLE_CLOSED_SURFACE(atomic_names, HeadCell);
    CRUCIBLE_CLOSED_SURFACE(atomic_names, TailPairCell);
    CRUCIBLE_CLOSED_SURFACE(atomic_names, HeadWriter);

    // A cell of one writer builds in a constant evaluation, so a constinit
    // cell needs no dynamic initialization.
    HeadCell const zero_cell{};
    HeadCell const seven_cell{std::uint64_t{7}};
    static_cast<void>(zero_cell);
    static_cast<void>(seven_cell);
    return true;
}
static_assert(sole_writer_checks());

}  // namespace detail::atomic_checks

}  // namespace foundation::core
