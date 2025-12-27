# DM_Balance_Hy_V1_0 项目说明

> 自平衡双腿轮式平台 (STM32H7 + FreeRTOS) —— 集成 BMI088 惯性传感、虚拟模型控制 (VMC)、LQR 系数随腿长多项式拟合、跳跃动作管理、姿态与行驶/转向/跳跃综合控制。
>
> 本 README 汇总系统架构、目录结构、核心任务、控制算法、外设驱动、数据与执行流、构建烧录、调试与故障排查，便于快速上手与二次开发。

---
## 1. 总体概述
平台采用 STM32H723 (H7 系列) 主控，运行 FreeRTOS 实时操作系统，实现：
- 双侧腿部 (左右各两关节 DM4310) + 双轮毂电机 (DM6215) 协调运动与自平衡。
- BMI088 惯性测量单元 (IMU) 读取加速度与陀螺仪；Mahony 四元数姿态解算；惯性系加速度分离与机体运动估计。
- 基于虚拟模型的腿部几何/力学映射：从关节角得到腿长、姿态参数、雅可比矩阵；从期望支撑力/髋关节力矩反推电机扭矩。
- LQR (线性二次调节) 系数随腿长 L0 三次多项式实时拟合，提升不同支撑高度时的稳定性。
- Kalman 滤波融合轮系/IMU 推导平台纵向速度与位移，增强抗噪性能。
- 跳跃三阶段 (压缩→加速→缩腿) 状态机与离地检测 (基于支撑力估计 FN 滤波判据)。
- 远程 SBUS 遥控器 (UART5 + DMA + 接收空闲中断) 对速度、转向、腿长、滚转补偿、跳跃触发等进行交互控制。
- 电压检测 (ADC + 分压) 与看门/报警任务发声提醒。

---
## 2. 目录结构与分层
```
Core/                HAL & FreeRTOS 初始化、系统时钟与外设底层
  Inc/               生成的外设与中断头文件
  Src/               ADC、SPI2、FDCAN(1/2/3)、TIM、UART5、freertos.c、main.c 等
Drivers/             ST 官方 CMSIS 与 HAL 驱动库
Middlewares/         (留空或第三方中间件)
MDK-ARM/             Keil uVision 工程、链接脚本、构建产物
USB_DEVICE/          USB 设备相关代码
User/                应用与控制逻辑主目录
  APP/               各 FreeRTOS 任务：INS、chassisR/chassisL、observe、remote、watch
  Algorithm/         姿态与控制算法：EKF、Kalman、Mahony、PID、VMC、LQR 系数
  Bsp/               板级支持：DWT、PWM、CAN 命令封装、UART(SBUS 解析)
  Controller/        通用控制器框架：Fuzzy PID、Feedforward、Disturbance Observer、Tracking Diff
  Devices/           具体器件驱动：BMI088 IMU、DM4310/DM6215 电机驱动协议
  Lib/               工具函数 (user_lib)
```
分层原则：
- Devices/Bsp 提供最小硬件抽象与通信接口。
- Algorithm 内部完成姿态、几何、滤波、控制律计算，不直接操作硬件。
- APP 任务层串联数据流，执行时序与动作管理。

---
## 3. FreeRTOS 任务与优先级
来自 `freertos.c`：
| 任务 | 优先级 | 堆栈 | 作用 |
|------|--------|------|------|
| INS_TASK (INS_Task) | Realtime | 512 | 读取 IMU，Mahony 姿态解算，重力分离、运动加速度与四元数、欧拉角、累计偏航角更新。|
| CHASSISR_TASK (ChassisR_Task) | AboveNormal | 512 | 右侧电机控制：腿部 VMC+LQR 输出、跳跃处理、离地逻辑、转向与滚转补偿。|
| CHASSISL_TASK (ChassisL_Task) | AboveNormal | 512 | 左侧电机控制：与右侧对称，协同跳跃与离地判定。|
| OBSERVE_TASK | High | 512 | Kalman 滤波融合 IMU 与轮系推导纵向速度/位移。|
| REMOTE_TASK | AboveNormal | 128 | SBUS 解析、遥控输入映射 (速度、转向、腿长、跳跃触发、功率管理)。|
| WATCH_TASK | Low | 128 | 电压与通信在线状态告警 (PWM 蜂鸣)。|
| defaultTask | Idle | 128 | USB 初始化与心跳空循环。|

