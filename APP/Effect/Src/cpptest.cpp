//
// Created by 22974 on 2026/8/29.
//

#include "stdint.h"

class cpptest
{
    public:
        uint16_t i;
        uint16_t get_i(void);
};

uint16_t cpptest::get_i(void)
{
    return i;
}