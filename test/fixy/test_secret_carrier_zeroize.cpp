// Each secret carrier of the base leaves no byte of its secret in a dead
// object.  A move gives the secret to the target and zeroizes the source,
// and the end of a carrier zeroizes its storage.
//
// THE ROSTER
// The carriers of the base are Secret<T>, DeclassifyOnSend<T, Policy> and
// CTPayload<T>.  The test runs each carrier through three hazards:
//
//   move        the source of a move construction holds zero bytes
//   assignment  the source of a move assignment holds zero bytes
//   end         the storage of a carrier holds zero bytes after its end
//
// A Secret has one more door, transform, and one more hazard:
//
//   transform   the callable reads the payload in the storage of the Secret,
//               so the call makes no copy of the payload, and the storage
//               holds zero bytes after the call
//
// The probe reaches a moved-from carrier through a reference, which the rule
// use-after-move does not read, as the owner probes of the base do.  The
// probe of the end builds a carrier in a buffer of bytes that outlives it,
// ends the carrier, and reads the buffer.  The probe first reads the secret
// in the buffer, so a buffer that a carrier never filled does not pass.

#include <fixy/Core.h>
#include <fixy/Secret.h>
#include <fixy/session/Classified.h>
#include <foundation/Quarantine.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace {

namespace secret_policy = ::fixy::tags::secret_policy;

// The bytes of a key.  Each byte of the pattern is above zero.
struct KeyBytes {
    ::fixy::FixedArray<std::byte, 16> bytes{};
};

struct[[= ::fixy::session::constant_time_value{}]] AuthTag {
    ::fixy::FixedArray<std::byte, 16> bytes{};
};

template <class Bytes>
[[nodiscard]] Bytes pattern() noexcept {
    Bytes value{};
    value.bytes.with_mut([](auto run) noexcept {
        std::size_t position = 0;
        for (std::byte& byte : run) {
            byte = static_cast<std::byte>(0xA0u + position);
            ++position;
        }
    });
    return value;
}

template <class Bytes>
[[nodiscard]] bool is_zero(Bytes const& value) noexcept {
    for (std::byte const byte : value.bytes) {
        if (byte != std::byte{0}) return false;
    }
    return true;
}

template <class Bytes>
[[nodiscard]] bool is_pattern(Bytes const& value) noexcept {
    std::size_t position = 0;
    for (std::byte const byte : value.bytes) {
        if (byte != static_cast<std::byte>(0xA0u + position)) return false;
        ++position;
    }
    return true;
}

// The carrier that a call gives back by reference.  The rule use-after-move
// does not read through it, so a hazard reads a moved-from carrier.
template <class Carrier>
[[nodiscard]] Carrier& through(Carrier& carrier) noexcept {
    return carrier;
}

int failures = 0;

void expect(bool is_held, ::fixy::TextView what) noexcept {
    if (is_held) return;
    ::fixy::report(::fixy::Sink::Err, "  FAILED: {}\n", what);
    ++failures;
}

// The payload of each carrier, read through the one exit of the carrier.
[[nodiscard]] KeyBytes payload_of(::fixy::Secret<KeyBytes>&& carrier) noexcept {
    return std::move(carrier).declassify<secret_policy::HashForCompare>();
}
[[nodiscard]] KeyBytes
payload_of(::fixy::session::DeclassifyOnSend<KeyBytes, secret_policy::WireSerialize>&& carrier) noexcept {
    return std::move(carrier).declassify_for_wire();
}
[[nodiscard]] AuthTag payload_of(::fixy::session::CTPayload<AuthTag>&& carrier) noexcept {
    return std::move(carrier).declassify_ct<secret_policy::WireSerialize>();
}

// One carrier of each kind, with the pattern as its secret.
struct SecretKind {
    using Carrier = ::fixy::Secret<KeyBytes>;
    static constexpr ::fixy::TextView name{"Secret"};
    [[nodiscard]] static Carrier make() noexcept { return ::fixy::mint_secret<KeyBytes>(pattern<KeyBytes>()); }
};

