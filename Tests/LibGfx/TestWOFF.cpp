/*
 * Copyright (c) 2023, Tim Ledbetter <timledbetter@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGfx/Font/WOFF/Loader.h>
#include <LibTest/TestCase.h>

#define TEST_INPUT(x) ("test-inputs/" x)

TEST_CASE(malformed_woff)
{
    Array test_inputs = {
        TEST_INPUT("woff/invalid_sfnt_size.woff"sv)
    };

    for (auto test_input : test_inputs) {
        auto file = MUST(Core::MappedFile::map(test_input));
        auto font_or_error = WOFF::try_load_from_bytes(file->bytes());
        EXPECT(font_or_error.is_error());
    }
}
