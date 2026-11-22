// Negative compile test.  This file must fail to compile.
//
// Fixture 1 of 2 for CDAG_VERSION, a Tagged<uint32_t,
// source::FormatVersion>.
//
// Premise: the in-process CDAG format version is not a raw integer.
// Byte writers may unwrap it explicitly with `.value()`, but callers
// must not silently erase the source::FormatVersion provenance.
//
// Distinct mismatch class from neg_cdag_version_external_cross_tag.cpp:
//   * This fixture: Tagged -> raw uint32_t implicit extraction rejected.
//   * Companion: FormatVersion -> External retag rejected.

#include <crucible/Serialize.h>

#include <cstdint>

int main() {
    // Must fail: CDAG_VERSION carries source::FormatVersion provenance.
    std::uint32_t raw = crucible::CDAG_VERSION;
    return static_cast<int>(raw);
}
