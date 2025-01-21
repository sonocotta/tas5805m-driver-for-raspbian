#pragma once

#define CFG_META_SWITCH (255)
#define CFG_META_DELAY (254)
#define CFG_META_BURST (253)
#define CFG_END_1 (0Xaa)
#define CFG_END_2 (0Xcc)
#define CFG_END_3 (0Xee)

#define REG_PAGE_SET 0x00
#define REG_BOOK_SET 0x7f

typedef struct {
  uint8_t offset;
  uint8_t value;
} tas5805m_cfg_reg_t;

enum {
    NORMAL_MODE = 0,  /* Normal mode (bit 1 = 0) */
    BRIDGE_MODE = 1   /* Bridge mode (bit 1 = 1) */
};

static const char * const dac_mode_text[] = {
    "Normal",  /* Text for value 0 */
    "Bridge"   /* Text for value 1 */
};

static const char * const modulation_mode_text[] = {
    "BD",  /* Text for value 0 */
    "1SPW",   /* Text for value 1 */
    "Hybrid"   /* Text for value 2 */
};

static const char * const switch_freq_text[] = {
    "768K",  /* Text for value 0 */
    "384K",   /* Text for value 1 */
};