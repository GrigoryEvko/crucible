#pragma once

// The entry points that two shared libraries export for
// test_process_wide_across_libraries.  The test loads the two libraries with
// dlopen and RTLD_LOCAL, as crucible_native.py loads the two vessel
// libraries.  process_wide_writer.cpp puts a value into each process-wide
// object, and process_wide_reader.cpp reads it back.  process_wide_objects.cpp
// is compiled into the two libraries, and it gives the address of each object
// as the library that asks sees it.

#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>

#include <cstdint>

namespace process_wide_libraries {

// The single-producer state whose claim the two libraries share.
struct SharedBrand {
    ::foundation::effects::host::ProducerClaim<SharedBrand> claim;
};

// Objects for each thread, in the three shapes that the marker takes.  The
// test shows one object for each thread across the two libraries.  The member
// has the shape of a flag that a thread keeps about the stage it holds.
CRUCIBLE_PROCESS_WIDE inline constinit thread_local std::uint64_t per_thread_value = 0;

CRUCIBLE_PROCESS_WIDE inline std::uint64_t& per_thread_local() noexcept {
    thread_local std::uint64_t instance = 0;
    return instance;
}

struct PerThreadHolder {
    CRUCIBLE_PROCESS_WIDE static inline constinit thread_local const std::uint64_t* held = nullptr;
};

// The controls carry no marker, so each library holds its own copy.  They
// show that the test loads two separate copies, so a shared address above is
// the work of the marker and not of the load.
inline constinit std::uint64_t control_value = 0;
inline constinit thread_local std::uint64_t control_thread_value = 0;

// Arbitrary schema hashes.  The test is about the tables, not the hash.
inline constexpr std::uint64_t kPublishedHash = 0x5A11'0000'0000'0001ULL;
inline constexpr std::uint64_t kRegisteredHash = 0x5A11'0000'0000'0002ULL;
inline constexpr std::uint64_t kNamedHash = 0x5A11'0000'0000'0003ULL;
inline constexpr char kSchemaName[] = "aten::process_wide_probe";

}  // namespace process_wide_libraries

extern "C" {

// Defined in the writer library.  process_wide_write publishes a static
// kernel table, registers one kernel and one schema name.  The brand is
// built there, and its claim is won on the calling thread.
[[gnu::visibility("default")]] void process_wide_write() noexcept;
[[gnu::visibility("default")]] void* process_wide_claim_brand() noexcept;
[[gnu::visibility("default")]] void process_wide_release_brand(void* brand) noexcept;

// Defined in the reader library.
[[gnu::visibility("default")]] std::uint8_t process_wide_classify(std::uint64_t schema_hash) noexcept;
[[gnu::visibility("default")]] const char* process_wide_schema_name(std::uint64_t schema_hash) noexcept;
[[gnu::visibility("default")]] int process_wide_is_brand_usable() noexcept;
[[gnu::visibility("default")]] int process_wide_does_second_claim_abort() noexcept;

// Defined in the two libraries.  Null past the last object.
[[gnu::visibility("default")]] const void* process_wide_object_address(unsigned index) noexcept;

// Defined in the two libraries, for the objects of the calling thread.  The
// address is null past the last object.  The set writes each object.
[[gnu::visibility("default")]] const void* process_wide_thread_object_address(unsigned index) noexcept;
[[gnu::visibility("default")]] void process_wide_set_thread_objects(std::uint64_t value) noexcept;
[[gnu::visibility("default")]] std::uint64_t process_wide_thread_object_value(unsigned index) noexcept;

// Defined in the two libraries: the address of each control, null past the last.
[[gnu::visibility("default")]] const void* process_wide_control_address(unsigned index) noexcept;

}  // extern "C"
