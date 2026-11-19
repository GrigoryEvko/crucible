#pragma once

// The Atomic family: one cell that two threads touch, with the memory
// order in the name of each operation.
//
// Atomic<T> holds one value whose atomic operations do not take a lock on
// this target.  Each operation uses the __atomic builtins on the plain
// value, so it compiles to the instructions of the same std::atomic
// operation.  The order is in the name:
//
//   load_acquire()                 the value, with acquire order
//   store_release(v)               stores v, with release order
//   exchange_acq_rel(v)            stores v, and gives the value before
//   cas_acq_rel(e, d)              stores d if the cell holds e, strong, and
//                                  gives a Result with the value it read
//   cas_weak_acq_rel(e, d)         the same, but it can fail when the cell
//                                  holds e, so a caller runs it in a loop
//   fetch_add_acq_rel(d)           adds d, for an integer
//   fetch_sub_acq_rel(d)           subtracts d, for an integer
//   fetch_or_acq_rel(b)            sets the bits b, for an unsigned integer
//   fetch_and_acq_rel(b)           keeps only the bits b, for an unsigned integer
//   fetch_xor_acq_rel(b)           flips the bits b, for an unsigned integer
//   fetch_max_acq_rel(v)           stores v if v is larger, for an integer
//   fetch_min_acq_rel(v)           stores v if v is smaller, for an integer
//
// The type has no operator and no conversion, so no operation takes the
// sequentially consistent order without a name that says so.  Only two cells
// take the relaxed order: the writer of a cell of one writer reads with it,
// and each thread adds to a count with it (below).
//
// A value of a class type is admitted when it has one bit pattern for each
// value.  The cell holds it as the unsigned integer of its size, and the
// compare of a compare-and-swap then compares the bits of the value.  The
// bits of a value with no padding are the value, so the compare agrees with
// a compare of the members.
//
// THE WORD OF A VALUE
//   A value that keeps an invariant, such as a Ref that is never null, is
//   not trivially copyable, so std::bit_cast cannot make it from bytes.  Its
//   type states a word beside its own definition: atomic_word<T> gives a
//   word_type, an integer, an enum or a pointer, and three members:
//
//     to_word(value)             the word of a value
//     from_word(key, word)       the value of a word that to_word gave
//     empty_word                 a constant word that no value gives
//
//   from_word takes an atomic_word_key<T> by reference.  Only the cells of
//   this family make one, so only a cell makes a value from a word, and a
//   cell holds only words that to_word gave.  The key of T opens only the
//   from_word of T.
//   A protocol whose from_word takes no key is refused, because each caller
//   could then make a value from a word.  The cell copies the value out on
//   each read and destroys no value, so the value is trivially destructible
//   and copies with no throw.  A value with one bit pattern for each value
//   keeps its own bits as its word, and an atomic_word for it is refused.
//
//   The word of Option<X> comes from the word of X: a full Option gives the
//   word of its payload, and an empty Option gives empty_word.  The cell
//   reads empty_word as an empty Option, and it never makes the empty value
//   of a niche.  A store of a full Option whose payload gives empty_word ends
//   the process, because the cell would read the Option back as empty.
//
// THE CELL OF ONE WRITER
//   Atomic<T, SoleWriter<Role, Brand>> names its writer in its type, and
//   each write takes the proof of that writer:
//
//     load_acquire()             the value, with acquire order, on each thread
//     load_owned(writer)         the value, with relaxed order, for the writer
//     store_release(writer, v)   stores v, with release order, for the writer
//
//   The holder of the proof is the only thread that writes the cell, so a
//   read by the writer reads a value that the same thread stored.  That read
//   needs no order.  A proof moves to a different thread only through a
//   handoff that orders it, such as a thread start, a join or a channel of
//   the base.  The last store of the old holder then happens before the
//   first read of the new holder.  The cell has no read-modify-write
//   operation: the writer reads with load_owned and stores with
//   store_release, with no locked instruction.
//
//   SoleWriter<Role, Brand> is the proof, a linear token of 8 bytes.
//   mint_sole_writer<Role>() is its one door, and each call site of the
//   door gives a fresh brand.  A brand is a call site: a mint in a loop
//   gives one type in each turn.  So each mint also takes a run-time id
//   (misc/12_10_2026_linear.md, phase B), and the token holds it.  The cell
//   takes its writer at construction and keeps the id of that writer.
//   load_owned and store_release compare the id of the proof with it, and a
//   proof of a different id ends the process.  So a token that leaves its
//   turn cannot read or write the cell of a different turn.  The cost is one
//   compare and one branch to a cold call at each read and each write of the
//   writer.
//
// THE COUNT
//   Atomic<T, Counter> is a count that each thread adds to, for an integer
//   that is not bool.  No thread reads other data through it, so a change
//   of the count needs no order:
//
//     load_acquire()             the count, with acquire order
//     fetch_add_relaxed(d)       adds d, with relaxed order
//     fetch_sub_relaxed(d)       subtracts d, with relaxed order
//     fetch_max_relaxed(v)       stores v if v is larger, with relaxed order
//     fetch_min_relaxed(v)       stores v if v is smaller, with relaxed order
//
//   Each change is one atomic read-modify-write, so no addition is lost.  A
//   read takes the acquire order, as a read of each other cell does, because
//   a relaxed read is only for a value of the own thread.  On x86-64 a relaxed
//   and an acq_rel addition are the same locked instruction, and an acquire
//   load is a plain mov.  On aarch64 a relaxed addition has no barrier.  The
//   cell has no store and no compare-and-swap, so no thread can publish data
//   through it.
//
// CacheLine<T> aligns a cell to 64 bytes and pads it to a multiple of 64
// bytes, so the type states that no other data shares its cache line.
// *line and line->member borrow the cell.
//
// Tally is a statistics counter of 64 bits over a count (THE COUNT).  add()
// is a relaxed fetch-add, and read() is an acquire load.  A value of a Tally
// orders no other data.
//
// THE FENCE
//   fence_seq_cst() is a fence with the sequentially consistent order.  It
//   orders each store of the calling thread before it against each load of
//   the calling thread after it, the store-load order.  Two threads that each
//   store one cell, run the fence and load the cell of the other thread
//   cannot both read the old values: at least one reads the store of the
//   other.  The wait of a park and wake protocol needs that order.  The
//   waiter enters a count of the waiters and then tests its condition again,
//   and the waker stores the condition and then loads the count.
//
//   No acquire, release or acq_rel operation gives that order.  Each of them
//   lets the core run a load before a store of the same thread to a
//   different cell.  The x86-64 code is a locked OR of zero to the top of
//   the stack (lock orq $0, (%rsp)), and the aarch64 code is dmb ish.
//
// THE WAIT
//   A cell has no wait and no notify of its own.  This header is in
//   foundation/core and cannot include a door of the kernel.  A thread waits
//   for a change of an Atomic<std::uint32_t> through the futex door of
//   fixy/os/Futex.h: wait, wake_one and wake_all, under the Block gate.  A
//   thread waits for each other condition through the event count of
//   fixy/concurrent/EventCount.h, and the cell of 64 bits is one of them,
//   because the futex of Linux reads only 32 bits.  Do not add a second wait
//   form.

