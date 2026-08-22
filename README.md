# 华为 3.98 寸 A0 四色墨水屏 NodeMCU 测试

本工程用于以 NodeMCU ESP8266 驱动 `SE0398NZ07A0`，并保留手机壳原 PCB、FPC 和原高压驱动电路。程序不联网、不保存整幅图片，只使用 192 字节行缓冲。

> 重要：必须先拆除或完全隔离原 PCB 上资料图片红圈内的原控制芯片。原控制器与 NodeMCU 不能同时连接 SPI/控制线。焊接和插拔 FPC 时必须断电。

## 文件

- `SE0398NZ07A0_NodeMCU_Test/SE0398NZ07A0_NodeMCU_Test.ino`：Arduino 测试程序，自动显示真实四色照片。
- `SE0398NZ07A0_NodeMCU_Test/sunset_image.h`：达马万德山图片的 768×552、2-bit 四色数据。
- `SE0398NZ07A0_NodeMCU_Test/marilyn_image.h`：梦露图片的 768×552、2-bit 四色数据。
- `SE0398NZ07A0_Shenzhen_Weather/SE0398NZ07A0_Shenzhen_Weather.ino`：比亚迪（002594）行情 + 深圳天气双页面看板，直接访问免 Key 接口。
- `SE0398NZ07A0_Shenzhen_Weather/weather_icons.h`：由 iconfont.cn 天气 SVG 离线栅格化的 96×96 图标掩码。
- `SE0398NZ07A0_Shenzhen_Weather/secrets.example.h`：Wi-Fi 配置模板；本地复制为 `secrets.h`，不要提交真实密码。
- `tools/generate_epd_image.swift`：图片裁剪、四色量化和 2-bit 打包工具。
- `tools/generate_weather_icons.swift`：将 `tools/iconfont_weather_paths.json` 转换为墨水屏图标头文件。
- `weather-preview/index.html`：与设备同为 768×552 坐标的实时天气网页预览。

## 适用开发板

这里按最常见的 **NodeMCU 1.0（ESP-12E/ESP-12F，ESP8266）** 编写。Arduino IDE 中选择：

```text
开发板：NodeMCU 1.0 (ESP-12E Module)
CPU Frequency：80 MHz
Upload Speed：115200
```

如果你的板上主芯片不是 `ESP8266EX`，先不要接屏幕。

## 接线

下表中的屏幕信号名，对应替换 PCB 接线图片里红线所指的测试焊盘。`SDA` 在这里是 SPI `MOSI`，不是 I2C。

![NodeMCU ESP8266 接线标注](nodemcu_wiring_annotated.png)

| 替换 PCB | NodeMCU 丝印 | ESP8266 GPIO | 方向 |
|---|---:|---:|---|
| `GND` | `G` / `GND` | GND | 共地 |
| `3.3V` | `3V3` | 3.3V | 给替换 PCB 供电 |
| `SDA/MOSI` | `D7` | GPIO13 | NodeMCU → 屏幕 |
| `CLK/SCK` | `D5` | GPIO14 | NodeMCU → 屏幕 |
| `CS` | `D8` | GPIO15 | 驱动板 → 屏幕 |
| `DC` | `D2` | GPIO4 | 驱动板 → 屏幕 |
| `RST` | `D1` | GPIO5 | 驱动板 → 屏幕 |
| `BUSY` | `D0` | GPIO16 | 屏幕 → 驱动板 |

建议颜色：红线接 3.3V、黑线接 GND，其余每根使用不同颜色并在两端贴标签。`SCK/MOSI/CS/DC/RST` 各串联一个 `100–330Ω` 电阻，推荐 `220Ω` 并尽量靠近 NodeMCU；飞线尽量短于 10cm，SCK 最短。`RST` 可加 `10kΩ` 上拉；不要把 `CS=D8/GPIO15` 上拉到 3.3V，因为 ESP8266 上电时该脚需要保持低电平才能正常启动。BUSY 可选串联 `1kΩ` 作为输入保护。