时序注意：腿部控制任务等待 `INS.ins_flag == 1` (姿态与加速度稳定) 后进入循环；Remote/Observe/Watch 以各自周期 `osDelay(n)` 驱动。

---
## 4. 传感与姿态解算
### BMI088 IMU
- SPI2 + DMA 读取；`BMI088_init` 循环确保成功；偏置/比例系数在头文件给出。
- 加速度/角速度经 Mahony 算法 (`mahony_filter.c`) 得到四元数 q0~q3 与欧拉角 roll/pitch/yaw。
- 通过重力向量 `gravity` 分离机体加速度：机体系 → 世界系转换与一阶低通。
- 累计偏航角：跨越 ±π 自动环计数，生成 `YawTotalAngle` 用于转向控制与恢复姿态判断。

### 运动状态估计 (Observe_task)
- 轮毂 + 腿部角速度合成机体纵向速度 (`aver_v`)，与机体前向惯性加速度进入二维 Kalman (`vaEstimateKF`)。
- 输出滤波速度 `v_filter` 与积分位移 `x_filter`，作为 LQR 状态量。

---
## 5. 几何与虚拟模型控制 (VMC)
文件 `VMC_calc.c/h`：
- 输入：关节角 `phi1`、`phi4` (来自关节电机编码位置)；姿态 pitch、gyro。
- 运算：求中间构型点坐标 XB,YB,XD,YD → 解析出 C 点 (脚端) 坐标与腿长 L0、腿姿角 phi0；推导 `theta = π/2 - pitch - phi0`。
- 动态：差分求 d_theta, d_L0, dd_theta, dd_L0。
- 支撑力估计 FN：组合力矩与惯性加速度项；经滑动平均判定离地 (< 阈值返回 1)。
- 雅可比矩阵计算 `j11..j22` 将脚端期望竖向力 F0 与髋关节力矩 Tp 转换为两个关节电机扭矩 `torque_set[0/1]`。

---
## 6. LQR 系数随腿长自适应
`chassisR_task.c` / `chassisL_task.c` 中：
- 多项式系数表 `Poly_Coefficient[12][4]` 描述每个 LQR 增益随 L0 的三次变化。
- `LQR_K_calc()`：`K(L0) = a*L0^3 + b*L0^2 + c*L0 + d`。
- 控制输出包含：姿态 (pitch)、关节几何 θ / dθ、位移/速度误差 (x_set - x_filter, v_set - v_filter)、滚转补偿、转向补偿。
- 轮毂扭矩与髋关节力矩加入跳跃与离地逻辑后限幅。

---
## 7. 跳跃状态机与离地检测
三阶段：
1. 压缩 (jump_flag=1)：腿长目标减小 + 较小 PD 增益，达到阈值计时。
2. 上升加速 (jump_flag=2)：腿长快速拉伸，前馈力增大。
3. 缩腿 (jump_flag=3)：收腿以减少空中摆动，落地后复位参数。
离地条件：左右腿 `ground_detectionR/L == 1` 且未处于压缩/加速阶段，或处于缩腿阶段。
离地时：轮毂扭矩置零、更新腿部力矩简化控制，重置位置参考。

---
## 8. 遥控与交互 (Remote_task)
- SBUS 帧解析：25 字节，通道值拆分 → 四个摇杆 (左右/上下)，四个拨档 (swa~swd)，两个变量通道 (a,b)。
- 电源管理：根据电压判断 4S / 6S 档；低电压累积计数自动停机。
- 控制映射：
  - 纵向速度 `v_set = right_vert * scale` (不同电压档不同比例)。
  - 位置目标积分：`x_set += v_set * dt`。
  - 转向 `turn_set += left_hori * scale`。
  - 滚转设定：拨档或摇杆调节 `roll_set`。
  - 腿长设定：`leg_set` 由左竖摇杆与偏移映射，变化触发 `leg_flag` 防止误判离地。
  - 跳跃触发：条件满足 (拨档、变量、腿长阈值、电压档) 设置 `jump_flag`。