#include <foundation/Brand.h>
#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/OpenExtension.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Report.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

template <class T>
class atomic_word_key;

// The word protocol of a value (THE WORD OF A VALUE).  The primary template
// has no definition, so a type with no specialization has no word.
template <class T>
struct[[= ::foundation::open_extension{
    "a type that keeps an invariant states the word of its atomic cell beside its own definition"}]] atomic_word;

namespace detail {

// The shape of each value of a cell: an object type, not an array and not
// cv-qualified.
template <class T>
concept AtomicObject = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>;

// A value that the cell holds as its own bits: trivially copyable, with one
// bit pattern for each value, and lock-free for each object of its size.
// The one bit pattern makes a compare-and-swap compare values: a type with
// padding, or a floating type with two zeros, has no such bits.
template <class T>
concept AtomicBitsValue = std::is_trivially_copyable_v<T> && std::has_unique_object_representations_v<T>
                       && __atomic_always_lock_free(sizeof(T), 0);

// The word of a word protocol: an integer, an enum or a pointer that the
// cell holds as its own bits.
template <class W>
concept AtomicWordType = AtomicBitsValue<W> && (std::is_integral_v<W> || std::is_enum_v<W> || std::is_pointer_v<W>);

// A type with a specialization of atomic_word.
template <class T>
concept HasAtomicWord = requires { sizeof(atomic_word<T>); };

// The members of a word protocol.  empty_word is a constant of the word
// type.  from_word takes the key of T, and no from_word takes the word
// alone.  The cell copies the value out on each read and destroys no value.
template <class T>
concept AtomicWordMembers =
    HasAtomicWord<T> && AtomicWordType<typename atomic_word<T>::word_type>
    && std::same_as<std::remove_cv_t<decltype(atomic_word<T>::empty_word)>, typename atomic_word<T>::word_type>
    && requires { typename std::bool_constant<(atomic_word<T>::empty_word == atomic_word<T>::empty_word)>; }
    && requires(T const& value, atomic_word_key<T> const& key, typename atomic_word<T>::word_type word) {
           { atomic_word<T>::to_word(value) } noexcept -> std::same_as<typename atomic_word<T>::word_type>;
           { atomic_word<T>::from_word(key, word) } noexcept -> std::same_as<T>;
       } && !requires(typename atomic_word<T>::word_type word) {
           atomic_word<T>::from_word(word);
       } && std::is_trivially_destructible_v<T> && std::is_nothrow_copy_constructible_v<T>;

// An Option whose payload declares a word.  The cell derives the word of the
// Option from the word of the payload.  IsOption of Choice.h holds for an
// Option<X>, and option_payload_t of Choice.h gives its X.
template <class T>
concept OptionOfWordShape = IsOption<T> && HasAtomicWord<option_payload_t<T>>;

}  // namespace detail

