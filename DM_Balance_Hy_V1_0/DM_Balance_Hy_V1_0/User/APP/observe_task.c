/**
	*********************************************************************
	* @file      observe_task.c
	* @brief     观测任务：使用卡尔曼滤波估计底盘线速度和位置
	* @note
	* @history
	*
	*********************************************************************
	*/

#include "observe_task.h"
#include "kalman_filter.h"
#include "cmsis_os.h"




KalmanFilter_t vaEstimateKF;    // 卡尔曼滤波器结构体

float vaEstimateKF_F[4] = {1.0f, 0.003f, 
													 0.0f, 1.0f};    // 状态转移矩阵 F（含采样时间，示例值）

float vaEstimateKF_P[4] = {1.0f, 0.0f,
													 0.0f, 1.0f};    // 状态协方差矩阵 P 初始值

float vaEstimateKF_Q[4] = {1.0f, 0.0f, 
													 0.0f, 1.0f};    // 过程噪声协方差 Q 初始值

float vaEstimateKF_R[4] = {200.0f, 0.0f, 
														0.0f,  200.0f}; 

float vaEstimateKF_K[4];

const float vaEstimateKF_H[4] = {1.0f, 0.0f,
																 0.0f, 1.0f};    // 观测矩阵 H（单位矩阵）

extern INS_t INS;        
extern chassis_t chassis_move;                                                                 
                                                                 
extern vmc_leg_t right;            
extern vmc_leg_t left;    

float vel_acc[2]; 
uint32_t OBSERVE_TIME=3; // 观测周期 3 ms

                                                                 
void     Observe_task(void)
{
		while(INS.ins_flag==0)
		{ // 等待 INS 初始化完毕
			osDelay(1);    
		}
		static float wr,wl=0.0f;
		static float vrb,vlb=0.0f;
		static float aver_v=0.0f;
        
		xvEstimateKF_Init(&vaEstimateKF);

		while(1)
		{  
				wr= -chassis_move.wheel_motor[0].para.vel-INS.Gyro[1]+right.d_alpha; // 计算右侧速度相关项（电机速度、陀螺仪和足端角变化）
				vrb=wr*0.0603f+right.L0*right.d_theta*arm_cos_f32(right.theta)+right.d_L0*arm_sin_f32(right.theta); // 右侧速度分量
        
				wl= -chassis_move.wheel_motor[1].para.vel+INS.Gyro[1]+left.d_alpha; // 计算左侧速度相关项
				vlb=wl*0.0603f+left.L0*left.d_theta*arm_cos_f32(left.theta)+left.d_L0*arm_sin_f32(left.theta); // 左侧速度分量
        
				aver_v=(vrb-vlb)/2.0f; // 取平均速度
				xvEstimateKF_Update(&vaEstimateKF,-INS.MotionAccel_b[0],aver_v);
        
				// 将卡尔曼滤波输出更新到全局状态
				chassis_move.v_filter=vel_acc[0]; // 从卡尔曼滤波结果获取速度估计
				chassis_move.x_filter=chassis_move.x_filter+chassis_move.v_filter*((float)OBSERVE_TIME/1000.0f);

		// 下面是另一种直接由轮速计算的速度估计（保留以备参考）
		//chassis_move.v_filter=(chassis_move.wheel_motor[0].para.vel-chassis_move.wheel_motor[1].para.vel)*(-0.0603f)/2.0f;
		//chassis_move.x_filter=chassis_move.x_filter+chassis_move.v_filter*((float)OBSERVE_TIME/1000.0f);
                 
		osDelay(OBSERVE_TIME);
		}
}

void xvEstimateKF_Init(KalmanFilter_t *EstimateKF)
{
		Kalman_Filter_Init(EstimateKF, 2, 0, 2);    // 状态维度2，控制量维度0，观测维度2
    
		memcpy(EstimateKF->F_data, vaEstimateKF_F, sizeof(vaEstimateKF_F));
		memcpy(EstimateKF->P_data, vaEstimateKF_P, sizeof(vaEstimateKF_P));
		memcpy(EstimateKF->Q_data, vaEstimateKF_Q, sizeof(vaEstimateKF_Q));
		memcpy(EstimateKF->R_data, vaEstimateKF_R, sizeof(vaEstimateKF_R));
		memcpy(EstimateKF->H_data, vaEstimateKF_H, sizeof(vaEstimateKF_H));

}

void xvEstimateKF_Update(KalmanFilter_t *EstimateKF ,float acc,float vel)
{    
		memcpy(EstimateKF->Q_data, vaEstimateKF_Q, sizeof(vaEstimateKF_Q));
		memcpy(EstimateKF->R_data, vaEstimateKF_R, sizeof(vaEstimateKF_R));
    
		// 更新测量向量
		EstimateKF->MeasuredVector[0] = vel; // 测量速度
		EstimateKF->MeasuredVector[1] = acc; // 测量加速度
            
		// 执行卡尔曼滤波更新
		Kalman_Filter_Update(EstimateKF);

		// 读取滤波结果
		for (uint8_t i = 0; i < 2; i++)
		{
			vel_acc[i] = EstimateKF->FilteredValue[i];
		}
}

fp32  RAMP_float( fp32  final, fp32  now, fp32  ramp )
{
			fp32  buffer = 0;
        
			buffer = final - now;
    
				if (buffer > 0)
				{
								if (buffer > ramp)
								{  
												now += ramp;
								}   
								else
								{
												now += buffer;
								}
				}
				else
				{
								if (buffer < -ramp)
								{
												now += -ramp;
								}
								else
								{
												now += buffer;
								}
				}
        
				return now;
}
