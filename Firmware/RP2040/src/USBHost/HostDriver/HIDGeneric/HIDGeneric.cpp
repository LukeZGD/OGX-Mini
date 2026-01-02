#include <cstring>
#include <memory>

#include "tusb.h"

#include "USBHost/HIDParser/HIDReportDescriptor.h"
#include "USBHost/HostDriver/HIDGeneric/HIDGeneric.h"

void HIDHost::initialize(Gamepad &gamepad, uint8_t address, uint8_t instance,
                         const uint8_t *report_desc, uint16_t desc_len) {
  if (!report_desc || desc_len == 0) {
    return;
  }

  report_desc_len_ = desc_len;
  std::memcpy(report_desc_buffer_.data(), report_desc,
              std::min(static_cast<size_t>(report_desc_len_),
                       report_desc_buffer_.size()));
  hid_joystick_ =
      std::make_unique<HIDJoystick>(std::make_shared<HIDReportDescriptor>(
          report_desc_buffer_.data(), report_desc_len_));

  tuh_hid_receive_report(address, instance);
}

void HIDHost::process_report(Gamepad &gamepad, uint8_t address,
                             uint8_t instance, const uint8_t *report,
                             uint16_t len) {
  if (std::memcmp(prev_report_in_.data(), report, len) == 0) {
    tuh_hid_receive_report(address, instance);
    return;
  }

  std::memcpy(prev_report_in_.data(), report, len);
  if (!hid_joystick_->parseData(const_cast<uint8_t *>(report), len,
                                &hid_joystick_data_)) {
    tuh_hid_receive_report(address, instance);
    return;
  }

  Gamepad::PadIn gp_in;

  switch (hid_joystick_data_.hat_switch) {
  case HIDJoystickHatSwitch::UP:
    gp_in.dpad |= gamepad.MAP_DPAD_UP;
    break;
  case HIDJoystickHatSwitch::UP_RIGHT:
    gp_in.dpad |= gamepad.MAP_DPAD_UP_RIGHT;
    break;
  case HIDJoystickHatSwitch::RIGHT:
    gp_in.dpad |= gamepad.MAP_DPAD_RIGHT;
    break;
  case HIDJoystickHatSwitch::DOWN_RIGHT:
    gp_in.dpad |= gamepad.MAP_DPAD_DOWN_RIGHT;
    break;
  case HIDJoystickHatSwitch::DOWN:
    gp_in.dpad |= gamepad.MAP_DPAD_DOWN;
    break;
  case HIDJoystickHatSwitch::DOWN_LEFT:
    gp_in.dpad |= gamepad.MAP_DPAD_DOWN_LEFT;
    break;
  case HIDJoystickHatSwitch::LEFT:
    gp_in.dpad |= gamepad.MAP_DPAD_LEFT;
    break;
  case HIDJoystickHatSwitch::UP_LEFT:
    gp_in.dpad |= gamepad.MAP_DPAD_UP_LEFT;
    break;
  default:
    break;
  }

  std::tie(gp_in.joystick_lx, gp_in.joystick_ly) =
      gamepad.scale_joystick_l(hid_joystick_data_.X, hid_joystick_data_.Y);
  std::tie(gp_in.joystick_rx, gp_in.joystick_ry) =
      gamepad.scale_joystick_r(hid_joystick_data_.Z, hid_joystick_data_.Rz);

  if (report_desc_len_ == 176) {
    // PS2->PS3 Adapter special mapping
    // User reported: Tri->Squ(X), Squ->Tri(Y), Cir->Cross(A), Cross->Cir(B)
    // with default mapping. Fixed mapping:
    if (hid_joystick_data_.buttons[1])
      gp_in.buttons |= gamepad.MAP_BUTTON_Y; // Triangle
    if (hid_joystick_data_.buttons[2])
      gp_in.buttons |= gamepad.MAP_BUTTON_B; // Circle
    if (hid_joystick_data_.buttons[3])
      gp_in.buttons |= gamepad.MAP_BUTTON_A; // Cross
    if (hid_joystick_data_.buttons[4])
      gp_in.buttons |= gamepad.MAP_BUTTON_X; // Square
  } else {
    // Standard Generic Mapping
    if (hid_joystick_data_.buttons[1])
      gp_in.buttons |= gamepad.MAP_BUTTON_X;
    if (hid_joystick_data_.buttons[2])
      gp_in.buttons |= gamepad.MAP_BUTTON_A;
    if (hid_joystick_data_.buttons[3])
      gp_in.buttons |= gamepad.MAP_BUTTON_B;
    if (hid_joystick_data_.buttons[4])
      gp_in.buttons |= gamepad.MAP_BUTTON_Y;
  }
  if (hid_joystick_data_.buttons[5])
    gp_in.buttons |= gamepad.MAP_BUTTON_LB;
  if (hid_joystick_data_.buttons[6])
    gp_in.buttons |= gamepad.MAP_BUTTON_RB;
  if (hid_joystick_data_.buttons[7])
    gp_in.trigger_l = Range::MAX<uint8_t>;
  if (hid_joystick_data_.buttons[8])
    gp_in.trigger_r = Range::MAX<uint8_t>;
  if (hid_joystick_data_.buttons[9])
    gp_in.buttons |= gamepad.MAP_BUTTON_BACK;
  if (hid_joystick_data_.buttons[10])
    gp_in.buttons |= gamepad.MAP_BUTTON_START;
  if (hid_joystick_data_.buttons[11])
    gp_in.buttons |= gamepad.MAP_BUTTON_L3;
  if (hid_joystick_data_.buttons[12])
    gp_in.buttons |= gamepad.MAP_BUTTON_R3;
  if (hid_joystick_data_.buttons[13])
    gp_in.buttons |= gamepad.MAP_BUTTON_SYS;
  if (hid_joystick_data_.buttons[14])
    gp_in.buttons |= gamepad.MAP_BUTTON_MISC;

  // PS3 Guitar Hero tilt sensor: Read raw accelerometer X from HID report
  // PS3 Guitar (VID 0x12BA) - Attempting new offset 41-42
  // Previous attempt at 42-43 failed.
  // Standard PS3 report usually has sensors starting around 41.
  if (len >= 43) {
    uint16_t raw_accel = (static_cast<uint16_t>(report[41]) << 8) | report[42];
    gp_in.accel_x = static_cast<int16_t>(raw_accel >> 6);
  }

  gamepad.set_pad_in(gp_in);

  tuh_hid_receive_report(address, instance);
}

bool HIDHost::send_feedback(Gamepad &gamepad, uint8_t address,
                            uint8_t instance) {
  // Power Saving: Vibration disabled by user request to prevent disconnects.
  // Adapter demands 500mA which exceeds available power when cascaded.
  return true;
}