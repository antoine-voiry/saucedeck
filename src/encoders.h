#pragma once

// EC11 knobs on the ESP32-S3 PCNT (hardware quadrature): no edges lost while the loop
// is busy with SPI/I2C. Falls back to polled decoding if PCNT setup fails.
void encodersBegin();
void encodersRead(int &knob1, int &knob2);  // detents since last call, CW > 0