// A complete word protocol of a value that has no bits of its own.  An
// Option of a payload with a word takes the word of the payload, so the
// protocol refuses a specialization of atomic_word for that Option.
template <class T>
concept AtomicWordProtocol =
    detail::AtomicWordMembers<T> && !detail::AtomicBitsValue<T> && !detail::OptionOfWordShape<T>;

namespace detail {

// The three gates of a value.  Each value takes one road: the word that it
// declares, the word of the payload of an Option, or its own bits.  Each
// gate holds for a value of a different road, so the note of a refusal names
// the gate of the road of the value.

// A value that declares a word gives a complete protocol.
template <class T>
concept DeclaredWordIsComplete = !HasAtomicWord<T> || AtomicWordProtocol<T>;

// An Option of a payload that declares a word: the protocol of the payload
// is complete, and the Option copies and ends as a plain value does.
template <class T>
concept OptionOfWordIsComplete = HasAtomicWord<T> || !OptionOfWordShape<T>
                              || (AtomicWordProtocol<option_payload_t<T>> && !AtomicBitsValue<T>
                                  && std::is_trivially_destructible_v<T> && std::is_nothrow_copy_constructible_v<T>);

// A value with no word has bits of its own.
template <class T>
concept OtherValueHasOwnBits = HasAtomicWord<T> || OptionOfWordShape<T> || AtomicBitsValue<T>;

}  // namespace detail

// The value of an Atomic: an object type, not an array and not
// cv-qualified, that the cell holds through the word that it declares,
// through the word of the payload of an Option, or as its own bits.  Each
// value takes one of the three, so the cell gives each value one word.
template <class T>
concept AtomicValue = detail::AtomicObject<T> && detail::DeclaredWordIsComplete<T> && detail::OptionOfWordIsComplete<T>
                   && detail::OtherValueHasOwnBits<T>;

// An integral value that an addition, a subtraction, a maximum or a minimum
// can change.  A bool or an enum has no meaning for these operations.
template <class T>
concept AtomicCount = AtomicValue<T> && std::integral<T> && !std::same_as<T, bool>;

// An unsigned integral value that a bitwise operation can change.
template <class T>
concept AtomicBits = AtomicCount<T> && std::unsigned_integral<T>;

// The error of a compare-and-swap that stored nothing: the value that the
// cell held.  After a strong compare-and-swap it is not the expected value.
// A weak compare-and-swap can also fail when the cell holds the expected
// value, and the error then holds the expected value.
template <class T>
struct CasRefusal final {
    T observed;
};

template <AtomicValue T, class Writer = void>
class Atomic;

// The writer of a count that each thread adds to with no order (THE COUNT).
struct Counter final {};

namespace detail {

// The word of a value that the cell holds as its own bits.  An integer, an
// enum and a pointer go to the builtins as they are.  Each other value goes
// as the unsigned integer of its size, because the builtins take only those.
template <class T>
struct atomic_storage {
    using type = std::conditional_t<
        std::is_integral_v<T> || std::is_enum_v<T> || std::is_pointer_v<T>, T,
        std::conditional_t<sizeof(T) == 1, std::uint8_t,
                           std::conditional_t<sizeof(T) == 2, std::uint16_t,
                                              std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>>;
};

// The word of a value that declares a word.
template <class T>
    requires HasAtomicWord<T>
struct atomic_storage<T> {
    using type = typename atomic_word<T>::word_type;
};

// The word of an Option of a payload that declares a word: the word of the
// payload.
template <class T>
    requires(!HasAtomicWord<T> && OptionOfWordShape<T>)
struct atomic_storage<T> {
    using type = typename atomic_word<option_payload_t<T>>::word_type;
};

// The word of a value, and the value of a word.  Each value round-trips
// through its word.  The members are private, and only the cells of this
// family call them, so only a cell makes a key and a value from a word.
template <class T>
class atomic_codec {
    using storage_type = typename atomic_storage<T>::type;

