/**
  *********************************************************************
  * @file      ps2_task.c/h
  * @brief     �������Ƕ�ȡ������ps2�ֱ�������ң�����ݣ�
	*            ��ң������ת��Ϊ�������ٶȡ�������ת�ǡ��������ȳ���
  * @note       
  * @history
  *
  @verbatim
  ==============================================================================

  ==============================================================================
  @endverbatim
  *********************************************************************
  */
	
#include "ps2_task.h"
#include "cmsis_os.h"


ps2data_t ps2data;

uint16_t Handkey;	// 按键值读取，临时存储。
uint8_t Comd[2]={0x01,0x42};	// 开始命令。请求数据
uint8_t Data[9]={0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}; // 数据存储数组
uint16_t MASK[]={
    PSB_SELECT,
    PSB_L3,
    PSB_R3 ,
    PSB_START,
    PSB_PAD_UP,
    PSB_PAD_RIGHT,
    PSB_PAD_DOWN,
    PSB_PAD_LEFT,
    PSB_L2,
    PSB_R2,
    PSB_L1,
    PSB_R1 ,
    PSB_GREEN,
    PSB_RED,
    PSB_BLUE,
    PSB_PINK
	};	// 按键值与按键名


extern chassis_t chassis_move;
extern INS_t INS;
uint32_t PS2_TIME=10;//ps2�ֱ�����������10ms
void pstwo_task(void)
{		
	PS2_SetInit();

   while(1)
	 {
		 if(Data[1]!=0x73)
		 {
		  PS2_SetInit();
		 }

		 PS2_data_read(&ps2data);//
		 PS2_data_process(&ps2data,&chassis_move,(float)PS2_TIME/1000.0f);//

	   osDelay(PS2_TIME);
	 }
}

//���ֱ���������
void PS2_Cmd(uint8_t CMD)
{
	volatile uint16_t ref=0x01;
	Data[1] = 0;
	for(ref=0x01;ref<0x0100;ref<<=1)
	{
		if(ref&CMD)
		{
			HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);                   //���һλ����λ DO_H;
		}
		else HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);//DO_L

		HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13, GPIO_PIN_SET);                        //ʱ������
		DWT_Delay(0.000005f);
		HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13, GPIO_PIN_RESET);
		DWT_Delay(0.000005f);
		HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13, GPIO_PIN_SET);
		if(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0))
			Data[1] = ref|Data[1];
	}
	DWT_Delay(0.000016f); 
}

/**************************************************************************
Function: Read the control of the ps2 handle
Input   : none
Output  : none
�������ܣ���ȡPS2�ֱ��Ŀ�����
��ڲ�������
����  ֵ����
**************************************************************************/	
uint8_t reve_flag=0;
void PS2_data_read(ps2data_t *data)
{
  //��ȡ������ֵ
	data->key=PS2_DataKey(); //��ȡ������ֵ

  //��ȡ���ң��X�᷽���ģ����	
	data->lx=PS2_AnologData(PSS_LX); 

  //��ȡ���ң��Y�᷽���ģ����	
	data->ly=PS2_AnologData(PSS_LY);

  //��ȡ�ұ�ң��X�᷽���ģ����  
	data->rx=PS2_AnologData(PSS_RX);

  //��ȡ�ұ�ң��Y�᷽���ģ����  
	data->ry=PS2_AnologData(PSS_RY);

	if((data->ry<=255&&data->ry>192)||(data->ry<64&&data->ry>=0))
	{
	  data->rx=127;
	}
	if((data->rx<=255&&data->rx>192)||(data->rx<64&&data->rx>=0))
	{
	  data->ry=128;
	}
}

