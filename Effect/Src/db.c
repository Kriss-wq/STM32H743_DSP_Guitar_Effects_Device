#include "db.h"
#include <stdint.h>
#include "arm_math.h"


void db_reduce(float *in, float *out, uint16_t size,float db)
{
    arm_scale_f32(in,db,out,size);
}