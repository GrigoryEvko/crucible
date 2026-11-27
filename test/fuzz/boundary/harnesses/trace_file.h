#pragma once

// A .crtrace file.  The loader either refuses the file or returns a trace
// whose descriptors are in range and whose per-operation metadata runs lie
// inside the metadata array.

#include "../harness.h"
#include "tensor_meta_claims.h"

#include <crucible/TraceLoader.h>
#include <fixy/Ctx.h>
#include <fixy/os/Path.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::fuzz::boundary {

// The bytes of one operation with one input and one output, two metadata
// records, and a name table of two names, with the second hash of the call.
// The header, the record and the metadata come from the encoders of the
// writer.
[[nodiscard]] inline SeedBytes write_trace_seed(SchemaHash second_hash) noexcept {
    SeedWriter seed{};
    const auto header = encode_trace_header(1, 2);
    seed.append_bytes(header.view());
    TraceOpRecord record{};
    record.schema_hash = SchemaHash{0xABCD};
    record.num_inputs = 1;
    record.num_outputs = 1;
    record.depth = OpDepth{1};
    const auto op = encode_trace_op(record);
    seed.append_bytes(op.view());
    TensorMeta meta{};
    meta.set_shape(vector_tensor_shape(tensor_dim(4)));
    meta.dtype = ScalarType::Float;
    const auto meta_bytes = encode_trace_meta(meta);
    seed.append_bytes(meta_bytes.view());
    seed.append_bytes(meta_bytes.view());
    seed.append_raw(std::uint32_t{2});
    seed.append_raw(std::uint64_t{0xABCD});
    seed.append_raw(std::uint16_t{7});
    seed.append_text("aten::x");
    seed.append_raw(second_hash.raw());
    seed.append_raw(std::uint16_t{7});
    seed.append_text("aten::y");
    return seed.finish();
}

// The hash that the reserve of the harness holds for the name aten::x.  The
// second hash of the first seed is 0xABCE, so one flip of its lowest bit
// gives the reserved hash.
inline constexpr SchemaHash kHarnessReservedHash{0xABCF};

// The search of the reserve of the harness: aten::x for the reserved hash,
// and no name for each other hash.
[[nodiscard]] constexpr ::fixy::Option<SchemaNameParts> find_harness_reserved_name(SchemaHash hash) noexcept {
    if (hash != kHarnessReservedHash) return ::fixy::Option<SchemaNameParts>{::fixy::none};
    return ::fixy::some(SchemaNameParts{::fixy::TextView{"aten::x"}});
}

// The reserve that each load of the harness takes.  A trace that gives the
// reserved hash another name does not load, and a trace that gives it
// aten::x loads.
inline constexpr SchemaNameReserve kHarnessReserve{SchemaNameSearchOf<&find_harness_reserved_name>{}};

// Three seeds of the version that the loader reads.  The first trace loads,
// with each of its two names, so the fuzzer starts from a trace that loads.
// The second trace gives one hash the name aten::x and then aten::y, and the
// load refuses the repeated hash whole.  The third trace gives the reserved
// hash the name aten::y, and the load refuses it before it registers a name.
[[nodiscard]] inline SeedList seeds_trace_file() noexcept {
    SeedList seeds = empty_seed_list();
    add_seed(seeds, write_trace_seed(SchemaHash{0xABCE}));
    add_seed(seeds, write_trace_seed(SchemaHash{0xABCD}));
    add_seed(seeds, write_trace_seed(kHarnessReservedHash));
    return seeds;
}

// True when a name of the image has a hash that the reserve of the harness
// holds for another name.  An image that fails its check holds no such name.
// Complexity: linear in the names of the image.
[[nodiscard]] inline bool holds_name_on_reserved_hash(::fixy::View<std::byte const> image) noexcept {
    const auto checked = CheckedTraceImage::check(image, kHarnessCtx.cap().alloc);
    return checked.match(
        [](CheckedTraceImage const& trace_image) noexcept {
            ::fixy::View<std::byte const> rest = trace_image.names();
            for (std::uint32_t index = 0; index < trace_image.num_names(); ++index) {
                const detail::TraceName name =
                    detail::take_trace_name_(rest).expect("the checked image holds each name of its count");
                const SchemaNameParts parts{detail::trace_name_text_(name).value()};
                if (kHarnessReserve.is_reserved_for_other_name(name.hash, parts)) return true;
            }
            return false;
        },
        [](TraceLoadError) noexcept { return false; });
}

