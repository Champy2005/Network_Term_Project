/** @file
 * @brief Declare bounded big-endian framing and incremental TCP stream decoding.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace chat::protocol {

inline constexpr std::uint32_t kMaxFrameSize = 1024U * 1024U;

/// @brief A fatal invalid-length or truncated-frame error.
class FrameError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// CNChat v1 frame: four-byte unsigned big-endian payload length, then payload.
/// A frame must contain between 1 and kMaxFrameSize bytes of serialized protobuf.
[[nodiscard]] std::vector<std::uint8_t> encode_frame(std::span<const std::uint8_t> payload);

/// Incrementally extracts frames from an arbitrary TCP byte stream. One feed()
/// can yield zero, one, or multiple complete payloads. A framing error is fatal
/// for this decoder and discards frames completed during that feed() call;
/// close the connection rather than trying to recover its byte stream.
class FrameDecoder {
  public:
    /// @brief Set a positive receive limit no larger than the protocol maximum.
    explicit FrameDecoder(std::uint32_t max_frame_size = kMaxFrameSize);

    /// @brief Consume bytes and return zero or more complete payloads in order.
    [[nodiscard]] std::vector<std::vector<std::uint8_t>> feed(std::span<const std::uint8_t> bytes);

    /// Call at EOF to reject a truncated header or payload.
    void finish() const;

  private:
    std::uint32_t max_frame_size_;
    std::array<std::uint8_t, 4> header_{};
    std::size_t header_size_ = 0;
    std::uint32_t payload_size_ = 0;
    std::vector<std::uint8_t> payload_;
    bool failed_ = false;
};

} // namespace chat::protocol
