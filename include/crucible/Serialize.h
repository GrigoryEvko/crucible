#pragma once

// The wire format is a 32-byte header followed by a flat payload. The header
// is a 4-byte magic, a 4-byte version, a 1-byte node kind, 7 zero pad bytes,
// an 8-byte merkle hash and an 8-byte content hash.  A region image ends with
// a 32-byte checksum of each byte before it (the checksum of a region image).
//
// Nothing in the format is a raw pointer, so a loaded image is independent of
// where it was written. A tensor's data pointer is written as zero, and a
// branch arm's target is written as the target's merkle hash and resolved
// through a caller-supplied lookup at load time.

#include <crucible/CKernelId.h>
#include <crucible/LoadError.h>
#include <crucible/MerkleDag.h>
#include <crucible/PoolAllocator.h>
#include <crucible/TraceRing.h>
#include <crucible/ValidatedOp.h>
#include <foundation/core/KeyedHash.h>
#include <fixy/Core.h>
#include <fixy/Protocol.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace crucible {

// The four bytes spell "CDAG" most significant first. A little-endian host
// stores them in the opposite order, so a hex dump of the file starts with
// 47 41 44 43, which prints as "GADC". Both spellings have been written down
// wrongly before; the value is what the reader compares, not the spelling.
static constexpr uint32_t CDAG_MAGIC = 0x43444147u;
using CdagFormatVersion = ::fixy::Tagged<uint32_t, ::fixy::tags::source::FormatVersion>;
using ExternalCdagVersion = ::fixy::Tagged<uint32_t, ::fixy::tags::source::External>;
// Loaded is an earned tag.  deserialize_region builds the node from Cipher
// bytes, so the node is Replayed first.  It becomes Loaded only after the
// fold of its operations matches the content hash that the image records,
// which is the discharge of the Replayed to Loaded edge.  The node is in the
// arena of the load, and a loaded value always holds a node, so the tag holds
// a Ref and not an address that can be null.
using LoadedRegionNode = ::fixy::Tagged<DagRef<RegionNode>, ::fixy::tags::source::Loaded>;
// 10 (2026-09-15): the content-hash fold changed mixer.  Every region on
// disk carries a content hash the current code no longer computes, so a
// version-9 file would deserialize into regions whose hashes disagree with
// their own contents and whose KernelCache entries key on nothing.  The
// bump makes cdag_version_matches reject those files outright.
// 11 (2026-11-13): the storage size of a tensor has 64 bits on the wire, so
// a storage of 4 GiB or more keeps its size.
// 12 (2026-11-16): each op has its nesting depth on the wire, after its kernel
// id, so a region of the store keeps the ops that a lowering takes.
// 13 (2026-11-16): each op has the tags of its scalars on the wire, after its
// depth, and the content hash folds them.
// 14 (2026-11-16): each region has the hash of its numerics policy on the wire,
// after its first schema, and the content hash folds it.
// 15 (2026-11-16): the shape hash of each op is its guard word, which folds
// the depth, the counts, the tags, the strides, the element types and the
// scalars.  A region of version 14 holds words that no live op gives, so it
// would diverge at its first op.  The bump refuses it at the load.
// 16 (2026-11-16): the word of each tensor in the content hash and in the guard
// word folds its rank, its layout, its negative and conjugate bits and its
// storage offset (pack_tensor_word).  A region of version 15 holds a content
// hash and guard words that the current code no longer computes.
// 17 (2026-11-17): the word of the tags of each op holds its data slots too
// (ScalarSlotKinds), and the guard word and the content hash read the tag of
// a data slot but not its value.  A region of version 16 holds a content hash
// and guard words that the current code no longer computes.
// 18 (2026-11-18): the hash of the Protocol family folds the blocks of the
// image with the step of foldhash-f, so the hash of each guard, the merkle
// hash of each branch, the signature of the feedback edges and the hash of a
// loop term change.  And each tensor descriptor goes on the wire as its image
// of the family, with no data pointer and no hash of the grad_fn: 16 bytes
// less for each tensor.  A region of version 17 holds other bytes and other
// hashes.
// 19 (2026-11-20): each region image ends with a checksum of each byte
// before it.  The content hash folds only the ops and the numerics, so a
// flipped bit of the merkle hash, the measured time, the variant or the plan
// loaded as a sound image.  A region of version 18 has no checksum, and the
// version refuses it.  The image of a branch changes only its version word.
static constexpr CdagFormatVersion CDAG_VERSION =
    ::fixy::mint_tagged<::fixy::tags::source::FormatVersion>(uint32_t{19});

[[nodiscard]] constexpr bool cdag_version_matches(ExternalCdagVersion disk_version) noexcept {
    return disk_version.value() == CDAG_VERSION.value();
}

// Ceilings on the counts a header may declare, an order of magnitude above
// anything a real trace reaches. A header is rejected against them before a
// single byte of the body is trusted, so a fabricated count cannot provoke a
// terabyte-scale allocation that only fails once the body turns out truncated.
static constexpr uint32_t CDAG_MAX_OPS = 1u << 22;
// The slot ceiling is the allocator's own, not a slack figure: a loaded plan
// goes straight into the allocator, whose matching precondition is only an
// assumption in a release build. Deriving the two from one constant makes
// "it deserialised" imply "it is safe to initialise" by construction, where a
// looser wire ceiling would admit counts the allocator cannot serve.
static constexpr uint32_t CDAG_MAX_SLOTS = ::crucible::PoolAllocator::kMaxNumSlots;
// The counts of an op are the counts of the entry of the ring that it came
// from: a region holds only ops that the ring recorded, and the build of a
// region copies each count of its entry.  So an op of an image holds at most
// kMaxOpInputs inputs, kMaxOpOutputs outputs and kScalarSlots scalars, the
// limits of the one validator of an entry (crucible/ValidatedOp.h).
static constexpr uint32_t CDAG_MAX_BRANCH_ARMS = 1u << 16;

namespace detail_ser {

// The writer of an image into the run of a frame.  It writes each value as
// its bytes, the low byte first, which is the layout that the reader reads.
// The writer holds the ViewMut of the frame, so it is the one writer of the
// run, and the brand keeps it in the frame body.  A write past the end of the
// image clears has_room, and the serialize function then gives zero.
template <class Brand, std::size_t Extent>
struct Writer {
    ::fixy::ViewMut<std::byte, Brand, Extent> image;
    size_t pos = 0;
    bool has_room = true;

