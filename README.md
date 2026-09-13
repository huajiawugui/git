# 鱼群智联 · 仿生鱼演示原型(FishBuoy)

《鱼群智联——仿生协同智能无线传输浮标系统》的实物演示原型与 3D 数字孪生。
一条 3 关节仿生鱼由 ESP32 驱动,集成 **WiFi 遥控**、**红外避障测距**、**泳姿数据库**(S 形 / C 形 / CPG 中枢模式发生器),并配套 three.js 网页 **3D 实时数字孪生**——真鱼舵机角度与红外距离实时回传,网页可下发泳姿指令遥控真鱼。

## 四大功能

| 需求 | 实现 | 参考来源 |
|---|---|---|
| WiFi 模块 | ESP32 开 AP 热点(FishBuoy / 192.168.4.1),WebSocket(81)+ HTTP(80)双服务,LittleFS 内置网页,手机/电脑连热点即用,**零外网依赖** | [bionicFish](https://github.com/Legolers/bionicFish) |
| 红外传感器 | GP2Y0A21YK0F 测距 + 自动避障:距离 ≈ 13/V,32 次均值滤波、两点标定;滞回阈值 5cm 触发 / 8cm 解除,触发 C 形转向后自动恢复原泳姿 | [Fish](https://github.com/SarthakJShetty/Fish) |
| 泳姿数据库 | S 形前进 12 步行波表、C 形左右转 16 步一次性动作表、CPG 三耦合相位振荡器(频率/幅值/偏置在线可调,前进/后退切换) | [Fish](https://github.com/SarthakJShetty/Fish) + [RoFish](https://github.com/MartinStokroos/RoFish) |
| 3D 可视化模拟 | three.js r128 程序化鱼模型(4 段躯干 3 关节 + 尾鳍/胸鳍/背鳍),演示模式 + WiFi 实时数字孪生双模式,鼠标/触摸轨道相机 | 自研(借鉴 [Robotic-Fish-Simulator](https://github.com/Waqas3923/Robotic-Fish-Simulator)) |

## 目录结构

```
FishBuoy/
├── README.md                # 本文件
├── firmware/                # ESP32 固件(Arduino IDE 2.x 工程)
│   ├── firmware.ino         # setup/loop:50ms 节拍调度,全部非阻塞(零 delay)
│   ├── config.h             # 引脚、WiFi、标定系数、SIM_SENSOR 编译开关
│   ├── gait.h / gait.cpp    # 泳姿数据库:S/C 步进表 + 非阻塞状态机
│   ├── cpg.h / cpg.cpp      # CPG:3 耦合相位振荡器(移植自 RoFish)
│   ├── sensor.h / .cpp      # 红外:ADC1 读取、均值滤波、两点标定、滞回阈值
│   ├── servo.h / .cpp       # 3 通道舵机封装,限幅 60~120°
│   ├── wifi_server.h / .cpp # AP + HTTP(80)LittleFS + WebSocket(81)+ JSON 协议
│   └── data/                # LittleFS 内容(index.html + three.min.js,与 web/ 同步)
├── web/                     # 网页先行开发目录
│   ├── index.html           # 单文件:场景/程序化鱼/关节动画/WS 客户端/HUD/面板
│   ├── three.min.js         # r128 单文件版(≈600KB;r150+ 无 UMD 版,勿升级)
│   └── test/gait_test.js    # 泳姿核心逻辑单元测试(node gait_test.js,14 项断言)
└── docs/
    ├── 泳姿数据库.md         # S/C/CPG 参数表与公式(网页与固件共用同一套数值)
    ├── 通信协议.md           # WebSocket JSON 协议定义
    ├── 演示脚本.md           # 5 分钟答辩演示流程
    └── 标定记录.md           # 红外两点标定模板
```

## 快速开始

### M1 — 纯网页演示(无需硬件,已可用)

双击 `web/index.html`(断网可用,内置演示模拟器):

- 按键 `1~6`:S 形前进 / C 形左转 / C 形右转 / CPG 前进 / CPG 后退 / 停止回中
- 按住按键 `7`/`8`(或面板按钮):上浮 / 下潜,松开即停;鱼的俯仰随深度变化
- 按键 `9`:自动巡游开/关 —— 鱼在指定范围(面板可调巡游半径与深度)内随机游动,
  优先前往未访问区域,一段时间后遍历整个三维水体空间;半透明框为巡游范围
- 转向位置不再丢失:所有泳姿共用同一套"航向 + 位移积分"运动学,动作切换后朝向与位置持续有效
- 右侧面板拖 CPG 滑杆:频率(游速)、幅度(摆尾)、偏置(连续转向),实时生效
- "模拟距离"滑到 5cm 以下或点"模拟障碍物":触发自动避障(C 形转向 → 自动恢复原泳姿)
- 鼠标拖动旋转视角、滚轮缩放;手机触摸单指旋转、双指缩放
- 场景中不再绘制独立浮标/信标:仿生鱼本体即浮漂本体(与实物一致)

### M2 — 硬件联调

1. 按下方清单采购硬件,按接线表组装(舵机**严禁**由 USB 口供电)
2. **ESP32 板级包已预装**:esp32@2.0.17 + 工具链已放入 `%LOCALAPPDATA%\Arduino15`(含 esptool、mklittlefs,经国内可达通道下载)。安装 Arduino IDE 2.x 后无需再装板级包,直接打开工程即可;命令行编译:`C:\Users\Ausa\arduino-cli\arduino-cli.exe compile --fqbn esp32:esp32:esp32 firmware`
3. 打开 `firmware/firmware.ino`,开发板选 "ESP32 Dev Module",Partition Scheme 选 `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)`(LittleFS 挂载于该分区)
4. 烧录 `firmware/data/` 到 LittleFS(免插件,用已预装工具):
   ```
   %LOCALAPPDATA%\Arduino15\packages\esp32\tools\mklittlefs\3.0.0-gnu12-dc7f933\mklittlefs.exe -c data -p 256 -b 4096 -s 1507328 littlefs.bin
   %LOCALAPPDATA%\Arduino15\packages\esp32\tools\esptool_py\4.5.1\esptool.exe --chip esp32 --port COM3 write_flash 0x290000 littlefs.bin
   ```
   (COM3 换成实际串口号;`0x290000` 为上述分区方案中 SPIFFS 分区地址)
5. 串口监视器 115200 波特率查看遥测;红外接好后把 `firmware/config.h` 的 `SIM_SENSOR` 改为 `0`,按 docs/标定记录.md 完成两点标定

### M3 — WiFi 数字孪生

烧录后 ESP32 自动开热点。手机/电脑连 WiFi **FishBuoy**(密码 `12345678`),浏览器访问 **http://192.168.4.1** —— 页面自动连接 `ws://192.168.4.1:81` 进入实时模式:3D 鱼与真鱼同步摆动,面板指令经 WiFi 下发遥控。

## 硬件接线

| ESP32 | 外设 | 说明 |
|---|---|---|
| GPIO25 | 尾段 SG90 信号线 | servo[0],中位 90° |
| GPIO26 | 中段 SG90 信号线 | servo[1] |
| GPIO27 | 头部 SG90 信号线 | servo[2] |
| GPIO34 | GP2Y0A21YK0F Vo | **ADC1 通道**(ADC2 被 WiFi 占用,勿接 4/0/2/15/13/12) |
| 5V | 红外 VCC + 舵机电源轨 | 独立 5V/2A 电源(充电宝/适配器) |
| GND | 全部共地 | 5V 电源轨并 470uF + 1000uF 电解电容防复位 |

## 硬件采购清单(淘宝参考价,合计约 60~130 元)

| 物品 | 规格 | 数量 | 单价 | 说明 |
|---|---|---|---|---|
| ESP32 开发板 | DevKitC V4 兼容,4MB | 2 | 15~25 | 1 用 1 备 |
| SG90 舵机 | 9g | 5 | 3~5 | 3 用 2 备,易扫齿 |
| 红外测距模块 | GP2Y0A21YK0F | 2 | 6~12 | 1 备 |
| 杜邦线/面包板 | 40P 套装 + MB-102 | 1 套 | ~15 | |
| 电解电容 | 470uF + 1000uF / 16V | 各 2 | 1~3 | 舵机电源轨滤波,**防 ESP32 复位必买** |
| 5V 电源 | 2A 充电宝/适配器 | 1 | 10~20 | 舵机独立供电 |
| (可选)DS18B20 | 防水探头 + 4.7k 电阻 | 1 | 4~8 | 水温遥测 |
| (可选)3D 打印骨架 | 学校打印 PLA | 1 套 | 10~50 | 急用可硬纸板/亚克力手作 |

## 里程碑

- [x] **M1** 纯网页离线泳姿动画(3D 鱼 + S/C/CPG + 避障 + 面板,HUD,轨道相机)
- [ ] **M2** ESP32 固件串口调试(硬件到货后:泳姿/避障/标定全链路,零 delay 非阻塞)
- [ ] **M3** WiFi + WebSocket 数字孪生联调(LittleFS + AP + 断线重连 + 拷机)

## 文档

- [docs/泳姿数据库.md](docs/泳姿数据库.md) — S/C/CPG 参数表与公式、避障状态机、验证方法
- [docs/通信协议.md](docs/通信协议.md) — WebSocket JSON 报文定义、心跳重连、降级策略
- [docs/演示脚本.md](docs/演示脚本.md) — 5 分钟答辩流程、关键话术、应急预案
- [docs/标定记录.md](docs/标定记录.md) — 红外两点标定步骤与记录表

## 参考开源项目

| 仓库 | 借鉴内容 |
|---|---|
| [Legolers/bionicFish](https://github.com/Legolers/bionicFish) | ESP32 WiFi 服务器驱动舵机的整体架构 |
| [SarthakJShetty/Fish](https://github.com/SarthakJShetty/Fish) | S/C 泳姿步进表、GP2Y0A21 红外避障思路 |
| [MartinStokroos/RoFish](https://github.com/MartinStokroos/RoFish) | CPG 中枢模式发生器模型(耦合相位振荡器) |
| [Waqas3923/Robotic-Fish-Simulator](https://github.com/Waqas3923/Robotic-Fish-Simulator) | 仿真可视化思路(本项目用 Web three.js 实现) |
