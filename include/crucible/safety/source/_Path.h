#pragma once

#include <crucible/safety/_Tagged.h>

namespace crucible::safety::source {

// Paths supplied by an interactive operator: argv entries, REPL input, stdin.
// The most adversarial input class, because the operator can construct a path
// specifically to escape the intended sandbox.
struct FromUserPath {};

// Paths read from environment variables. The environment may be inherited from
// a parent process Crucible does not own, so the bytes stay untrusted even when
// a deployment harness is expected to set them.
struct FromEnvPath {};

// Paths drawn from operator-authored configuration files. A structured parser
// normally applies a schema check first, so a sanitize policy for this lane can
// rely on malformed input already having been rejected. The bytes are still
// external.
struct FromConfigPath {};

// Paths assembled inside Crucible from an already-sanitized storage root and a
// hex-formatted content hash. The bytes never crossed an untrusted boundary, so
// the directory-anchored open helpers trust a value carrying this tag. Keeping
// this tag out of the three external lanes is what stops operator-supplied
// bytes from reaching those helpers.
struct CipherPath {};

}  // namespace crucible::safety::source

namespace crucible::safety {

// These admittances permit the type transition only. They do not witness that a
// sanitize pass ran. Discharging that obligation is the caller's.

template <>
struct retag_policy<source::FromUserPath, source::Sanitized> {
    static constexpr bool allowed = true;
};

template <>
struct retag_policy<source::FromEnvPath, source::Sanitized> {
    static constexpr bool allowed = true;
};

template <>
struct retag_policy<source::FromConfigPath, source::Sanitized> {
    static constexpr bool allowed = true;
};

}  // namespace crucible::safety

namespace crucible::safety::source::detail::v232_self_test {

static_assert(!std::is_same_v<FromUserPath, FromEnvPath>,
              "FromUserPath and FromEnvPath must be distinct types: separate provenance lanes "
              "carry separate per-source policy.");
static_assert(!std::is_same_v<FromUserPath, FromConfigPath>, "FromUserPath and FromConfigPath must be distinct types.");
static_assert(!std::is_same_v<FromEnvPath, FromConfigPath>, "FromEnvPath and FromConfigPath must be distinct types.");

static_assert(!std::is_same_v<FromUserPath, External>,
              "FromUserPath must be a distinct type from External: widening erases the record of "
              "where the path came from.");
static_assert(!std::is_same_v<FromEnvPath, External>, "FromEnvPath must be a distinct type from External.");
static_assert(!std::is_same_v<FromConfigPath, External>, "FromConfigPath must be a distinct type from External.");

static_assert(!std::is_same_v<CipherPath, FromUserPath>,
              "CipherPath must be distinct from FromUserPath: Cipher-emitted paths are not "
              "user-typed.");
static_assert(!std::is_same_v<CipherPath, FromEnvPath>, "CipherPath must be distinct from FromEnvPath.");
static_assert(!std::is_same_v<CipherPath, FromConfigPath>, "CipherPath must be distinct from FromConfigPath.");
static_assert(!std::is_same_v<CipherPath, External>, "CipherPath must be distinct from External: internally "
                                                     "constructed bytes never crossed an untrusted boundary.");
static_assert(!std::is_same_v<CipherPath, Sanitized>, "CipherPath must be distinct from Sanitized: Sanitized is the "
                                                      "post-sanitize lane, CipherPath the internally constructed "
                                                      "lane.");

static_assert(retag_policy<FromUserPath, Sanitized>::allowed,
              "source::FromUserPath must be admitted into source::Sanitized so the sanitize "
              "boundary can discharge.");
static_assert(retag_policy<FromEnvPath, Sanitized>::allowed,
              "source::FromEnvPath must be admitted into source::Sanitized.");
static_assert(retag_policy<FromConfigPath, Sanitized>::allowed,
              "source::FromConfigPath must be admitted into source::Sanitized.");

static_assert(RetagAllowed<FromUserPath, Sanitized>, "The RetagAllowed concept must admit FromUserPath into "
                                                     "Sanitized.");
static_assert(RetagAllowed<FromEnvPath, Sanitized>, "The RetagAllowed concept must admit FromEnvPath into "
                                                    "Sanitized.");
static_assert(RetagAllowed<FromConfigPath, Sanitized>, "The RetagAllowed concept must admit FromConfigPath into "
                                                       "Sanitized.");

static_assert(retag_policy<External, Sanitized>::allowed, "source::External must remain admitted into "
                                                          "source::Sanitized.");

static_assert(!retag_policy<Sanitized, FromUserPath>::allowed,
              "Sanitized must stay rejected into FromUserPath: re-introducing taint defeats the "
              "sanitize boundary.");
static_assert(!retag_policy<Sanitized, FromEnvPath>::allowed, "Sanitized must stay rejected into FromEnvPath.");
static_assert(!retag_policy<Sanitized, FromConfigPath>::allowed, "Sanitized must stay rejected into FromConfigPath.");

static_assert(!retag_policy<FromUserPath, FromEnvPath>::allowed,
              "FromUserPath must stay rejected into FromEnvPath: provenance lanes are orthogonal, "
              "so cross-narrowing lies about origin.");
static_assert(!retag_policy<FromEnvPath, FromConfigPath>::allowed,
              "FromEnvPath must stay rejected into FromConfigPath.");
static_assert(!retag_policy<FromConfigPath, FromUserPath>::allowed,
              "FromConfigPath must stay rejected into FromUserPath.");

static_assert(!retag_policy<External, FromUserPath>::allowed,
              "External must stay rejected into FromUserPath: back-filling a narrower provenance "
              "claims a lineage the value does not have.");
static_assert(!retag_policy<External, FromEnvPath>::allowed, "External must stay rejected into FromEnvPath.");
static_assert(!retag_policy<External, FromConfigPath>::allowed, "External must stay rejected into FromConfigPath.");

static_assert(!retag_policy<FromUserPath, External>::allowed,
              "FromUserPath must stay rejected into External: widening erases the narrower audit "
              "trail.");
static_assert(!retag_policy<FromEnvPath, External>::allowed, "FromEnvPath must stay rejected into External.");
static_assert(!retag_policy<FromConfigPath, External>::allowed, "FromConfigPath must stay rejected into External.");

static_assert(retag_policy<FromUserPath, FromUserPath>::allowed,
              "Identity retag from FromUserPath to FromUserPath must stay admitted.");
static_assert(retag_policy<FromEnvPath, FromEnvPath>::allowed,
              "Identity retag from FromEnvPath to FromEnvPath must stay admitted.");
static_assert(retag_policy<FromConfigPath, FromConfigPath>::allowed,
              "Identity retag from FromConfigPath to FromConfigPath must stay admitted.");

}  // namespace crucible::safety::source::detail::v232_self_test
