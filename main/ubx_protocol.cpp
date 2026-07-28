#include "ubx_protocol.h"

#include <utility>

namespace ubx {

namespace {
// The protocol caps payloads at 504 bytes (512-byte total packet). Give a
// little headroom for the resync search below.
constexpr size_t kMaxPlausiblePacket = 600;
}

size_t buildPacket(uint8_t msgClass, uint8_t msgId, const uint8_t* payload, uint16_t payloadLen,
                    uint8_t* outBuf, size_t outBufSize) {
    const size_t total = 6 + static_cast<size_t>(payloadLen) + 2;
    if (outBufSize < total) {
        return 0;
    }

    size_t i = 0;
    outBuf[i++] = kSync1;
    outBuf[i++] = kSync2;
    outBuf[i++] = msgClass;
    outBuf[i++] = msgId;
    outBuf[i++] = payloadLen & 0xFF;
    outBuf[i++] = (payloadLen >> 8) & 0xFF;
    for (uint16_t p = 0; p < payloadLen; ++p) {
        outBuf[i++] = payload[p];
    }

    uint8_t ckA = 0, ckB = 0;
    for (size_t k = 2; k < i; ++k) {
        ckA = static_cast<uint8_t>(ckA + outBuf[k]);
        ckB = static_cast<uint8_t>(ckB + ckA);
    }
    outBuf[i++] = ckA;
    outBuf[i++] = ckB;

    return total;
}

Parser::Parser(MessageHandler handler) : handler_(std::move(handler)) {
    buffer_.reserve(kMaxPlausiblePacket);
}

void Parser::feed(const uint8_t* data, size_t length) {
    buffer_.insert(buffer_.end(), data, data + length);
    processBuffer();
}

void Parser::processBuffer() {
    for (;;) {
        while (!buffer_.empty() && buffer_[0] != kSync1) {
            buffer_.erase(buffer_.begin());
        }
        if (buffer_.size() < 2) {
            return;
        }
        if (buffer_[1] != kSync2) {
            buffer_.erase(buffer_.begin());
            continue;
        }
        if (buffer_.size() < 6) {
            return; // need class, id and length fields
        }

        const uint16_t payloadLen = static_cast<uint16_t>(buffer_[4] | (buffer_[5] << 8));
        const size_t totalLen = 6 + static_cast<size_t>(payloadLen) + 2;

        if (totalLen > kMaxPlausiblePacket) {
            // Not a plausible RaceBox packet - a false sync match. Drop the
            // sync bytes and keep searching.
            buffer_.erase(buffer_.begin(), buffer_.begin() + 2);
            continue;
        }
        if (buffer_.size() < totalLen) {
            return; // wait for the rest to arrive in a later notification
        }

        uint8_t ckA = 0, ckB = 0;
        for (size_t k = 2; k < totalLen - 2; ++k) {
            ckA = static_cast<uint8_t>(ckA + buffer_[k]);
            ckB = static_cast<uint8_t>(ckB + ckA);
        }

        if (ckA == buffer_[totalLen - 2] && ckB == buffer_[totalLen - 1]) {
            if (handler_) {
                handler_(buffer_[2], buffer_[3], buffer_.data() + 6, payloadLen);
            }
            buffer_.erase(buffer_.begin(), buffer_.begin() + totalLen);
        } else {
            // Checksum mismatch: this wasn't really a packet start. Drop the
            // sync bytes and resync on the remaining data.
            buffer_.erase(buffer_.begin(), buffer_.begin() + 2);
        }
    }
}

} // namespace ubx
