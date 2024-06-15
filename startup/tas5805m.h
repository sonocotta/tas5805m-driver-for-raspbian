#pragma once

#define CFG_META_SWITCH (255)
#define CFG_META_DELAY (254)
#define CFG_META_BURST (253)
#define CFG_END_1 (0Xaa)
#define CFG_END_2 (0Xcc)
#define CFG_END_3 (0Xee)

typedef struct {
  uint8_t offset;
  uint8_t value;
} tas5805m_cfg_reg_t;
