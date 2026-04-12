#pragma once
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct DaydreamData {
    uint8_t seq;
    bool volUp;
    bool volDown;
    bool app;
    bool home;
    bool click;
    float touchX; // 0.0 to 1.0
    float touchY; // 0.0 to 1.0
    float accelX, accelY, accelZ;
    float gyroX, gyroY, gyroZ;
    float oriX, oriY, oriZ;
};

class DaydreamPacketParser {
private:
    static int extractBits(const uint8_t* data, int bitOffset, int bitCount) {
        int result = 0;
        for (int i = 0; i < bitCount; ++i) {
            int bitIdx = bitOffset + i;
            int byteIdx = bitIdx / 8;
            int bitInByte = 7 - (bitIdx % 8); 
            if (data[byteIdx] & (1 << bitInByte)) {
                result |= (1 << (bitCount - 1 - i));
            }
        }
        return result;
    }

    static int extractSigned13Bits(const uint8_t* data, int bitOffset) {
        int val = extractBits(data, bitOffset, 13);
        if (val & (1 << 12)) {
            val |= 0xFFFFE000;
        }
        return val;
    }

public:
    static DaydreamData Parse(const uint8_t* data, int length) {
        DaydreamData result = {};
        if (length < 20) return result;

        result.seq = extractBits(data, 9, 5);

        result.oriX = extractSigned13Bits(data, 14) * (2.0f * M_PI / 4095.0f);
        result.oriY = extractSigned13Bits(data, 27) * (2.0f * M_PI / 4095.0f);
        result.oriZ = extractSigned13Bits(data, 40) * (2.0f * M_PI / 4095.0f);

        result.accelX = extractSigned13Bits(data, 53) * (8.0f * 9.8f / 4095.0f);
        result.accelY = extractSigned13Bits(data, 66) * (8.0f * 9.8f / 4095.0f);
        result.accelZ = extractSigned13Bits(data, 79) * (8.0f * 9.8f / 4095.0f);

        result.gyroX = extractSigned13Bits(data, 92) * (2048.0f / 180.0f * M_PI / 4095.0f);
        result.gyroY = extractSigned13Bits(data, 105) * (2048.0f / 180.0f * M_PI / 4095.0f);
        result.gyroZ = extractSigned13Bits(data, 118) * (2048.0f / 180.0f * M_PI / 4095.0f);

        // Normalize touchpad 0..255 to 0.0..1.0
        // Or -1.0 to 1.0. Let's make it -1.0 to 1.0 since OpenVR uses -1 to 1.
        result.touchX = ((data[16] / 255.0f) * 2.0f) - 1.0f;
        result.touchY = ((data[17] / 255.0f) * 2.0f) - 1.0f;

        // Button state
        uint8_t buttons = data[18];
        result.click   = (buttons & 0x01) != 0;
        result.home    = (buttons & 0x02) != 0;
        result.app     = (buttons & 0x04) != 0;
        result.volDown = (buttons & 0x08) != 0;
        result.volUp   = (buttons & 0x10) != 0;

        return result;
    }
};
