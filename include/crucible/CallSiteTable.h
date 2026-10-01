#pragma once

#include <crucible/Types.h>
#include <fixy/Mutation.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

#include <cstdint>
#include <string>
#include <vector>

namespace crucible {

// Maps the hash of a source location to the location itself. Each recorded
// op carries such a hash, and after the first iteration of a loop every hash
// is already present, so the query answers yes without touching a string.
struct CallSiteTable {
    // A line number of zero is the unset marker, so the bound is at least
    // zero rather than strictly positive. A negative value can only come from
    // corrupted data.
    using Lineno = ::fixy::NonNegative<int32_t>;

    // The table keeps one copy of each name for each hash, whichever caller
    // wrote it first, so a stored name claims Interned and neither tag of its
    // caller.  Nothing here sanitizes a name, so a stored name does not claim
    // Sanitized.  No retag leaves Interned, so a reader cannot hand a stored
    // name back to something that wants an External or an internal name.
    using InternedName = ::fixy::Tagged<std::string, ::fixy::tags::source::Interned>;

    struct Entry {
        CallsiteHash hash;
        InternedName filename;
        InternedName funcname;
        Lineno lineno = ::fixy::mint_refined<::fixy::non_negative>(int32_t{0});
    };

    ::fixy::AppendOnly<Entry> entries = ::fixy::mint_append_only<Entry>();

    // An open-addressed set of the hashes seen so far. A zero hash marks an
    // empty slot.
    static constexpr uint32_t SET_CAP = 4096;
    static constexpr uint32_t SET_MASK = SET_CAP - 1;
    CallsiteHash seen[SET_CAP]{};

    [[nodiscard, gnu::hot]] bool has(CallsiteHash hash) const noexcept {
        // Rejected up front, because a query for the empty marker would
        // otherwise match the first empty slot the probe reaches.
        if (!hash) return false;
        uint32_t idx = static_cast<uint32_t>(hash.raw()) & SET_MASK;
        for (uint32_t p = 0; p < SET_CAP; p++) {
            const auto& h = seen[(idx + p) & SET_MASK];
            if (h == hash) return true;
            if (h == CallsiteHash{}) return false;
        }
        return false;
    }

    // The parameter type carries the not-the-empty-marker invariant, so the
    // body below needs no check of its own.
    using NonZeroHash = ::fixy::NonZero<CallsiteHash>;

    void insert(NonZeroHash hash_nz, std::string filename, std::string funcname, int32_t lineno) {
        const CallsiteHash hash = hash_nz.value();
        if (has(hash)) return;
        uint32_t idx = static_cast<uint32_t>(hash.raw()) & SET_MASK;
        for (uint32_t p = 0; p < SET_CAP; p++) {
            auto& h = seen[(idx + p) & SET_MASK];
            if (h == CallsiteHash{}) {
                h = hash;
                // This is where the line number is checked: the checked mint
                // of the wrapper is the guard.
                entries.emplace(hash, ::fixy::mint_tagged<::fixy::tags::source::Interned>(std::move(filename)),
                                ::fixy::mint_tagged<::fixy::tags::source::Interned>(std::move(funcname)),
                                ::fixy::mint_refined<::fixy::non_negative>(lineno));
                return;
            }
        }
    }

    // These overloads strip the incoming tag and forward. The table only
    // ever prints these strings in diagnostics and never opens them as paths
    // or passes them to a shell, so the tag records where a string came from
    // rather than gating what may be done with it.
    using ExternalName = ::fixy::Tagged<std::string, ::fixy::tags::source::External>;
    using InternalName = ::fixy::Tagged<std::string, ::fixy::tags::source::FromInternal>;

    void insert(NonZeroHash hash_nz, ExternalName filename, ExternalName funcname, int32_t lineno) {
        insert(hash_nz, std::move(filename).into(), std::move(funcname).into(), lineno);
    }

    void insert(NonZeroHash hash_nz, InternalName filename, InternalName funcname, int32_t lineno) {
        insert(hash_nz, std::move(filename).into(), std::move(funcname).into(), lineno);
    }

    [[nodiscard]] uint32_t size() const { return static_cast<uint32_t>(entries.size()); }
};

}  // namespace crucible