struct DeclassifyOnSendKind {
    using Carrier = ::fixy::session::DeclassifyOnSend<KeyBytes, secret_policy::WireSerialize>;
    static constexpr ::fixy::TextView name{"DeclassifyOnSend"};
    [[nodiscard]] static Carrier make() noexcept { return Carrier{::fixy::mint_secret<KeyBytes>(pattern<KeyBytes>())}; }
};

struct CTPayloadKind {
    using Carrier = ::fixy::session::CTPayload<AuthTag>;
    static constexpr ::fixy::TextView name{"CTPayload"};
    [[nodiscard]] static Carrier make() noexcept { return Carrier{pattern<AuthTag>()}; }
};

// The move and the end of each carrier run the zeroize, so none of the three
// is trivial, and none of them throws.
template <class Carrier>
inline constexpr bool zeroizes_on_move_and_end =
    !std::is_trivially_move_constructible_v<Carrier> && !std::is_trivially_move_assignable_v<Carrier>
    && !std::is_trivially_destructible_v<Carrier> && std::is_nothrow_move_constructible_v<Carrier>
    && std::is_nothrow_move_assignable_v<Carrier> && std::is_nothrow_destructible_v<Carrier>;

static_assert(zeroizes_on_move_and_end<SecretKind::Carrier>);
static_assert(zeroizes_on_move_and_end<DeclassifyOnSendKind::Carrier>);
static_assert(zeroizes_on_move_and_end<CTPayloadKind::Carrier>);

template <class Kind>
void check_move() noexcept {
    typename Kind::Carrier source = Kind::make();
    typename Kind::Carrier target{std::move(through(source))};
    expect(is_pattern(payload_of(std::move(target))), "the target of a move holds the secret");
    expect(is_zero(payload_of(std::move(through(source)))), "the source of a move holds zero bytes");
}

