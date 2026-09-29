#include "chat/framing.hpp"

#include <algorithm>
#include <utility>

namespace chat::protocol {

std::vector<std::uint8_t> encode_frame(const std::span<const std::uint8_t> payload) {
    if (payload.empty()) {
        throw FrameError("frame payload must not be empty");
    }
    if (payload.size() > kMaxFrameSize) {
        throw FrameError("frame payload exceeds maximum size");
    }

    const auto size = static_cast<std::uint32_t>(payload.size());
    std::vector<std::uint8_t> frame;
    frame.reserve(4U + payload.size());
    frame.push_back(static_cast<std::uint8_t>((size >> 24U) & 0xffU));
    frame.push_back(static_cast<std::uint8_t>((size >> 16U) & 0xffU));
    frame.push_back(static_cast<std::uint8_t>((size >> 8U) & 0xffU));
    frame.push_back(static_cast<std::uint8_t>(size & 0xffU));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

FrameDecoder::FrameDecoder(const std::uint32_t max_frame_size) : max_frame_size_(max_frame_size) {
    if (max_frame_size_ == 0U || max_frame_size_ > kMaxFrameSize) {
        throw FrameError("invalid maximum frame size");
    }
}

std::vector<std::vector<std::uint8_t>>
FrameDecoder::feed(const std::span<const std::uint8_t> bytes) {
    if (failed_) {
        throw FrameError("decoder is in a failed state");
    }

    std::vector<std::vector<std::uint8_t>> frames;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        if (header_size_ < header_.size()) {
            const auto count = std::min(header_.size() - header_size_, bytes.size() - offset);
            std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), count,
                        header_.begin() + static_cast<std::ptrdiff_t>(header_size_));
            header_size_ += count;
            offset += count;
            if (header_size_ < header_.size()) {
                break;
            }

            payload_size_ = (static_cast<std::uint32_t>(header_[0]) << 24U) |
                            (static_cast<std::uint32_t>(header_[1]) << 16U) |
                            (static_cast<std::uint32_t>(header_[2]) << 8U) |
                            static_cast<std::uint32_t>(header_[3]);
            if (payload_size_ == 0U || payload_size_ > max_frame_size_) {
                failed_ = true;
                throw FrameError("invalid frame payload size");
            }
            payload_.reserve(payload_size_);
        }

        const auto remaining = static_cast<std::size_t>(payload_size_) - payload_.size();
        const auto count = std::min(remaining, bytes.size() - offset);
        payload_.insert(payload_.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                        bytes.begin() + static_cast<std::ptrdiff_t>(offset + count));
        offset += count;
        if (payload_.size() == payload_size_) {
            frames.push_back(std::move(payload_));
            payload_.clear();
            payload_size_ = 0;
            header_size_ = 0;
        }
    }
    return frames;
}

void FrameDecoder::finish() const {
    if (failed_) {
        throw FrameError("decoder is in a failed state");
    }
    if (header_size_ != 0U) {
        throw FrameError("connection closed with a truncated frame");
    }
}

} // namespace chat::protocol
