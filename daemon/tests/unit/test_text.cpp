#include "common/text.h"

#include <gtest/gtest.h>

#include <array>
#include <string>

using namespace readout;

TEST(Text, TakeLineSplitsWithoutNewlines) {
    std::string_view text = "first\nsecond\n\nlast";
    std::string_view line;
    ASSERT_TRUE(takeLine(text, line));
    EXPECT_EQ(line, "first");
    ASSERT_TRUE(takeLine(text, line));
    EXPECT_EQ(line, "second");
    ASSERT_TRUE(takeLine(text, line));
    EXPECT_EQ(line, "");
    ASSERT_TRUE(takeLine(text, line));
    EXPECT_EQ(line, "last");
    EXPECT_FALSE(takeLine(text, line));
}

TEST(Text, TakeTokenSkipsSpacesAndTabs) {
    std::string_view text = "  cpu0\t12  34 ";
    EXPECT_EQ(takeToken(text), "cpu0");
    EXPECT_EQ(takeToken(text), "12");
    EXPECT_EQ(takeToken(text), "34");
    EXPECT_EQ(takeToken(text), "");
    EXPECT_EQ(takeToken(text), "");
}

TEST(Text, ParseNumberHandlesValidAndInvalidInput) {
    EXPECT_EQ(parseNumber<int>("42"), 42);
    EXPECT_EQ(parseNumber<int>(" 42\n"), 42);
    EXPECT_EQ(parseNumber<int>("+7"), 7);
    EXPECT_EQ(parseNumber<int>("-3"), -3);
    EXPECT_EQ(parseNumber<std::uint64_t>("18446744073709551615"), UINT64_MAX);
    EXPECT_FALSE(parseNumber<std::uint64_t>("18446744073709551616").has_value());
    EXPECT_FALSE(parseNumber<int>("").has_value());
    EXPECT_FALSE(parseNumber<int>("abc").has_value());
    EXPECT_DOUBLE_EQ(parseNumber<double>("0.25").value(), 0.25);
}

TEST(Text, ParseHexAcceptsPrefix) {
    EXPECT_EQ(parseHex("0x10de"), 0x10deU);
    EXPECT_EQ(parseHex("8086\n"), 0x8086U);
    EXPECT_EQ(parseHex("0x030000"), 0x030000U);
    EXPECT_FALSE(parseHex("zz").has_value());
}

TEST(Text, SplitKeyValueTrims) {
    std::string_view key;
    std::string_view value;
    ASSERT_TRUE(splitKeyValue("MemTotal:   1024 kB", ':', key, value));
    EXPECT_EQ(key, "MemTotal");
    EXPECT_EQ(value, "1024 kB");
    EXPECT_FALSE(splitKeyValue("no separator", ':', key, value));
}

TEST(Text, SanitizedUtf8KeepsValidText) {
    std::array<char, 64> output{};
    copySanitizedUtf8("Café ☕ 😀", output.data(), output.size());
    EXPECT_STREQ(output.data(), "Café ☕ 😀");
}

TEST(Text, SanitizedUtf8ReplacesInvalidBytes) {
    std::array<char, 64> output{};
    // Lone continuation byte, overlong encoding, truncated sequence, and a surrogate.
    copySanitizedUtf8(std::string_view("a\x80" "b\xC0\xAF" "c\xE2\x82" "\xED\xA0\x80", 11), output.data(), output.size());
    EXPECT_STREQ(output.data(), "a?b??c?????");
}

TEST(Text, SanitizedUtf8TruncatesOnCharacterBoundary) {
    std::array<char, 5> output{};
    copySanitizedUtf8("ab€cd", output.data(), output.size()); // € is 3 bytes
    EXPECT_STREQ(output.data(), "ab");
}

TEST(Text, SanitizedUtf8StopsAtEmbeddedNul) {
    std::array<char, 16> output{};
    copySanitizedUtf8(std::string_view("ab\0cd", 5), output.data(), output.size());
    EXPECT_STREQ(output.data(), "ab?cd");
}