    // Gives write the Count bytes at pos, and moves pos past them.  A
    // reborrow lends the run to the window, and the run comes back to image
    // after the write.  When the bytes do not fit, the function clears
    // has_room and writes nothing.  Complexity: O(1), and the cost of write.
    template <size_t Count, class Write>
    void write_window(Write const& write) noexcept {
        const size_t at = pos;
        bool is_written = false;
        image = ::fixy::reborrow(std::move(image), [at, &write, &is_written](auto lent) noexcept {
            ::fixy::take_window<Count>(std::move(lent), at)
                .match(
                    [&write, &is_written](auto window) noexcept {
                        write(std::move(window));
                        is_written = true;
                    },
                    []() noexcept {});
        });
        if (is_written) {
            pos += Count;
        } else {
            has_room = false;
        }
    }

    // One unsigned word as its sizeof(Word) bytes.  Complexity:
    // O(sizeof(Word)).
    template <::fixy::UnsignedInteger Word>
    void write_word(Word word) noexcept {
        write_window<sizeof(Word)>([word](auto bytes) noexcept {
            size_t shift = 0;
            for (std::byte& slot : bytes) {
                slot = static_cast<std::byte>(static_cast<uint8_t>(word >> shift));
                shift += 8;
            }
        });
    }

    // An integer, a bool, an enumerator or a float.  An enumerator gives its
    // underlying integer, and a bool gives one byte of 0 or 1.  A signed
    // integer and a float give the bits of the unsigned word of their size.
    template <typename T>
    void w(T value) {
        if constexpr (std::is_enum_v<T>) {
            w(std::to_underlying(value));
        } else if constexpr (std::is_same_v<T, bool>) {
            write_word(static_cast<uint8_t>(value ? 1 : 0));
        } else if constexpr (std::is_same_v<T, float>) {
            write_word(::fixy::bit_cast<uint32_t>(value));
        } else if constexpr (std::is_signed_v<T>) {
            write_word(::fixy::bit_cast<std::make_unsigned_t<T>>(value));
        } else {
            write_word(value);
        }
    }

    // N bytes of zero, which is the one value of a pad byte.
    template <size_t N>
    void write_zero_pad() {
        for (size_t index = 0; index < N; ++index) {
            write_word(uint8_t{0});
        }
    }

    // The image of a record of the Protocol family: each field in
    // declaration order, the low byte first, with no padding byte.
    // Complexity: O(encoded_size_v<T>).
    template <::fixy::Encodable T>
    void write_image(T const& value) noexcept {
        write_window<::fixy::encoded_size_v<T>>(
            [&value](auto window) noexcept { ::fixy::encode(value, std::move(window)); });
    }
};

// The reader of an image.  It reads each value from its bytes, the low byte
// first, as the writer laid them out on a little-endian host.  A read past
// the end of the image records the reason truncated and gives zero, and the
// parse that follows it is discarded.
struct Reader {
    ::fixy::View<const std::byte> image{};
    size_t pos = 0;
    // The first reason that a read refused the image, or none.  A later
    // refusal keeps the first reason, so the loader reports the first fault.
    // After a refusal the reads go on and give zeros or substitutes, and a
    // check of such a value would name a wrong reason.  So each refusal of
    // the loader reports this reason when it holds one.
    ::fixy::Option<LoadError> refusal{};

    void refuse(LoadError reason) noexcept {
        if (refusal.is_none()) refusal = ::fixy::Option<LoadError>::some(reason);
    }

    // The recorded reason, or reason when the reads refused nothing.
    [[nodiscard]] LoadError reason_or(LoadError reason) const noexcept {
        return ::fixy::Option<LoadError>{refusal}.value_or(reason);
    }

    // The next sizeof(Word) bytes as one unsigned word.  Complexity:
    // O(sizeof(Word)).
    template <::fixy::UnsignedInteger Word>
    [[nodiscard]] Word read_word() {
        return image.template window<sizeof(Word)>(pos).match(
            [this](::fixy::View<const std::byte, sizeof(Word)> bytes) noexcept {
                Word word = 0;
                for (const auto index : bytes.indices()) {
                    word = static_cast<Word>(word | (static_cast<Word>(bytes.at(index)) << (8 * index.raw())));
                }
                pos += sizeof(Word);
                return word;
            },
            [this] noexcept {
                refuse(LoadError::truncated);
                return Word{0};
            });
    }

    // An integer or a float of the image.  A signed integer and a float take
    // the bits of the unsigned word of their size.
    template <typename T>
    [[nodiscard]] T r() {
        if constexpr (std::is_same_v<T, float>) {
            return ::fixy::bit_cast<float>(read_word<uint32_t>());
        } else if constexpr (std::is_signed_v<T>) {
            return ::fixy::bit_cast<T>(read_word<std::make_unsigned_t<T>>());
        } else {
            return read_word<T>();
        }
    }

    // The real runtime branch behind every validated read in this file.
    //
    // A refinement type constructed straight from a wire byte is not enough
    // on its own. Its precondition is a genuine check only while contracts
    // are enforced. In a release build it degrades to a promise handed to the
    // optimiser, and a byte that breaks the predicate then becomes a lie the
    // optimiser is entitled to act on. So untrusted input is rejected here
    // first, by a plain predicate call that no build configuration removes,
    // and the refinement that follows it is a second, typed check that holds
    // by construction.
    //
    // A rejected read yields the substitute rather than the offending value.
    // The caller passes the result to a refinement constructor, and handing
    // that constructor a value its own precondition rejects would abort while
    // contracts are enforced and pass silently otherwise. Returning a value
    // the predicate accepts makes the outcome the same either way: the failure
    // travels as the refusal field_out_of_range, which every entry point
    // turns into its error, and the substituted value is discarded with the
    // rest of the parse. A value that passes is returned unchanged, so the
    // bytes a good image produces are untouched.
    //
    // Every value is judged, also after a refusal. An earlier refused read
    // leaves the bytes that follow it real, so a read after it returns a
    // wire byte, and the refinement that receives it assumes its predicate in
    // a release build. Each caller passes a substitute that its predicate
    // accepts.
    template <typename T, typename Pred>
    [[nodiscard]] T read_gated(Pred pred, T valid_substitute = T{0}) {
        const T v = r<T>();
        if (!pred(v)) [[unlikely]] {
            refuse(LoadError::field_out_of_range);
            return valid_substitute;
        }
        return v;
    }

