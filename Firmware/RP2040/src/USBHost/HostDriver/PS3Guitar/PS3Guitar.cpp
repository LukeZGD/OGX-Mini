#include <algorithm>
#include <cstring>

#include "tusb.h"

#include "Board/ogxm_log.h"
#include "USBHost/HostDriver/PS3Guitar/PS3Guitar.h"

// Debug: Enable to see raw accelerometer values
// #define DEBUG_GUITAR_TILT

// Initialization logic to enable reports (Magic Packet for PS3 controllers)
void PS3GuitarHost::initialize(Gamepad &gamepad, uint8_t address,
                               uint8_t instance, const uint8_t *report_desc,
                               uint16_t desc_len) {
  gamepad.set_analog_host(true);

  init_state_.dev_addr = address;
  init_state_.init_buffer.fill(0);

  // Standard PS3 initialization command (GET_REPORT Feature 0xF2)
  // This wakes up the controller/guitar and makes it start sending input
  // reports
  tusb_control_request_t init_request = {
      .bmRequestType = 0xA1, // Device-to-Host, Class, Interface
      .bRequest = 0x01,      // GET_REPORT
      .wValue = (HID_REPORT_TYPE_FEATURE << 8) | 0xF2,
      .wIndex = 0x0000,
      .wLength = 17};

  send_control_xfer(address, &init_request, init_state_.init_buffer.data(),
                    init_complete_cb,
                    reinterpret_cast<uintptr_t>(&init_state_));

  tuh_hid_receive_report(address, instance);
}

bool PS3GuitarHost::send_control_xfer(uint8_t dev_addr,
                                      const tusb_control_request_t *request,
                                      uint8_t *buffer,
                                      tuh_xfer_cb_t complete_cb,
                                      uintptr_t user_data) {
  tuh_xfer_s transfer = {.daddr = dev_addr,
                         .ep_addr = 0x00,
                         .setup = request,
                         .buffer = buffer,
                         .complete_cb = complete_cb,
                         .user_data = user_data};
  return tuh_control_xfer(&transfer);
}

void PS3GuitarHost::init_complete_cb(tuh_xfer_s *xfer) {
  InitState *init_state = reinterpret_cast<InitState *>(xfer->user_data);
  if (init_state) {
    init_state->stage = InitStage::DONE;
  }
}