本项目按 `EspInfo` 中“Waveshare e-Paper ESP8266 Driver Board”旧版映射使用 `D8/D2/D1/D0`。对应关系是 `CS=GPIO15`、`DC=GPIO4`、`RST=GPIO5`、`BUSY=GPIO16`；硬件 SPI 仍为 `D5/GPIO14=SCK` 和 `D7/GPIO13=MOSI`。该驱动板不使用电平转换器，板上已处理启动相关连接。

驱动板板载 `FLASH` 按键对应 `D3/GPIO0`，测试固件用它切换图片，天气固件用它强制更新；不按键时画面保持不变。

### 3.3V 焊点必须先确认

现有低清接线图片能看清 GND 和六根信号线，但黄色 `3.3V` 字样没有明确指向一个唯一焊盘。不要因为文字靠近 FPC 就直接往 FPC 触点上焊。

在接 3.3V 前：

1. 用通断档确认 GND 焊盘与 PCB 大面积地铜相通。
2. 若原手机壳仍能正常供电，在未改装状态测量候选电源点相对 GND，应稳定在约 3.3V。
3. 完全断电后拆除原控制芯片，并检查附近没有焊锡桥或脱落元件。
4. 再次确认 3.3V 与 GND 没有短路，最后才接电源线。

请提供拆壳后 PCB 正反面的垂直高清照片后，再最终确认具体 3.3V 焊盘。

## 供电

首次测试可以先通过 USB 给 NodeMCU 供电，并由 NodeMCU 的 `3V3` 引脚给原 PCB 供电。测试程序会关闭 Wi-Fi，降低供电负担。不同 NodeMCU 克隆板的稳压器能力差异很大：如果串口出现重启、乱码或刷新时电压明显下降，应立即断电，改用稳定的独立 3.3V、至少 500mA（建议留出 1A 余量）电源给屏幕板供电，并与 NodeMCU 共地。

- 原 PCB 禁止连接 `VIN`、`VU`、`5V` 或 USB 5V。
- 使用独立 3.3V 时，不要把两个不同的 3.3V 电源输出直接并联。
- 可在原 PCB 电源焊点附近并联 `100–470uF` 电解电容和 `0.1uF` 陶瓷电容。
- 原 PCB 刷新期间会产生正负高压，不要触碰或测量未知的高压测试点。

## 烧录和测试

1. 先只用 USB 连接 NodeMCU，不接屏幕，烧录 `.ino`。
2. 断开 USB，按照接线表焊接；GND 最先接，3.3V 最后接。
3. 检查所有相邻焊盘无短路，再连接 USB。
4. 程序上电等待 3 秒后显示天气页；短按驱动板 `FLASH`（GPIO0）刷新当前页，长按约 1.2 秒切换天气页/比亚迪行情页。
5. 串口监视器是可选的；使用时设置为 `115200 baud`，可以查看 BUSY、写入进度和错误原因。
6. 刷新时 BUSY 会变为 LOW，四色全刷可能持续数十秒。不要在刷新期间断电。
7. 每次切换图片都会关闭墨水屏高压并完整刷新；刷新完成后保持当前画面，不会自动切换。

驱动板上的 `FLASH` 按键为 `D3/GPIO0`、低电平有效。烧录或复位时不要按住它。

预期画面一：从 Wikimedia Commons 获取并量化的伊朗达马万德山日落照片。原图作者 Mahdi Kalhor，采用 CC BY 3.0：

```text
       RED sunset clouds
     YELLOW mountain light
       BLACK Damavand ridge
          WHITE sky/water
```