    // A bool takes one byte, and only 0 and 1 are values of it.  Copying any
    // other byte into a bool makes a value that the program cannot read, so
    // the byte is judged as an integer first.
    [[nodiscard]] bool read_bool() {
        return read_gated<uint8_t>([](uint8_t byte) noexcept { return byte <= 1; }) != 0;
    }

    // An enumerator of a closed E.  fixy::enum_from gives the enumerator of
    // a byte, so a new enumerator needs no edit here.  A byte that names no
    // enumerator is refused, and none reaches a switch.  The caller gives an
    // enumerator as the substitute of a refused byte.
    template <::fixy::ClosedEnumeration E>
    [[nodiscard]] E read_enumerator(E valid_substitute) {
        return ::fixy::enum_from<E>(r<std::underlying_type_t<E>>())
            .match([](E value) noexcept { return value; },
                   [this, valid_substitute] noexcept {
                       refuse(LoadError::field_out_of_range);
                       return valid_substitute;
                   });
    }

    // Pad bytes carry no value.  A writer sets them to zero, so a non-zero
    // byte means the image is not one that a writer produced.  The reader
    // stores no pad byte: the pad of each object that it builds is zero from
    // the construction of the object.
    template <size_t N>
    void read_zero_pad() {
        for (size_t index = 0; index < N; ++index) {
            if (r<uint8_t>() != 0) [[unlikely]]
                refuse(LoadError::field_out_of_range);
        }
    }

    // A record of the Protocol family from its image.  decode refuses a
    // bool byte other than 0 and 1, a value that names no enumerator and a
    // pad byte that is not zero, each as field_out_of_range, and an image
    // cut short is truncated.  A refused read gives valid_substitute, as
    // read_gated does.  Complexity: O(encoded_size_v<T>).
    template <::fixy::Decodable T>
    [[nodiscard]] T read_image(T const& valid_substitute) {
        constexpr size_t bytes = ::fixy::encoded_size_v<T>;
        return image.template window<bytes>(pos).match(
            [this, &valid_substitute](::fixy::View<const std::byte, bytes> window) noexcept {
                pos += bytes;
                return ::fixy::decode<T>(window).match([](T value) noexcept { return value; },
                                                       [this, &valid_substitute](::fixy::DecodeFault) noexcept {
                                                           refuse(LoadError::field_out_of_range);
                                                           return valid_substitute;
                                                       });
            },
            [this, &valid_substitute] noexcept {
                refuse(LoadError::truncated);
                return valid_substitute;
            });
    }

    [[nodiscard]] size_t remaining() const noexcept { return (pos <= image.size()) ? (image.size() - pos) : 0; }

