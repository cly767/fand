#pragma once

// should we be more verbose?
//#define DEBUG

// should we use the hardware pwm generator of the Pi4?
#define HARD_PWM 1

// BCM GPIO numbering of control line.
// if HARD_PWM is 1, then fan_control must be one of the following:
// 12, 18; or 13, 19. each pair share a hardware PWM channel.
const int fan_control = 18;

// path to desired sysfs node containing coretemp
const char temp_path[] = "/sys/class/thermal/thermal_zone0/temp";
// path to control knob
const char fifo_path[] = "/run/fanctl";

const unsigned interval = 3000; // update interval, in milliseconds
const double threshold = 50.0; // above which the fan starts, in degrees celsius
const double hysteris = 5.0; // within which below threshold the fan will hold at the lowest speed
const unsigned idle_timeout = 60000; // before which the fan runs in idle state, in milliseconds
const int dc_low = 50; // lowest allowed dc value

static double temp;
static inline int fan_curve(void) {
	return (temp <= 80) ? (-0.05555)*(temp - 80)*(temp - 80) + 100 : 100;
}