---
## 9. 监视与告警 (Watch_task)
- 电压低于警戒或遥控离线时驱动 TIM12 PWM 输出蜂鸣器闪鸣。
- 周期 2s 执行；对不同事件使用不同鸣叫节奏进行区分。

---
## 10. 控制器框架 (controller.c/h)
- Fuzzy PID：动态调节 Kp/Ki/Kd 隶属度与规则表 (7x7)。
- PID 优化选项：积分限幅、变速积分、测量微分、输出低通、误差处理等。
- Feedforward / Disturbance Observer / Tracking Differentiator：可扩展用于更精细的期望轨迹跟踪与扰动估计，目前平台主控制逻辑集中在 LQR+VMC。

---
## 11. 外设与硬件抽象
| 模块 | 文件 | 作用 |
|------|------|------|
| DWT 计时 | `bsp_dwt.h/.c` | 精确定时、周期 dt 计算与延迟。|
| PWM | `bsp_PWM.c` | 蜂鸣器/指示灯等 PWM 输出封装。|
| CAN | `can_bsp.c`, `dm4310_drv.c` | 过滤配置、发送封装、解析电机反馈、MIT/位置/速度模式指令。|
| UART5 + DMA | `uart_bsp.c` | SBUS 接收空闲中断与帧解析。|
| ADC1 + DMA | `adc.c` | 电池电压与其它模拟量读取。|
| SPI2 + DMA | `BMI088driver.*` | IMU 传感数据交换。|

---
## 12. 数据与执行流 (简化 ASCII 图)
```
        +-----------------+      BMI088 SPI2       +------------------+
        |  INS_Task       | <--------------------> |  BMI088driver    |
        |  (Mahony, Accel)|                         +------------------+
        |  q,Roll/Pitch/Yaw|--> gravity sep
        +--------+--------+
                 | MotionAccel_n/b, YawTotal
                 v
        +-----------------+   Legs geom          +-------------------+
        | ChassisR/L_Task |<-------------------->|  VMC_calc         |
        |  LQR Gain Adapt |--> torque_set        +-------------------+
        |  Jump State     |                     ^
        +---+---------+---+                     |
            | Wheel vel, IMU gyro               | L0, theta, d_theta
            v                                   |
        +-----------------+  KF fuse  +------------------+
        | Observe_Task    |---------> | Kalman Filter     |
        | v_filter,x_filter|          +------------------+
            ^       ^                          
            |       | Remote setpoints (v_set,x_set,leg_set,turn_set,roll_set)
        +---+-------+---+
        | Remote_Task   | UART5 SBUS
        +---+-----------+
            | Alarms (voltage/offline)
            v
        +-----------------+
        | Watch_Task      | PWM Buzzer
        +-----------------+
      |
      v USB CDC (Virtual COM)
    +-----------------+
    |  usb_comm.c     | (cmd_vel,cmd_wel,f1,f2,f3,f4) RX
    |  (x,y) TX       | latest command in g_usb_cmd
    +-----------------+
```

---
## 13. 构建与烧录
### 开发环境
- IDE：Keil uVision (工程位于 `MDK-ARM/CtrlBoard-H7_IMU.uvprojx`).
- 设备：STM32H723 MCU，J-Link 或 ST-Link 下载调试。

### 步骤
1. 使用 Keil 打开工程文件。确保安装对应 STM32H7 Pack。
2. 选择目标配置 (Release/Debug)。
3. 编译生成 `CtrlBoard-H7_IMU.axf`。
4. 连接调试器，下载程序到板子。
5. 复位运行，观察串口/电机/蜂鸣器及姿态初始化。

### 常见宏与配置
- 中断优先级：CAN/IMU/SBUS 使用 FreeRTOS 安全优先级 (>=5)。
- FreeRTOS 配置：`FreeRTOSConfig.h` 根据实时性调整任务优先级与堆大小。

