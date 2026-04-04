# 工程结构与控制逻辑分析报告

本报告对 `DM_Balance_Hy_V1_0` 工程进行了详细的结构、流程、模块及控制逻辑分析。该项目是一个基于 STM32H7 的轮腿平衡机器人控制系统。

## 1. 工程结构概述

项目采用标准的 STM32CubeMX 生成的 HAL 库工程结构，结合 FreeRTOS 实时操作系统。用户核心代码集中在 `User/` 目录下。

### 关键目录说明

*   **Core/**: STM32CubeMX 生成的核心代码 (main, gpio, dma, fdcan, freertos 等配置)。
*   **User/**: 用户自定义应用代码。
    *   **APP/**: 顶层任务实现 (ChassisTask, INSTask, RemoteTask 等)。
    *   **Algorithm/**: 核心算法库 (PID, Kalman Filter, Mahony, EKF, VMC)。
    *   **Bsp/**: 板级支持包 (CAN, PWM, UART, USB, LED)。
    *   **Controller/**: 控制器通用库 (Fuzzy PID 等)。
    *   **Devices/**: 外设驱动 (BMI088 IMU, DM电机驱动)。
    *   **Lib/**: 通用工具库。

## 2. 系统流程与架构

系统启动流程遵循 `Reset -> SystemInit -> main() -> HAL_Init -> SystemClock_Config -> MX_Peripherals_Init -> MX_FREERTOS_Init -> osKernelStart`。

### FreeRTOS 任务调度
系统创建了多个任务，按优先级（从高到低）大致如下：

1.  **INS_TASK** (Realtime): 惯性导航/姿态解算任务。最高优先级，确保姿态数据的实时性。
2.  **OBSERVE_TASK** (High): 系统状态监测/观测器任务。
3.  **CHASSISL_TASK / CHASSISR_TASK** (AboveNormal): 左/右腿及底盘控制任务。这是运动控制的核心。
4.  **REMOTE_TASK** (AboveNormal): 遥控器数据处理任务。
5.  **WATCH_TASK** (Low): 看门狗或低频监控任务。
6.  **defaultTask** (Idle): 初始化 USB 后进入休眠。

## 3. 核心控制逻辑

控制系统采用分层架构，融合了 VMC (虚拟模型控制) 和 LQR (线性二次型调节器)。

### 3.1 姿态解算 (INS)
*   **传感器**: BMI088 (6轴 IMU)。
*   **算法**: 使用 **Mahony 互补滤波** 融合陀螺仪和加速度计数据，解算四元数 (Quaternion) 和欧拉角。
*   **位置**: `User/APP/INS_task.c`。
*   **功能**: 提供机器人的 Pitch (俯仰角), Roll (横滚角) 及角速度，用于平衡控制。

### 3.2 运动控制 (Chassis Control)
控制逻辑被拆分为左右两个对称的任务 (`ChassisL_task`, `ChassisR_task`)。
*   **VMC (Virtual Model Control)**:
    *   利用五连杆机构学，将关节电机的位置和力矩映射到虚拟腿的长度 ($L_0$) 和摆角 ($\theta$, $\phi$)。
    *   通过雅可比矩阵 (Jacobian Transpose) 将末端虚拟力 ($F$, $\tau$) 映射回关节电机的力矩。
    *   文件: `User/Algorithm/VMC/VMC_calc.c`。
*   **LQR (Linear Quadratic Regulator)**:
    *   用于计算维持平衡所需的力矩。 LQR 增益 ($K$) 根据腿长 ($L_0$) 动态调整（查表或多项式拟合）。
    *   **状态变量**: 包含 杆角度 ($\theta$), 杆角速度 ($\dot{\theta}$), 位移 ($x$), 速度 ($v$), 机身俯仰角 (Pitch), 机身俯仰角速度 (Gyro)。
    *   **控制目标**: 轮毂电机力矩 + 髋关节力矩。
*   **腿长控制**:
    *   使用前馈 (Feedforward, $mg/\cos\theta$) + PID 控制来维持期望腿长或执行跳跃动作。
*   **跳跃状态机**:
    *   实现了跳跃逻辑：压缩 (Compress) -> 加速上升 (Thrust) -> 空中缩腿 (Retract) -> 落地恢复。

### 3.3 电机控制
*   **通信协议**: MIT Cheetah 协议 (位置, 速度, KP, KD, 前馈力矩)。
*   **执行器**:
    *   关节电机: DM4310 (达妙)。
    *   轮毂电机: DM6215。

## 4. 硬件与模块使用

### 4.1 核心硬件
*   **MCU**: STM32H723xx (高性能 Cortex-M7)。
*   **IMU**: Bosch BMI088 (SPI 通信)。
*   **电机**: 达妙 (Direct Drive Motor) 系列。

### 4.2 通信接口
*   **FDCAN (CAN FD)**:
    *   主要用于电机控制。
    *   `hfdcan2`: 连接左右腿关节电机及轮毂电机。
*   **SPI**:
    *   连接 BMI088 IMU 模块。
*   **USB**:
    *   CDC 类，可能用于上位机调试或数据回传。
*   **UART**:
    *   可能用于接收遥控器 (SBUS/IBUS) 或蓝牙调试。

## 5. 文件概览

*   **`User/APP/chassisL_task.c`**: 左腿控制核心，包含状态机、LQR 计算、VMC 力矩映射及 CAN 发送。
*   **`User/APP/INS_task.c`**:读取 BMI088，运行 Mahony 滤波，输出姿态。
*   **`User/Algorithm/VMC/VMC_calc.c`**: 五连杆运动学解算。
*   **`User/Controller/controller.c`**: 通用 PID 及模糊 PID 实现。
*   **`Core/Src/freertos.c`**: 任务创建及调度配置。

## 总结
该项目是一个高阶的平衡机器人控制系统，其特点在于使用了高性能 H7 芯片处理复杂的 VMC 和 LQR 算法。代码结构清晰，通过 FreeRTOS 实现了姿态解算与运动控制的解耦，控制策略上采用了先进的基于模型的控制方法，而非简单的 PID 级联，具备跳跃和动态适应腿长变化的能力。
