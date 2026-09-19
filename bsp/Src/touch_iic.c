
#include "touch_iic.h"

void Touch_IIC_GPIO_Config (void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	Touch_IIC_SCL_CLK_ENABLE;
	Touch_IIC_SDA_CLK_ENABLE;
	Touch_INT_CLK_ENABLE;
	Touch_RST_CLK_ENABLE;

	GPIO_InitStruct.Pin 			= Touch_IIC_SCL_PIN;
	GPIO_InitStruct.Mode 		= GPIO_MODE_OUTPUT_OD;
	GPIO_InitStruct.Pull 		= GPIO_NOPULL;
	GPIO_InitStruct.Speed 		= GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(Touch_IIC_SCL_PORT, &GPIO_InitStruct);

	GPIO_InitStruct.Pin 			= Touch_IIC_SDA_PIN;
	HAL_GPIO_Init(Touch_IIC_SDA_PORT, &GPIO_InitStruct);

	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull  = GPIO_PULLUP;

	GPIO_InitStruct.Pin = Touch_INT_PIN;
	HAL_GPIO_Init(Touch_INT_PORT, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = Touch_RST_PIN;
	HAL_GPIO_Init(Touch_RST_PORT, &GPIO_InitStruct);

	HAL_GPIO_WritePin(Touch_IIC_SCL_PORT, Touch_IIC_SCL_PIN, GPIO_PIN_SET);
	HAL_GPIO_WritePin(Touch_IIC_SDA_PORT, Touch_IIC_SDA_PIN, GPIO_PIN_SET);
	HAL_GPIO_WritePin(Touch_INT_PORT, 	  Touch_INT_PIN,     GPIO_PIN_RESET);
	HAL_GPIO_WritePin(Touch_RST_PORT,     Touch_RST_PIN,     GPIO_PIN_SET);

}

void Touch_IIC_Delay(uint32_t a)
{
	volatile uint16_t i;
	while (a --)
	{
		for (i = 0; i < 1; i++);
	}
}

void Touch_INT_Out(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull  = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	GPIO_InitStruct.Pin   = Touch_INT_PIN ;

	HAL_GPIO_Init(Touch_INT_PORT, &GPIO_InitStruct);
}

void Touch_INT_In(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull  = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	GPIO_InitStruct.Pin   = Touch_INT_PIN ;

	HAL_GPIO_Init(Touch_INT_PORT, &GPIO_InitStruct);

}

void Touch_IIC_Start(void)
{
	Touch_IIC_SDA(1);
	Touch_IIC_SCL(1);
	Touch_IIC_Delay(IIC_DelayVaule);

	Touch_IIC_SDA(0);
	Touch_IIC_Delay(IIC_DelayVaule);
	Touch_IIC_SCL(0);
	Touch_IIC_Delay(IIC_DelayVaule);
}

void Touch_IIC_Stop(void)
{
	Touch_IIC_SCL(0);
	Touch_IIC_Delay(IIC_DelayVaule);
	Touch_IIC_SDA(0);
	Touch_IIC_Delay(IIC_DelayVaule);

	Touch_IIC_SCL(1);
	Touch_IIC_Delay(IIC_DelayVaule);
	Touch_IIC_SDA(1);
	Touch_IIC_Delay(IIC_DelayVaule);
}

void Touch_IIC_ACK(void)
{
	Touch_IIC_SCL(0);
	Touch_IIC_Delay(IIC_DelayVaule);
	Touch_IIC_SDA(0);
	Touch_IIC_Delay(IIC_DelayVaule);
	Touch_IIC_SCL(1);
	Touch_IIC_Delay(IIC_DelayVaule);

	Touch_IIC_SCL(0);
	Touch_IIC_SDA(1);

	Touch_IIC_Delay(IIC_DelayVaule);

}

void Touch_IIC_NoACK(void)
{
	Touch_IIC_SCL(0);
	Touch_IIC_Delay(IIC_DelayVaule);
	Touch_IIC_SDA(1);
	Touch_IIC_Delay(IIC_DelayVaule);
	Touch_IIC_SCL(1);
	Touch_IIC_Delay(IIC_DelayVaule);

	Touch_IIC_SCL(0);
	Touch_IIC_Delay(IIC_DelayVaule);
}

uint8_t Touch_IIC_WaitACK(void)
{
	Touch_IIC_SDA(1);
	Touch_IIC_Delay(IIC_DelayVaule);
	Touch_IIC_SCL(1);
	Touch_IIC_Delay(IIC_DelayVaule);

	if( HAL_GPIO_ReadPin(Touch_IIC_SDA_PORT,Touch_IIC_SDA_PIN) != 0)
	{
		Touch_IIC_SCL(0);
		Touch_IIC_Delay( IIC_DelayVaule );
		return ACK_ERR;
	}
	else
	{
		Touch_IIC_SCL(0);
		Touch_IIC_Delay( IIC_DelayVaule );
		return ACK_OK;
	}
}

uint8_t Touch_IIC_WriteByte(uint8_t IIC_Data)
{
	uint8_t i;

	for (i = 0; i < 8; i++)
	{
		Touch_IIC_SDA(IIC_Data & 0x80);

		Touch_IIC_Delay( IIC_DelayVaule );
		Touch_IIC_SCL(1);
		Touch_IIC_Delay( IIC_DelayVaule );
		Touch_IIC_SCL(0);
		if(i == 7)
		{
			Touch_IIC_SDA(1);
		}
		IIC_Data <<= 1;
	}

	return Touch_IIC_WaitACK();
}

uint8_t Touch_IIC_ReadByte(uint8_t ACK_Mode)
{
	uint8_t IIC_Data = 0;
	uint8_t i = 0;

	for (i = 0; i < 8; i++)
	{
		IIC_Data <<= 1;

		Touch_IIC_SCL(1);
		Touch_IIC_Delay( IIC_DelayVaule );
		IIC_Data |= (HAL_GPIO_ReadPin(Touch_IIC_SDA_PORT,Touch_IIC_SDA_PIN) & 0x01);
		Touch_IIC_SCL(0);
		Touch_IIC_Delay( IIC_DelayVaule );
	}

	if ( ACK_Mode == 1 )
		Touch_IIC_ACK();
	else
		Touch_IIC_NoACK();

	return IIC_Data;
}
