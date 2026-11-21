[README.md](https://github.com/user-attachments/files/35207947/README.md)
# 电控组第四次培训 · 第 9 课综合作业：PWM 驱动 SG90 舵机

- **张正轩**
- ***20263008261***

- **开发板**：STM32F103C8T6 最小系统板
- **开发环境**：STM32CubeMX + CLion（CMake / arm-none-eabi-gcc / OpenOCD）

---

## 一、完成情况

| 层级 | 要求 | 状态 |
|---|---|---|
| 必做 | 舵机按 0° → 45° → 90° → 135° → 180° 依次转动，每点停 1 秒 | ✅ |
| 必做 | OLED 显示「目标角度 / CCR / 脉宽」三行，与舵机同步刷新 | ✅ |
| 必做 | 串口打印与 OLED 相同的内容 | ✅ |
| 进阶 A | 两个按键，一个 +15°、一个 −15°，范围限制 0°~180° | ✅ |
| 进阶 A | 按键消抖，按下立即生效 | ✅ |

> 实现细节：自动序列用 `HAL_GetTick()` 时间戳驱动，**没有用 `HAL_Delay`**，
> 所以序列运行时按键也能立刻响应；一按按键就切换为手动模式，OLED 第 4 行显示 `MODE: MANUAL`。

---

## 二、PSC / ARR 的推导（本次作业的核心）

### 2.1 定时器时钟

TIM2 挂在 **APB1** 总线上。CubeMX 把 HCLK 配到 72 MHz 后，
APB1 的分频系数为 2，即 PCLK1 = 36 MHz。

但 STM32 的规则是：**当 APB 预分频系数 ≠ 1 时，定时器时钟 = PCLK × 2**，所以

```
TIM2 计数时钟 = 36 MHz × 2 = 72 MHz
```

### 2.2 让 1 个计数 = 1 µs

```
计数频率 = 72 MHz / (PSC + 1) = 1 MHz
→ PSC + 1 = 72
→ PSC = 71
```

这样 **CCR 的数值就直接等于脉宽的微秒数**，后面角度换算不用再乘除，也不容易算错。

### 2.3 让周期 = 20 ms（50 Hz）

```
周期 = (ARR + 1) / 1 MHz = 20 ms
→ ARR + 1 = 20000
→ ARR = 19999
```

### 2.4 验算

```
72 MHz ÷ (71 + 1) ÷ (19999 + 1)
= 72 000 000 ÷ 72 ÷ 20000
= 1 000 000 ÷ 20000
= 50 Hz   ✅
```

| 参数 | 值 | 来历 |
|---|---|---|
| 目标频率 | 50 Hz | 舵机只认这个频率 |
| 定时器时钟 | 72 MHz | TIM2 在 APB1，36 MHz × 2 |
| 1 个计数 | 1 µs | 72 MHz ÷ 72 |
| **PSC** | **71** | 72 − 1 |
| **ARR** | **19999** | 20000 − 1 |

---

## 三、角度与脉宽（两端为什么这样取）

SG90 的名义值是 **0.5 ms = 0°，2.5 ms = 180°**，但真实行程普遍比标称窄。
如果直接用到 500 µs / 2500 µs，舵机会顶到机械限位、发出“咔咔”声并堵转发热，时间长了会烧。

所以两端各留余量，取 **550 µs ~ 2450 µs**（约 4.5° 余量）：

```c
/* 角度 → CCR：两端取 550 / 2450，避免顶到机械限位 */
return (uint16_t)(550.0f + angle * (2450.0f - 550.0f) / 180.0f + 0.5f);
```

| 角度 | 脉宽 | CCR（= 脉宽 µs） |
|---|---|---|
| 0° | 550 µs | 550 |
| 45° | 1025 µs | 1025 |
| 90° | 1500 µs | 1500 |
| 135° | 1975 µs | 1975 |
| 180° | 2450 µs | 2450 |

90° 刚好落在 1500 µs —— 这也是上电时的初始位置（中间位置），
避免上电瞬间舵机往某一侧甩到底。

---

## 四、接线

| 信号 | 接到 | 说明 |
|---|---|---|
| 舵机 橙线（信号） | **PA0** | TIM2_CH1，50 Hz PWM |
| 舵机 红线（VCC） | **独立 5V 电源** | 不接板子的 5V，SG90 堵转电流近 700 mA |
| 舵机 棕线（GND） | 独立电源负极 | **同时与板子 GND 相连** |
| 按键「+15°」 | **PA1** ↔ GND | 内部上拉，按下为低电平 |
| 按键「−15°」 | **PA2** ↔ GND | 同上 |
| OLED SCL / SDA | **PB8 / PB9** | 软件 I²C，3.3V 供电 |
| USB 转 TTL TX / RX | **PA10 / PA9** | 交叉接，波特率 115200，必须共地 |

**两个必须照做的地方：**

1. **舵机独立供电**：SG90 堵转电流接近 700 mA，从板子的 5V 取电会抖动、甚至让单片机反复复位；
2. **必须共地**：独立电源的负极一定要和板子的 GND 连在一起，否则 PA0 的信号对方“看不懂”，表现为抖动或偶尔动一下。

---

## 五、OLED 显示格式

四行固定格式，方便验收时一眼核对：

| 行 | 显示示例 | 含义 |
|---|---|---|
| 第 1 行 | `Target:090deg` | 目标角度（3 位定宽） |
| 第 2 行 | `CCR   :1500` | 实际写进比较寄存器的值 |
| 第 3 行 | `Pulse :1.500ms` | 换算出的脉宽，整数/小数分开显示（避开浮点打印） |
| 第 4 行 | `MODE: AUTO` / `MODE: MANUAL` | 当前是自动序列还是按键手动 |

**刷新策略**：只在角度变化时刷新（每次调用 `Servo_Apply()`），不在主循环里反复刷全屏——
软件 I²C 刷一次全屏要几十毫秒，反复刷会明显闪烁，也会拖慢按键响应。

---

## 六、串口输出

使用 GCC 下的 `_write` 重定向，`printf` 直接从 USART1 输出（Keil 的 `__io_putchar` 在这里不适用）：

```c
int _write(int fd, char *ptr, int len)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, (uint16_t)len, 100);
    return len;
}

printf("Target=%3d  CCR=%4d  Pulse=%d.%03dms\r\n",
       (int)deg, (int)g_ccr, (int)(g_ccr / 1000), (int)(g_ccr % 1000));
```

串口助手设置：**115200 / 8 / 无校验 / 1 停止位**，接收选文本模式。

输出示例：

```
Target=  0  CCR= 550  Pulse=0.550ms
Target= 45  CCR=1025  Pulse=1.025ms
Target= 90  CCR=1500  Pulse=1.500ms
Target=135  CCR=1975  Pulse=1.975ms
Target=180  CCR=2450  Pulse=2.450ms
```

截图见：

<img width="937" height="560" alt="image" src="https://github.com/user-attachments/assets/c34e2ad2-c939-42a6-bede-3515c134f6e1" />


---

## 七、代码结构

| 函数 | 作用 |
|---|---|
| `AngleToCCR(angle)` | 角度 → CCR，含 0~180 限幅和两端余量 |
| `Servo_SetAngle(angle)` | 把 CCR 写进 `TIM2_CH1` |
| `OLED_ShowAll(deg, ccr)` | 画四行显示 |
| `Servo_Apply(deg)` | 统一入口：设角度 + 刷屏 + 打印，保证三处数值一致 |
| `Key_Once(...)` | 15 ms 时间戳消抖，按住不放只触发一次 |
| `App_Task()` | 主循环任务：按键 ±15°，无按键时按 1 秒步进自动序列 |
| `_write(...)` | printf 重定向到 USART1 |

主循环：

```c
while (1)
{
    App_Task();      /* 按键扫描 + 自动序列步进，均不阻塞 */
}
```

### 关于 `cmake/` 目录的说明

作业要求仓库里包含 `cmake/` 目录，本工程确实没有这个目录，原因是 **CubeMX 版本差异**：
较新的 CubeMX（6.x）在生成 CLion 工程时，把 CMake 配置全部写在根目录的 `CMakeLists.txt` 里，
不再生成 `cmake/stm32cubemx/CMakeLists.txt` 这种子目录结构。

本工程根目录的 `CMakeLists.txt` 是**自包含**的，关键内容如下：

```cmake
include_directories(Core/Inc Drivers/STM32F1xx_HAL_Driver/Inc ... )   # 头文件路径
file(GLOB_RECURSE SOURCES "Core/*.*" "Drivers/*.*")                  # 自动收集源文件
add_executable(${PROJECT_NAME}.elf ${SOURCES} ${LINKER_SCRIPT})      # 生成 elf
```

也就是说，编译所需的一切都在这个文件里，`cmake/` 目录并非必要。
仓库中其他必需项（`.ioc`、`Core/`、`CMakeLists.txt`、`Drivers/`）均已包含。

---

## 八、调试过程中遇到的问题

| 现象 | 原因 | 解决 |
|---|---|---|
| 链接报 `non constant or forward reference address expression for section .ARM.extab` | 新版 CubeMX 生成的链接脚本里 `(READONLY)` 是 GCC 11+ 语法，本机是 GCC 10.3.1 | 把 `.ld` 里 5 处 `(READONLY)` 改成 `()` |
| CLion 运行时 JSON 报 `could not connect (error 138)`、`target is 'exec'` | 运行配置是普通 CMake Application，不是 `OpenOCD Download & Run` | 改用 OpenOCD 命令行烧录（`program … verify reset exit`） |
| 舵机上电不动 / 偶尔抽搐一下 | 舵机电源与板子未共地、或电源电流不足 | 独立 5V 供电，并把电源负极与板子 GND 相连 |

---

## 九、参考资料与驱动来源

- **STM32CubeMX / HAL 库**：ST 官方，`STM32F1xx_HAL_Driver`
- **OLED 驱动**：电控组群文件提供的软件 I²C 驱动（`OLED.c` / `OLED.h` / `OLED_Font.h`）
  - 本人做了以下修改，均在代码注释中注明：
    1. SCL/SDA 引脚设为 **PB8 / PB9**；
    2. 显示方向由 `0xA1 / 0xC8` 改为 **`0xA0 / 0xC0`**（本模块针脚朝上）；
    3. 显示起始行由 `0x40` 改为 **`0x42`**（修正末行绕回顶部）；
    4. 补全 `OLED_I2C_Init()` 中 `GPIO_InitTypeDef` 的零初始化与 `Pull = GPIO_NOPULL`。
- **参考资料**：《南海鲨电控第四次培训·自学引导文档》第 9 课；Keysking《STM32 入门》定时器与 PWM 相关章节；江协科技教程 PWM/舵机章节；ST UM1850（HAL 库说明）
