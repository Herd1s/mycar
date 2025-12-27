#include "remote_task.h"
#include "watch_task.h"
#include "cmsis_os.h"
#include "ws2812.h"
#include "ws2812_effects.h"
#include "usb_comm.h"
#include <stdlib.h>
#include <math.h>

// remote_task.c 负责处理遥控器输入、底盘闭环与灯效输出，仅添加注释不触发功能变化。

// TIM 与 DMA 句柄用于驱动 WS2812，定义在 tim.c 中
extern TIM_HandleTypeDef htim1;
extern DMA_HandleTypeDef hdma_tim1_ch1;


#define REMOTE_OVERTIME	1000
// 遥控接收超时时间，超过则认为失联
#define JOY_DEADZONE 50

// 层级之外的底盘/惯导状态、两条腿和 ADC 采样在其他模块中维护
extern chassis_t chassis_move;
extern INS_t INS;
uint32_t REMOTE_TIME=10;//ps2�ֱ�����������10ms
// 遥控任务周期（单位 ms），常用于 osDelay

extern vmc_leg_t right;			
extern vmc_leg_t left;

extern uint16_t adc_val[2];

int pos = 0;
/**************************************************************************
Function: Sbus Remote
Input   : none
Output  : none
Auth    : DHY 
Date		: 2024
**************************************************************************/	
// 远程控制任务主循环：解析遥控数据，切换模式，并驱动 LED、跳跃与 USB 上报
void Remote_task(void)
{
	// 先启动 UART DMA 接收，等待遥控包到达并触发空闲中断
	HAL_UARTEx_ReceiveToIdle_DMA(&huart5, rx_buff, BUFF_SIZE*2);

	// 初始化 WS2812 灯带驱动，使用 TIM1_CH1 (PE9) 输出信号
	WS2812_Init(&htim1, &hdma_tim1_ch1);


	// 无效果模块，仅使用基础驱动。保持简单初始显示效果
	float dt = REMOTE_TIME / 1000.0f; // 任务周期转换为秒
	static float last_jump_vrb = 0; // 上一次跳跃变量 b 的值，用于边缘检测
	static uint32_t vbat_low_count = 0; // 连续低电压计数器
	
	// 任务主循环，持续处理遥控数据
	while(1)
	{
		// 通过 ADC 采样值估算电池电压，分压比例约为 11
		chassis_move.vbus = (adc_val[0]*3.3f/65535)*11.0f;
		
		// 初次进入时根据电压判断电池节数并设置阈值
		if(chassis_move.vbus_mode == 0)
		{
			
			if(chassis_move.vbus > VBAT_LOW_VAL_6S)
			{
				//6s
				VBAT_WARNNING_VAL = VBAT_WARNNING_VAL_6S;
				VBAT_LOW_VAL = VBAT_LOW_VAL_6S;
				chassis_move.vbus_mode = 2;
			}
			else if(chassis_move.vbus > VBAT_LOW_VAL_4S)
			{
				//4s
				VBAT_WARNNING_VAL = VBAT_WARNNING_VAL_4S;
				VBAT_LOW_VAL = VBAT_LOW_VAL_4S;
				chassis_move.vbus_mode = 1;
			}
			else
			{
				// 未启动状态：复位为安全默认目标
				chassis_move.vbus_mode = 0;
				VBAT_WARNNING_VAL = 0.0f;
				VBAT_LOW_VAL = 0.0f;
			}
			
		}
		
		// 超过超时时间则认为遥控器失联
		if((HAL_GetTick() - remoter.sbus_recever_time) > REMOTE_OVERTIME)
		{
			remoter.online = 0;
		}
		
		// 遥控器在线时执行各种开关与摇杆逻辑
		if(remoter.online)
		{
			if(remoter.toggle.swd==0)
			{
				// SWD 置 0：强制关机并清除状态
				chassis_move.start_flag=0;
				chassis_move.recover_flag=0;
				
				vbat_low_count = 0;
			}
			else if(remoter.toggle.swd >= 1)
			{
				// SWD 打开时根据电压和计数判断是否保持上电状态
//				if(chassis_move.vbus > VBAT_LOW_VAL)
//				{
//					//Power On
//					chassis_move.start_flag=1;
//				}
//				else
//				{
//					
//					//Power Off
//					chassis_move.start_flag=0;
//					chassis_move.recover_flag=0;
//				}
				
				if((chassis_move.vbus < VBAT_LOW_VAL)&&(chassis_move.start_flag==1))
					vbat_low_count++;
				
				if(vbat_low_count>100)
				{
					chassis_move.start_flag=0;
					chassis_move.recover_flag=0;
				}
				else
				{
					// 当 SWC 允许、摇杆轴回到高位并腿长合适时触发跳跃
					//Power On
					chassis_move.start_flag=1;
				}
				
			}
			
			// 俯仰角过大时自动进入翻正态势且恢复腿长初始值
			if(chassis_move.recover_flag==0
					&&((chassis_move.myPithR<((-3.1415926f)/4.0f)&&chassis_move.myPithR>((-3.1415926f)/2.0f))
					||(chassis_move.myPithR>(3.1415926f/4.0f)&&chassis_move.myPithR<(3.1415926f/2.0f))))
			{
				chassis_move.recover_flag=1;//��Ҫ����
				chassis_move.leg_set=0.08f;//ԭʼ�ȳ�
			}
			
			// 启动状态：更新速度/转向/滚转/腿长/跳跃的目标值
			if(chassis_move.start_flag==1)
			{
				if (remoter.toggle.swd == 1)
				{
					// SWD=1: 手动控制模式 (遥控器)
					if(chassis_move.vbus_mode == 1)
					{
						// 4S 模式：速度响应较弱以保护电压
						chassis_move.v_set=((float)remoter.joy.right_vert)*(-0.00097f * 0.5f);
					}
					else if(chassis_move.vbus_mode == 2)
					{
						// 6S 模式：速度响应更强
						chassis_move.v_set=((float)remoter.joy.right_vert)*(-0.00097f * 1.3f);
					}
					
					// 右摇杆水平量控制转向设定
					chassis_move.turn_set += ((float)remoter.joy.right_hori)*(-0.0000625f);
				}
				else if (remoter.toggle.swd == 2)
				{
					// SWD=2: 上位机控制模式 (USB)
					chassis_move.v_set = -g_usb_cmd.cmd_vel;
					// 假设 cmd_wel 是角速度 (rad/s)，积分到 turn_set
					chassis_move.turn_set += g_usb_cmd.cmd_wel * dt;
				}

				// 通过速度和周期积分得到位置设定
				chassis_move.x_set=chassis_move.x_set+chassis_move.v_set*dt;
				
				// SWA 控制 roll 的基础值或逐步叠加
				if(remoter.toggle.swa == 0)
				{
					chassis_move.roll_set = -0.03f;
				}
				else
				{
					chassis_move.roll_set += ((float)remoter.joy.left_hori)*(0.000016f);
				}
				// 限制 roll 目标在安全范围内
				mySaturate(&chassis_move.roll_set,-0.40f,0.40f);
				
				// SWC 决定是否固定腿长或随左摇杆微调
				if(remoter.toggle.swc == 0)
				{
					chassis_move.leg_set = 0.12f;
				}
				else
				{
					chassis_move.leg_set= (remoter.joy.left_vert + 1024)*(0.00007f) + 0.072f;//chassis_move.leg_set+((float)(data->ly-128))*(-0.000016f); 
				}
				
				// 限制腿长设定范围，避免插入或过度伸展
				mySaturate(&chassis_move.leg_set,0.09f,0.21f);
				
				// 若腿长显著变化，置位 flag 让腿部控制器刷新参数
				if(fabsf(chassis_move.last_leg_set-chassis_move.leg_set)>0.0001f)
				{//ң���������ȳ��ڱ仯
					right.leg_flag=1;	//Ϊ1��־��ң�����ڿ����ȳ����������������־���Բ�������ؼ��?��Ϊ���ȳ�����������ʱ����ؼ��������?�����?
					left.leg_flag=1;	 			
				}
				chassis_move.last_leg_set=chassis_move.leg_set;
				
				// SWB 负责跳跃开关
				if(remoter.toggle.swb == 0)
				{
						//chassis_move.jump_flag=0;
						//chassis_move.jump_flag2=0;
				}
				else
				{
					if(remoter.toggle.swc != 0 && remoter.var.b < 500 && last_jump_vrb>=500 && chassis_move.leg_set <= 0.16f)
					{
						if(chassis_move.vbus_mode == 2)
						{
							//6s
							chassis_move.jump_flag=1;
							chassis_move.jump_flag2=1;
					
						}
					}
				}
				
				// 记录当前 var.b 用于下一轮跳跃边缘判断
				last_jump_vrb = remoter.var.b;
			}
			
			else
			{
				chassis_move.v_set=0.0f;//����
				chassis_move.x_set=chassis_move.x_filter;//����
				chassis_move.turn_set=chassis_move.total_yaw;//����
				chassis_move.leg_set=0.1f;//ԭʼ�ȳ�
				chassis_move.roll_set=-0.03f; 
			}
			
		}
		else
		{
			// 遥控器离线：强制复位所有动作并清除计数
			chassis_move.start_flag=0;
			chassis_move.recover_flag=0;
			vbat_low_count = 0;
			
			chassis_move.v_set=0.0f;//����
			chassis_move.x_set=chassis_move.x_filter;//����
			chassis_move.turn_set=chassis_move.total_yaw;//����
			chassis_move.leg_set=0.1f;//ԭʼ�ȳ�
			chassis_move.roll_set=-0.03f; 
		}
		
		// 每轮结束重新启动 DMA 接收，保持持续监听
		HAL_UARTEx_ReceiveToIdle_DMA(&huart5, rx_buff, BUFF_SIZE*2); // ������Ϻ�����?

//		for (uint16_t i = 0; i < 10; ++i)
//		{
//			WS2812_SetPixel(i, 255, 0, 0);
//		}

		//white_breath();
		// 簡單的 WS2812 灯效：中心向两侧扩散
		//center_to_sides();

		// 通过 USB 发送摇杆数据用于调试或可视化
		USB_SendXY((float)remoter.joy.right_vert / 800.0f, (float)remoter.joy.right_hori / 800.0f);

		// 以 REMOTE_TIME 定义的周期延时
		osDelay(REMOTE_TIME);
	}
}