    // Ask before growing the arena for count elements of element_bytes each.
    // A header can claim a count near the ceiling with a truncated body, and
    // without this the arena grows by the full amount before the first
    // element read discovers the end of the buffer, leaving space only a
    // detach can reclaim.  A product that wraps has no room.
    [[nodiscard]] bool has_remaining(size_t count, size_t element_bytes) const noexcept {
        return ::foundation::core::checked_mul(count, element_bytes).is_some_and([this](size_t need) noexcept {
            return remaining() >= need;
        });
    }
};

// A tensor descriptor goes on the wire as its image of the Protocol family
// (Writer::write_image): each field in declaration order, the low byte first.
// The data pointer, the storage generation and the hash of the grad_fn are
// local parts of a TensorMeta, so the image holds no byte of them, and no
// image can carry an address or an identity of one process to another.
template <class Brand, std::size_t Extent>
inline void write_meta(Writer<Brand, Extent>& w, const TensorMeta& m) {
    w.write_image(m);
}

// The reader decodes the image as an UncheckedTensorMeta, whose image has the
// same bytes, and decode_tensor_meta makes the TensorMeta of it.  That door
// refuses each field that breaks a claim of a descriptor: a rank past
// kMaxTensorNDim, a size or a stride past kMaxTensorDimExtent, a lane past
// the rank that holds no zero, an element type, a device type or a layout
// that names no enumerator, a device index below -1, a gradient byte other
// than 0 and 1, and a reserved bit of the flags.  Each refusal is
// field_out_of_range.  A refused descriptor gives the default TensorMeta,
// and the parse that follows it is discarded.  The descriptor has no data
// pointer and no grad_fn.
inline TensorMeta read_meta(Reader& r) {
    const UncheckedTensorMeta fields = r.read_image(UncheckedTensorMeta{});
    ::fixy::Option<TensorMeta> meta = decode_tensor_meta(fields, external_data_ptr(nullptr));
    if (meta.is_none()) [[unlikely]]
        r.refuse(LoadError::field_out_of_range);
    return std::move(meta).value_or(TensorMeta{});
}

// A plan slot and a guard go on the wire as their images of the Protocol
// family (Writer::write_image, Reader::read_image): each field in declaration
// order, the low byte first, with each pad byte zero.  The reader judges each
// enumerator, the bool and each pad byte before it becomes a field, so a slot
// and a guard hold only values of their field types.  A guard kind outside
// the enumeration would reach the replay switch on the kind.
inline constexpr size_t kTensorSlotWireBytes = ::fixy::encoded_size_v<TensorSlot>;

// The zero bytes between the node kind and the merkle hash of a header.
inline constexpr size_t kHeaderPadBytes = 7;

template <class Brand, std::size_t Extent>
inline void write_header(Writer<Brand, Extent>& w, TraceNodeKind kind, MerkleHash merkle_hash,
                         ContentHash content_hash) {
    w.w(CDAG_MAGIC);
    w.w(CDAG_VERSION.value());
    w.w(std::to_underlying(kind));
    w.template write_zero_pad<kHeaderPadBytes>();
    w.w(merkle_hash.raw());
    w.w(content_hash.raw());
}

struct Header {
    uint32_t magic = 0;
    ExternalCdagVersion version{};
    TraceNodeKind kind{};
    MerkleHash merkle_hash;
    ContentHash content_hash;
};

inline Header read_header(Reader& r) {
    Header h{};
    h.magic = r.r<uint32_t>();
    h.version = ::fixy::mint_tagged<::fixy::tags::source::External>(r.r<uint32_t>());
    h.kind = r.read_enumerator(TraceNodeKind::REGION);
    r.read_zero_pad<kHeaderPadBytes>();
    h.merkle_hash = MerkleHash{r.r<uint64_t>()};
    h.content_hash = ContentHash{r.r<uint64_t>()};
    return h;
}

// The bytes of a header: the magic, the version, the kind byte, the pad, the
// merkle hash and the content hash.
inline constexpr size_t kHeaderWireBytes = 4 + 4 + 1 + kHeaderPadBytes + 8 + 8;

// The first fault of a header that read_header read, in the order of its
// fields, or none.  An image shorter than a header is truncated, and a
// different magic number or version comes before a bad byte of the kind or
// the pad, so a file of another format reports its format.  The kind of the
// loader comes last.
[[nodiscard]] inline ::fixy::Option<LoadError> header_fault(Reader const& r, Header const& hdr, TraceNodeKind kind) {
    if (r.image.size() < kHeaderWireBytes) return ::fixy::some(LoadError::truncated);
    if (hdr.magic != CDAG_MAGIC) return ::fixy::some(LoadError::bad_magic);
    if (!cdag_version_matches(hdr.version)) return ::fixy::some(LoadError::version_mismatch);
    if (r.refusal.is_some()) return r.refusal;
    if (hdr.kind != kind) return ::fixy::some(LoadError::wrong_kind);
    return ::fixy::none;
}

// Checksum of a region image
//   A region image ends with the keyed BLAKE3 of each byte before it, under
//   the key of kRegionChecksumKeyText.  The content hash stays the identity
//   of the region, and it folds only the ops and the numerics.  The checksum
//   covers the other fields too: the merkle hash, the measured time, the
//   variant and the plan.  So a flipped bit of a stored image gives another
//   checksum, and the loader refuses the image.  The key is a constant of the
//   format and names no secret.  Anyone who writes the store can compute the
//   checksum, so it finds a damaged image, and the checks of the fields stay
//   the defense against a hostile writer.  The loader checks the checksum
//   after the fields, so a field that a check refuses keeps its reason.

// The text whose 32 bytes are the key of the checksum of a region image.
inline constexpr ::fixy::TextView kRegionChecksumKeyText{"crucible cdag region checksum 01"};

// The key whose 32 bytes are the characters of text.  A text of another size
// is no constant expression, so a wrong key text does not compile.
[[nodiscard]] consteval ::foundation::core::KeyedHashKeyBytes key_of_text(::fixy::TextView text) noexcept {
    if (text.size() != 32) ::fixy::unreachable();
    const auto bytes = ::fixy::build_fixed_array<std::byte, 32>([text](auto run) noexcept {
        auto cursor = run.begin();
        for (char const letter : text) {
            *cursor = static_cast<std::byte>(letter);
            ++cursor;
        }
    });
    return ::foundation::core::KeyedHashKeyBytes{bytes.view()};
}

inline constexpr ::foundation::core::KeyedHashKeyBytes kRegionChecksumKey = key_of_text(kRegionChecksumKeyText);

// The bytes of the checksum at the end of a region image.
inline constexpr size_t kRegionChecksumBytes = 32;

// The checksum of the bytes of a region image before its checksum.
// Complexity: linear in the count of the bytes.
[[nodiscard]] inline ::fixy::FixedArray<std::byte, kRegionChecksumBytes>
region_checksum(::fixy::View<const std::byte> covered) noexcept {
    return ::foundation::core::keyed_hash(kRegionChecksumKey, covered).bytes();
}

// The fault of an image whose last kRegionChecksumBytes bytes must be the
// checksum of each byte before them, or none.  An image shorter than covered
// and a checksum is truncated.  A checksum that differs, and a byte between
// the covered bytes and the checksum, give checksum_mismatch.  Complexity:
// linear in the size of the image.
[[nodiscard]] inline ::fixy::Option<LoadError> checksum_fault(::fixy::View<const std::byte> image,
                                                              size_t covered) noexcept {
    if (covered > image.size() || image.size() - covered < kRegionChecksumBytes) {
        return ::fixy::some(LoadError::truncated);
    }
    if (image.size() - covered != kRegionChecksumBytes) return ::fixy::some(LoadError::checksum_mismatch);
    const ::fixy::FixedArray<std::byte, kRegionChecksumBytes> expected =
        region_checksum(image.window(0, covered).expect("the covered bytes are inside the image"));
    const bool is_same = ::fixy::equal(
        image.window(covered, kRegionChecksumBytes).expect("the checksum is inside the image"), expected.view());
    if (!is_same) return ::fixy::some(LoadError::checksum_mismatch);
    return ::fixy::none;
}

}  // namespace detail_ser

// The node that a serialize reads: a Ref of a RegionNode or of a BranchNode,
// with or without a const and with any brand.  A Ref is never null, and the
// serialize only reads it.
template <class Node, class Kind>
concept SerializedNode = std::same_as<std::remove_const_t<Node>, Kind>;

