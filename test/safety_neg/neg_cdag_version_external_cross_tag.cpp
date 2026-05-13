// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for #1013 WRAP-Serialize-4
// (CDAG_VERSION raw uint32_t -> Tagged<uint32_t, source::FormatVersion>).
//
// Premise: disk-read header versions enter as source::External and
// are validated against the in-process source::FormatVersion constant.
// The two tags are intentionally distinct; no implicit retag may move
// a trusted format constant into an external-input lane.
//
// Distinct mismatch class from neg_cdag_version_raw_uint32.cpp:
//   * Companion: implicit raw extraction rejected.
//   * This fixture: cross-tag assignment rejected.

#include <crucible/ir001/Serialize.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>

int main() {
  using ExternalVersion = crucible::safety::Tagged<
      std::uint32_t,
      crucible::safety::source::External>;

  // MUST fail: source::FormatVersion is not source::External.
  ExternalVersion disk = crucible::CDAG_VERSION;
  return static_cast<int>(disk.value());
}