extern vmc_leg_t right;			
extern vmc_leg_t left;	
void PS2_data_process(ps2data_t *data,chassis_t *chassis,float dt)
{   
	if(data->last_key!=4&&data->key==4&&chassis->start_flag==0) 
	{
		//�ֱ��ϵ�Start����������
		chassis->start_flag=1;
	}
	else if(data->last_key!=4&&data->key==4&&chassis->start_flag==1) 
	{
		//�ֱ��ϵ�Start����������
		chassis->start_flag=0;
		chassis->recover_flag=0;
	}
 if(chassis->recover_flag==0
			&&((chassis->myPithR<((-3.1415926f)/4.0f)&&chassis->myPithR>((-3.1415926f)/2.0f))
		  ||(chassis->myPithR>(3.1415926f/4.0f)&&chassis->myPithR<(3.1415926f/2.0f))))
		{
		  chassis->recover_flag=1;//��Ҫ����
			 chassis->leg_set=0.08f;//ԭʼ�ȳ�
		}

  if(data->last_key!=1&&data->key==1&&chassis->prejump_flag==0&&chassis->start_flag==1) 
	{
		//�ֱ��ϵ�select������һ����1
		chassis->prejump_flag=1;//Ԥ��Ծ��־��1
	}
	else if(data->last_key!=1&&data->key==1&&chassis->prejump_flag==1) 
	{
		//�ֱ��ϵ�select�����ٰ�һ����0
		chassis->prejump_flag=0;
	}
	
	if(data->last_key!=5&&data->key==5&&chassis->prejump_flag==1&&chassis->jump_flag==0&&chassis->jump_flag2==0) 
	{
		//�ֱ��ϵ���ߵ��������Ҽ����ϰ��������£�������Ծ
		//ֻ�е�Ԥ��Ծ��־��1��������������ܿ�����Ծ
		chassis->jump_flag=1;
		chassis->jump_flag2=1;
	}

	data->last_key=data->key;
  
	
	if(chassis->start_flag==1)
	{//����
		chassis->v_set=((float)(data->ry-128))*(-0.008f);//��ǰ����0
	  chassis->x_set=chassis->x_set+chassis->v_set*dt;
    			
		chassis->turn_set=chassis->turn_set+(data->rx-127)*(-0.0005f);//���Ҵ���0
	  
		chassis->roll_set=chassis->roll_set+((float)(data->lx-127))*(-0.00007f);

		mySaturate(&chassis->roll_set,-0.40f,0.40f);	

		chassis->leg_set=chassis->leg_set+((float)(data->ly-128))*(-0.000016f); 
		mySaturate(&chassis->leg_set,0.072f,0.21f);

		if(fabsf(chassis->last_leg_set-chassis->leg_set)>0.0001f)
		{//ң���������ȳ��ڱ仯
			right.leg_flag=1;	//Ϊ1��־��ң�����ڿ����ȳ����������������־���Բ�������ؼ�⣬��Ϊ���ȳ�����������ʱ����ؼ�������Ϊ�����
      left.leg_flag=1;	 			
		}
		chassis->last_leg_set=chassis->leg_set;
	} 
	else if(chassis->start_flag==0)
	{//�ر�
	  chassis->v_set=0.0f;//����
		chassis->x_set=chassis->x_filter;//����
	  chassis->turn_set=chassis->total_yaw;//����
	  chassis->leg_set=0.08f;//ԭʼ�ȳ�
	  chassis->roll_set=-0.03f; 
	}
	
	if(data->key==9)
	{
	  chassis->roll_set=-0.03f;	
	}
		
}




//�ж��Ƿ�Ϊ���ģʽ,0x41=ģ���̵ƣ�0x73=ģ����
//����ֵ��0�����ģʽ
//		  ����������ģʽ
uint8_t PS2_RedLight(void)
{
	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_RESET);
	PS2_Cmd(Comd[0]);  //��ʼ����
	PS2_Cmd(Comd[1]);  //��������
	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_SET);
	if( Data[1] == 0X73)   return 0 ;
	else return 1;

}
//��ȡ�ֱ�����
void PS2_ReadData(void)
{
	volatile uint8_t byte=0;
	volatile uint16_t ref=0x01;
	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_RESET);//CS_L
	PS2_Cmd(Comd[0]);  //��ʼ����
	PS2_Cmd(Comd[1]);  //��������
	for(byte=2;byte<9;byte++)          //��ʼ��������
	{
		for(ref=0x01;ref<0x100;ref<<=1)
		{
			HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13, GPIO_PIN_SET);//CLK_H
			DWT_Delay(0.000005f);
			HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13, GPIO_PIN_RESET);//CLK_L
			DWT_Delay(0.000005f);
			HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13, GPIO_PIN_SET);//CLK_H
		      if(HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0))//DI
		      Data[byte] = ref|Data[byte];
		}
        DWT_Delay(0.000016f); 
	}
	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_SET);//CS_H
}

//�Զ�������PS2�����ݽ��д���,ֻ������������  
//ֻ��һ����������ʱ����Ϊ0�� δ����Ϊ1
uint8_t PS2_DataKey()
{
	uint8_t index;

	PS2_ClearData();
	PS2_ReadData();

	Handkey=(Data[4]<<8)|Data[3];     //����16������  ����Ϊ0�� δ����Ϊ1
	for(index=0;index<16;index++)
	{	    
		if((Handkey&(1<<(MASK[index]-1)))==0)
		return index+1;
	}
	return 0;          //û���κΰ�������
}

