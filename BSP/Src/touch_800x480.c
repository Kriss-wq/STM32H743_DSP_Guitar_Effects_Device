
#include "touch_800x480.h"
#include <stdio.h>
#include <string.h>

volatile TouchStructure touchInfo;
volatile static uint8_t Modify_Flag = 0;

void GT9XX_Reset(void)
{
	Touch_INT_Out();

	HAL_GPIO_WritePin(Touch_INT_PORT,Touch_INT_PIN,GPIO_PIN_RESET);
	HAL_GPIO_WritePin(Touch_RST_PORT,Touch_RST_PIN,GPIO_PIN_SET);
	Touch_IIC_Delay(10000);

	HAL_GPIO_WritePin(Touch_RST_PORT,Touch_RST_PIN,GPIO_PIN_RESET);
	Touch_IIC_Delay(150000);
	HAL_GPIO_WritePin(Touch_RST_PORT,Touch_RST_PIN,GPIO_PIN_SET);
	Touch_IIC_Delay(350000);
	Touch_INT_In();
	Touch_IIC_Delay(20000);

}

uint8_t GT9XX_WriteHandle (uint16_t addr)
{
	uint8_t status;

	Touch_IIC_Start();
	if( Touch_IIC_WriteByte(GT9XX_IIC_WADDR) == ACK_OK )
	{
		if( Touch_IIC_WriteByte((uint8_t)(addr >> 8)) == ACK_OK )
		{
			if( Touch_IIC_WriteByte((uint8_t)(addr)) != ACK_OK )
			{
				status = ERROR;
			}
		}
	}
	status = SUCCESS;
	return status;
}

uint8_t GT9XX_WriteData (uint16_t addr,uint8_t value)
{
	uint8_t status;

	Touch_IIC_Start();

	if( GT9XX_WriteHandle(addr) == SUCCESS)
	{
		if (Touch_IIC_WriteByte(value) != ACK_OK)
		{
			status = ERROR;
		}
	}
	Touch_IIC_Stop();

	status = SUCCESS;
	return status;
}

uint8_t GT9XX_WriteReg (uint16_t addr, uint8_t cnt, uint8_t *value)
{
	uint8_t status;
	uint8_t i;

	Touch_IIC_Start();

	if( GT9XX_WriteHandle(addr) == SUCCESS)
	{
		for(i = 0 ; i < cnt; i++)
		{
			Touch_IIC_WriteByte(value[i]);
		}
		Touch_IIC_Stop();
		status = SUCCESS;
	}
	else
	{
		Touch_IIC_Stop();
		status = ERROR;
	}
	return status;
}

uint8_t GT9XX_ReadReg (uint16_t addr, uint8_t cnt, uint8_t *value)
{
	uint8_t status;
	uint8_t i;

	status = ERROR;
	Touch_IIC_Start();

	if( GT9XX_WriteHandle(addr) == SUCCESS)
	{
		Touch_IIC_Start();
		if (Touch_IIC_WriteByte(GT9XX_IIC_RADDR) == ACK_OK)
		{
			for(i = 0 ; i < cnt; i++)
			{
				if (i == (cnt - 1))
				{
					value[i] = Touch_IIC_ReadByte(0);
				}
				else
				{
					value[i] = Touch_IIC_ReadByte(1);
				}
			}
			Touch_IIC_Stop();
			status = SUCCESS;
		}
	}
	Touch_IIC_Stop();
	return (status);
}

void	PanelRecognition (void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	Touch_INT_CLK_ENABLE;
	Touch_RST_CLK_ENABLE;

	GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull  = GPIO_PULLDOWN;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	GPIO_InitStruct.Pin   = Touch_INT_PIN ;

	HAL_GPIO_Init(Touch_INT_PORT, &GPIO_InitStruct);

	GPIO_InitStruct.Pin  = Touch_RST_PIN;
	HAL_GPIO_Init(Touch_RST_PORT, &GPIO_InitStruct);

	Touch_IIC_Delay(4000);

	if( (HAL_GPIO_ReadPin(Touch_RST_PORT,Touch_RST_PIN) != 1) && (HAL_GPIO_ReadPin(Touch_INT_PORT,Touch_INT_PIN) != 1)  )
	{

		Modify_Flag	= 1;
	}
}

uint8_t Touch_Init(void)
{
	uint8_t GT9XX_Info[11];
	uint8_t cfgVersion = 0;

	PanelRecognition();

	Touch_IIC_GPIO_Config();
	GT9XX_Reset();

	GT9XX_ReadReg (GT9XX_ID_ADDR,11,GT9XX_Info);
	GT9XX_ReadReg (GT9XX_CFG_ADDR,1,&cfgVersion);

	if( GT9XX_Info[0] == '9' )
	{

		if( ( (GT9XX_Info[7]<<8) + GT9XX_Info[6] ) == 1024 )
		{

			Modify_Flag	= 1;
		}
		else if( ( (GT9XX_Info[7]<<8) + GT9XX_Info[6] ) == 800 )
		{
			Modify_Flag	= 0;
		}

		return SUCCESS;
	}
	else
	{

		return ERROR;
	}

}

void Touch_Scan(void)
{
	uint8_t  touchData[2 + 8 * TOUCH_MAX];
	uint8_t  i = 0;
	uint8_t  status;
	uint8_t  num;
	static uint8_t release_cnt = 0;
	const uint8_t release_debounce = 3; /* 连续 N 次无触摸才抬起 */

	/* GT911: bit7=缓冲就绪，bit3:0=点数 */
	if(GT9XX_ReadReg(GT9XX_READ_ADDR, 1, &status) != SUCCESS) {
		return;
	}

	/* 无新数据：保持上次状态，不做按下/抬起切换 */
	if((status & 0x80U) == 0U) {
		return;
	}

	num = status & 0x0FU;
	if((num >= 1U) && (num <= TOUCH_MAX)) {
		if(GT9XX_ReadReg(GT9XX_READ_ADDR, 2 + 8 * TOUCH_MAX, touchData) != SUCCESS) {
			GT9XX_WriteData(GT9XX_READ_ADDR, 0);
			return;
		}

		for(i = 0; i < num; i++) {
			touchInfo.y[i] = (uint16_t)((touchData[5 + 8 * i] << 8) | touchData[4 + 8 * i]);
			touchInfo.x[i] = (uint16_t)((touchData[3 + 8 * i] << 8) | touchData[2 + 8 * i]);
		}
		touchInfo.num = num;
		touchInfo.flag = 1;
		release_cnt = 0;
	} else {
		/* 抬起消抖：避免按住时偶发 num=0 触发反复 PR/REL */
		if(release_cnt < 255U) {
			release_cnt++;
		}
		if(release_cnt >= release_debounce) {
			touchInfo.num = 0;
			touchInfo.flag = 0;
		}
	}

	GT9XX_WriteData(GT9XX_READ_ADDR, 0);
}