void PS3GuitarHost::process_report(Gamepad &gamepad, uint8_t address,
                                   uint8_t instance, const uint8_t *report,
                                   uint16_t len) {
  // PS3 Guitar report is 27 bytes without Report ID
  // TinyUSB may or may not include Report ID as first byte
  constexpr size_t REPORT_SIZE_NO_ID = 27;
  constexpr size_t REPORT_SIZE_WITH_ID = 28;

  // Determine if report includes Report ID
  const uint8_t *report_data = report;
  uint16_t report_len = len;
  
  // If we get 28 bytes and first byte is 0x00 or 0x01, it's a Report ID
  if (len == REPORT_SIZE_WITH_ID && (report[0] == 0x00 || report[0] == 0x01)) {
    report_data = &report[1];
    report_len = len - 1;
  }

  // Need at least 27 bytes (up to and including accel sensors at bytes 19-26)
  if (report_len < REPORT_SIZE_NO_ID) {
    tuh_hid_receive_report(address, instance);
    return;
  }

  // Check if report changed to avoid processing identical data
  if (std::memcmp(&prev_in_report_, report_data, REPORT_SIZE_NO_ID) == 0) {
    tuh_hid_receive_report(address, instance);
    return;
  }

  // Save report for next comparison
  std::memcpy(&prev_in_report_, report_data, REPORT_SIZE_NO_ID);

  // Cast to struct
  const PS3Guitar::InReport *in_report =
      reinterpret_cast<const PS3Guitar::InReport *>(report_data);

  // CRITICAL: Read accel_x manually from raw bytes to avoid alignment issues
  // Bytes 19-20 contain accel_x in Little Endian format
  // Verified: Rest = 455 (0x01C7), Tilted = 388 (0x0184)
  uint16_t raw_accel_x = static_cast<uint16_t>(report_data[19]) | 
                         (static_cast<uint16_t>(report_data[20]) << 8);

#ifdef DEBUG_GUITAR_TILT
  static uint8_t debug_counter = 0;
  if (debug_counter++ % 50 == 0) {
    OGXM_LOG("Guitar[%d]: accel_x=%d (raw bytes: %02X %02X)\n",
             len, raw_accel_x, report_data[19], report_data[20]);
  }
#endif

  Gamepad::PadIn gp_in;

  // --- Button Mapping ---
  // PS3 Guitar -> Generic Gamepad -> XInput Guitar

  // Frets (buttons1)
  if (in_report->buttons1 & PS3Guitar::Buttons1::GREEN)
    gp_in.buttons |= gamepad.MAP_BUTTON_A; // Green -> A
  if (in_report->buttons1 & PS3Guitar::Buttons1::RED)
    gp_in.buttons |= gamepad.MAP_BUTTON_B; // Red -> B
  if (in_report->buttons1 & PS3Guitar::Buttons1::YELLOW)
    gp_in.buttons |= gamepad.MAP_BUTTON_Y; // Yellow -> Y
  if (in_report->buttons1 & PS3Guitar::Buttons1::BLUE)
    gp_in.buttons |= gamepad.MAP_BUTTON_X; // Blue -> X
  if (in_report->buttons1 & PS3Guitar::Buttons1::ORANGE)
    gp_in.buttons |= gamepad.MAP_BUTTON_LB; // Orange -> LB

  // Strum Bar (D-Pad in buttons0)
  if (in_report->buttons0 & PS3Guitar::Buttons0::STRUM_UP)
    gp_in.dpad |= gamepad.MAP_DPAD_UP;
  if (in_report->buttons0 & PS3Guitar::Buttons0::STRUM_DOWN)
    gp_in.dpad |= gamepad.MAP_DPAD_DOWN;

  // Other Buttons
  if (in_report->buttons0 & PS3Guitar::Buttons0::START)
    gp_in.buttons |= gamepad.MAP_BUTTON_START;
  if (in_report->buttons0 & PS3Guitar::Buttons0::SELECT)
    gp_in.buttons |= gamepad.MAP_BUTTON_BACK;
  if (in_report->buttons2 & PS3Guitar::Buttons2::PS)
    gp_in.buttons |= gamepad.MAP_BUTTON_SYS; // Xbox Guide

  // Whammy Bar - mapped to Right Stick X
  gp_in.joystick_rx =
      static_cast<int16_t>((in_report->joystick_rx - 128) * 256);

  // Joystick (some guitars have a small joystick for effects)
  gp_in.joystick_lx =
      static_cast<int16_t>((in_report->joystick_lx - 128) * 256);
  gp_in.joystick_ly =
      static_cast<int16_t>((in_report->joystick_ly - 128) * 256);

  // --- TILT SENSOR IMPLEMENTATION ---
  // Use raw_accel_x read manually from bytes 19-20 (Little Endian)
  // Verified values from real PS3 Guitar:
  //   Rest (flat):    accel_x ~455 (0x01C7)
  //   Tilted (raised): accel_x ~388 (0x0184)
  // Tilt is detected when accel_x DECREASES (guitar neck raised)
  
  // Validate reading is in expected 10-bit range (0-1023)
  if (raw_accel_x > 0 && raw_accel_x < 1024) {
    gp_in.accel_x = static_cast<int16_t>(raw_accel_x);
    
    // DEBUG: Map accel_x to left trigger for visual feedback
    // 0-1023 -> 0-255 scaling
    gp_in.trigger_l = static_cast<uint8_t>(raw_accel_x / 4);
  } else {
    // Invalid reading - use default rest value
    gp_in.accel_x = 455;
  }

  gamepad.set_pad_in(gp_in);

  tuh_hid_receive_report(address, instance);
}

bool PS3GuitarHost::send_feedback(Gamepad &gamepad, uint8_t address,
                                  uint8_t instance) {
  // Guitar Hero guitars don't typically have rumble
  // But we could implement LED control here if needed
  return true;
}