    template <AtomicValue U, class Writer>
    friend class ::foundation::core::Atomic;

    [[nodiscard]] static constexpr storage_type to_storage_(T const& value) noexcept {
        if constexpr (HasAtomicWord<T>) {
            return atomic_word<T>::to_word(value);
        } else if constexpr (OptionOfWordShape<T>) {
            using Payload = option_payload_t<T>;
            return value.match([](Payload const& payload) noexcept { return payload_word_(payload); },
                               []() noexcept { return atomic_word<Payload>::empty_word; });
        } else if constexpr (std::same_as<storage_type, T>) {
            return value;
        } else {
            return __builtin_bit_cast(storage_type, value);
        }
    }

    [[nodiscard]] static constexpr T from_storage_(storage_type word) noexcept {
        if constexpr (HasAtomicWord<T>) {
            return atomic_word<T>::from_word(atomic_word_key<T>{}, word);
        } else if constexpr (OptionOfWordShape<T>) {
            using Payload = option_payload_t<T>;
            if (word == atomic_word<Payload>::empty_word) return T{};
            return T::some(atomic_word<Payload>::from_word(atomic_word_key<Payload>{}, word));
        } else if constexpr (std::same_as<storage_type, T>) {
            return word;
        } else {
            return __builtin_bit_cast(T, word);
        }
    }

    // The word of the payload of a full Option.  The empty word would read
    // back as an empty Option, so a protocol that gives it ends the process.
    template <class Payload>
    [[nodiscard]] static constexpr storage_type payload_word_(Payload const& payload) noexcept {
        storage_type const word = atomic_word<Payload>::to_word(payload);
        if (word == atomic_word<Payload>::empty_word) [[unlikely]] {
            fatal("atomic_word: to_word gave empty_word for the payload of a full Option, and the cell would read the "
                  "Option back as empty");
        }
        return word;
    }
};

}  // namespace detail

// The key of from_word (THE WORD OF A VALUE).  Only a cell of this family
// makes one, and the key of T opens only the from_word of T.  The protocol
// takes the key by reference, as the niche protocol of Choice.h takes its
// key, so nothing copies one.  Both constructors are user-provided and
// private, so std::bit_cast and a lifetime start over bytes build no key.
template <class T>
class [[nodiscard]] atomic_word_key final {
    template <class U>
    friend class detail::atomic_codec;

    constexpr atomic_word_key() noexcept {}
    constexpr atomic_word_key(atomic_word_key const&) noexcept {}

public:
    atomic_word_key& operator=(atomic_word_key const&) = delete("a word key lives for one call of the word protocol");
};

namespace detail {

// The role of a sole writer: an empty class that is not cv-qualified.  A role
// names a kind of cell, such as the head of a ring.
template <class Role>
concept SoleWriterRole =
    std::is_class_v<Role> && std::is_empty_v<Role> && !std::is_const_v<Role> && !std::is_volatile_v<Role>;

// The template arguments of mint_sole_writer: a role and no other argument.
// A second argument would name the brand of a writer that the caller holds.
template <class Role, class... Arguments>
concept SoleWriterMint = SoleWriterRole<Role> && sizeof...(Arguments) == 0;

}  // namespace detail

template <class Role, class Brand>
class SoleWriter;

// The one door to a sole writer.  The fresh brand is the last template
// parameter, after a pack, so an explicit second argument goes into the pack,
// and the constraint refuses it.  The result type holds the brand in
// undeducible_brand_t, so no type sets the brand by deduction.  An address of
// the mint with a target type that names a held brand then matches no
// specialization, and the declared type of an explicit specialization sets
// no brand, so no specialization becomes a friend of the key.  Each call
// takes the next run-time id of the process, with one atomic add, so the
// mint runs only at run time.
template <class Role, class... Arguments, class Brand = CRUCIBLE_FRESH_BRAND>
    requires detail::SoleWriterMint<Role, Arguments...>
