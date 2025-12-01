## fand

简体中文 | [English](README.md)

一个用 C 写的适用于树莓派 4B 的 PWM 风扇控制守护进程。

如果修改得当，可能可以支持其他树莓派型号，甚至是其他单板机。

## 功能特性

- 通过命名管道实现手动控制
- 使用实时调度器，在较高系统负载下仍然保持稳定
- 通过 C 语言和硬件 PWM 实现极低的 CPU & 内存占用

## 安装

首先安装 `WiringPi`。

```
git clone https://github.com/cly767/fand
cd fand
make
sudo make install
systemctl enable --now fand.service
```

## 食用方法

`fand` 默认以自动方式启动。

手动调整风扇转速：

```
# echo [value] > [fifo_path]
```

例如：

```
# echo 70 > /run/fanctl
```

将 `[value]` 换成一个超范围的值就可以切换回自动模式。参见[#配置](#配置) 及 [#逻辑](#逻辑)。

## 配置

`fand` 的配置文件就是 `config.h`，这一点借鉴了 [suckless](https://suckless.org) 的做法。要更新配置，你需要重新编译 `fand`。

各配置项的解释如下：

|配置项|解释|例子|
|------|----|----|
|`macro DEBUG`|定义这个宏可以打开更多输出信息（温度，置空比，手动/自动切换，etc）|`#define DEBUG`|
|`macro HARD_PWM`|是否使用树莓派 4B 的硬件 PWM 发生器。如果你不使用板载的模拟音频输出，推荐使用硬件 PWM。它可以输出更高更稳定的频率（在 `fand` 中默认是 25kHz），对 PWM 风扇控制更友好。~~（不然就等着听沙哑的噪音吧！）~~|`#define HARD_PWM 1`|
|`const int fan_control`|风扇控制引脚的 BCM GPIO 编号。如果 `HARD_PWM` 为 1, 那么 `fan_control` 必须为下列值之一：12，18；或者13，19。每一对引脚共用一个 PWM 分值器。|`18`|
|`const char temp_path[]`|`/sys` 中的温度信息。`fand` 只使用一个温度信息文件，因为我的 Pi4B 上就是只有一个。|`"/sys/class/thermal/thermal_zone0/temp"`|
|`const char fifo_path[]`|用于手动控制的命名管道。将来可能会换成 socket 方案（好像更好）。~~我不会 POSIX~~|`"/run/fanctl"`|
|`const unsigned interval`|更新间隔，以毫秒为单位。**切勿使用过小的间隔**，如果 `fand` 运行在 `SCHED_FIFO` 调度器下可能会**用尽所有 CPU 时间，使系统彻底卡死！**|`3000`|
|`const double threshold`|风扇启动阈值，以摄氏度为单位。|`53.5`|
|`const double hysteris`|温度介于 `(threshold, threshold - hysteris)` 之间时，`fand` 以空闲模式运行（风扇以 `dc_low` 指定的最低速度运行）。|`5.0`|
|`const unsigned idle_timeout`|`fand` 在空闲模式运行的最长时间，以毫秒为单位。|`60000`|
|`const int dc_low`|最低占空比，防止某些风扇在低占空比下停转。|`50`|
|`static inline int fan_curve(void)`|风扇占空比曲线。函数头上面的 `static double temp` 只是一个前向声明。|`{ return temp <= 80 ? (-0.05555)*(temp - 80)*(temp - 80) + 100 : 100'; }`|

## 逻辑

### 状态 *states*

- **运行中** *running*：根据 `fan_curve` 或者手动控制调节风扇
- **空闲** *idle*：当温度介于 `(threshold, threshold - hysteris` 之间时，以 `dc_low` 指定的最低速度运行
- **停止** *stopped*：当温度低于 `threshold - hysteris` 时完全停转

### “运行中”的模式 *modes*

- **自动** *auto*：根据核心温度和风扇曲线计算占空比
- **手动** *manual*：从 `fifo_path` 获取占空比。当 `fifo_path` 中有新数据时，`fand` 将会强制进入手动模式（也会进入**运行中**状态）

## License

MIT