template <class Kind>
void check_assignment() noexcept {
    typename Kind::Carrier source = Kind::make();
    typename Kind::Carrier target = Kind::make();
    target = std::move(through(source));
    expect(is_pattern(payload_of(std::move(target))), "the target of a move assignment holds the secret");
    expect(is_zero(payload_of(std::move(through(source)))), "the source of a move assignment holds zero bytes");
}

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a secret carrier leaves no byte of its secret in its buffer after its end")
// The storage of a carrier after its end: 1 when each byte is zero, 0 when
// a byte of the secret stays, and 2 when the buffer did not hold the secret
// before the end.  The reads are volatile, so the optimizer reads each byte
// from memory.
template <class Kind>
[[nodiscard]] int storage_after_end() noexcept {
    using Carrier = typename Kind::Carrier;
    alignas(Carrier) unsigned char storage[sizeof(Carrier)] = {};
    Carrier* const carrier = ::new(static_cast<void*>(storage)) Carrier(Kind::make());
    volatile unsigned char const* const bytes = storage;
    bool is_filled = false;
    for (std::size_t index = 0; index < sizeof(Carrier); ++index) {
        is_filled = is_filled || bytes[index] != 0;
    }
    carrier->~Carrier();
    if (!is_filled) return 2;
    for (std::size_t index = 0; index < sizeof(Carrier); ++index) {
        if (bytes[index] != 0) return 0;
    }
    return 1;
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The facts of one transform of a Secret in a buffer of bytes.
struct TransformFacts {
    bool is_payload = false;
    bool is_in_storage = false;
    bool is_zero_after = false;
    int derived = 0;
};

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the callable of a transform reads the payload in the buffer of the Secret")
// Transforms a Secret in a buffer of bytes with a callable that takes the
// payload as Param.  The callable records where its parameter lives, and the
// probe reads the buffer after the call, before the end of the Secret.
template <class Param>
[[nodiscard]] TransformFacts storage_after_transform() noexcept {
    using Carrier = SecretKind::Carrier;
    alignas(Carrier) unsigned char storage[sizeof(Carrier)] = {};
    Carrier* const carrier = ::new(static_cast<void*>(storage)) Carrier(SecretKind::make());
    std::uintptr_t const first = std::bit_cast<std::uintptr_t>(static_cast<unsigned char*>(storage));
    std::uintptr_t const last = first + sizeof(Carrier);
    TransformFacts facts{};
    ::fixy::Secret<int> derived = std::move(*carrier).transform([&facts, first, last](Param payload) noexcept {
        std::uintptr_t const address = std::bit_cast<std::uintptr_t>(&payload);
        facts.is_in_storage = address >= first && address + sizeof(KeyBytes) <= last;
        facts.is_payload = is_pattern(payload);
        return 7;
    });
    volatile unsigned char const* const bytes = storage;
    facts.is_zero_after = true;
    for (std::size_t index = 0; index < sizeof(Carrier); ++index) {
        facts.is_zero_after = facts.is_zero_after && bytes[index] == 0;
    }
    carrier->~Carrier();
    facts.derived = std::move(derived).declassify<secret_policy::HashForCompare>();
    return facts;
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// A payload that counts each copy and each move of itself.  A payload of
// bytes leaves no trace of a copy, so the count shows the route of a value:
// each copy or move is one more object that holds the bytes of the secret.
struct CountedPayload {
    static inline int copies_and_moves = 0;
    int value = 0;

    constexpr explicit CountedPayload(int initial) noexcept : value{initial} {}
    CountedPayload(CountedPayload const& other) noexcept : value{other.value} { ++copies_and_moves; }
    CountedPayload(CountedPayload&& other) noexcept : value{other.value} { ++copies_and_moves; }
    CountedPayload& operator=(CountedPayload const&) = delete("the probe counts constructions only");
    CountedPayload& operator=(CountedPayload&&) = delete("the probe counts constructions only");
    ~CountedPayload() = default;
};

// The mint builds the payload in its place in the Secret, and the transform
// builds the result of its callable in its place in the derived Secret.  So
// no parameter and no local holds a copy of either payload.
void check_payload_route() noexcept {
    CountedPayload::copies_and_moves = 0;
    ::fixy::Secret<CountedPayload> minted = ::fixy::mint_secret<CountedPayload>(7);
    expect(CountedPayload::copies_and_moves == 0, "the mint of a Secret makes no copy and no move of the payload");

    CountedPayload::copies_and_moves = 0;
    ::fixy::Secret<CountedPayload> derived = std::move(minted).transform(
        [](CountedPayload const& payload) noexcept { return CountedPayload{payload.value + 1}; });
    expect(CountedPayload::copies_and_moves == 0,
           "the transform of a Secret builds the result in the derived Secret, with no copy and no move");
    static_cast<void>(std::move(derived).declassify<secret_policy::HashForCompare>());
}

template <class Param>
void check_transform() noexcept {
    TransformFacts const facts = storage_after_transform<Param>();
    expect(facts.is_payload, "the callable of a transform reads the secret");
    expect(facts.is_in_storage, "the callable of a transform reads the payload in the storage of the Secret");
    expect(facts.is_zero_after, "the storage of a Secret holds zero bytes after its transform");
    expect(facts.derived == 7, "the transform gives the value of the callable");
}

template <class Kind>
void check_carrier() noexcept {
    ::fixy::report(::fixy::Sink::Err, "[{}]\n", Kind::name);
    check_move<Kind>();
    check_assignment<Kind>();
    int const end = storage_after_end<Kind>();
    expect(end != 2, "the buffer of the carrier holds the secret before the end");
    expect(end == 1, "the buffer of the carrier holds zero bytes after the end");
}

}  // namespace

int main() {
    check_carrier<SecretKind>();
    check_carrier<DeclassifyOnSendKind>();
    check_carrier<CTPayloadKind>();
    ::fixy::report(::fixy::Sink::Err, "[Secret transform]\n");
    check_transform<KeyBytes&&>();
    check_transform<KeyBytes const&>();
    ::fixy::report(::fixy::Sink::Err, "[Secret payload route]\n");
    check_payload_route();
    if (failures != 0) {
        ::fixy::report(::fixy::Sink::Err, "test_secret_carrier_zeroize: {} failed\n", failures);
        return 1;
    }
    ::fixy::report(::fixy::Sink::Err, "test_secret_carrier_zeroize: all passed\n");
    return 0;
}
