/** @file
 * @brief Verify frame length bounds, byte order, partial reads, coalescing, and EOF.
 */
#include "chat/protocol/framing.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using chat::protocol::encode_frame;
using chat::protocol::FrameDecoder;
using chat::protocol::FrameError;
using chat::protocol::kMaxFrameSize;

/// @brief Check the exact four header bytes and unchanged payload.
TEST(FramingTest, EncodesBigEndianLengthAndPayload) {
    const std::array<std::uint8_t, 3> payload{0x12, 0x34, 0x56};
    const auto frame = encode_frame(payload);
    EXPECT_EQ(frame, (std::vector<std::uint8_t>{0, 0, 0, 3, 0x12, 0x34, 0x56}));
}

/// @brief Exercise a length requiring more than one nonzero header byte.
TEST(FramingTest, EncodesMultibyteBigEndianLength) {
    const std::vector<std::uint8_t> payload(258, 0x42);
    const auto frame = encode_frame(payload);
    ASSERT_EQ(frame.size(), 262U);
    EXPECT_EQ(frame[0], 0U);
    EXPECT_EQ(frame[1], 0U);
    EXPECT_EQ(frame[2], 1U);
    EXPECT_EQ(frame[3], 2U);
}

/// @brief Retain state when every byte arrives in a separate read.
TEST(FramingTest, DecodesHeaderAndPayloadOneByteAtATime) {
    const std::array<std::uint8_t, 4> payload{1, 2, 3, 4};
    const auto frame = encode_frame(payload);
    FrameDecoder decoder;
    std::vector<std::vector<std::uint8_t>> decoded;
    for (const auto byte : frame) {
        const std::array<std::uint8_t, 1> fragment{byte};
        auto next = decoder.feed(fragment);
        decoded.insert(decoded.end(), next.begin(), next.end());
    }
    decoder.finish();
    ASSERT_EQ(decoded.size(), 1U);
    EXPECT_EQ(decoded.front(), (std::vector<std::uint8_t>{1, 2, 3, 4}));
}

/// @brief Emit coalesced frames in their original order.
TEST(FramingTest, DecodesSeveralFramesInOneRead) {
    const std::array<std::uint8_t, 1> first{0xa1};
    const std::array<std::uint8_t, 2> second{0xb1, 0xb2};
    auto bytes = encode_frame(first);
    const auto next = encode_frame(second);
    bytes.insert(bytes.end(), next.begin(), next.end());

    FrameDecoder decoder;
    const auto decoded = decoder.feed(bytes);
    decoder.finish();
    ASSERT_EQ(decoded.size(), 2U);
    EXPECT_EQ(decoded[0], (std::vector<std::uint8_t>{0xa1}));
    EXPECT_EQ(decoded[1], (std::vector<std::uint8_t>{0xb1, 0xb2}));
}

/// @brief Accept a complete header while waiting for its payload.
TEST(FramingTest, CompletesHeaderBeforeReceivingAnyPayload) {
    FrameDecoder decoder;
    const std::array<std::uint8_t, 4> header{0, 0, 0, 1};
    EXPECT_TRUE(decoder.feed(header).empty());
    const std::array<std::uint8_t, 1> payload{0x7f};
    const auto decoded = decoder.feed(payload);
    ASSERT_EQ(decoded.size(), 1U);
    EXPECT_EQ(decoded.front(), (std::vector<std::uint8_t>{0x7f}));
    decoder.finish();
}

/// @brief Round-trip the inclusive 1 MiB payload limit.
TEST(FramingTest, AcceptsMaximumPayload) {
    const std::vector<std::uint8_t> payload(kMaxFrameSize, 0x5a);
    const auto frame = encode_frame(payload);
    FrameDecoder decoder;
    const auto decoded = decoder.feed(frame);
    ASSERT_EQ(decoded.size(), 1U);
    EXPECT_EQ(decoded.front(), payload);
}

/// @brief Deny outgoing payloads outside the wire bounds.
TEST(FramingTest, RejectsEmptyAndOversizedOutgoingPayloads) {
    EXPECT_THROW((void)encode_frame({}), FrameError);
    const std::vector<std::uint8_t> oversized(kMaxFrameSize + 1U, 0);
    EXPECT_THROW((void)encode_frame(oversized), FrameError);
}

/// @brief Fail the decoder on invalid announced lengths.
TEST(FramingTest, RejectsZeroAndOversizedIncomingLengths) {
    FrameDecoder zero_decoder;
    const std::array<std::uint8_t, 4> zero{0, 0, 0, 0};
    EXPECT_THROW((void)zero_decoder.feed(zero), FrameError);
    EXPECT_THROW((void)zero_decoder.feed(zero), FrameError);

    FrameDecoder large_decoder;
    const std::array<std::uint8_t, 4> large{0, 0x10, 0, 1};
    EXPECT_THROW((void)large_decoder.feed(large), FrameError);
}

/// @brief Apply a stricter caller-specified receive cap.
TEST(FramingTest, EnforcesConfiguredSmallerLimit) {
    EXPECT_THROW(FrameDecoder(0), FrameError);
    EXPECT_THROW(FrameDecoder(kMaxFrameSize + 1U), FrameError);
    FrameDecoder decoder(2);
    const std::array<std::uint8_t, 4> three_bytes{0, 0, 0, 3};
    EXPECT_THROW((void)decoder.feed(three_bytes), FrameError);
}

/// @brief Reject EOF inside either a header or payload.
TEST(FramingTest, RejectsTruncatedFrameAtEof) {
    FrameDecoder header_decoder;
    const std::array<std::uint8_t, 2> partial_header{0, 0};
    EXPECT_TRUE(header_decoder.feed(partial_header).empty());
    EXPECT_THROW(header_decoder.finish(), FrameError);

    FrameDecoder payload_decoder;
    const std::array<std::uint8_t, 5> partial_payload{0, 0, 0, 2, 0x7f};
    EXPECT_TRUE(payload_decoder.feed(partial_payload).empty());
    EXPECT_THROW(payload_decoder.finish(), FrameError);
}

} // namespace
