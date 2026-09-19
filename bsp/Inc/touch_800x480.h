#ifndef __TOUCH_H
#define __TOUCH_H

#include "stm32h7xx_hal.h"
#include "touch_iic.h"

#ifndef SUCCESS
#define SUCCESS 0U
#endif
#ifndef ERROR
#define ERROR   1U
#endif

#define TOUCH_MAX   5

typedef struct
{
	uint8_t  flag;
	uint8_t  num;
	uint16_t x[TOUCH_MAX];
	uint16_t y[TOUCH_MAX];
} TouchStructure;

typedef struct
{
	uint8_t  flag;
	uint8_t  num;
	uint16_t x[TOUCH_MAX];
	uint16_t y[TOUCH_MAX];
} TouchSnapshot;

extern volatile TouchStructure touchInfo;

#define GT9XX_IIC_RADDR 0xBB
#define GT9XX_IIC_WADDR 0xBA

#define GT9XX_CFG_ADDR  0x8047
#define GT9XX_READ_ADDR 0x814E
#define GT9XX_ID_ADDR   0x8140

uint8_t Touch_Init(void);
void    Touch_Scan(void);
void    Touch_Info_Get(TouchSnapshot *out);
void    GT9XX_Reset(void);
void    Touch_INT_IRQ_Init(void);
void    Touch_INT_IRQHandler(void);
void    GT9XX_SendCfg(void);
void    GT9XX_ReadCfg(void);

#endif
