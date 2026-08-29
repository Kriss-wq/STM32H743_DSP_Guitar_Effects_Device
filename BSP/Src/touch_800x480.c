
#include "touch_800x480.h"
#include <stdio.h>
#include <string.h>

volatile TouchStructure touchInfo;
static uint8_t Modify_Flag = 0;
static volatile uint8_t touch_irq_pending = 0;

#define TOUCH_I2C_RETRY        3U
#define TOUCH_BACKUP_POLL_MS   50U

static void Touch_ApplyCalib(uint16_t *x, uint16_t *y);
static uint8_t GT9XX_ReadReg_Retry(uint16_t addr, uint8_t cnt, uint8_t *value, uint8_t retries);
static void GT9XX_ClearStatus(void);
static void Touch_Info_Update(uint8_t flag, uint8_t num, const uint16_t *x, const uint16_t *y);
static uint32_t Touch_CriticalEnter(void);
static void Touch_CriticalExit(uint32_t primask);

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

uint8_t GT9XX_WriteHandle(uint16_t addr)
{
	Touch_IIC_Start();
	if (Touch_IIC_WriteByte(GT9XX_IIC_WADDR) != ACK_OK)
	{
		Touch_IIC_Stop();
		return ERROR;
	}
	if (Touch_IIC_WriteByte((uint8_t)(addr >> 8)) != ACK_OK)
	{
		Touch_IIC_Stop();
		return ERROR;
	}
	if (Touch_IIC_WriteByte((uint8_t)(addr)) != ACK_OK)
	{
		Touch_IIC_Stop();
		return ERROR;
	}
	return SUCCESS;
}

uint8_t GT9XX_WriteData(uint16_t addr, uint8_t value)
{
	uint8_t status = ERROR;

	if (GT9XX_WriteHandle(addr) == SUCCESS)
	{
		if (Touch_IIC_WriteByte(value) == ACK_OK)
		{
			status = SUCCESS;
		}
	}
	Touch_IIC_Stop();
	return status;
}

uint8_t GT9XX_WriteReg(uint16_t addr, uint8_t cnt, uint8_t *value)
{
	uint8_t i;

	if (GT9XX_WriteHandle(addr) != SUCCESS)
	{
		Touch_IIC_Stop();
		return ERROR;
	}

	for (i = 0; i < cnt; i++)
	{
		if (Touch_IIC_WriteByte(value[i]) != ACK_OK)
		{
			Touch_IIC_Stop();
			return ERROR;
		}
	}

	Touch_IIC_Stop();
	return SUCCESS;
}

uint8_t GT9XX_ReadReg(uint16_t addr, uint8_t cnt, uint8_t *value)
{
	uint8_t i;

	if (GT9XX_WriteHandle(addr) != SUCCESS)
	{
		Touch_IIC_Stop();
		return ERROR;
	}

	Touch_IIC_Start();
	if (Touch_IIC_WriteByte(GT9XX_IIC_RADDR) != ACK_OK)
	{
		Touch_IIC_Stop();
		return ERROR;
	}

	for (i = 0; i < cnt; i++)
	{
		value[i] = Touch_IIC_ReadByte((i < (cnt - 1U)) ? 1U : 0U);
	}

	Touch_IIC_Stop();
	return SUCCESS;
}

void Touch_INT_IRQ_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	Touch_INT_CLK_ENABLE;
	GPIO_InitStruct.Pin = Touch_INT_PIN;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(Touch_INT_PORT, &GPIO_InitStruct);

	HAL_NVIC_SetPriority(EXTI15_10_IRQn, 6, 0);
	HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void Touch_INT_IRQHandler(void)
{
	if (__HAL_GPIO_EXTI_GET_IT(Touch_INT_PIN) != 0U)
	{
		__HAL_GPIO_EXTI_CLEAR_IT(Touch_INT_PIN);
		touch_irq_pending = 1U;
	}
}

void PanelRecognition(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	Touch_INT_CLK_ENABLE;
	Touch_RST_CLK_ENABLE;

	GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull  = GPIO_PULLDOWN;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	GPIO_InitStruct.Pin   = Touch_INT_PIN;

	HAL_GPIO_Init(Touch_INT_PORT, &GPIO_InitStruct);

	GPIO_InitStruct.Pin  = Touch_RST_PIN;
	HAL_GPIO_Init(Touch_RST_PORT, &GPIO_InitStruct);

	Touch_IIC_Delay(4000);

	if ((HAL_GPIO_ReadPin(Touch_RST_PORT, Touch_RST_PIN) != 1) &&
	    (HAL_GPIO_ReadPin(Touch_INT_PORT, Touch_INT_PIN) != 1))
	{
		Modify_Flag = 1;
	}
}