---
## 14. 参数与可调项
| 类别 | 名称 | 位置 | 说明 |
|------|------|------|------|
| PID | `ROLL_PID_KP/KD` | `chassisR_task.h` | 横滚补偿增益。|
| LQR 多项式 | `Poly_Coefficient` | `chassisR_task.c` | 12 个增益随腿长变化。|
| Leg PID | `LEG_PID_*` | `VMC_calc.h` | 腿长闭环 PD。|
| 跳跃阈值 | `L0` 比较值 & 计数 | `chassisR/L_task.c` | 三阶段切换条件。|
| 遥控比例 | 速度/转向 scale | `remote_task.c` | 电压档区分速度系数。|
| 电压门限 | `VBAT_*` | `watch_task.h` | 电池低电压告警/停机。|
| USB 接收缓冲 | `rxbuf` | `usb_comm.c` | 解析 (6) 元组指令的内部循环缓冲。|

---
## 15. 故障排查指南
| 现象 | 可能原因 | 建议处理 |
|------|----------|----------|
| IMU 初始化卡死 | BMI088 SPI 或供电不稳定 | 检查 CS/CLK/MOSI/MISO 线路与上电序列。|
| 姿态漂移大 | Mahony 参数不适或陀螺零偏异常 | 调整 `mahony_init(Kp,Ki)`，重新标定偏置。|
| 电机无响应 | CAN 过滤/速率或使能帧丢失 | 确认 `FDCAN*_Config()` 与电机 ID, 反复发送使能。|
| 跳跃不触发 | 条件组合未满足、腿长阈值偏差 | 检查 `jump_flag` 设置与遥控拨档逻辑。|
| 离地误判频繁 | 支撑力阈值过低/滤波窗口过窄 | 调整 `ground_detection*` 中 `<3.0f` 阈值。|
| 位移/速度抖动 | Kalman Q/R 不匹配 | 调整 `vaEstimateKF_Q/R` 初始矩阵。|
| 水平偏航不收敛 | Turn PID 参数偏差 | 调整 `TURN_PID_*`，适度增加 D。|
| 低电压不停机 | 计数未达阈值或 ADC 标定误差 | 校准分压系数，降低判定次数。|
| USB 无法接收 | 未打开 CDC 端口或格式不匹配 | 确认 PC 端发送 "(a,b,f1,f2,f3,f4)" 结尾含 `)`；波特率与虚拟串口驱动。|
| USB 发送堵塞 | 主机未及时读走或忙状态 | 检查 `CDC_Transmit_HS` 返回值是否 `USBD_BUSY`，降低发送频率。|

---
## 16. 扩展建议 & 下一步
- 引入 IMU 温度补偿与硬铁/软铁校正，提升姿态稳定性。
- 根据操作模式切换不同 LQR 权重集（例如高速巡航与静态平衡）。
- 增加腿部位置/力矩前馈结合能量管理，优化跳跃高度一致性。
- 使用 Disturbance Observer 输出辅助抗地面冲击扰动。
- 添加单元测试/仿真 (Python + Eigen) 验证 VMC 逆解与增益自适应曲线。
- USB 通信增加二进制帧与 CRC 校验，降低 ASCII 解析开销。
- 引入命令超时保护：`g_usb_cmd.last_rx_tick` 超过阈值自动降级速度指令。

---
## 附录 A — 算法详解（补充章节）

本节把实现中使用的主要算法用更严谨的数学/工程语言描述，指明实现文件、关键函数与可调参数，便于复现与调参。

