//
// Created by 22974 on 2026/4/12.
//

#include "delay.h"
#include "tim.h"
void delay_us(uint16_t time) {
    __HAL_TIM_SET_COUNTER(&htim1, 0);
    HAL_TIM_Base_Start(&htim1);
    while (__HAL_TIM_GET_COUNTER(&htim1) < time);
    HAL_TIM_Base_Stop(&htim1);
}