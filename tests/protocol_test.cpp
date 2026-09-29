#include "chat.pb.h"
#include "chat/framing.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

TEST(ProtocolTest, ProtobufEnvelopeRoundTripsThroughFrame) {
    chat::protocol::Envelope outgoing;
    outgoing.set_protocol_version(1);
    outgoing.set_request_id("login-1");
    outgoing.mutable_login_request()->set_username("alice");

    std::string serialized;
    ASSERT_TRUE(outgoing.SerializeToString(&serialized));
    const auto payload = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(serialized.data()), serialized.size());
    const auto frame = chat::protocol::encode_frame(payload);
    chat::protocol::FrameDecoder decoder;
    const auto decoded = decoder.feed(frame);
    ASSERT_EQ(decoded.size(), 1U);

    chat::protocol::Envelope incoming;
    ASSERT_TRUE(
        incoming.ParseFromArray(decoded.front().data(), static_cast<int>(decoded.front().size())));
    EXPECT_EQ(incoming.protocol_version(), 1U);
    EXPECT_EQ(incoming.request_id(), "login-1");
    EXPECT_EQ(incoming.payload_case(), chat::protocol::Envelope::kLoginRequest);
    EXPECT_EQ(incoming.login_request().username(), "alice");
}

TEST(ProtocolTest, MalformedProtobufPayloadIsRejectedByParser) {
    // 0x0a says a length-delimited field follows, but no length byte exists.
    const std::vector<std::uint8_t> malformed{0x0a};
    const auto frame = chat::protocol::encode_frame(malformed);
    chat::protocol::FrameDecoder decoder;
    const auto decoded = decoder.feed(frame);
    ASSERT_EQ(decoded.size(), 1U);
    chat::protocol::Envelope envelope;
    EXPECT_FALSE(
        envelope.ParseFromArray(decoded.front().data(), static_cast<int>(decoded.front().size())));
}

} // namespace
