#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// Minimal U-Blox UBX packet framing used by the RaceBox BLE protocol
// (see "RaceBox BLE Protocol Description", "Packets Format").
namespace ubx {

constexpr uint8_t kSync1 = 0xB5;
constexpr uint8_t kSync2 = 0x62;

constexpr uint8_t CLASS_RACEBOX = 0xFF;

constexpr uint8_t ID_ACK = 0x02;
constexpr uint8_t ID_NACK = 0x03;
constexpr uint8_t ID_RECORDING_STATUS = 0x22;
constexpr uint8_t ID_RECORDING_CONFIG = 0x25;
constexpr uint8_t ID_STATE_CHANGE = 0x26;
constexpr uint8_t ID_UNLOCK_MEMORY = 0x30;

// Encodes a full packet (sync + class/id + length + payload + checksum) into
// outBuf. Returns the number of bytes written, or 0 if outBuf is too small.
size_t buildPacket(uint8_t msgClass, uint8_t msgId, const uint8_t* payload, uint16_t payloadLen,
                    uint8_t* outBuf, size_t outBufSize);

// Reassembles UBX packets out of a stream of bytes that may arrive split
// across, or combined within, multiple BLE notifications (the RaceBox
// protocol doc explicitly requires clients to handle this). Validates the
// checksum before invoking the handler.
class Parser {
public:
    using MessageHandler =
        std::function<void(uint8_t msgClass, uint8_t msgId, const uint8_t* payload, uint16_t payloadLen)>;

    explicit Parser(MessageHandler handler);

    // Feed newly received bytes; may invoke the handler zero or more times.
    void feed(const uint8_t* data, size_t length);

private:
    void processBuffer();

    std::vector<uint8_t> buffer_;
    MessageHandler handler_;
};

} // namespace ubx