namespace detail_ser {

// The body of serialize_region.  It takes the ViewMut of the frame, so it is
// a template on the brand of the frame, and each frame of a caller makes one
// copy of it.
template <class Brand, std::size_t Extent>
CRUCIBLE_COLD [[nodiscard]] inline size_t write_region_image(const RegionNode& region,
                                                             ::fixy::ViewMut<std::byte, Brand, Extent> image) {
    Writer<Brand, Extent> w{.image = std::move(image)};

    write_header(w, TraceNodeKind::REGION, region.merkle_hash, region.content_hash);

    w.w(region.num_ops());
    w.w(region.first_op_schema.raw());
    w.w(region.numerics.raw());
    w.w(region.measured_ms);
    w.w(region.variant_id.get());

    // The writes of the image are byte writes, and the compiler cannot prove
    // that they leave the link of the region as it is.  The copy of the link
    // keeps one test of the plan.
    const PlanLink plan_link = region.plan.option();
    w.w(plan_link.is_some());
    for (const DagRef<MemoryPlan const>& plan_ref : plan_link) {
        const MemoryPlan& plan = *plan_ref;
        w.w(plan.pool_bytes);
        w.w(plan.num_slots());
        w.w(plan.num_external);
        w.w(plan.device_type);
        w.w(plan.device_idx);
        w.template write_zero_pad<sizeof(plan.pad0)>();
        w.w(plan.device_capability);
        w.w(plan.rank);
        w.w(plan.world_size);
        for (const TensorSlot& slot : plan.slots.view()) {
            w.write_image(slot);
        }
    }

    for (const TraceEntry& te : region.ops.view()) {
        w.w(te.schema_hash.raw());
        w.w(te.shape_hash.raw());
        w.w(te.scope_hash.raw());
        w.w(te.callsite_hash.raw());
        w.w(te.num_inputs());
        w.w(te.num_outputs());
        w.w(te.num_scalar_args());
        w.w(te.grad_enabled);
        // One byte, laid out the same way the recording ring lays it out.
        {
            uint8_t flags = 0;
            if (te.inference_mode) flags |= op_flag::INFERENCE_MODE;
            if (te.is_mutable) flags |= op_flag::IS_MUTABLE;
            flags |= (static_cast<uint8_t>(te.training_phase) & 0x3) << op_flag::PHASE_SHIFT;
            if (te.torch_function) flags |= op_flag::TORCH_FUNCTION;
            w.w(flags);
        }
        w.w(std::to_underlying(te.kernel_id));
        w.w(te.depth.raw());
        w.w(te.scalar_kinds.bits());

        // Each array of the entry holds its count.
        for (const TensorMeta& meta : te.input_metas()) {
            write_meta(w, meta);
        }
        for (const TensorMeta& meta : te.output_metas()) {
            write_meta(w, meta);
        }
        for (const int64_t value : te.scalar_args()) {
            w.w(value);
        }
        for (const OpIndex producer : te.input_trace_indices()) {
            w.w(producer.raw());
        }
        for (const SlotId slot_id : te.input_slot_ids()) {
            w.w(slot_id.raw());
        }
        for (const SlotId slot_id : te.output_slot_ids()) {
            w.w(slot_id.raw());
        }
    }

    // The checksum of each byte before it is the last field (the checksum of
    // a region image).
    if (w.has_room) {
        const ::fixy::FixedArray<std::byte, kRegionChecksumBytes> checksum = region_checksum(
            ::fixy::View<const std::byte>{w.image.view()}.window(0, w.pos).expect("the writer wrote inside its image"));
        for (const std::byte byte : checksum) {
            w.write_word(static_cast<uint8_t>(byte));
        }
    }

    return w.has_room ? w.pos : 0;
}

// The body of serialize_branch.  It takes the ViewMut of the frame, so it is
// a template on the brand of the frame, and each frame of a caller makes one
// copy of it.
template <class Brand, std::size_t Extent>
[[nodiscard]] inline size_t write_branch_image(const BranchNode& branch,
                                               ::fixy::ViewMut<std::byte, Brand, Extent> image) {
    Writer<Brand, Extent> w{.image = std::move(image)};

    // A branch node repurposes the header's content-hash field to hold the
    // merkle hash of the continuation, the suffix the arms merge back into.
    const MerkleHash cont_hash = branch.next.match(
        [](DagRef<TraceNode const> const& continuation) noexcept { return continuation->merkle_hash; },
        []() noexcept { return MerkleHash{}; });
    write_header(w, TraceNodeKind::BRANCH, branch.merkle_hash, ContentHash{cont_hash.raw()});

    w.write_image(branch.guard);

    w.w(branch.num_arms());
    for (const BranchNode::Arm& arm : branch.arms()) {
        w.w(arm.value);
        // An arm with no target writes the hash zero.
        const uint64_t target_hash =
            arm.target.match([](DagRef<TraceNode const> target) noexcept { return target->merkle_hash.raw(); },
                             []() noexcept { return uint64_t{0}; });
        w.w(target_hash);
    }

    return w.has_room ? w.pos : 0;
}

}  // namespace detail_ser

// Writes the image of the region into the first bytes of the run of image,
// the ViewMut of a frame.  Returns the byte count written, or zero if image
// was too small.  Each entry of the region carries its own tensor metadata,
// so the function reads no metadata log.  The serialize reads the region
// only as const.
template <class Region, ::foundation::brand::IsBrand Brand, class ImageBrand, std::size_t Extent>
    requires SerializedNode<Region, RegionNode>
[[nodiscard]] inline size_t serialize_region(::fixy::Ref<Region, Brand> node,
                                             ::fixy::ViewMut<std::byte, ImageBrand, Extent> image) {
    return detail_ser::write_region_image(*node, std::move(image));
}

