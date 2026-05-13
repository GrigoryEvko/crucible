// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for #1013 WRAP-Serialize-4
// (CDAG_VERSION raw uint32_t -> Tagged<uint32_t, source::FormatVersion>).
//
// Premise: the in-process CDAG format version is not a raw integer.
// Byte writers may unwrap it explicitly with `.value()`, but callers
// must not silently erase the source::FormatVersion provenance.
//
// Distinct mismatch class from neg_cdag_version_external_cross_tag.cpp:
//   * This fixture: Tagged -> raw uint32_t implicit extraction rejected.
//   * Companion: FormatVersion -> External retag rejected.

#include <crucible/ir001/Serialize.h>

#include <cstdint>

int main() {
  // MUST fail: CDAG_VERSION carries source::FormatVersion provenance.
  std::uint32_t raw = crucible::CDAG_VERSION;
  return static_cast<int>(raw);
}