//�õ�һ��ҡ�˵�ģ����	 ��Χ0~256
uint8_t PS2_AnologData(uint8_t button)
{
	return Data[button];
}

//������ݻ�����
void PS2_ClearData()
{
	uint8_t a;
	for(a=0;a<9;a++)
		Data[a]=0x00;
}
/******************************************************
Function:    void PS2_Vibration(u8 motor1, u8 motor2)
Description: �ֱ��𶯺�����
Calls:		 void PS2_Cmd(u8 CMD);
Input: motor1:�Ҳ�С�𶯵�� 0x00�أ�������
	   motor2:�����𶯵�� 0x40~0xFF �������ֵԽ�� ��Խ��
******************************************************/
void PS2_Vibration(uint8_t motor1, uint8_t motor2)
{
	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_RESET);//CS_L
	DWT_Delay(0.000016f); 
  PS2_Cmd(0x01);  //��ʼ����
	PS2_Cmd(0x42);  //��������
	PS2_Cmd(0X00);
	PS2_Cmd(motor1);
	PS2_Cmd(motor2);
	PS2_Cmd(0X00);
	PS2_Cmd(0X00);
	PS2_Cmd(0X00);
	PS2_Cmd(0X00);
	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_SET);//CS_H
	DWT_Delay(0.000016f);  
}

//short poll
void PS2_ShortPoll(void)
{
	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_RESET);//CS_L
	DWT_Delay(0.000016f); 
	PS2_Cmd(0x01);  
	PS2_Cmd(0x42);  
	PS2_Cmd(0X00);
	PS2_Cmd(0x00);
	PS2_Cmd(0x00);
	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_SET);//CS_H
	DWT_Delay(0.000016f); 	
}

//��������
void PS2_EnterConfing(void)
{
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_RESET);//CS_L
	DWT_Delay(0.000016f); 
	PS2_Cmd(0x01);  
	PS2_Cmd(0x43);  
	PS2_Cmd(0X00);
	PS2_Cmd(0x01);
	PS2_Cmd(0x00);
	PS2_Cmd(0X00);
	PS2_Cmd(0X00);
	PS2_Cmd(0X00);
	PS2_Cmd(0X00);
	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_SET);//CS_H
	DWT_Delay(0.000016f); 
}
//����ģʽ����
void PS2_TurnOnAnalogMode(void)
{
	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_RESET);//CS_L
	PS2_Cmd(0x01);  
	PS2_Cmd(0x44);  
	PS2_Cmd(0X00);
	PS2_Cmd(0x01); //analog=0x01;digital=0x00  �������÷���ģʽ
	PS2_Cmd(0x03); //Ox03�������ã�������ͨ��������MODE������ģʽ��
				   //0xEE�������������ã���ͨ��������MODE������ģʽ��
	PS2_Cmd(0X00);
	PS2_Cmd(0X00);
	PS2_Cmd(0X00);
	PS2_Cmd(0X00);
	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_SET);//CS_H
	DWT_Delay(0.000016f); 
}
//������
void PS2_VibrationMode(void)
{
	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_RESET);//CS_L
	DWT_Delay(0.000016f); 
	PS2_Cmd(0x01);  
	PS2_Cmd(0x4D);  
	PS2_Cmd(0X00);
	PS2_Cmd(0x00);
	PS2_Cmd(0X01);
	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_SET);//CS_H
	DWT_Delay(0.000016f); 
}
//��ɲ���������
void PS2_ExitConfing(void)
{
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_RESET);//CS_L
	DWT_Delay(0.000016f); 
	PS2_Cmd(0x01);  
	PS2_Cmd(0x43);  
	PS2_Cmd(0X00);
	PS2_Cmd(0x00);
	PS2_Cmd(0x5A);
	PS2_Cmd(0x5A);
	PS2_Cmd(0x5A);
	PS2_Cmd(0x5A);
	PS2_Cmd(0x5A);
	HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9, GPIO_PIN_SET);//CS_H
	DWT_Delay(0.000016f); 
}

//�ֱ����ó�ʼ��
void PS2_SetInit(void)
{
	PS2_ShortPoll();
	PS2_ShortPoll();
	PS2_ShortPoll();
	PS2_EnterConfing();		//��������ģʽ
	PS2_TurnOnAnalogMode();	//�����̵ơ�����ģʽ����ѡ���Ƿ񱣴�
	//PS2_VibrationMode();	//������ģʽ
	PS2_ExitConfing();		//��ɲ���������
}