// Each name of a loaded trace passes sanitize_schema_name, the validator of
// the C entry, and the global table holds it with its characters.  The image
// gives each hash one time, and the table started empty, so the table holds
// as many names as the image.
inline void claim_names_registered(::fixy::View<std::byte const> image) noexcept {
    const auto checked = CheckedTraceImage::check(image, kHarnessCtx.cap().alloc);
    CRUCIBLE_FUZZ_CLAIM("trace_file", checked.is_ok());
    checked.match(
        [](CheckedTraceImage const& trace_image) noexcept {
            CRUCIBLE_FUZZ_CLAIM("trace_file", global_schema_table().count() == trace_image.num_names());
            ::fixy::View<std::byte const> rest = trace_image.names();
            for (std::uint32_t index = 0; index < trace_image.num_names(); ++index) {
                const detail::TraceName name =
                    detail::take_trace_name_(rest).expect("the checked image holds each name of its count");
                const auto text = detail::trace_name_text_(name);
                CRUCIBLE_FUZZ_CLAIM("trace_file", sanitize_schema_name(name.hash, text).is_ok());
                // No loaded name takes a reserved hash with another name.
                const SchemaNameParts parts{text.value()};
                CRUCIBLE_FUZZ_CLAIM("trace_file", !kHarnessReserve.is_reserved_for_other_name(name.hash, parts));
                const ::fixy::Option<InternedSchemaName> held = global_schema_table().lookup(name.hash);
                const bool is_held_name = held.match(
                    [&text](InternedSchemaName const& found) noexcept { return found.value().view() == text.value(); },
                    [] noexcept { return false; });
                CRUCIBLE_FUZZ_CLAIM("trace_file", is_held_name);
            }
        },
        [](TraceLoadError) noexcept {});
}

inline void run_trace_file(::fixy::View<std::byte const> bytes) noexcept {
    constexpr std::size_t kMaxInputBytes = std::size_t{1} << 20;
    const ::fixy::View<std::byte const> image =
        bytes.size() > kMaxInputBytes ? bytes.window(0, kMaxInputBytes).expect("the cap is inside the input") : bytes;

    // The scratch path is absolute and holds no ".." component.
    ::foundation::os::PathText<::fixy::kPathBytes> input{};
    if (input.append(scratch_dir().path()).is_err() || input.append(::fixy::TextView{"/input.crtrace"}).is_err())
        return;
    auto sanitized_path =
        ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(input.c_text().view()));
    if (sanitized_path.is_err()) return;
    const ::fixy::Path<::fixy::tags::source::Sanitized> path =
        std::move(sanitized_path).expect("the scratch path passes the check");
    // The descriptor closes in the write, before the load.
    if (!write_scratch_file("input.crtrace", image)) return;
    // The load registers the names of the trace into the global table, each
    // of them or none.  The table starts empty at each run, so no name of an
    // earlier input collides.  A trace can name more schemas than the table
    // holds, and the load then refuses the whole trace.
    // The load takes the reserve of the harness.
    constexpr VigilFgCtx kForeground = ::foundation::effects::testing::foreground<Vigil>();
    global_schema_table().clear(TableReopen<SchemaTable>{::foundation::effects::testing::test()});
    auto loaded = load_trace(kHarnessCtx, kForeground, path, kHarnessReserve);
    // A refused load registers no name.  A refusal of a reserved hash names
    // a name of the image on a reserved hash.
    if (loaded.is_err()) {
        CRUCIBLE_FUZZ_CLAIM("trace_file", global_schema_table().count() == 0);
        const bool is_reserved_refusal = loaded.err() == ::fixy::some(TraceLoadError::NameReserved);
        CRUCIBLE_FUZZ_CLAIM("trace_file", !is_reserved_refusal || holds_name_on_reserved_hash(image));
        return;
    }
    const LoadedTrace trace = std::move(loaded).expect("the load holds a trace after the check of its error");

    CRUCIBLE_FUZZ_CLAIM("trace_file", global_schema_table().count() <= SCHEMA_TABLE_CAP);
    claim_names_registered(image);
    CRUCIBLE_FUZZ_CLAIM("trace_file", trace.metas.size() == trace.num_metas);
    CRUCIBLE_FUZZ_CLAIM("trace_file", trace.entries.size() == trace.num_ops);
    CRUCIBLE_FUZZ_CLAIM("trace_file", trace.depths.size() == trace.num_ops);
    for (const TensorMeta& meta : trace.metas)
        claim_meta_in_range(as_harness("trace_file"), meta);
    for (const auto [entry, meta_start] : trace.entries.view()
                                              .walk()
                                              .try_zip(trace.meta_starts.view().walk())
                                              .expect("the arrays of a trace are parallel")) {
        // The load admits only an entry that the trust ladder of the vessel
        // admits.
        CRUCIBLE_FUZZ_CLAIM("trace_file", entry_is_well_formed(*entry));
        const std::uint32_t tensors = std::uint32_t{entry->num_inputs} + std::uint32_t{entry->num_outputs};
        if (tensors == 0) continue;
        const MetaIndex start = *meta_start;
        CRUCIBLE_FUZZ_CLAIM("trace_file", start.is_valid());
        CRUCIBLE_FUZZ_CLAIM("trace_file", std::uint64_t{start.raw()} + tensors <= trace.num_metas);
    }
}

}  // namespace crucible::fuzz::boundary