namespace detail_ser {

// The load of deserialize_region below, in one frame on its arena: the node,
// the plan and each run of the region are objects of the frame.
template <class Fresh>
[[nodiscard]] ::fixy::Result<LoadedRegionNode, LoadError>
read_region_(::foundation::effects::Alloc a, ::fixy::View<const std::byte> image, DagFrame<Fresh>& frame) {
    Reader r{.image = image};

    const Header hdr = read_header(r);
    for (const LoadError fault : header_fault(r, hdr, TraceNodeKind::REGION))
        return ::fixy::err(fault);

    const uint32_t num_ops = r.r<uint32_t>();
    if (num_ops > CDAG_MAX_OPS) return ::fixy::err(r.reason_or(LoadError::count_above_limit));
    const SchemaHash first_op_schema = SchemaHash{r.r<uint64_t>()};
    // The numerics hash that the content hash folds, or zero for a region
    // that its maker hashed with no policy.  The sentinel names no policy.
    const NumericsHash numerics = NumericsHash{r.r<uint64_t>()};
    if (numerics.is_sentinel()) [[unlikely]] {
        return ::fixy::err(r.reason_or(LoadError::content_hash_mismatch));
    }
    // RegionNode::set_measured_ms keeps the time finite and not negative.
    // The comparison also refuses a NaN.
    const float measured_ms =
        r.read_gated<float>([](float ms) noexcept { return ms >= 0.0f && ms <= ::fixy::max_of<float>; }, 0.0f);
    const uint32_t variant_id = r.r<uint32_t>();

    // The plan of the region, and the count of its slots.  A region with no
    // plan binds to no slot table, and its count stays zero.
    ::fixy::Option<DagFrameRef<MemoryPlan, Fresh>> plan{};
    uint32_t num_slots = 0;
    const bool has_plan = r.read_bool();
    if (has_plan) {
        const DagFrameRef<MemoryPlan, Fresh> loaded_plan = frame.template make<MemoryPlan>(a);
        plan = ::fixy::Option<DagFrameRef<MemoryPlan, Fresh>>::some(loaded_plan);
        loaded_plan->pool_bytes = r.r<uint64_t>();
        // The next three checks all guard the same thing: a loaded plan goes
        // on to initialise the replay pool, whose own preconditions are only
        // assumptions in a release build. Each bound is taken from the
        // allocator's own constant, or is the structural relation the
        // allocator assumes, so a value that survives here is one the
        // allocator can serve. The pool size bound is inclusive, so only a
        // strictly larger value is refused.
        if (loaded_plan->pool_bytes > ::crucible::PoolAllocator::kMaxPoolBytes) [[unlikely]] {
            return ::fixy::err(r.reason_or(LoadError::plan_unservable));
        }
        num_slots = r.r<uint32_t>();
        if (num_slots > CDAG_MAX_SLOTS) return ::fixy::err(r.reason_or(LoadError::count_above_limit));
        loaded_plan->num_external = r.r<uint32_t>();
        // The external slots are a subset of the slots, so their count cannot
        // exceed the total.
        if (loaded_plan->num_external > num_slots) return ::fixy::err(r.reason_or(LoadError::plan_unservable));
        // An unrecognised device type would reach pool selection unchecked.
        loaded_plan->device_type = r.read_enumerator(DeviceType::CPU);
        loaded_plan->device_idx = r.r<int8_t>();
        r.read_zero_pad<sizeof(loaded_plan->pad0)>();
        loaded_plan->device_capability = r.r<uint64_t>();
        loaded_plan->rank = r.r<int32_t>();
        loaded_plan->world_size = r.r<int32_t>();
        if (!r.has_remaining(num_slots, kTensorSlotWireBytes)) {
            return ::fixy::err(r.reason_or(LoadError::truncated));
        }
        DagFrameRun<TensorSlot, Fresh> slots_run = frame.template array<TensorSlot>(a, num_slots, TensorSlot{});
        const ::fixy::View<TensorSlot> slots = slots_run.view();
        frame.store(loaded_plan, &MemoryPlan::slots, slots_run);
        uint32_t slot_position = 0;
        for (TensorSlot& slot : slots) {
            slot = r.read_image(TensorSlot{});
            // The pool allocator aborts on a slot it cannot serve, so a
            // loaded plan holds only slots that it can serve.
            if (!::crucible::PoolAllocator::can_serve_slot(slot, loaded_plan->pool_bytes)) [[unlikely]] {
                return ::fixy::err(r.reason_or(LoadError::plan_unservable));
            }
            // The ops name a slot by its position, and the replay registers
            // each external slot under its position.  The build of a region
            // gives each slot its position as its id, the planner refuses
            // another id, and the writer writes each slot as it is.  So the
            // load refuses a slot with another id, in place of a replay that
            // reads the slots under other ids.
            if (slot.slot_id != SlotId{slot_position}) [[unlikely]] {
                return ::fixy::err(r.reason_or(LoadError::slot_out_of_place));
            }
            ++slot_position;
        }
    }

    // An entry is variable-length, but its fixed part of hashes, counts and
    // flags gives a floor per entry, which is enough to reject a declared op
    // count that the remaining bytes cannot possibly hold.
    constexpr size_t kTraceEntryMinWireBytes = 40;
    if (!r.has_remaining(num_ops, kTraceEntryMinWireBytes)) {
        return ::fixy::err(r.reason_or(LoadError::truncated));
    }
    DagFrameRun<TraceEntry, Fresh> ops_run = make_op_run(a, frame, num_ops);
    const ::fixy::View<TraceEntry> ops = ops_run.view();

    for (std::size_t position = 0; position < num_ops; ++position) {
        // The op, as the holder of the stores of its arrays.
        const auto op = hold_op(ops_run, position);
        TraceEntry& te = *op;
        te.schema_hash = SchemaHash{r.r<uint64_t>()};
        te.shape_hash = ShapeHash{r.r<uint64_t>()};
        te.scope_hash = ScopeHash{r.r<uint64_t>()};
        te.callsite_hash = CallsiteHash{r.r<uint64_t>()};
        const uint16_t num_inputs = r.r<uint16_t>();
        const uint16_t num_outputs = r.r<uint16_t>();
        const uint16_t num_scalar_args = r.r<uint16_t>();
        const bool is_count_above_limit =
            num_inputs > kMaxOpInputs || num_outputs > kMaxOpOutputs || num_scalar_args > kScalarSlots;
        if (is_count_above_limit) return ::fixy::err(r.reason_or(LoadError::count_above_limit));
        te.grad_enabled = r.read_bool();
        {
            // The writer sets only these bits, so a byte with another bit
            // set is not an image that the writer produced.
            constexpr uint8_t kWrittenFlags =
                op_flag::INFERENCE_MODE | op_flag::IS_MUTABLE | op_flag::PHASE_MASK | op_flag::TORCH_FUNCTION;
            const uint8_t flags =
                r.read_gated<uint8_t>([](uint8_t byte) noexcept { return (byte & ~kWrittenFlags) == 0; });
            te.inference_mode = (flags & op_flag::INFERENCE_MODE) != 0;
            te.is_mutable = (flags & op_flag::IS_MUTABLE) != 0;
            te.training_phase =
                ::fixy::enum_from_field<TrainingPhase, 2>((flags & op_flag::PHASE_MASK) >> op_flag::PHASE_SHIFT);
            te.torch_function = (flags & op_flag::TORCH_FUNCTION) != 0;
        }
        // The kernel id: enum_from refuses each byte that names no id, in
        // each build, and the refusal gives the error of the field.
        te.kernel_id = r.read_enumerator(CKernelId::OPAQUE);
        // Each value of the word is a depth (OpDepth).
        te.depth = OpDepth{r.r<uint16_t>()};
        // Each value of two bits is a tag, and each bit of the data slots is
        // a role.  The writer writes only the bits of the slots below the
        // count, so a word with a bit of a later slot is not an image that
        // the writer produced.
        {
            const ScalarCount tagged =
                ScalarCount::check(num_scalar_args).expect("the count test above keeps the scalars in the slots");
            const uint16_t kinds = r.read_gated<uint16_t>([tagged](uint16_t word) noexcept {
                for (const ScalarSlotKinds read : ScalarSlotKinds::from_bits(word))
                    return read.first(tagged).bits() == word;
                return false;
            });
            te.scalar_kinds = ScalarSlotKinds::from_bits(kinds).expect(
                "the gate refuses a word past the data slots, and a refused word reads as zero");
        }
        // The op passes the one validator of an entry, as the entry of the
        // ring that it came from did, before the arrays of its counts take
        // arena memory.  The counts and the data slots passed their tests
        // above, so the validator refuses a schema hash of zero, the sentinel
        // of no schema, on which the content hash and the region cache key.
        {
            TraceRing::Entry probe{};
            probe.schema_hash = te.schema_hash;
            probe.num_inputs = num_inputs;
            probe.num_outputs = num_outputs;
            probe.num_scalar_args = static_cast<uint8_t>(num_scalar_args);
            probe.set_data_slots(te.scalar_kinds.data_slots());
            if (!entry_is_well_formed(probe)) [[unlikely]] {
                return ::fixy::err(r.reason_or(LoadError::field_out_of_range));
            }
        }

        DagFrameRun<TensorMeta, Fresh> input_metas_run = frame.template array<TensorMeta>(a, num_inputs, TensorMeta{});
        for (TensorMeta& meta : input_metas_run.view()) {
            meta = read_meta(r);
        }

        DagFrameRun<TensorMeta, Fresh> output_metas_run =
            frame.template array<TensorMeta>(a, num_outputs, TensorMeta{});
        for (TensorMeta& meta : output_metas_run.view()) {
            meta = read_meta(r);
        }

        DagFrameRun<int64_t, Fresh> scalar_args_run = frame.template array<int64_t>(a, num_scalar_args, int64_t{0});
        for (int64_t& value : scalar_args_run.view()) {
            value = r.r<int64_t>();
        }
        TraceEntry::set_scalar_args(frame, op, scalar_args_run);

        DagFrameRun<OpIndex, Fresh> producers_run = frame.template array<OpIndex>(a, num_inputs, OpIndex{});
        for (OpIndex& producer : producers_run.view()) {
            producer = OpIndex{r.r<uint32_t>()};
        }

        DagFrameRun<SlotId, Fresh> input_slot_ids_run = frame.template array<SlotId>(a, num_inputs, SlotId{});
        for (SlotId& slot_id : input_slot_ids_run.view()) {
            const SlotId sid = SlotId{r.r<uint32_t>()};
            // A slot id indexes the replay slot table, and replay checks only
            // that the id is not the none sentinel: the upper bound is a
            // construction-time invariant it assumes rather than tests, so
            // indexing with an out-of-range id reads past the table. When the
            // region carries a plan the table is built from that plan, so the
            // bound is known here and enforced here. A region without a plan
            // binds to no table and cannot execute until one is supplied
            // elsewhere, so there is no bound to enforce for it.
            if (has_plan && sid.is_valid() && sid.raw() >= num_slots) [[unlikely]] {
                return ::fixy::err(r.reason_or(LoadError::slot_out_of_range));
            }
            slot_id = sid;
        }
        TraceEntry::set_inputs(frame, op, input_metas_run, producers_run, input_slot_ids_run);

        DagFrameRun<SlotId, Fresh> output_slot_ids_run = frame.template array<SlotId>(a, num_outputs, SlotId{});
        for (SlotId& slot_id : output_slot_ids_run.view()) {
            const SlotId sid = SlotId{r.r<uint32_t>()};
            // The same bound as for the input slot ids above.
            if (has_plan && sid.is_valid() && sid.raw() >= num_slots) [[unlikely]] {
                return ::fixy::err(r.reason_or(LoadError::slot_out_of_range));
            }
            slot_id = sid;
        }
        TraceEntry::set_outputs(frame, op, output_metas_run, output_slot_ids_run);
    }

    for (const LoadError refusal : r.refusal)
        return ::fixy::err(refusal);

    // The content hash is the region's identity and the key of its kernels
    // in the cache.  The header states it, and the header comes from outside,
    // so the loader folds the operations again and refuses a region whose
    // stated hash is not the hash of its operations.  A peer that could state
    // any hash could make this region take the kernels of another one.
    //
    // The fold is the one of the background pipeline under the numerics hash
    // of the image, or the one make_region uses with no recipe when the hash
    // is zero.  A region hashed under a recipe is refused here, because the
    // image does not carry the recipe and the loader cannot fold it.
    const ContentHash folded =
        ::foundation::decide::is_non_zero(numerics) ? compute_content_hash(ops, numerics) : compute_content_hash(ops);
    if (folded != hdr.content_hash) [[unlikely]] {
        return ::fixy::err(LoadError::content_hash_mismatch);
    }
    // make_region sets the first schema from the operations, so the field is
    // not free to disagree with them.
    if (first_op_schema != first_op_schema_of(ops)) [[unlikely]] {
        return ::fixy::err(LoadError::first_schema_mismatch);
    }
    // The checksum covers the fields that no fold covers (the checksum of a
    // region image).  It comes after the checks of the fields, so a field that
    // a check refuses keeps its reason.
    for (const LoadError fault : checksum_fault(image, r.pos))
        return ::fixy::err(fault);

    const DagFrameRef<RegionNode, Fresh> node = mint_dag_node<RegionNode>(a, frame);
    node->kind = TraceNodeKind::REGION;
    node->merkle_hash = hdr.merkle_hash;
    node->content_hash = hdr.content_hash;
    node->numerics = numerics;
    // A new node holds no continuation, and the caller links it.
    frame.store(node, &RegionNode::ops, ops_run);
    node->first_op_schema = first_op_schema;
    node->measured_ms = measured_ms;
    // A new node holds the counter at zero, and each value on disk is at
    // least zero, so the advance does not go back.  A value of zero means
    // that no variant was selected when the region was written.
    node->variant_id.advance(variant_id);
    // The plan of the image is a node of this frame, so frame.store links it.
    for (DagFrameRef<MemoryPlan, Fresh> const& image_plan : plan)
        frame.store(node, &RegionNode::plan, image_plan);
    // The node is rebuilt from Cipher bytes, and the fold above matched the
    // content hash that the image records, so it crosses the edge to Loaded.
    return ::fixy::mint_tagged<::fixy::tags::source::Replayed>(node.weaken())
        .template retag<::fixy::tags::source::Loaded>();
}

}  // namespace detail_ser

