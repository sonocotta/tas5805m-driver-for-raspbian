#pragma once

#include "../tas58xx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char cfg_u8;

typedef struct {
    cfg_u8 page;
    cfg_u8 offset;
    cfg_u8 value;
} reg_sequence_eq;

/* BQ registers */
#define TAS58XX_REG_BOOK_EQ 0xAA

#define TAS58XX_EQ_MAX_DB             15
#define TAS58XX_EQ_MIN_DB            -TAS58XX_EQ_MAX_DB

#define TAS58XX_EQ_STEPS    (1 + TAS58XX_EQ_MAX_DB - TAS58XX_EQ_MIN_DB)
#define TAS58XX_EQ_BANDS              15
#define TAS58XX_EQ_KOEF_PER_BAND       5
#define TAS58XX_EQ_REG_PER_KOEF        4
#define TAS58XX_EQ_REG_PER_STEP (TAS58XX_EQ_BANDS * TAS58XX_EQ_KOEF_PER_BAND * TAS58XX_EQ_REG_PER_KOEF)

#define TAS58XX_EQ_PROFILE_BANDS 3
#define TAS58XX_EQ_PROFILE_REG_PER_STEP (TAS58XX_EQ_PROFILE_BANDS * TAS58XX_EQ_KOEF_PER_BAND * TAS58XX_EQ_REG_PER_KOEF)

#ifdef __cplusplus
}
#endif