[[nodiscard]] SoleWriter<Role, ::foundation::brand::undeducible_brand_t<Brand>> mint_sole_writer() noexcept;

// The key of the constructor of a sole writer.  Only mint_sole_writer makes
// one, and no explicit specialization of the mint matches its declaration.
// The two constructors are user-provided, so std::bit_cast and a lifetime
// start over bytes build no key.  The key stays in this namespace, because a
// friend declaration in a nested namespace would declare a different mint
// there.
class sole_writer_key final {
    constexpr sole_writer_key() noexcept {}

public:
    constexpr sole_writer_key(sole_writer_key const&) noexcept {}

private:
    template <class Role, class... Arguments, class Brand>
        requires detail::SoleWriterMint<Role, Arguments...>
    friend SoleWriter<Role, ::foundation::brand::undeducible_brand_t<Brand>> mint_sole_writer() noexcept;
};

// The proof of the one thread that writes a cell (THE CELL OF ONE WRITER).
// The token holds the run-time id of its mint, 8 bytes, and only the cells of
// this family read it.  A move gives the id to the target and leaves the id 0
// in the source.  No mint gives 0, and no cell takes a writer of 0, so a
// moved-from token authorizes no operation.  The move constructor is then
// user-provided, so the ABI passes a token by value in memory.  Each cell
// takes its writer by const reference, so the code of a cell does not change.
// The token is not trivially copyable, so std::bit_cast builds no token.  The
// annotation no_start_over_bytes refuses the checked lifetime start of
// foundation/Lifetime.h.
template <class Role, class Brand>
class [[nodiscard]][[= ::foundation::linear::linear{}]][[= ::foundation::linear::authority{}]]
                   [[= ::foundation::lifetime::no_start_over_bytes{}]] SoleWriter final {
    static_assert(detail::SoleWriterRole<Role>, "SoleWriter<Role, Brand>: the role names a kind of cell, such as the "
                                                "head of a ring.  It must be an empty class that is not cv-qualified.");
    static_assert(::foundation::brand::IsFreshBrand<Brand>,
                  "SoleWriter<Role, Brand>: the brand is the identity of one writer.  DefaultBrand would give each "
                  "writer of the role one type, so the writer of one cell could write a different cell.  Mint the "
                  "writer with mint_sole_writer<Role>().");

    // The run-time id of the mint of this token.  The cell of the writer
    // keeps the same id.
    std::uint64_t identity_ = 0;

    template <AtomicValue T, class Writer>
    friend class Atomic;

public:
    using brand_type = Brand;

    // A writer of the run-time id, with the key that only the mint makes.
    constexpr SoleWriter(sole_writer_key const& /*key*/, std::uint64_t identity) noexcept : identity_{identity} {}

    SoleWriter(SoleWriter const&) = delete("SoleWriter: a copy gives the cell a second writer, and load_owned then "
                                           "reads a value that a different thread stores.  Move the writer.");
    SoleWriter& operator=(SoleWriter const&) = delete("SoleWriter: a copy gives the cell a second writer.");

    constexpr SoleWriter(SoleWriter&& other) noexcept : identity_{other.identity_} { other.identity_ = 0; }

    // The source gives its id up before the target takes it, so a self move
    // keeps the id.
    constexpr SoleWriter& operator=(SoleWriter&& other) noexcept {
        std::uint64_t const taken = other.identity_;
        other.identity_ = 0;
        identity_ = taken;
        return *this;
    }
    ~SoleWriter() = default;

private:
    // A writer lives in a scope, a handle or a parameter.  A new-expression
    // gives it storage that no scope ends, and each copy of the pointer
    // reaches the one token.  The class operator new hides each global form,
    // and it is private, so it is no part of the public surface.
    static void* operator new(std::size_t) = delete("a SoleWriter lives in a scope or a handle, not on the heap");
    static void* operator new[](std::size_t) = delete("a SoleWriter lives in a scope or a handle, not on the heap");
    static void operator delete(void*) = delete;
    static void operator delete[](void*) = delete;
};

// The writer of a cell: void for a cell that each thread can write, Counter
// for a count that each thread adds to, or a SoleWriter for a cell of one
// writer.  Each step of a cell is atomic, so two threads read and write one
// cell at the same time (foundation/Transfer.h).
template <AtomicValue T, class Writer>
class[[= ::foundation::transfer::shareable{}]] Atomic : ::foundation::Pinned<Atomic<T, Writer>> {
    static_assert(std::is_void_v<Writer>,
                  "Atomic<T, Writer>: the writer is void, for a cell that each thread can write, Counter, for a "
                  "count that each thread adds to, or SoleWriter<Role, Brand>, for a cell of one writer.");