// Gives the region of the image, or the reason that refused the image: a
// parse error, a version mismatch, a count or a plan past its bound, or a
// content hash that the operations do not fold to.  The reason is the first
// fault of the image.  The load opens one frame on the arena, and it builds
// each structure of the region in that frame.  The result is a family Ref, so
// no slot of another frame takes the loaded node.
CRUCIBLE_COLD [[nodiscard]] inline ::fixy::Result<LoadedRegionNode, LoadError>
deserialize_region(::foundation::effects::Alloc a, ::fixy::View<const std::byte> image,
                   ::fixy::BumpArena<DagArena>& arena) {
    return ::foundation::core::with_arena_frame(
        arena, [a, image](auto& frame) noexcept { return detail_ser::read_region_(a, image, frame); });
}

// The fault of a stored image of the region of hash, or none, with no parse of
// its body: a header of a region of this version, the content hash hash in
// that header, and the checksum at the end (the checksum of a region image).
// The object store reads it before it counts an object that a writer stored
// as durable, so a torn or a cut object does not count.  A hostile writer can
// compute the checksum, so the load of deserialize_region stays the check of
// the fields.  Complexity: linear in the size of the image.
[[nodiscard]] inline ::fixy::Option<LoadError> stored_region_fault(::fixy::View<const std::byte> image,
                                                                   ContentHash hash) {
    using namespace detail_ser;
    Reader r{.image = image};
    const Header hdr = read_header(r);
    for (const LoadError fault : header_fault(r, hdr, TraceNodeKind::REGION))
        return ::fixy::some(fault);
    if (hdr.content_hash != hash) return ::fixy::some(LoadError::object_hash_mismatch);
    if (image.size() < kHeaderWireBytes + kRegionChecksumBytes) return ::fixy::some(LoadError::truncated);
    return checksum_fault(image, image.size() - kRegionChecksumBytes);
}

