#pragma once

// SipHash-2-4 as a message authentication code over a classified key.
// foundation/core/SipHash.h holds the hash over a raw key, the types of its
// key and its tag, and their byte order.
//
// The key is a classified value.  It stays in a fixy::Secret, and each
// operation takes the key and gives it back beside its result.  The key
// leaves the Secret only inside transform(), for the duration of one hash.
//
// A tag leaves classification through HashForCompare: a tag is a hash of
// the message under the key, and it does not release the key.  verify()
// compares the tag it computes with fixy::ct::eq, which reads every byte of
// the two tags.  A refusal then takes the same time wherever the first
// wrong byte is.

#include <fixy/ConstantTime.h>
#include <fixy/Secret.h>
#include <fixy/Tags.h>
#include <foundation/core/Region.h>
#include <foundation/core/SipHash.h>

#include <cstddef>
#include <utility>

namespace fixy::siphash {

// Computes the tag of the message and gives the key back beside it.
[[nodiscard]] constexpr std::pair<Secret<::foundation::core::SipKeyBytes>, ::foundation::core::SipTag>
sign(Secret<::foundation::core::SipKeyBytes>&& key, ::foundation::core::View<std::byte const> message) noexcept {
    Secret<::foundation::core::SipTag> tag = mint_secret<::foundation::core::SipTag>();
    Secret<::foundation::core::SipKeyBytes> kept =
        std::move(key).transform([&](::foundation::core::SipKeyBytes const& raw) noexcept {
            tag = mint_secret<::foundation::core::SipTag>(::foundation::core::sip_hash(raw, message));
            return raw;
        });
    return {std::move(kept), std::move(tag).declassify<tags::secret_policy::HashForCompare>()};
}

// Gives the key back beside the answer to one question: does the tag
// authenticate the message under this key?
[[nodiscard]] constexpr std::pair<Secret<::foundation::core::SipKeyBytes>, bool>
verify(Secret<::foundation::core::SipKeyBytes>&& key, ::foundation::core::View<std::byte const> message,
       ::foundation::core::SipTag const& tag) noexcept {
    Secret<bool> matches = mint_secret<bool>(false);
    Secret<::foundation::core::SipKeyBytes> kept =
        std::move(key).transform([&](::foundation::core::SipKeyBytes const& raw) noexcept {
            ::foundation::core::SipTag const computed = ::foundation::core::sip_hash(raw, message);
            matches = mint_secret<bool>(ct::eq(computed.view(), tag.view()));
            return raw;
        });
    return {std::move(kept), std::move(matches).declassify<tags::secret_policy::HashForCompare>()};
}

}  // namespace fixy::siphash
