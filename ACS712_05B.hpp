#ifndef ACS712_05B_H
#define ACS712_05B_H

#define VCC 4.65

#include <Arduino.h>

class ACS712 {
public:

	ACS712(uint8_t _pin) : pin(_pin) {}

	int calibrate() {
		uint16_t acc = 0;
		for (int i = 0; i < 10; i++) { acc += analogRead(pin); }
		zero = acc / 10;
		return zero;
	}

	void setZeroPoint(int _zero) { zero = _zero; }

	void setSensitivity(float sens) { sensitivity = sens; }

	float getCurrentDC() {
		int sum = 0;
		for (int i = 0; i < 10; i++) {
		  sum += analogRead(pin);
		}
    log_i("raw sensor dat: %d", sum / 10);
    float vIout = (float)sum / 10.0 / 4095 * 3.3 * 1.3286;
    log_i("vIout: %lf V", vIout);
    return (vIout - zero) / sensitivity;
  }

  operator float() { return getCurrentDC(); }

private:
  float zero = VCC / 2;
  const float sensitivity = 0.185;
  uint8_t pin;
};

#endif
