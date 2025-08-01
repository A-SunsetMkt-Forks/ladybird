/*
 * Copyright (c) 2025, Luke Wilde <luke@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibIPC/Decoder.h>
#include <LibIPC/Encoder.h>
#include <LibWeb/ContentSecurityPolicy/SerializedPolicy.h>

namespace IPC {

template<>
ErrorOr<void> encode(Encoder& encoder, Web::ContentSecurityPolicy::SerializedPolicy const& serialized_policy)
{
    TRY(encoder.encode(serialized_policy.directives));
    TRY(encoder.encode(serialized_policy.disposition));
    TRY(encoder.encode(serialized_policy.source));
    TRY(encoder.encode(serialized_policy.self_origin));
    TRY(encoder.encode(serialized_policy.pre_parsed_policy_string));

    return {};
}

template<>
ErrorOr<Web::ContentSecurityPolicy::SerializedPolicy> decode(Decoder& decoder)
{
    return Web::ContentSecurityPolicy::SerializedPolicy {
        .directives = TRY(decoder.decode<Vector<Web::ContentSecurityPolicy::Directives::SerializedDirective>>()),
        .disposition = TRY(decoder.decode<Web::ContentSecurityPolicy::Policy::Disposition>()),
        .source = TRY(decoder.decode<Web::ContentSecurityPolicy::Policy::Source>()),
        .self_origin = TRY(decoder.decode<URL::Origin>()),
        .pre_parsed_policy_string = TRY(decoder.decode<String>()),
    };
}

}