uint8_t Touch_Init(void)
{
	uint8_t GT9XX_Info[11];
	uint8_t cfgVersion = 0;

	PanelRecognition();

	Touch_IIC_GPIO_Config();
	GT9XX_Reset();

	if (GT9XX_ReadReg_Retry(GT9XX_ID_ADDR, 11, GT9XX_Info, TOUCH_I2C_RETRY) != SUCCESS)
	{
		return ERROR;
	}
	GT9XX_ReadReg(GT9XX_CFG_ADDR, 1, &cfgVersion);

	if (GT9XX_Info[0] == '9')
	{
		if ((((uint16_t)GT9XX_Info[7] << 8) + GT9XX_Info[6]) == 1024U)
		{
			Modify_Flag = 1;
		}
		else if ((((uint16_t)GT9XX_Info[7] << 8) + GT9XX_Info[6]) == 800U)
		{
			Modify_Flag = 0;
		}

		Touch_INT_IRQ_Init();
		Touch_Info_Update(0, 0, NULL, NULL);
		return SUCCESS;
	}

	return ERROR;
}

void Touch_Info_Get(TouchSnapshot *out)
{
	uint32_t primask;
	uint8_t i;

	if (out == NULL)
	{
		return;
	}

	primask = Touch_CriticalEnter();
	out->flag = touchInfo.flag;
	out->num = touchInfo.num;
	for (i = 0; i < TOUCH_MAX; i++)
	{
		out->x[i] = touchInfo.x[i];
		out->y[i] = touchInfo.y[i];
	}
	Touch_CriticalExit(primask);
}

void Touch_Scan(void)
{
	uint8_t touchData[2 + 8 * TOUCH_MAX];
	uint8_t i = 0;
	uint8_t status;
	uint8_t num;
	uint16_t x[TOUCH_MAX];
	uint16_t y[TOUCH_MAX];
	static uint8_t release_cnt = 0;
	const uint8_t release_debounce = 3;
	static uint32_t last_backup_poll = 0;
	uint32_t now = HAL_GetTick();

	if ((touch_irq_pending == 0U) && ((now - last_backup_poll) < TOUCH_BACKUP_POLL_MS))
	{
		return;
	}

	touch_irq_pending = 0U;
	last_backup_poll = now;

	if (GT9XX_ReadReg_Retry(GT9XX_READ_ADDR, 1, &status, TOUCH_I2C_RETRY) != SUCCESS)
	{
		GT9XX_ClearStatus();
		return;
	}

	if ((status & 0x80U) == 0U)
	{
		return;
	}

	num = status & 0x0FU;
	if ((num >= 1U) && (num <= TOUCH_MAX))
	{
		if (GT9XX_ReadReg_Retry(GT9XX_READ_ADDR, (uint8_t)(2U + 8U * TOUCH_MAX), touchData, TOUCH_I2C_RETRY) != SUCCESS)
		{
			GT9XX_ClearStatus();
			return;
		}

		for (i = 0; i < num; i++)
		{
			y[i] = (uint16_t)((touchData[5 + 8 * i] << 8) | touchData[4 + 8 * i]);
			x[i] = (uint16_t)((touchData[3 + 8 * i] << 8) | touchData[2 + 8 * i]);
			Touch_ApplyCalib(&x[i], &y[i]);
		}

		Touch_Info_Update(1, num, x, y);
		release_cnt = 0;
	}
	else
	{
		if (release_cnt < 255U)
		{
			release_cnt++;
		}
		if (release_cnt >= release_debounce)
		{
			Touch_Info_Update(0, 0, NULL, NULL);
		}
	}

	GT9XX_ClearStatus();
}

static void Touch_ApplyCalib(uint16_t *x, uint16_t *y)
{
	if (Modify_Flag != 0U)
	{
		*x = (uint16_t)((uint32_t)(*x) * 799U / 1023U);
		*y = (uint16_t)((uint32_t)(*y) * 479U / 599U);
	}

	if (*x > 799U)
	{
		*x = 799U;
	}
	if (*y > 479U)
	{
		*y = 479U;
	}
}

static uint8_t GT9XX_ReadReg_Retry(uint16_t addr, uint8_t cnt, uint8_t *value, uint8_t retries)
{
	while (retries-- > 0U)
	{
		if (GT9XX_ReadReg(addr, cnt, value) == SUCCESS)
		{
			return SUCCESS;
		}
		Touch_IIC_Delay(50);
	}
	return ERROR;
}

static void GT9XX_ClearStatus(void)
{
	uint8_t retries = TOUCH_I2C_RETRY;

	while (retries-- > 0U)
	{
		if (GT9XX_WriteData(GT9XX_READ_ADDR, 0) == SUCCESS)
		{
			return;
		}
		Touch_IIC_Delay(50);
	}
}

static void Touch_Info_Update(uint8_t flag, uint8_t num, const uint16_t *x, const uint16_t *y)
{
	uint8_t i;
	uint32_t primask = Touch_CriticalEnter();

	touchInfo.flag = flag;
	touchInfo.num = num;
	if ((flag != 0U) && (x != NULL) && (y != NULL))
	{
		for (i = 0; i < num; i++)
		{
			touchInfo.x[i] = x[i];
			touchInfo.y[i] = y[i];
		}
	}
	Touch_CriticalExit(primask);
}

static uint32_t Touch_CriticalEnter(void)
{
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	return primask;
}

static void Touch_CriticalExit(uint32_t primask)
{
	if (primask == 0U)
	{
		__enable_irq();
	}
}