![Damavand sunset original](https://upload.wikimedia.org/wikipedia/commons/thumb/1/16/%D8%A2%D8%AA%D8%B4%D9%81%D8%B4%D8%A7%D9%86_%D8%AF%D9%85%D8%A7%D9%88%D9%86%D8%AF_%D8%AF%D8%B1_%D8%A2%D8%AA%D8%B4_%D8%BA%D8%B1%D9%88%D8%A8%D8%8C_%D8%AA%D9%82%D8%AF%DB%8C%D9%85_%D8%A8%D9%87_%D8%A7%DB%8C%D8%B1%D8%A7%D9%86%DB%8C%D8%A7%D9%86_Damavand_or_Diamond%2C_to_dear_Iranian%2C_Polur%2C_Mazandaran%2C_Iran_-_panoramio.jpg/1280px-thumbnail.jpg)

原图页面：<https://commons.wikimedia.org/wiki/File:%D8%A2%D8%AA%D8%B4%D9%81%D8%B4%D8%A7%D9%86_%D8%AF%D9%85%D8%A7%D9%88%D9%86%D8%AF_%D8%AF%D8%B1_%D8%A2%D8%AA%D8%B4_%D8%BA%D8%B1%D9%88%D8%A8%D8%8C_%D8%AA%D9%82%D8%AF%DB%8C%D9%85_%D8%A8%D9%87_%D8%A7%DB%8C%D8%B1%D8%A7%D9%86%DB%8C%D8%A7%D9%86_Damavand_or_Diamond,_to_dear_Iranian,_Polur,_Mazandaran,_Iran_-_panoramio.jpg>

预期画面二：Wikimedia Commons 上的《Marilyn Monroe I》绘画，作者 Silvia Klippert（照片 John Klippert），采用 CC BY-SA 3.0。它不是直接复制 Andy Warhol 原版丝网印刷，而是将可合法再利用的梦露绘画转换为黑、白、黄、红四色波普风格：

![Marilyn Monroe I original](https://upload.wikimedia.org/wikipedia/commons/0/09/Marilyn_Monroe_I.jpg)

原图页面：<https://commons.wikimedia.org/wiki/File:Marilyn_Monroe_I.jpg>

原图下载地址和许可信息以页面为准。两张图各自每像素 2 bit，合计约 207 KiB；编译后的固件仍在 NodeMCU 1MB Flash 范围内。

如果图案整体精确旋转了 180 度，把程序中的：

```cpp
constexpr bool kRotateImage180 = false;
```

改成 `true` 后重新烧录。不要同时修改 A0 的物理行交错算法。

## 常见故障

| 现象 | 首先检查 |
|---|---|
| `BUSY timeout during hardware reset` | 3.3V、GND、BUSY、RST，以及原控制芯片是否真正隔离 |
| 刷新时 NodeMCU 重启 | 3.3V 电源压降、USB 线、屏幕电源去耦 |
| BUSY 正常但屏幕完全不变 | CS、DC、SDA/MOSI、CLK，或原控制器仍在占用信号线 |
| 出现隔行、撕裂或上下半屏错位 | 确认面板确为 `SE0398NZ07A0`，不要使用 A1 驱动 |
| 图案正好上下颠倒且左右也反向 | 打开 `kRotateImage180` |

程序使用 1MHz SPI、逐字节 CS、刷新阶段 180 秒 BUSY 超时和 A0 特有的行映射：前 276 个逻辑行写入物理偶数行，后 276 个逻辑行反向写入物理奇数行。

### 比亚迪 + 深圳天气固件

该 sketch 使用深圳实际坐标（22.5431, 114.0579）访问免费、免 Key 的 Open-Meteo，在白底天气页显示当前天气、设备 IP、更新时间、近 8 个连续小时的温度与天气图标，以及未来 7 天的星期、日期、高低温和降水概率。行情页访问腾讯免 Key 行情接口 `qt.gtimg.cn` 获取比亚迪 `sz002594`。ESP8266 只支持 2.4GHz Wi-Fi。Wi-Fi 凭据写在本地 `secrets.h`（该文件被 Git 忽略，不会提交）。上电、短按 `FLASH` 或切换页面时更新当前页，默认每小时自动更新一次。

网页坐标预览位于 `weather-preview/index.html`；可直接打开，联网时显示深圳实时天气，接口不可用时显示明确标记的示例数据。通过本地 HTTP 服务打开时，浏览器对实时接口和图标图层的兼容性更稳定。

CoreGraphics 输出的中文位图内存行与墨水屏的上到下行序相反，因此字体生成器会把 16 行上下倒序后再写入 `weather_font.h`。横向列、ASCII 和 A0 行写入器都保持原方向，不能用整屏或单字左右翻转来修正中文。修改字体生成器或 `setPixel` 时应同时运行 `python3 tools/test_text_raster.py`。
