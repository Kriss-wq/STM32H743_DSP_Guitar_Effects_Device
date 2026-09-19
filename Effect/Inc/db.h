
#ifndef PROJECT2_DB_H
#define PROJECT2_DB_H
#include "stdint.h"

#define DB_10 0.316227766f
#define DB_15 0.177827941f
#define DB_20 0.1f
#define DB_25 0.056234133f
#define DB_30 0.0316227766f
#define DB_35 0.017782794f
#define DB_40 0.01f
#define DB_45 0.005623413f
#define DB_50 0.00316227766f

void db_reduce(float *in, float *out, uint16_t size,float db);

#endif //PROJECT2_DB_H