#ifndef ACS712_05B_HPP
#define ACS712_05B_HPP

#include <Arduino.h>

class ACS712 {
public:
  ACS712(uint8_t _pin1, uint8_t _pin2) : pin_viout(_pin1), pin_offst(_pin2) {
    float zeroCurrent = 0;
    for (int i = 0; i < 10; i++) { 
      zeroCurrent += readDC();
      delay(50);
    }
    current_offset = zeroCurrent / 10;
  }

	float readDC() {
    int sum_vIout = 0;
    int sum_offst = 0;
		for (int i = 0; i < 10; i++) {
      sum_vIout += analogRead(pin_viout);
      sum_offst += analogRead(pin_offst);
		}
    float vIout = (float)sum_vIout / 10.0 / 4096 * Attenuator;
    float offst = (float)sum_offst / 10.0 / 4096 * Attenuator;
    log_d("vIout: %lf V\tOffset: %lf V", vIout, offst);
    return (vIout - offst) / Sensitivity - current_offset;
  }

  operator float() { return readDC(); }

private:
  const float Sensitivity = 0.185;
  const float Attenuator = 3.6; // 11 dB Attenuator
  float current_offset = 0;
  uint8_t pin_viout;
  uint8_t pin_offst;
};

#endif
