#pragma once

#include <stdint.h>

namespace PS3Guitar {
// PS3 Guitar Hero Controller Report
// Based on Linux HID descriptor capture and raw report analysis
// Report size: 27 bytes (without Report ID) or 28 bytes (with Report ID)
// 
// HID Descriptor parsed from Linux:
// - 13 buttons + 3 padding (2 bytes)
// - 4-bit HAT switch + 4-bit padding (1 byte)  
// - 4x 8-bit joystick axes (4 bytes)
// - 12x vendor-defined bytes (12 bytes)
// - 4x 16-bit accelerometer/gyro (8 bytes, Little Endian, 0-1023 range)
//
// Raw data verified:
// Repouso:   Accel X = 455 (0x01C7)
// Levantada: Accel X = 388 (0x0184)

struct InReport {
  // Bytes 0-1: Buttons (13 buttons + 3 padding bits)
  // buttons0: Select, L3, R3, Start, DPad Up, DPad Right, DPad Down, DPad Left
  // buttons1: L2, R2, Orange(L1), R1, Yellow(△), Red(○), Blue(×), Green(□)
  uint8_t buttons0;
  uint8_t buttons1;

  // Byte 2: D-Pad HAT (lower 4 bits) + PS button and padding (upper 4 bits)
  // HAT: 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW, 0x0F=center
  uint8_t buttons2;

  // Bytes 3-6: Joystick axes (0-255, center=128)
  uint8_t joystick_lx;  // Byte 3
  uint8_t joystick_ly;  // Byte 4
  uint8_t joystick_rx;  // Byte 5 - Whammy Bar
  uint8_t joystick_ry;  // Byte 6

  // Bytes 7-18: Vendor-defined data (12 bytes)
  uint8_t vendor_data[12];

  // Bytes 19-26: Motion sensors (16-bit Little Endian, 0-1023 range)
  // Tilt detection: accel_x decreases when guitar is raised
  // Normal ~455, Tilted ~388
  uint16_t accel_x;  // Bytes 19-20 - TILT SENSOR
  uint16_t accel_y;  // Bytes 21-22
  uint16_t accel_z;  // Bytes 23-24
  uint16_t gyro_z;   // Bytes 25-26
} __attribute__((packed));

static_assert(sizeof(InReport) == 27, "PS3Guitar::InReport must be 27 bytes");

struct Buttons0 {
  static const uint8_t SELECT = 0x01;
  static const uint8_t L3 = 0x02;
  static const uint8_t R3 = 0x04;
  static const uint8_t START = 0x08;
  static const uint8_t STRUM_UP = 0x10; // D-Pad Up
  static const uint8_t DPAD_RIGHT = 0x20;
  static const uint8_t STRUM_DOWN = 0x40; // D-Pad Down
  static const uint8_t DPAD_LEFT = 0x80;
};

struct Buttons1 {
  static const uint8_t L2 = 0x01;
  static const uint8_t R2 = 0x02;
  static const uint8_t ORANGE = 0x04; // L1
  static const uint8_t R1 = 0x08;
  static const uint8_t YELLOW = 0x10; // Triangle
  static const uint8_t RED = 0x20;    // Circle
  static const uint8_t BLUE = 0x40;   // Cross
  static const uint8_t GREEN = 0x80;  // Square
};

struct Buttons2 {
  static const uint8_t PS = 0x01; // Bit 0 of Byte 3
};
} // namespace PS3Guitar
