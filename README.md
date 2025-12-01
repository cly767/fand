## fand

[简体中文](README.zh_CN.md) | English

Simple PWM fan controlling for the Raspberry Pi 4B, in plain C.

May support other Pis or even other SBCs if you hack it properly.

## Features

- Manual control via named pipes
- Real-time scheduling, allowing for stable responses even under high system loads
- Tiny CPU & memory footprint tnanks to hardware PWM & C

## Installation

First make sure you have the `WiringPi` library installed.

```
git clone https://github.com/cly767/fand
cd fand
make
sudo make install
systemctl enable --now fand.service
```

## Usage

`fand` starts in auto mode by default.

To manually adjust the fan speed:

```
# echo [value] > [fifo_path]
```

e.g.

```
# echo 70 > /run/fanctl
```

To switch back to auto mode, issue the above command but replace `[value]` with an out-of-range value. See [#Configuration](#Configuration) and [#Logic](#Logic) for details.

## Configuration

Configuration of `fand` is done through the `config.h` header file, similar to the [suckless](https://suckless.org) fashion. Updating the configuration will require re-compiling.

Options explained:

|Option|Explanation|Example|
|--------|--------|---------|
|`macro DEBUG`|Define this macro to enable some verbose outputs(temperature, duty cycle, mode switching, etc.)|`#define DEBUG`|
|`macro HARD_PWM`|Whether to use the hardware PWM generator of the Raspberry Pi 4. Hardware PWM is recommended, if you do not use the onboard analog audio, as it produces higher and more stable frequencies (default is 25kHz in `fand`) which is more feasible in PWM fan controlling.|`#define HARD_PWM 1`|
|`const int fan_control`|BCM GPIO numbering of the fan control line. If `HARD_PWM` is 1, then `fan_control` must be one of the following: 12, 18; or 13, 19. Each pair share a hardware PWM channel.|`18`|
|`const char temp_path[]`|Path to one of the sysfs nodes in `/sys` containing the core temperature. `fand` only uses one thermal file as this is the case on the Pi4B.|`"/sys/class/thermal/thermal_zone0/temp"`|
|`const char fifo_path[]`|Path to the named pipe used for manual control. Planned to be replaced by a socket as this seems to be a better option. ~~I'm new to POSIX~~|`"/run/fanctl"`|
|`const unsigned interval`|Update interval, in milliseconds. **Avoid using tiny invervals** as `fand` runs under `SCHED_FIFO` and may **use up all CPU time(effectively hang the system entirely!**|`3000`|
|`const double threshold`|Above which the fan starts, in degrees Celsius.|`53.5`|
|`const double hysteris`|Within which below `threshold`, `fand` will run in idle state(the fan will be held at the lowest speed specified by `dc_low`).|`5.0`|
|`const unsigned idle_timeout`|Before which `fand` runs in idle state, in milliseconds.|`60000`|
|`const int dc_low`|Lowest allowed ducy cycle, to avoid some fans stopping under low ducy cycles.|`50`|
|`static inline int fan_curve(void)`|The fan duty cycle curve. The above `static double temp` is just a forward declaration.|`{ return temp <= 80 ? (-0.05555)*(temp - 80)*(temp - 80) + 100 : 100'; }`|

## Logic

### states

- **running**: running according to fan curve or manual control
- **idle**: running at lowest speed specified by `dc_low` with temperature within `hysteris` degrees C below `threshold`
- **stopped**: completely stopping the fan with temperature below `threshold - hysteris`

### running modes

- **auto**: calculate the duty cycle from the core temperature according to the fan curve
- **manual**: get the duty cycle from `fifo_path`. `fand` is forced into this mode (and implicitly the **running** state when there is new value arriving through `fifo_path`)

## License

MIT
