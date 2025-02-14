#pragma once

#include "../tas5805m.h"

#ifdef __cplusplus
extern "C" {
#endif

static const struct reg_sequence tas5805m_init_sequence[] = {
// RESET
    { TAS5805M_REG_PAGE_SET, 0x00 },
    { TAS5805M_REG_BOOK_SET, 0x00 },
    { 0x03, 0x02 },
    { 0x01, 0x11 },
    { 0x03, 0x02 },
    { TAS5805M_CFG_META_DELAY, 5 },
    { 0x03, 0x00 },
    { 0x46, 0x11 },
    { 0x03, 0x02 },
    { 0x61, 0x0b },
    { 0x60, 0x01 },
    { 0x7d, 0x11 },
    { 0x7e, 0xff },
    { TAS5805M_REG_PAGE_SET, 0x01 },
    { 0x51, 0x05 },
    { TAS5805M_REG_PAGE_SET, 0x00 },
    { 0x66, 0x87 }, //   EQReg
    { TAS5805M_REG_BOOK_SET, 0x8c },
    { TAS5805M_REG_PAGE_SET, 0x29 },
    { 0x18, 0x00 }, //  Input Mixer Left to left = -6 dB
    { 0x19, 0x40 },
    { 0x1a, 0x26 },
    { 0x1b, 0xe7 },
    { 0x1c, 0x00 }, //  Input Mixer Right to left = -6 dB
    { 0x1d, 0x40 },
    { 0x1e, 0x26 },
    { 0x1f, 0xe7 },
    { 0x20, 0x00 }, //  Input Mixer Left to right = -110 dB
    { 0x21, 0x00 },
    { 0x22, 0x00 },
    { 0x23, 0x00 },
    { 0x24, 0x00 }, //  Input Mixer Right to right = -110 dB
    { 0x25, 0x00 },
    { 0x26, 0x00 },
    { 0x27, 0x00 },
    { 0x28, 0x00 }, //  Mono Mixer Left to Woofer = -6 dB
    { 0x29, 0x40 },
    { 0x2a, 0x26 },
    { 0x2b, 0xe7 },
    { 0x2c, 0x00 }, //  Mono Mixer Right to Woofer = -6 dB
    { 0x2d, 0x40 },
    { 0x2e, 0x26 },
    { 0x2f, 0xe7 },
    { 0x30, 0x00 }, //  Mono Mixer Left EQ to Woofer = -110 dB
    { 0x31, 0x00 },
    { 0x32, 0x00 },
    { 0x33, 0x00 },
    { 0x34, 0x00 }, //  Mono Mixer Right EQ to Woofer = -110 dB
    { 0x35, 0x00 },
    { 0x36, 0x00 },
    { 0x37, 0x00 },
    { TAS5805M_REG_PAGE_SET, 0x2a },
    { 0x24, 0x00 }, //  Volume Left = 0 dB
    { 0x25, 0x80 },
    { 0x26, 0x00 },
    { 0x27, 0x00 },
    { 0x28, 0x00 }, //  Volume Right = 0 dB
    { 0x29, 0x80 },
    { 0x2a, 0x00 },
    { 0x2b, 0x00 },
    { 0x2c, 0x00 }, //  Sub Volume = 0 dB
    { 0x2d, 0x80 },
    { 0x2e, 0x00 },
    { 0x2f, 0x00 },
    { 0x30, 0x00 }, //  Volume Alpha = 3 ms
    { 0x31, 0xe2 },
    { 0x32, 0xc4 },
    { 0x33, 0x6b },
    { TAS5805M_REG_PAGE_SET, 0x2c },
    { 0x0c, 0x00 }, //  Left Input to Left Level Meter = -110 dB
    { 0x0d, 0x00 },
    { 0x0e, 0x00 },
    { 0x0f, 0x00 },
    { 0x10, 0x00 }, //  Right Input to Left Level Meter = -110 dB
    { 0x11, 0x00 },
    { 0x12, 0x00 },
    { 0x13, 0x00 },
    { 0x14, 0x00 }, //  Left Output to Left Level Meter = 0 dB
    { 0x15, 0x80 },
    { 0x16, 0x00 },
    { 0x17, 0x00 },
    { 0x18, 0x00 }, //  Right Output to Left Level Meter = -110 dB
    { 0x19, 0x00 },
    { 0x1a, 0x00 },
    { 0x1b, 0x00 },
    { 0x1c, 0x00 }, //  Output Crossbar Left to Amp Left = 0 dB
    { 0x1d, 0x80 },
    { 0x1e, 0x00 },
    { 0x1f, 0x00 },
    { 0x20, 0x00 }, //  Output Crossbar Right to Amp Left = -110 dB
    { 0x21, 0x00 },
    { 0x22, 0x00 },
    { 0x23, 0x00 },
    { 0x24, 0x00 }, //  Output Crossbar Mono to Amp Left = -110 dB
    { 0x25, 0x00 },
    { 0x26, 0x00 },
    { 0x27, 0x00 },
    { 0x28, 0x00 }, //  Output Crossbar Left to Amp Right = -110 dB
    { 0x29, 0x00 },
    { 0x2a, 0x00 },
    { 0x2b, 0x00 },
    { 0x2c, 0x00 }, //  Output Crossbar Right to Amp Right = -110 dB
    { 0x2d, 0x00 },
    { 0x2e, 0x00 },
    { 0x2f, 0x00 },
    { 0x30, 0x00 }, //  Output Crossbar Mono to Amp Right = 0 dB
    { 0x31, 0x80 },
    { 0x32, 0x00 },
    { 0x33, 0x00 },
    { 0x34, 0x00 }, //  Output Crossbar Left to I2S Left = 0 dB
    { 0x35, 0x80 },
    { 0x36, 0x00 },
    { 0x37, 0x00 },
    { 0x38, 0x00 }, //  Output Crossbar Right to I2S Left = -110 dB
    { 0x39, 0x00 },
    { 0x3a, 0x00 },
    { 0x3b, 0x00 },
    { 0x3c, 0x00 }, //  Output Crossbar Mono to I2S Left = -110 dB
    { 0x3d, 0x00 },
    { 0x3e, 0x00 },
    { 0x3f, 0x00 },
    { 0x48, 0x00 }, //  Output Crossbar Left to I2S Right = -110 dB
    { 0x49, 0x00 },
    { 0x4a, 0x00 },
    { 0x4b, 0x00 },
    { 0x4c, 0x00 }, //  Output Crossbar Right to I2S Right = -110 dB
    { 0x4d, 0x00 },
    { 0x4e, 0x00 },
    { 0x4f, 0x00 },
    { 0x50, 0x00 }, //  Output Crossbar Mono to I2S Right = 0 dB
    { 0x51, 0x80 },
    { 0x52, 0x00 },
    { 0x53, 0x00 },
    { 0x5c, 0x00 }, //  AGL Release Rate: 0.001
    { 0x5d, 0x00 },
    { 0x5e, 0xae },
    { 0x5f, 0xc3 },
    { 0x60, 0x01 }, //  AGL Attack Rate: 0.2
    { 0x61, 0x12 },
    { 0x62, 0x6e },
    { 0x63, 0x98 },
    { 0x64, 0x08 }, //  AGL Threshold: 0 dB
    { 0x65, 0x00 },
    { 0x66, 0x00 },
    { 0x67, 0x00 },
    { 0x68, 0x40 }, //  AGL OnOff: 0
    { 0x69, 0x00 },
    { 0x6a, 0x00 },
    { 0x6b, 0x00 },
    { 0x6c, 0x04 }, //  AGL Softening Alpha: 0.55 ms
    { 0x6d, 0xc1 },
    { 0x6e, 0xff },
    { 0x6f, 0x93 },
    { 0x74, 0x00 }, //  Thermal Foldback Scale: 0 dB
    { 0x75, 0x80 },
    { 0x76, 0x00 },
    { 0x77, 0x00 },
    { TAS5805M_REG_PAGE_SET, 0x2d },
    { 0x18, 0x7b }, //  AGL Softening Omega: 0.55 ms
    { 0x19, 0x3e },
    { 0x1a, 0x00 },
    { 0x1b, 0x6d },
    { 0x1c, 0x00 }, //  Level Meter Time Constant = 1000 ms
    { 0x1d, 0x00 },
    { 0x1e, 0xae },
    { 0x1f, 0xc3 },
    { 0x20, 0x00 }, //  Left Input to Right Level Meter = -110 dB
    { 0x21, 0x00 },
    { 0x22, 0x00 },
    { 0x23, 0x00 },
    { 0x24, 0x00 }, //  Right Input to Right Level Meter = -110 dB
    { 0x25, 0x00 },
    { 0x26, 0x00 },
    { 0x27, 0x00 },
    { 0x28, 0x00 }, //  Left Output to Right Level Meter = -110 dB
    { 0x29, 0x00 },
    { 0x2a, 0x00 },
    { 0x2b, 0x00 },
    { 0x2c, 0x00 }, //  Right Output to Right Level Meter = 0 dB
    { 0x2d, 0x80 },
    { 0x2e, 0x00 },
    { 0x2f, 0x00 },
    { TAS5805M_REG_PAGE_SET, 0x2e },
    { 0x24, 0x02 }, //  Thermal Foldback Time Constant: 1 ms
    { 0x25, 0xa3 },
    { 0x26, 0x9a },
    { 0x27, 0xcc },
    { TAS5805M_REG_PAGE_SET, 0x00 },
    { TAS5805M_REG_BOOK_SET, 0xaa },
    { TAS5805M_REG_PAGE_SET, 0x24 },
    { 0x18, 0x08 }, //  Biquad -  BQ1 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x19, 0x00 },
    { 0x1a, 0x00 },
    { 0x1b, 0x00 },
    { 0x1c, 0x00 },
    { 0x1d, 0x00 },
    { 0x1e, 0x00 },
    { 0x1f, 0x00 },
    { 0x20, 0x00 },
    { 0x21, 0x00 },
    { 0x22, 0x00 },
    { 0x23, 0x00 },
    { 0x24, 0x00 },
    { 0x25, 0x00 },
    { 0x26, 0x00 },
    { 0x27, 0x00 },
    { 0x28, 0x00 },
    { 0x29, 0x00 },
    { 0x2a, 0x00 },
    { 0x2b, 0x00 },
    { 0x2c, 0x08 }, //  Biquad -  BQ2 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x2d, 0x00 },
    { 0x2e, 0x00 },
    { 0x2f, 0x00 },
    { 0x30, 0x00 },
    { 0x31, 0x00 },
    { 0x32, 0x00 },
    { 0x33, 0x00 },
    { 0x34, 0x00 },
    { 0x35, 0x00 },
    { 0x36, 0x00 },
    { 0x37, 0x00 },
    { 0x38, 0x00 },
    { 0x39, 0x00 },
    { 0x3a, 0x00 },
    { 0x3b, 0x00 },
    { 0x3c, 0x00 },
    { 0x3d, 0x00 },
    { 0x3e, 0x00 },
    { 0x3f, 0x00 },
    { 0x40, 0x08 }, //  Biquad -  BQ3 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x41, 0x00 },
    { 0x42, 0x00 },
    { 0x43, 0x00 },
    { 0x44, 0x00 },
    { 0x45, 0x00 },
    { 0x46, 0x00 },
    { 0x47, 0x00 },
    { 0x48, 0x00 },
    { 0x49, 0x00 },
    { 0x4a, 0x00 },
    { 0x4b, 0x00 },
    { 0x4c, 0x00 },
    { 0x4d, 0x00 },
    { 0x4e, 0x00 },
    { 0x4f, 0x00 },
    { 0x50, 0x00 },
    { 0x51, 0x00 },
    { 0x52, 0x00 },
    { 0x53, 0x00 },
    { 0x54, 0x08 }, //  Biquad -  BQ4 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x55, 0x00 },
    { 0x56, 0x00 },
    { 0x57, 0x00 },
    { 0x58, 0x00 },
    { 0x59, 0x00 },
    { 0x5a, 0x00 },
    { 0x5b, 0x00 },
    { 0x5c, 0x00 },
    { 0x5d, 0x00 },
    { 0x5e, 0x00 },
    { 0x5f, 0x00 },
    { 0x60, 0x00 },
    { 0x61, 0x00 },
    { 0x62, 0x00 },
    { 0x63, 0x00 },
    { 0x64, 0x00 },
    { 0x65, 0x00 },
    { 0x66, 0x00 },
    { 0x67, 0x00 },
    { 0x68, 0x08 }, //  Biquad -  BQ5 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x69, 0x00 },
    { 0x6a, 0x00 },
    { 0x6b, 0x00 },
    { 0x6c, 0x00 },
    { 0x6d, 0x00 },
    { 0x6e, 0x00 },
    { 0x6f, 0x00 },
    { 0x70, 0x00 },
    { 0x71, 0x00 },
    { 0x72, 0x00 },
    { 0x73, 0x00 },
    { 0x74, 0x00 },
    { 0x75, 0x00 },
    { 0x76, 0x00 },
    { 0x77, 0x00 },
    { 0x78, 0x00 },
    { 0x79, 0x00 },
    { 0x7a, 0x00 },
    { 0x7b, 0x00 },
    { 0x7c, 0x08 }, //  Biquad -  BQ6 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x7d, 0x00 },
    { 0x7e, 0x00 },
    { TAS5805M_REG_BOOK_SET, 0x00 },
    { TAS5805M_REG_PAGE_SET, 0x25 },
    { 0x08, 0x00 },
    { 0x09, 0x00 },
    { 0x0a, 0x00 },
    { 0x0b, 0x00 },
    { 0x0c, 0x00 },
    { 0x0d, 0x00 },
    { 0x0e, 0x00 },
    { 0x0f, 0x00 },
    { 0x10, 0x00 },
    { 0x11, 0x00 },
    { 0x12, 0x00 },
    { 0x13, 0x00 },
    { 0x14, 0x00 },
    { 0x15, 0x00 },
    { 0x16, 0x00 },
    { 0x17, 0x00 },
    { 0x18, 0x08 }, //  Biquad -  BQ7 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x19, 0x00 },
    { 0x1a, 0x00 },
    { 0x1b, 0x00 },
    { 0x1c, 0x00 },
    { 0x1d, 0x00 },
    { 0x1e, 0x00 },
    { 0x1f, 0x00 },
    { 0x20, 0x00 },
    { 0x21, 0x00 },
    { 0x22, 0x00 },
    { 0x23, 0x00 },
    { 0x24, 0x00 },
    { 0x25, 0x00 },
    { 0x26, 0x00 },
    { 0x27, 0x00 },
    { 0x28, 0x00 },
    { 0x29, 0x00 },
    { 0x2a, 0x00 },
    { 0x2b, 0x00 },
    { 0x2c, 0x08 }, //  Biquad -  BQ8 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x2d, 0x00 },
    { 0x2e, 0x00 },
    { 0x2f, 0x00 },
    { 0x30, 0x00 },
    { 0x31, 0x00 },
    { 0x32, 0x00 },
    { 0x33, 0x00 },
    { 0x34, 0x00 },
    { 0x35, 0x00 },
    { 0x36, 0x00 },
    { 0x37, 0x00 },
    { 0x38, 0x00 },
    { 0x39, 0x00 },
    { 0x3a, 0x00 },
    { 0x3b, 0x00 },
    { 0x3c, 0x00 },
    { 0x3d, 0x00 },
    { 0x3e, 0x00 },
    { 0x3f, 0x00 },
    { 0x40, 0x08 }, //  Biquad -  BQ9 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x41, 0x00 },
    { 0x42, 0x00 },
    { 0x43, 0x00 },
    { 0x44, 0x00 },
    { 0x45, 0x00 },
    { 0x46, 0x00 },
    { 0x47, 0x00 },
    { 0x48, 0x00 },
    { 0x49, 0x00 },
    { 0x4a, 0x00 },
    { 0x4b, 0x00 },
    { 0x4c, 0x00 },
    { 0x4d, 0x00 },
    { 0x4e, 0x00 },
    { 0x4f, 0x00 },
    { 0x50, 0x00 },
    { 0x51, 0x00 },
    { 0x52, 0x00 },
    { 0x53, 0x00 },
    { 0x54, 0x08 }, //  Biquad -  BQ10 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x55, 0x00 },
    { 0x56, 0x00 },
    { 0x57, 0x00 },
    { 0x58, 0x00 },
    { 0x59, 0x00 },
    { 0x5a, 0x00 },
    { 0x5b, 0x00 },
    { 0x5c, 0x00 },
    { 0x5d, 0x00 },
    { 0x5e, 0x00 },
    { 0x5f, 0x00 },
    { 0x60, 0x00 },
    { 0x61, 0x00 },
    { 0x62, 0x00 },
    { 0x63, 0x00 },
    { 0x64, 0x00 },
    { 0x65, 0x00 },
    { 0x66, 0x00 },
    { 0x67, 0x00 },
    { 0x68, 0x08 }, //  Biquad -  BQ11 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x69, 0x00 },
    { 0x6a, 0x00 },
    { 0x6b, 0x00 },
    { 0x6c, 0x00 },
    { 0x6d, 0x00 },
    { 0x6e, 0x00 },
    { 0x6f, 0x00 },
    { 0x70, 0x00 },
    { 0x71, 0x00 },
    { 0x72, 0x00 },
    { 0x73, 0x00 },
    { 0x74, 0x00 },
    { 0x75, 0x00 },
    { 0x76, 0x00 },
    { 0x77, 0x00 },
    { 0x78, 0x00 },
    { 0x79, 0x00 },
    { 0x7a, 0x00 },
    { 0x7b, 0x00 },
    { 0x7c, 0x08 }, //  Biquad -  BQ12 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x7d, 0x00 },
    { 0x7e, 0x00 },
    { TAS5805M_REG_BOOK_SET, 0x00 },
    { TAS5805M_REG_PAGE_SET, 0x26 },
    { 0x08, 0x00 },
    { 0x09, 0x00 },
    { 0x0a, 0x00 },
    { 0x0b, 0x00 },
    { 0x0c, 0x00 },
    { 0x0d, 0x00 },
    { 0x0e, 0x00 },
    { 0x0f, 0x00 },
    { 0x10, 0x00 },
    { 0x11, 0x00 },
    { 0x12, 0x00 },
    { 0x13, 0x00 },
    { 0x14, 0x00 },
    { 0x15, 0x00 },
    { 0x16, 0x00 },
    { 0x17, 0x00 },
    { 0x18, 0x08 }, //  Biquad -  BQ13 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x19, 0x00 },
    { 0x1a, 0x00 },
    { 0x1b, 0x00 },
    { 0x1c, 0x00 },
    { 0x1d, 0x00 },
    { 0x1e, 0x00 },
    { 0x1f, 0x00 },
    { 0x20, 0x00 },
    { 0x21, 0x00 },
    { 0x22, 0x00 },
    { 0x23, 0x00 },
    { 0x24, 0x00 },
    { 0x25, 0x00 },
    { 0x26, 0x00 },
    { 0x27, 0x00 },
    { 0x28, 0x00 },
    { 0x29, 0x00 },
    { 0x2a, 0x00 },
    { 0x2b, 0x00 },
    { 0x2c, 0x08 }, //  Biquad -  BQ14 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x2d, 0x00 },
    { 0x2e, 0x00 },
    { 0x2f, 0x00 },
    { 0x30, 0x00 },
    { 0x31, 0x00 },
    { 0x32, 0x00 },
    { 0x33, 0x00 },
    { 0x34, 0x00 },
    { 0x35, 0x00 },
    { 0x36, 0x00 },
    { 0x37, 0x00 },
    { 0x38, 0x00 },
    { 0x39, 0x00 },
    { 0x3a, 0x00 },
    { 0x3b, 0x00 },
    { 0x3c, 0x00 },
    { 0x3d, 0x00 },
    { 0x3e, 0x00 },
    { 0x3f, 0x00 },
    { 0x40, 0x08 }, //  Biquad -  BQ15 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x41, 0x00 },
    { 0x42, 0x00 },
    { 0x43, 0x00 },
    { 0x44, 0x00 },
    { 0x45, 0x00 },
    { 0x46, 0x00 },
    { 0x47, 0x00 },
    { 0x48, 0x00 },
    { 0x49, 0x00 },
    { 0x4a, 0x00 },
    { 0x4b, 0x00 },
    { 0x4c, 0x00 },
    { 0x4d, 0x00 },
    { 0x4e, 0x00 },
    { 0x4f, 0x00 },
    { 0x50, 0x00 },
    { 0x51, 0x00 },
    { 0x52, 0x00 },
    { 0x53, 0x00 },
    { 0x54, 0x08 }, //  Biquad -  BQ1 Right   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x55, 0x00 },
    { 0x56, 0x00 },
    { 0x57, 0x00 },
    { 0x58, 0x00 },
    { 0x59, 0x00 },
    { 0x5a, 0x00 },
    { 0x5b, 0x00 },
    { 0x5c, 0x00 },
    { 0x5d, 0x00 },
    { 0x5e, 0x00 },
    { 0x5f, 0x00 },
    { 0x60, 0x00 },
    { 0x61, 0x00 },
    { 0x62, 0x00 },
    { 0x63, 0x00 },
    { 0x64, 0x00 },
    { 0x65, 0x00 },
    { 0x66, 0x00 },
    { 0x67, 0x00 },
    { 0x68, 0x08 }, //  Biquad -  BQ2 Right   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x69, 0x00 },
    { 0x6a, 0x00 },
    { 0x6b, 0x00 },
    { 0x6c, 0x00 },
    { 0x6d, 0x00 },
    { 0x6e, 0x00 },
    { 0x6f, 0x00 },
    { 0x70, 0x00 },
    { 0x71, 0x00 },
    { 0x72, 0x00 },
    { 0x73, 0x00 },
    { 0x74, 0x00 },
    { 0x75, 0x00 },
    { 0x76, 0x00 },
    { 0x77, 0x00 },
    { 0x78, 0x00 },
    { 0x79, 0x00 },
    { 0x7a, 0x00 },
    { 0x7b, 0x00 },
    { 0x7c, 0x08 }, //  Biquad -  BQ3 Right   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x7d, 0x00 },
    { 0x7e, 0x00 },
    { TAS5805M_REG_BOOK_SET, 0x00 },
    { TAS5805M_REG_PAGE_SET, 0x27 },
    { 0x08, 0x00 },
    { 0x09, 0x00 },
    { 0x0a, 0x00 },
    { 0x0b, 0x00 },
    { 0x0c, 0x00 },
    { 0x0d, 0x00 },
    { 0x0e, 0x00 },
    { 0x0f, 0x00 },
    { 0x10, 0x00 },
    { 0x11, 0x00 },
    { 0x12, 0x00 },
    { 0x13, 0x00 },
    { 0x14, 0x00 },
    { 0x15, 0x00 },
    { 0x16, 0x00 },
    { 0x17, 0x00 },
    { 0x18, 0x08 }, //  Biquad -  BQ4 Right   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x19, 0x00 },
    { 0x1a, 0x00 },
    { 0x1b, 0x00 },
    { 0x1c, 0x00 },
    { 0x1d, 0x00 },
    { 0x1e, 0x00 },
    { 0x1f, 0x00 },
    { 0x20, 0x00 },
    { 0x21, 0x00 },
    { 0x22, 0x00 },
    { 0x23, 0x00 },
    { 0x24, 0x00 },
    { 0x25, 0x00 },
    { 0x26, 0x00 },
    { 0x27, 0x00 },
    { 0x28, 0x00 },
    { 0x29, 0x00 },
    { 0x2a, 0x00 },
    { 0x2b, 0x00 },
    { 0x2c, 0x08 }, //  Biquad -  BQ5 Right   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x2d, 0x00 },
    { 0x2e, 0x00 },
    { 0x2f, 0x00 },
    { 0x30, 0x00 },
    { 0x31, 0x00 },
    { 0x32, 0x00 },
    { 0x33, 0x00 },
    { 0x34, 0x00 },
    { 0x35, 0x00 },
    { 0x36, 0x00 },
    { 0x37, 0x00 },
    { 0x38, 0x00 },
    { 0x39, 0x00 },
    { 0x3a, 0x00 },
    { 0x3b, 0x00 },
    { 0x3c, 0x00 },
    { 0x3d, 0x00 },
    { 0x3e, 0x00 },
    { 0x3f, 0x00 },
    { 0x40, 0x08 }, //  Biquad -  BQ6 Right   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x41, 0x00 },
    { 0x42, 0x00 },
    { 0x43, 0x00 },
    { 0x44, 0x00 },
    { 0x45, 0x00 },
    { 0x46, 0x00 },
    { 0x47, 0x00 },
    { 0x48, 0x00 },
    { 0x49, 0x00 },
    { 0x4a, 0x00 },
    { 0x4b, 0x00 },
    { 0x4c, 0x00 },
    { 0x4d, 0x00 },
    { 0x4e, 0x00 },
    { 0x4f, 0x00 },
    { 0x50, 0x00 },
    { 0x51, 0x00 },
    { 0x52, 0x00 },
    { 0x53, 0x00 },
    { 0x54, 0x08 }, //  Biquad -  BQ7 Right   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x55, 0x00 },
    { 0x56, 0x00 },
    { 0x57, 0x00 },
    { 0x58, 0x00 },
    { 0x59, 0x00 },
    { 0x5a, 0x00 },
    { 0x5b, 0x00 },
    { 0x5c, 0x00 },
    { 0x5d, 0x00 },
    { 0x5e, 0x00 },
    { 0x5f, 0x00 },
    { 0x60, 0x00 },
    { 0x61, 0x00 },
    { 0x62, 0x00 },
    { 0x63, 0x00 },
    { 0x64, 0x00 },
    { 0x65, 0x00 },
    { 0x66, 0x00 },
    { 0x67, 0x00 },
    { 0x68, 0x08 }, //  Biquad -  BQ8 Right   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x69, 0x00 },
    { 0x6a, 0x00 },
    { 0x6b, 0x00 },
    { 0x6c, 0x00 },
    { 0x6d, 0x00 },
    { 0x6e, 0x00 },
    { 0x6f, 0x00 },
    { 0x70, 0x00 },
    { 0x71, 0x00 },
    { 0x72, 0x00 },
    { 0x73, 0x00 },
    { 0x74, 0x00 },
    { 0x75, 0x00 },
    { 0x76, 0x00 },
    { 0x77, 0x00 },
    { 0x78, 0x00 },
    { 0x79, 0x00 },
    { 0x7a, 0x00 },
    { 0x7b, 0x00 },
    { 0x7c, 0x08 }, //  Biquad -  BQ9 Right   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x7d, 0x00 },
    { 0x7e, 0x00 },
    { TAS5805M_REG_BOOK_SET, 0x00 },
    { TAS5805M_REG_PAGE_SET, 0x28 },
    { 0x08, 0x00 },
    { 0x09, 0x00 },
    { 0x0a, 0x00 },
    { 0x0b, 0x00 },
    { 0x0c, 0x00 },
    { 0x0d, 0x00 },
    { 0x0e, 0x00 },
    { 0x0f, 0x00 },
    { 0x10, 0x00 },
    { 0x11, 0x00 },
    { 0x12, 0x00 },
    { 0x13, 0x00 },
    { 0x14, 0x00 },
    { 0x15, 0x00 },
    { 0x16, 0x00 },
    { 0x17, 0x00 },
    { 0x18, 0x08 }, //  Biquad -  BQ10 Right   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x19, 0x00 },
    { 0x1a, 0x00 },
    { 0x1b, 0x00 },
    { 0x1c, 0x00 },
    { 0x1d, 0x00 },
    { 0x1e, 0x00 },
    { 0x1f, 0x00 },
    { 0x20, 0x00 },
    { 0x21, 0x00 },
    { 0x22, 0x00 },
    { 0x23, 0x00 },
    { 0x24, 0x00 },
    { 0x25, 0x00 },
    { 0x26, 0x00 },
    { 0x27, 0x00 },
    { 0x28, 0x00 },
    { 0x29, 0x00 },
    { 0x2a, 0x00 },
    { 0x2b, 0x00 },
    { 0x2c, 0x08 }, //  Biquad -  BQ11 Right   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x2d, 0x00 },
    { 0x2e, 0x00 },
    { 0x2f, 0x00 },
    { 0x30, 0x00 },
    { 0x31, 0x00 },
    { 0x32, 0x00 },
    { 0x33, 0x00 },
    { 0x34, 0x00 },
    { 0x35, 0x00 },
    { 0x36, 0x00 },
    { 0x37, 0x00 },
    { 0x38, 0x00 },
    { 0x39, 0x00 },
    { 0x3a, 0x00 },
    { 0x3b, 0x00 },
    { 0x3c, 0x00 },
    { 0x3d, 0x00 },
    { 0x3e, 0x00 },
    { 0x3f, 0x00 },
    { 0x40, 0x08 }, //  Biquad -  BQ12 Right   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x41, 0x00 },
    { 0x42, 0x00 },
    { 0x43, 0x00 },
    { 0x44, 0x00 },
    { 0x45, 0x00 },
    { 0x46, 0x00 },
    { 0x47, 0x00 },
    { 0x48, 0x00 },
    { 0x49, 0x00 },
    { 0x4a, 0x00 },
    { 0x4b, 0x00 },
    { 0x4c, 0x00 },
    { 0x4d, 0x00 },
    { 0x4e, 0x00 },
    { 0x4f, 0x00 },
    { 0x50, 0x00 },
    { 0x51, 0x00 },
    { 0x52, 0x00 },
    { 0x53, 0x00 },
    { 0x54, 0x08 }, //  Biquad -  BQ13 Right   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x55, 0x00 },
    { 0x56, 0x00 },
    { 0x57, 0x00 },
    { 0x58, 0x00 },
    { 0x59, 0x00 },
    { 0x5a, 0x00 },
    { 0x5b, 0x00 },
    { 0x5c, 0x00 },
    { 0x5d, 0x00 },
    { 0x5e, 0x00 },
    { 0x5f, 0x00 },
    { 0x60, 0x00 },
    { 0x61, 0x00 },
    { 0x62, 0x00 },
    { 0x63, 0x00 },
    { 0x64, 0x00 },
    { 0x65, 0x00 },
    { 0x66, 0x00 },
    { 0x67, 0x00 },
    { 0x68, 0x08 }, //  Biquad -  BQ14 Right   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x69, 0x00 },
    { 0x6a, 0x00 },
    { 0x6b, 0x00 },
    { 0x6c, 0x00 },
    { 0x6d, 0x00 },
    { 0x6e, 0x00 },
    { 0x6f, 0x00 },
    { 0x70, 0x00 },
    { 0x71, 0x00 },
    { 0x72, 0x00 },
    { 0x73, 0x00 },
    { 0x74, 0x00 },
    { 0x75, 0x00 },
    { 0x76, 0x00 },
    { 0x77, 0x00 },
    { 0x78, 0x00 },
    { 0x79, 0x00 },
    { 0x7a, 0x00 },
    { 0x7b, 0x00 },
    { 0x7c, 0x08 }, //  Biquad -  BQ15 Right   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x7d, 0x00 },
    { 0x7e, 0x00 },
    { TAS5805M_REG_BOOK_SET, 0x00 },
    { TAS5805M_REG_PAGE_SET, 0x29 },
    { 0x08, 0x00 },
    { 0x09, 0x00 },
    { 0x0a, 0x00 },
    { 0x0b, 0x00 },
    { 0x0c, 0x00 },
    { 0x0d, 0x00 },
    { 0x0e, 0x00 },
    { 0x0f, 0x00 },
    { 0x10, 0x00 },
    { 0x11, 0x00 },
    { 0x12, 0x00 },
    { 0x13, 0x00 },
    { 0x14, 0x00 },
    { 0x15, 0x00 },
    { 0x16, 0x00 },
    { 0x17, 0x00 },
    { 0x38, 0x00 }, //  SubBiquad -  BQ1 Left   -     Filter: Low Pass-Chebyshev  Frequency: 60 Hz  Gain: 1dB  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x39, 0x00 },
    { 0x3a, 0x08 },
    { 0x3b, 0xe0 },
    { 0x3c, 0x00 },
    { 0x3d, 0x00 },
    { 0x3e, 0x11 },
    { 0x3f, 0xc0 },
    { 0x40, 0x00 },
    { 0x41, 0x00 },
    { 0x42, 0x08 },
    { 0x43, 0xe0 },
    { 0x44, 0x0f },
    { 0x45, 0xee },
    { 0x46, 0x47 },
    { 0x47, 0xc2 },
    { 0x48, 0xf8 },
    { 0x49, 0x11 },
    { 0x4a, 0x94 },
    { 0x4b, 0xbd },
    { 0x4c, 0x08 }, //  SubBiquad -  BQ2 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x4d, 0x00 },
    { 0x4e, 0x00 },
    { 0x4f, 0x00 },
    { 0x50, 0x00 },
    { 0x51, 0x00 },
    { 0x52, 0x00 },
    { 0x53, 0x00 },
    { 0x54, 0x00 },
    { 0x55, 0x00 },
    { 0x56, 0x00 },
    { 0x57, 0x00 },
    { 0x58, 0x00 },
    { 0x59, 0x00 },
    { 0x5a, 0x00 },
    { 0x5b, 0x00 },
    { 0x5c, 0x00 },
    { 0x5d, 0x00 },
    { 0x5e, 0x00 },
    { 0x5f, 0x00 },
    { 0x60, 0x08 }, //  SubBiquad -  BQ3 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x61, 0x00 },
    { 0x62, 0x00 },
    { 0x63, 0x00 },
    { 0x64, 0x00 },
    { 0x65, 0x00 },
    { 0x66, 0x00 },
    { 0x67, 0x00 },
    { 0x68, 0x00 },
    { 0x69, 0x00 },
    { 0x6a, 0x00 },
    { 0x6b, 0x00 },
    { 0x6c, 0x00 },
    { 0x6d, 0x00 },
    { 0x6e, 0x00 },
    { 0x6f, 0x00 },
    { 0x70, 0x00 },
    { 0x71, 0x00 },
    { 0x72, 0x00 },
    { 0x73, 0x00 },
    { 0x74, 0x08 }, //  SubBiquad -  BQ4 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x75, 0x00 },
    { 0x76, 0x00 },
    { 0x77, 0x00 },
    { 0x78, 0x00 },
    { 0x79, 0x00 },
    { 0x7a, 0x00 },
    { 0x7b, 0x00 },
    { 0x7c, 0x00 },
    { 0x7d, 0x00 },
    { 0x7e, 0x00 },
    { TAS5805M_REG_BOOK_SET, 0x00 },
    { TAS5805M_REG_PAGE_SET, 0x2a },
    { 0x08, 0x00 },
    { 0x09, 0x00 },
    { 0x0a, 0x00 },
    { 0x0b, 0x00 },
    { 0x0c, 0x00 },
    { 0x0d, 0x00 },
    { 0x0e, 0x00 },
    { 0x0f, 0x00 },
    { 0x10, 0x08 }, //  SubBiquad -  BQ5 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x11, 0x00 },
    { 0x12, 0x00 },
    { 0x13, 0x00 },
    { 0x14, 0x00 },
    { 0x15, 0x00 },
    { 0x16, 0x00 },
    { 0x17, 0x00 },
    { 0x18, 0x00 },
    { 0x19, 0x00 },
    { 0x1a, 0x00 },
    { 0x1b, 0x00 },
    { 0x1c, 0x00 },
    { 0x1d, 0x00 },
    { 0x1e, 0x00 },
    { 0x1f, 0x00 },
    { 0x20, 0x00 },
    { 0x21, 0x00 },
    { 0x22, 0x00 },
    { 0x23, 0x00 },
    { TAS5805M_REG_PAGE_SET, 0x2e },
    { 0x7c, 0x08 }, //  AMP EQ -  BQ1 Left   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x7d, 0x00 },
    { 0x7e, 0x00 },
    { TAS5805M_REG_BOOK_SET, 0x00 },
    { TAS5805M_REG_PAGE_SET, 0x2f },
    { 0x08, 0x00 },
    { 0x09, 0x00 },
    { 0x0a, 0x00 },
    { 0x0b, 0x00 },
    { 0x0c, 0x00 },
    { 0x0d, 0x00 },
    { 0x0e, 0x00 },
    { 0x0f, 0x00 },
    { 0x10, 0x00 },
    { 0x11, 0x00 },
    { 0x12, 0x00 },
    { 0x13, 0x00 },
    { 0x14, 0x00 },
    { 0x15, 0x00 },
    { 0x16, 0x00 },
    { 0x17, 0x00 },
    { 0x1c, 0x08 }, //  AMP EQ -  BQ1 Right   -     Filter: All Pass  Frequency: 1000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x1d, 0x00 },
    { 0x1e, 0x00 },
    { 0x1f, 0x00 },
    { 0x20, 0x00 },
    { 0x21, 0x00 },
    { 0x22, 0x00 },
    { 0x23, 0x00 },
    { 0x24, 0x00 },
    { 0x25, 0x00 },
    { 0x26, 0x00 },
    { 0x27, 0x00 },
    { 0x28, 0x00 },
    { 0x29, 0x00 },
    { 0x2a, 0x00 },
    { 0x2b, 0x00 },
    { 0x2c, 0x00 },
    { 0x2d, 0x00 },
    { 0x2e, 0x00 },
    { 0x2f, 0x00 },
    { TAS5805M_REG_PAGE_SET, 0x2a },
    { 0x48, 0x00 }, //  Low F2 BQ2   -     Filter: Low Pass-ButterWorth 2  Frequency: 200 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x49, 0x05 },
    { 0x4a, 0x83 },
    { 0x4b, 0x2a },
    { 0x4c, 0x00 },
    { 0x4d, 0x05 },
    { 0x4e, 0x83 },
    { 0x4f, 0x2a },
    { 0x50, 0x00 },
    { 0x51, 0x05 },
    { 0x52, 0x83 },
    { 0x53, 0x2a },
    { 0x54, 0x7d },
    { 0x55, 0xa1 },
    { 0x56, 0x77 },
    { 0x57, 0x3d },
    { 0x58, 0x84 },
    { 0x59, 0xa7 },
    { 0x5a, 0x04 },
    { 0x5b, 0xdf },
    { TAS5805M_REG_PAGE_SET, 0x00 },
    { TAS5805M_REG_BOOK_SET, 0x8c },
    { TAS5805M_REG_PAGE_SET, 0x2b },
    { 0x34, 0x00 }, //   DRC Settings of Tweeter band1:   attack: 25 ms, release: 50 ms, energy: 25 ms, Region1 - Threshold: -124 dB, Offset: 0 dB, Ratio: 100   Region2 - Threshold: -60 dB, Offset: 0 dB, Ratio: 100   Region3 - Threshold: 0 dB, Offset: 0 dB, Ratio: 100
    { 0x35, 0x1b },
    { 0x36, 0x4b },
    { 0x37, 0x98 },
    { 0x38, 0x00 },
    { 0x39, 0x1b },
    { 0x3a, 0x4b },
    { 0x3b, 0x98 },
    { 0x3c, 0x00 },
    { 0x3d, 0x0d },
    { 0x3e, 0xa6 },
    { 0x3f, 0x86 },
    { 0x40, 0x00 },
    { 0x41, 0x00 },
    { 0x42, 0x00 },
    { 0x43, 0x00 },
    { 0x44, 0x00 },
    { 0x45, 0x00 },
    { 0x46, 0x00 },
    { 0x47, 0x00 },
    { 0x48, 0xff },
    { 0x49, 0x81 },
    { 0x4a, 0x47 },
    { 0x4b, 0xae },
    { 0x4c, 0xf9 },
    { 0x4d, 0x06 },
    { 0x4e, 0x21 },
    { 0x4f, 0xa9 },
    { 0x50, 0xfe },
    { 0x51, 0x01 },
    { 0x52, 0xc0 },
    { 0x53, 0x79 },
    { 0x54, 0x00 },
    { 0x55, 0x00 },
    { 0x56, 0x00 },
    { 0x57, 0x00 },
    { 0x58, 0x00 },
    { 0x59, 0x00 },
    { 0x5a, 0x00 },
    { 0x5b, 0x00 },
    { TAS5805M_REG_PAGE_SET, 0x2d },
    { 0x58, 0x02 }, //   DRC Settings of Tweeter band3:   attack: 1 ms, release: 10 ms, energy: 1 ms, Region1 - Threshold: -124 dB, Offset: 0 dB, Ratio: 100   Region2 - Threshold: -60 dB, Offset: 0 dB, Ratio: 100   Region3 - Threshold: 0 dB, Offset: 0 dB, Ratio: 100
    { 0x59, 0xa3 },
    { 0x5a, 0x9a },
    { 0x5b, 0xcc },
    { 0x5c, 0x02 },
    { 0x5d, 0xa3 },
    { 0x5e, 0x9a },
    { 0x5f, 0xcc },
    { 0x60, 0x00 },
    { 0x61, 0x44 },
    { 0x62, 0x32 },
    { 0x63, 0x13 },
    { 0x64, 0x00 },
    { 0x65, 0x00 },
    { 0x66, 0x00 },
    { 0x67, 0x00 },
    { 0x68, 0x00 },
    { 0x69, 0x00 },
    { 0x6a, 0x00 },
    { 0x6b, 0x00 },
    { 0x6c, 0xff },
    { 0x6d, 0x81 },
    { 0x6e, 0x47 },
    { 0x6f, 0xae },
    { 0x70, 0xf9 },
    { 0x71, 0x06 },
    { 0x72, 0x21 },
    { 0x73, 0xa9 },
    { 0x74, 0xfe },
    { 0x75, 0x01 },
    { 0x76, 0xc0 },
    { 0x77, 0x79 },
    { 0x78, 0x00 },
    { 0x79, 0x00 },
    { 0x7a, 0x00 },
    { 0x7b, 0x00 },
    { 0x7c, 0x00 },
    { 0x7d, 0x00 },
    { 0x7e, 0x00 },
    { TAS5805M_REG_BOOK_SET, 0x00 },
    { TAS5805M_REG_PAGE_SET, 0x00 },
    { TAS5805M_REG_BOOK_SET, 0xaa },
    { TAS5805M_REG_PAGE_SET, 0x2e },
    { 0x40, 0x06 }, //  Mid F3 BQ3   -     Filter: Low Pass-ButterWorth 2  Frequency: 4000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x41, 0x55 },
    { 0x42, 0xaf },
    { 0x43, 0xd8 },
    { 0x44, 0x06 },
    { 0x45, 0x55 },
    { 0x46, 0xaf },
    { 0x47, 0xd8 },
    { 0x48, 0x06 },
    { 0x49, 0x55 },
    { 0x4a, 0xaf },
    { 0x4b, 0xd8 },
    { 0x4c, 0x51 },
    { 0x4d, 0xe5 },
    { 0x4e, 0x7f },
    { 0x4f, 0x65 },
    { 0x50, 0xc2 },
    { 0x51, 0xde },
    { 0x52, 0x41 },
    { 0x53, 0xd5 },
    { TAS5805M_REG_PAGE_SET, 0x2b },
    { 0x20, 0x58 }, //  High F2 BQ2   -     Filter: High Pass-ButterWorth 2  Frequency: 4000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x21, 0x3b },
    { 0x22, 0x2f },
    { 0x23, 0x3d },
    { 0x24, 0xa7 },
    { 0x25, 0xc4 },
    { 0x26, 0xd0 },
    { 0x27, 0xc3 },
    { 0x28, 0x58 },
    { 0x29, 0x3b },
    { 0x2a, 0x2f },
    { 0x2b, 0x3d },
    { 0x2c, 0x51 },
    { 0x2d, 0xe5 },
    { 0x2e, 0x7f },
    { 0x2f, 0x65 },
    { 0x30, 0xc2 },
    { 0x31, 0xde },
    { 0x32, 0x41 },
    { 0x33, 0xd5 },
    { 0x0c, 0x58 }, //  High F1 BQ1   -     Filter: High Pass-ButterWorth 2  Frequency: 4000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x0d, 0x3b },
    { 0x0e, 0x2f },
    { 0x0f, 0x3d },
    { 0x10, 0xa7 },
    { 0x11, 0xc4 },
    { 0x12, 0xd0 },
    { 0x13, 0xc3 },
    { 0x14, 0x58 },
    { 0x15, 0x3b },
    { 0x16, 0x2f },
    { 0x17, 0x3d },
    { 0x18, 0x51 },
    { 0x19, 0xe5 },
    { 0x1a, 0x7f },
    { 0x1b, 0x65 },
    { 0x1c, 0xc2 },
    { 0x1d, 0xde },
    { 0x1e, 0x41 },
    { 0x1f, 0xd5 },
    { TAS5805M_REG_PAGE_SET, 0x2a },
    { 0x34, 0x00 }, //  Low F1 BQ1   -     Filter: Low Pass-ButterWorth 2  Frequency: 200 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x35, 0x05 },
    { 0x36, 0x83 },
    { 0x37, 0x2a },
    { 0x38, 0x00 },
    { 0x39, 0x05 },
    { 0x3a, 0x83 },
    { 0x3b, 0x2a },
    { 0x3c, 0x00 },
    { 0x3d, 0x05 },
    { 0x3e, 0x83 },
    { 0x3f, 0x2a },
    { 0x40, 0x7d },
    { 0x41, 0xa1 },
    { 0x42, 0x77 },
    { 0x43, 0x3d },
    { 0x44, 0x84 },
    { 0x45, 0xa7 },
    { 0x46, 0x04 },
    { 0x47, 0xdf },
    { TAS5805M_REG_PAGE_SET, 0x00 },
    { TAS5805M_REG_BOOK_SET, 0x8c },
    { TAS5805M_REG_PAGE_SET, 0x2d },
    { 0x30, 0x02 }, //   DRC Settings of Tweeter band2:   attack: 1 ms, release: 100 ms, energy: 1 ms, Region1 - Threshold: -124 dB, Offset: 0 dB, Ratio: 100   Region2 - Threshold: -60 dB, Offset: 0 dB, Ratio: 100   Region3 - Threshold: 0 dB, Offset: 0 dB, Ratio: 100
    { 0x31, 0xa3 },
    { 0x32, 0x9a },
    { 0x33, 0xcc },
    { 0x34, 0x02 },
    { 0x35, 0xa3 },
    { 0x36, 0x9a },
    { 0x37, 0xcc },
    { 0x38, 0x00 },
    { 0x39, 0x06 },
    { 0x3a, 0xd3 },
    { 0x3b, 0x72 },
    { 0x3c, 0x00 },
    { 0x3d, 0x00 },
    { 0x3e, 0x00 },
    { 0x3f, 0x00 },
    { 0x40, 0x00 },
    { 0x41, 0x00 },
    { 0x42, 0x00 },
    { 0x43, 0x00 },
    { 0x44, 0xff },
    { 0x45, 0x81 },
    { 0x46, 0x47 },
    { 0x47, 0xae },
    { 0x48, 0xf9 },
    { 0x49, 0x06 },
    { 0x4a, 0x21 },
    { 0x4b, 0xa9 },
    { 0x4c, 0xfe },
    { 0x4d, 0x01 },
    { 0x4e, 0xc0 },
    { 0x4f, 0x79 },
    { 0x50, 0x00 },
    { 0x51, 0x00 },
    { 0x52, 0x00 },
    { 0x53, 0x00 },
    { 0x54, 0x00 },
    { 0x55, 0x00 },
    { 0x56, 0x00 },
    { 0x57, 0x00 },
    { TAS5805M_REG_PAGE_SET, 0x00 },
    { TAS5805M_REG_BOOK_SET, 0xaa },
    { TAS5805M_REG_PAGE_SET, 0x2a },
    { 0x5c, 0x7d }, //  Mid F1 BQ1   -     Filter: High Pass-ButterWorth 2  Frequency: 200 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x5d, 0xa6 },
    { 0x5e, 0xfa },
    { 0x5f, 0x66 },
    { 0x60, 0x82 },
    { 0x61, 0x59 },
    { 0x62, 0x05 },
    { 0x63, 0x9a },
    { 0x64, 0x7d },
    { 0x65, 0xa6 },
    { 0x66, 0xfa },
    { 0x67, 0x66 },
    { 0x68, 0x7d },
    { 0x69, 0xa1 },
    { 0x6a, 0x77 },
    { 0x6b, 0x3d },
    { 0x6c, 0x84 },
    { 0x6d, 0xa7 },
    { 0x6e, 0x04 },
    { 0x6f, 0xdf },
    { 0x70, 0x7d }, //  Mid F2 BQ2   -     Filter: High Pass-ButterWorth 2  Frequency: 200 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x71, 0xa6 },
    { 0x72, 0xfa },
    { 0x73, 0x66 },
    { 0x74, 0x82 },
    { 0x75, 0x59 },
    { 0x76, 0x05 },
    { 0x77, 0x9a },
    { 0x78, 0x7d },
    { 0x79, 0xa6 },
    { 0x7a, 0xfa },
    { 0x7b, 0x66 },
    { 0x7c, 0x7d },
    { 0x7d, 0xa1 },
    { 0x7e, 0x77 },
    { TAS5805M_REG_BOOK_SET, 0x3d },
    { TAS5805M_REG_PAGE_SET, 0x2b },
    { 0x08, 0x84 },
    { 0x09, 0xa7 },
    { 0x0a, 0x04 },
    { 0x0b, 0xdf },
    { TAS5805M_REG_PAGE_SET, 0x2e },
    { 0x54, 0x06 }, //  Mid F4 BQ4   -     Filter: Low Pass-ButterWorth 2  Frequency: 4000 Hz  QVal: 0.71  Bandwidth: 1000 Hz
    { 0x55, 0x55 },
    { 0x56, 0xaf },
    { 0x57, 0xd8 },
    { 0x58, 0x06 },
    { 0x59, 0x55 },
    { 0x5a, 0xaf },
    { 0x5b, 0xd8 },
    { 0x5c, 0x06 },
    { 0x5d, 0x55 },
    { 0x5e, 0xaf },
    { 0x5f, 0xd8 },
    { 0x60, 0x51 },
    { 0x61, 0xe5 },
    { 0x62, 0x7f },
    { 0x63, 0x65 },
    { 0x64, 0xc2 },
    { 0x65, 0xde },
    { 0x66, 0x41 },
    { 0x67, 0xd5 },
    { TAS5805M_REG_PAGE_SET, 0x00 },
    { TAS5805M_REG_BOOK_SET, 0x8c },
    { TAS5805M_REG_PAGE_SET, 0x2e },
    { 0x10, 0x00 }, //  DRC Tweeter Band3 Mixer Gain: 1
    { 0x11, 0x80 },
    { 0x12, 0x00 },
    { 0x13, 0x00 },
    { 0x0c, 0x00 }, //  DRC Tweeter Band2 Mixer Gain: 1
    { 0x0d, 0x80 },
    { 0x0e, 0x00 },
    { 0x0f, 0x00 },
    { 0x08, 0x00 }, //  DRC Tweeter Band1 Mixer Gain: 1
    { 0x09, 0x80 },
    { 0x0a, 0x00 },
    { 0x0b, 0x00 },
    { TAS5805M_REG_PAGE_SET, 0x2b },
    { 0x5c, 0x00 }, //   DRC Settings of Woofer:   attack: 25 ms, release: 50 ms, energy: 25 ms, Region1 - Threshold: -124 dB, Offset: 0 dB, Ratio: 1   Region2 - Threshold: -60 dB, Offset: 0 dB, Ratio: 1   Region3 - Threshold: 0 dB, Offset: 0 dB, Ratio: 100
    { 0x5d, 0x1b },
    { 0x5e, 0x4b },
    { 0x5f, 0x98 },
    { 0x60, 0x00 },
    { 0x61, 0x1b },
    { 0x62, 0x4b },
    { 0x63, 0x98 },
    { 0x64, 0x00 },
    { 0x65, 0x0d },
    { 0x66, 0xa6 },
    { 0x67, 0x86 },
    { 0x68, 0x00 },
    { 0x69, 0x00 },
    { 0x6a, 0x00 },
    { 0x6b, 0x00 },
    { 0x6c, 0x00 },
    { 0x6d, 0x00 },
    { 0x6e, 0x00 },
    { 0x6f, 0x00 },
    { 0x70, 0xff },
    { 0x71, 0x81 },
    { 0x72, 0x47 },
    { 0x73, 0xae },
    { 0x74, 0xf9 },
    { 0x75, 0x06 },
    { 0x76, 0x21 },
    { 0x77, 0xa9 },
    { 0x78, 0xfe },
    { 0x79, 0x01 },
    { 0x7a, 0xc0 },
    { 0x7b, 0x79 },
    { 0x7c, 0x00 },
    { 0x7d, 0x00 },
    { 0x7e, 0x00 },
    { TAS5805M_REG_BOOK_SET, 0x00 },
    { TAS5805M_REG_PAGE_SET, 0x2c },
    { 0x08, 0x00 },
    { 0x09, 0x00 },
    { 0x0a, 0x00 },
    { 0x0b, 0x00 },
    { TAS5805M_REG_PAGE_SET, 0x2e },
    { 0x18, 0x00 }, //   THD Clipper Tweeter: 0 dB
    { 0x19, 0x80 },
    { 0x1a, 0x00 },
    { 0x1b, 0x00 },
    { 0x1c, 0x40 }, //   THD Finevolume Tweeter Left: 0 dB
    { 0x1d, 0x00 },
    { 0x1e, 0x00 },
    { 0x1f, 0x00 },
    { 0x20, 0x40 }, //   THD Finevolume Tweeter Right: 0 dB
    { 0x21, 0x00 },
    { 0x22, 0x00 },
    { 0x23, 0x00 },
    { 0x6c, 0x00 }, //   THD Clipper Woofer: 0 dB
    { 0x6d, 0x80 },
    { 0x6e, 0x00 },
    { 0x6f, 0x00 },
    { 0x70, 0x40 }, //   THD Finevolume Woofer: 0 dB
    { 0x71, 0x00 },
    { 0x72, 0x00 },
    { 0x73, 0x00 },
//Register Tuning
    { TAS5805M_REG_PAGE_SET, 0x00 },
    { TAS5805M_REG_BOOK_SET, 0x00 },
    { 0x02, 0x00 },
    { 0x30, 0x00 },
    { 0x4c, 0x30 },
    { 0x53, 0x00 },
    { 0x54, 0x00 },
    { 0x03, 0x03 },
    { 0x78, 0x80 },

};

#ifdef __cplusplus
}
#endif