// Writes the image of the branch into the first bytes of the run of image,
// the ViewMut of a frame.  Returns the byte count written, or zero if image
// was too small.  The serialize reads the branch only as const.
template <class Branch, ::foundation::brand::IsBrand Brand, class ImageBrand, std::size_t Extent>
    requires SerializedNode<Branch, BranchNode>
[[nodiscard]] inline size_t serialize_branch(::fixy::Ref<Branch, Brand> node,
                                             ::fixy::ViewMut<std::byte, ImageBrand, Extent> image) {
    return detail_ser::write_branch_image(*node, std::move(image));
}

// The resolver turns each persisted merkle hash back into a node of the frame,
// or into no node.  Gives the branch, or the reason that refused the image: a
// parse error, a version mismatch, a count past its bound, or arms out of the
// order of their values.  The branch and the target of each arm are in the
// arena of the frame.
template <class Fresh, typename Resolve>
    requires std::is_invocable_r_v<::fixy::Option<DagFrameRef<TraceNode, Fresh>>, Resolve&, MerkleHash>
[[nodiscard]] ::fixy::Result<DagFrameRef<BranchNode, Fresh>, LoadError>
deserialize_branch(::foundation::effects::Alloc a, ::fixy::View<const std::byte> image, DagFrame<Fresh>& frame,
                   Resolve&& resolve) {
    Resolve resolver = std::forward<Resolve>(resolve);
    using namespace detail_ser;
    Reader r{.image = image};

    const Header hdr = read_header(r);
    for (const LoadError fault : header_fault(r, hdr, TraceNodeKind::BRANCH))
        return ::fixy::err(fault);
    // The header's content-hash field holds the continuation's merkle hash.

    const Guard guard = r.read_image(Guard{});

    const uint32_t num_arms = r.r<uint32_t>();
    if (num_arms > CDAG_MAX_BRANCH_ARMS) return ::fixy::err(r.reason_or(LoadError::count_above_limit));

    // Reject a truncated body before growing the arena for the arms. The wire
    // cost is spelled out rather than taken from the in-memory arm: the two
    // sizes coincide today, but the in-memory arm holds a resolved link
    // where the wire holds a hash, so only one of them is the file's business.
    constexpr size_t kArmWireBytes = sizeof(int64_t) + sizeof(uint64_t);
    if (!r.has_remaining(num_arms, kArmWireBytes)) {
        return ::fixy::err(r.reason_or(LoadError::truncated));
    }

    const DagFrameRef<BranchNode, Fresh> node = mint_dag_node<BranchNode>(a, frame);
    node->kind = TraceNodeKind::BRANCH;
    node->merkle_hash = hdr.merkle_hash;
    // A new node holds no continuation, and the caller resolves it separately.
    node->guard = guard;
    DagFrameRun<BranchNode::Arm, Fresh> arms = frame.template build_array<BranchNode::Arm>(
        a, ::fixy::ElementCount{num_arms}, [](::fixy::RunStart) noexcept { return BranchNode::Arm{}; });
    frame.store(node, &BranchNode::arm_run, arms);
    // The resolver gives a node of the frame, so frame.store links it to the
    // arm.  An arm with no resolved node keeps its empty target.
    for (size_t index = 0; index < num_arms; ++index) {
        const auto arm =
            arms.element(::fixy::RunStart{::fixy::ElementCount{index}}).expect("the run holds each arm of the image");
        arm->value = r.r<int64_t>();
        const MerkleHash target_h = MerkleHash{r.r<uint64_t>()};
        const ::fixy::Option<DagFrameRef<TraceNode, Fresh>> target = resolver(target_h);
        for (DagFrameRef<TraceNode, Fresh> const& resolved : target)
            frame.store(arm, &BranchNode::Arm::target, resolved);
    }

    // Replay finds an arm by binary search on the value, so an image with
    // unsorted arms would route a guard outcome to the wrong target.
    for (const LoadError refusal : r.refusal)
        return ::fixy::err(refusal);
    if (!node->are_arms_sorted_by_value()) return ::fixy::err(LoadError::arms_unsorted);
    return node;
}

// The branch with no resolver: the target of each arm stays empty, and the
// caller resolves the targets later.
template <class Fresh>
[[nodiscard]] ::fixy::Result<DagFrameRef<BranchNode, Fresh>, LoadError>
deserialize_branch(::foundation::effects::Alloc a, ::fixy::View<const std::byte> image, DagFrame<Fresh>& frame) {
    return deserialize_branch(a, image, frame,
                              [](MerkleHash) noexcept { return ::fixy::Option<DagFrameRef<TraceNode, Fresh>>{}; });
}

}  // namespace crucible