### A.1 Mahony 四元数姿态融合（实现：`User/Algorithm/mahony_filter.c` / `mahony_filter.h`）
- 输入：陀螺角速度 $\boldsymbol{\omega}=(\omega_x,\omega_y,\omega_z)$（rad/s），加速度矢量 $\mathbf{a}$（m/s^2）。
- 输出：四元数 $\mathbf{q}=[q_0,q_1,q_2,q_3]^T$，欧拉角 roll/pitch/yaw。
- 算法要点：使用 Mahony 互补滤波器的 PI 形式，将陀螺高频积分与加速度提供的重力方向低频修正组合：
  - 误差项 $\mathbf{e} = \mathbf{v}_{meas} \times \mathbf{v}_{est}$，其中 $\mathbf{v}_{meas}=\mathbf{a}/\|\mathbf{a}\|$，$\mathbf{v}_{est}$ 为由当前 $\mathbf{q}$ 旋转得到的重力方向估计。
  - 角速度修正：$\boldsymbol{\omega}_{corr}=\boldsymbol{\omega} + K_p \mathbf{e} + K_i \int \mathbf{e}\,dt$。
  - 四元数导数：$\dot{\mathbf{q}} = \tfrac{1}{2}\mathbf{q}\otimes [0,\boldsymbol{\omega}_{corr}]$，随后归一化 $\mathbf{q}$。
- 关键参数：`mahony_init(Kp, Ki)`；若姿态漂移，优先检查陀螺零偏标定，再调小 Ki 或增大 Kp（增加观测融合力度）。

### A.2 二维卡尔曼滤波器用于纵向速度/位移估计（实现：`User/APP/observe_task.c` + `User/Algorithm/kalman_filter.*`）
- 状态向量：$\mathbf{x}=[v\;\;x]^T$（v：纵向速度，x：前向位移）。
- 离散时间状态模型（采样 T）：
  $$\mathbf{x}_{k+1} = F\mathbf{x}_k + \mathbf{w}_k, \quad F=\begin{bmatrix}1 & 0.003\\0 & 1\end{bmatrix}$$
  （这里示例矩阵与代码中 `vaEstimateKF_F` 一致；根据实际采样周期调整）。
- 观测向量：$\mathbf{z}=[v_{wheel}, a_{body}]^T$，其中 $v_{wheel}$ 为轮系/腿部合成的速度量（`aver_v`），$a_{body}$ 为机体前向惯性加速度。观测矩阵$H$ 在实现中为单位矩阵。
- 噪声：过程噪声协方差 $Q$ 和观测噪声协方差 $R$（实现变量 `vaEstimateKF_Q`, `vaEstimateKF_R`）。调参建议：若输出抖动，增大 $R$（使滤波器更信任模型）；若响应滞后，增大 $Q$。
- 用途：输出 `v_filter`（滤波后的速度）与 `x_filter`（积分位移）供 LQR 与轨迹误差计算使用。

### A.3 虚拟模型控制（VMC）与腿部几何映射（实现：`User/APP/VMC_calc.c/h`）
- 思路：定义脚端坐标与髋关节位置、腿长 $L_0$ 与腿轴角 $\phi_0$，利用雅可比矩阵 $J$ 将脚端力/力矩映射到关节扭矩：
  $$\boldsymbol{\tau} = J^T \begin{bmatrix}F_0\\T_p\end{bmatrix}$$
  其中 $F_0$ 为期望竖向支撑力，$T_p$ 为髋关节扭矩（用于姿态补偿/转向）。
- 计算步骤：从电机编码得到关节角 -> 计算几何中间点 -> 求脚端位置与 $L_0,\phi_0$ -> 通过差分或滤波得到 $\dot L_0,\dot\phi_0$。
- 实际实现注意：雅可比求导数时避免奇异（腿完全伸直/收缩边界），对角度使用饱和与小角近似防止数值爆炸。

### A.4 LQR 增益多项式自适应（实现：`chassisR_task.c` / `chassisL_task.c`）
- 原理：离散线性化系统在不同腿长下的最优 LQR 增益不同。工程上预先对若干工况离线计算出 LQR 增益，然后用三次多项式对每个增益随 $L_0$ 的变化进行拟合：
  $$K_i(L_0)=a_iL_0^3+b_iL_0^2+c_iL_0+d_i$$
  在运行时通过 `LQR_K_calc()` 根据实时测得的 $L_0$ 计算当前增益向量 $K(L_0)$。
- 实现细节：`Poly_Coefficient[12][4]` 存储 12 个增益（根据实现可能为姿态/速度/位移等子项），每项 4 个多项式系数。
- 调参建议：若跨工况稳定性差，可增加分段拟合或使用二次/更高阶多项式并结合过拟合正则化。另需保证在边界 $L_0$ 外进行常数外推或饱和，避免多项式发散。