    using codec_type = detail::atomic_codec<T>;
    using storage_type = typename detail::atomic_storage<T>::type;

    alignas(sizeof(storage_type)) storage_type value_{};

    [[nodiscard]] static constexpr storage_type to_storage_(T const& value) noexcept {
        return codec_type::to_storage_(value);
    }

    [[nodiscard]] static constexpr T from_storage_(storage_type word) noexcept {
        return codec_type::from_storage_(word);
    }

public:
    using value_type = T;

    constexpr Atomic() noexcept
        requires std::is_nothrow_default_constructible_v<T>
        : value_{to_storage_(T{})} {}

    explicit constexpr Atomic(T initial) noexcept : value_{to_storage_(initial)} {}

    [[nodiscard]] T load_acquire() const noexcept { return from_storage_(__atomic_load_n(&value_, __ATOMIC_ACQUIRE)); }

    void store_release(T value) noexcept { __atomic_store_n(&value_, to_storage_(value), __ATOMIC_RELEASE); }

    [[nodiscard]] T exchange_acq_rel(T value) noexcept {
        return from_storage_(__atomic_exchange_n(&value_, to_storage_(value), __ATOMIC_ACQ_REL));
    }

    // A strong compare-and-swap: it fails only when the cell holds a value
    // other than expected.  A failure reads the cell with acquire order, and
    // its error holds the value that it read.
    [[nodiscard]] Result<Unit, CasRefusal<T>> cas_acq_rel(T expected, T desired) noexcept {
        storage_type observed = to_storage_(expected);
        if (__atomic_compare_exchange_n(&value_, &observed, to_storage_(desired), false, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            return Unit{};
        }
        return err(CasRefusal<T>{from_storage_(observed)});
    }

    // A weak compare-and-swap: it can fail when the cell holds expected, so
    // a caller runs it in a loop that reads the cell again.  On x86-64 it is
    // the same lock cmpxchg as the strong form.  On aarch64 it is one pair of
    // exclusive load and store, with no loop of its own.
    [[nodiscard]] Result<Unit, CasRefusal<T>> cas_weak_acq_rel(T expected, T desired) noexcept {
        storage_type observed = to_storage_(expected);
        if (__atomic_compare_exchange_n(&value_, &observed, to_storage_(desired), true, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            return Unit{};
        }
        return err(CasRefusal<T>{from_storage_(observed)});
    }

    // Unsigned arithmetic wraps, and signed arithmetic of an atomic
    // operation wraps too.
    [[nodiscard]] T fetch_add_acq_rel(T delta) noexcept
        requires AtomicCount<T>
    {
        return __atomic_fetch_add(&value_, delta, __ATOMIC_ACQ_REL);
    }

    [[nodiscard]] T fetch_sub_acq_rel(T delta) noexcept
        requires AtomicCount<T>
    {
        return __atomic_fetch_sub(&value_, delta, __ATOMIC_ACQ_REL);
    }

    [[nodiscard]] T fetch_or_acq_rel(T bits) noexcept
        requires AtomicBits<T>
    {
        return __atomic_fetch_or(&value_, bits, __ATOMIC_ACQ_REL);
    }

    [[nodiscard]] T fetch_and_acq_rel(T bits) noexcept
        requires AtomicBits<T>
    {
        return __atomic_fetch_and(&value_, bits, __ATOMIC_ACQ_REL);
    }

    [[nodiscard]] T fetch_xor_acq_rel(T bits) noexcept
        requires AtomicBits<T>
    {
        return __atomic_fetch_xor(&value_, bits, __ATOMIC_ACQ_REL);
    }

    // GCC has no builtin for a maximum or a minimum, so each one is a loop
    // of compare-and-swap operations, as the fallback of std::atomic is.
    // A value that does not change the cell writes nothing, and the
    // operation is then an acquire load.
    [[nodiscard]] T fetch_max_acq_rel(T value) noexcept
        requires AtomicCount<T>
    {
        T observed = __atomic_load_n(&value_, __ATOMIC_ACQUIRE);
        while (observed < value
               && !__atomic_compare_exchange_n(&value_, &observed, value, true, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {}
        return observed;
    }

    [[nodiscard]] T fetch_min_acq_rel(T value) noexcept
        requires AtomicCount<T>
    {
        T observed = __atomic_load_n(&value_, __ATOMIC_ACQUIRE);
        while (value < observed
               && !__atomic_compare_exchange_n(&value_, &observed, value, true, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {}
        return observed;
    }
};

namespace detail {

// The counter of the run-time ids of the process (misc/12_10_2026_linear.md,
// phase B).  mint_sole_writer() and mint_identity() of
// foundation/core/Identity.h take its next value, so no two mints give one
// id.  It is one object for the process (CRUCIBLE_PROCESS_WIDE), also when two
// shared libraries include this header, and it starts at 1.  A counter of 64
// bits does not wrap: at one mint in each nanosecond, it gets to 2^64 after
// 584 years.
CRUCIBLE_PROCESS_WIDE inline constinit Atomic<std::uint64_t> next_identity_{1};

}  // namespace detail

template <class Role, class... Arguments, class Brand>
    requires detail::SoleWriterMint<Role, Arguments...>
[[nodiscard]] SoleWriter<Role, ::foundation::brand::undeducible_brand_t<Brand>> mint_sole_writer() noexcept {
    return SoleWriter<Role, Brand>{sole_writer_key{}, detail::next_identity_.fetch_add_acq_rel(1)};
}

// The cell of one writer (THE CELL OF ONE WRITER).  The parameter of the
// brand has the name Identity, so the rule erased-brand reads no brand of the
// primary template here.
template <AtomicValue T, class Role, class Identity>
class[[= ::foundation::transfer::shareable{}]]
    Atomic<T, SoleWriter<Role, Identity>> : ::foundation::Pinned<Atomic<T, SoleWriter<Role, Identity>>> {
    static_assert(detail::SoleWriterRole<Role>, "Atomic<T, SoleWriter<Role, Brand>>: the role names a kind of cell.  "
                                                "It must be an empty class that is not cv-qualified.");
    static_assert(::foundation::brand::IsFreshBrand<Identity>,
                  "Atomic<T, SoleWriter<Role, Brand>>: the brand is the identity of one writer.  DefaultBrand would "
                  "give each writer of the role one type, so the writer of one cell could write a different cell.");

    using codec_type = detail::atomic_codec<T>;
    using storage_type = typename detail::atomic_storage<T>::type;

    alignas(sizeof(storage_type)) storage_type value_{};
    // The run-time id of the writer that the constructor took.
    std::uint64_t writer_identity_ = 0;

    // The id of the writer that builds the cell.  A moved-from writer holds 0,
    // and a cell of 0 would take each moved-from writer, so the build ends the
    // process.
    [[nodiscard]] static constexpr std::uint64_t builder_identity_(SoleWriter<Role, Identity> const& writer) noexcept {
        if (writer.identity_ == 0) [[unlikely]] {
            fatal("Atomic<T, SoleWriter<Role, Brand>>: a moved-from writer built the cell.  A move gives the id of the "
                  "writer to its target, so build the cell with the target.");
        }
        return writer.identity_;
    }

    // Each read and each write of the writer shows a proof of the id of the
    // cell.  A proof of a different id comes from a different run of the mint
    // site of the writer, such as a different turn of a loop.  A moved-from
    // writer holds 0, which no cell holds.
    void expect_writer_(SoleWriter<Role, Identity> const& writer) const noexcept {
        if (writer.identity_ != writer_identity_) [[unlikely]] {
            fatal("Atomic<T, SoleWriter<Role, Brand>>: a writer of a different run of the mint site used the cell.  "
                  "Each run of one mint site gives one brand, and the cell keeps the run-time id of the writer that "
                  "built it.");
        }
    }

public:
    using value_type = T;

    explicit constexpr Atomic(SoleWriter<Role, Identity> const& writer) noexcept
        requires std::is_nothrow_default_constructible_v<T>
        : value_{codec_type::to_storage_(T{})}, writer_identity_{builder_identity_(writer)} {}

    constexpr Atomic(SoleWriter<Role, Identity> const& writer, T initial) noexcept
        : value_{codec_type::to_storage_(initial)}, writer_identity_{builder_identity_(writer)} {}

    // A read on each thread, with acquire order.  It pairs with each
    // store_release of the writer.
    [[nodiscard]] T load_acquire() const noexcept {
        return codec_type::from_storage_(__atomic_load_n(&value_, __ATOMIC_ACQUIRE));
    }

    // The read of the writer.  Each value of the cell is a value that the
    // holder of the proof stored, so the read needs no order.
    [[nodiscard]] T load_owned(SoleWriter<Role, Identity> const& writer) const noexcept {
        expect_writer_(writer);
        return codec_type::from_storage_(__atomic_load_n(&value_, __ATOMIC_RELAXED));
    }

    void store_release(SoleWriter<Role, Identity> const& writer, T value) noexcept {
        expect_writer_(writer);
        __atomic_store_n(&value_, codec_type::to_storage_(value), __ATOMIC_RELEASE);
    }
};

// The cell of a count (THE COUNT).  An integer is its own word, so the cell
// holds the value with no codec.
template <AtomicValue T>
class[[= ::foundation::transfer::shareable{}]] Atomic<T, Counter> : ::foundation::Pinned<Atomic<T, Counter>> {
    static_assert(AtomicCount<T>, "Atomic<T, Counter>: a count is an integer that is not bool, because only an "
                                  "integer has an addition.");

    alignas(sizeof(T)) T value_{};

public:
    using value_type = T;

    constexpr Atomic() noexcept = default;

    explicit constexpr Atomic(T initial) noexcept : value_{initial} {}

    [[nodiscard]] T load_acquire() const noexcept { return __atomic_load_n(&value_, __ATOMIC_ACQUIRE); }

    // Unsigned arithmetic wraps, and signed arithmetic of an atomic
    // operation wraps too.
    [[nodiscard]] T fetch_add_relaxed(T delta) noexcept { return __atomic_fetch_add(&value_, delta, __ATOMIC_RELAXED); }

    [[nodiscard]] T fetch_sub_relaxed(T delta) noexcept { return __atomic_fetch_sub(&value_, delta, __ATOMIC_RELAXED); }

    // A loop of weak compare-and-swap operations, as fetch_max_acq_rel is.
    // A value that does not change the count writes nothing.
    [[nodiscard]] T fetch_max_relaxed(T value) noexcept {
        T observed = __atomic_load_n(&value_, __ATOMIC_RELAXED);
        while (observed < value
               && !__atomic_compare_exchange_n(&value_, &observed, value, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
        return observed;
    }

    [[nodiscard]] T fetch_min_relaxed(T value) noexcept {
        T observed = __atomic_load_n(&value_, __ATOMIC_RELAXED);
        while (value < observed
               && !__atomic_compare_exchange_n(&value_, &observed, value, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
        return observed;
    }
};

// A cell alone on its cache lines.  The platform floor of
// foundation/Platform.h holds the line at 64 bytes.
template <class T>
concept CacheLineCell = std::is_object_v<T> && !std::is_array_v<T>;

// A CacheLine always holds its cell, so *line and line->member are total.
// A borrow of a temporary line dangles at the end of the full expression,
// so the rvalue forms are deleted.
template <CacheLineCell T>
class alignas(64) CacheLine {
    T cell_;

public:
    template <class... Args>
        requires std::constructible_from<T, Args&&...>
    explicit constexpr CacheLine(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args&&...>)
        : cell_(static_cast<Args&&>(args)...) {}

    [[nodiscard]] constexpr T& operator*() & noexcept { return cell_; }
    [[nodiscard]] constexpr T const& operator*() const& noexcept { return cell_; }
    [[nodiscard]] constexpr T* operator->() & noexcept { return __builtin_addressof(cell_); }
    [[nodiscard]] constexpr T const* operator->() const& noexcept { return __builtin_addressof(cell_); }

    T& operator*() && = delete("a borrow of a temporary CacheLine dangles at the end of the full expression");
    T const&
    operator*() const&& = delete("a borrow of a temporary CacheLine dangles at the end of the full expression");
    T* operator->() && = delete("a borrow of a temporary CacheLine dangles at the end of the full expression");
    T const*
    operator->() const&& = delete("a borrow of a temporary CacheLine dangles at the end of the full expression");
};

class Tally : ::foundation::Pinned<Tally> {
    Atomic<std::uint64_t, Counter> count_{};

public:
    constexpr Tally() noexcept = default;

    void add(std::uint64_t amount) noexcept { static_cast<void>(count_.fetch_add_relaxed(amount)); }

    [[nodiscard]] std::uint64_t read() const noexcept { return count_.load_acquire(); }
};

// The fence of the store-load order (THE FENCE).
CRUCIBLE_INLINE void fence_seq_cst() noexcept { __atomic_thread_fence(__ATOMIC_SEQ_CST); }

}  // namespace foundation::core