### A.5 PID / Fuzzy PID（实现：`User/Controller/controller.c` / `controller.h`）
- 标准 PID 计算含 P/I/D 三项，并提供多项增强机制：
  - 梯形积分（Trapezoid）：减小积分误差数值积分偏差。
  - 积分限幅（Integral Limit）：防止积分风up。
  - 测量微分（Derivative on Measurement）与滤波（OLS / LPF）：减少噪声放大。
  - 输出低通、比例限幅等。
- Fuzzy PID：在误差/误差变化量的隶属度上对 Kp/Ki/Kd 作在线修正（`FuzzyRule` 结构与 7x7 规则表）。

### A.6 实现文件与关键函数映射（快速索引）
- Mahony: `User/Algorithm/mahony_filter.c` — 初始化 `mahony_init(Kp,Ki)`，周期调用 `mahony_update(gyro, accel, dt)`。
- Kalman（速度/位移）：`User/Algorithm/kalman_filter.c` + `User/APP/observe_task.c` — `Kalman_Filter_Init`, `Kalman_Filter_Update`, `xvEstimateKF_Init/Update`。
- VMC/几何：`User/APP/VMC_calc.c/h` — `VMC_calc()`、雅可比与扭矩映射函数。
- LQR 多项式：`chassisR_task.c` / `chassisL_task.c` — `LQR_K_calc()`，系数数组 `Poly_Coefficient`。
- PID/Fuzzy PID：`User/Controller/controller.c/h` — `PID_Init`, `PID_Calculate`, 以及若干优化步骤 `f_Trapezoid_Intergral`、`f_Integral_Limit` 等。

### A.7 调参建议与常见问题（更精细）
- Mahony：先标定陀螺零偏，使长时漂移最小；典型起始值 Kp=2~10, Ki=0~1（视采样与噪声）。若角度震荡，减小 Kp 或启用角度低通。
- Kalman：先依据实际传感器噪声估算 $R$（测 100~500 次方差），把 $Q$ 设为小量开始再放宽。观察系统阶跃响应并在仿真中调整。
- VMC：在脚端受力接近零点（接触/离地边界）处需额外判据（时间窗 + 最小力阈）避免抖动。
- LQR：对短时跳跃/冲击事件，可以在切换期间临时退避到保守增益（降低积分与高频增益），避免超调。

### A.8 建议的验证步骤（单元/仿真）
1. 在 PC 环境用 Python + NumPy/Eigen 建立小规模仿真：实现 Mahony、2D Kalman、VMC 的正向/逆向映射，验证在噪声/偏置下的稳健性。2D KF 与真值轨迹对比误差曲线。
2. 对 LQR 多项式做交叉验证（留一法）以防过拟合；并在边界 $L_0$ 做饱和或线性外推测试。
3. 在板上启用记录（UART/USB）导出时间序列（acc/gyro/v_filter/x_filter/torque）进行离线分析。

---
## 17. 版权与许可
- HAL/CMSIS：STMicroelectronics 官方许可。
- 自研代码未附明确开源协议，默认内部使用；建议后续添加 MIT 或 BSD 以开放协作。

---
## 18. 快速检查清单
- [ ] IMU 初始化成功 (姿态稳定 < 数秒)
- [ ] 遥控在线 (remote_task 更新)
- [ ] 电机反馈正常 (CAN 回包 state/p/vel/tor)
- [ ] Kalman 速度与积分位移平滑
- [ ] 跳跃三阶段动作逻辑正确
- [ ] 低电压告警与停机有效
- [ ] USB 上位机发送指令被正确解析 (观察 g_usb_cmd.new_cmd)
- [ ] USB 定期输出 (x,y) 帧格式正确 (括号+逗号+回车换行)

---
## 19. 联系与维护
如需共享改进或反馈问题，建议：
- 添加 CHANGELOG 与版本号
- 使用 issue 模板记录控制参数修改
- 引入脚本导出当前参数集 (JSON)

---
**完成时间：2025-11-12**

> 若需英文版或添加框图 (PlantUML / Mermaid)，可继续提出。
