#!/usr/bin/env python3
"""Compile the actual PPC3 cache/replay helpers against fake kernel types."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

DRIVER = Path(__file__).resolve().parents[1] / "tas58xx.c"

def function(source, name):
    match = re.search(r"^static [^\n]*\b" + re.escape(name) + r"\(", source, re.M)
    if not match:
        raise RuntimeError(f"Could not find {name}")
    return source[match.start():source.index("\n}\n", match.start()) + 3]

C_HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int32_t s32;
typedef uint8_t u8;
#define EIO 5
#define EINVAL 22
#define TAS5825M_PPC3_TOTAL_COEFFS 4000
#define TAS5825M_PPC3_MAX_CTRL_VALUES 10
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define WORD_BITS (sizeof(unsigned long) * 8)
#define DECLARE_BITMAP(name, n) unsigned long name[((n) + WORD_BITS - 1) / WORD_BITS]
static bool test_bit(unsigned int bit, const unsigned long *bitmap) {
    return (bitmap[bit / WORD_BITS] >> (bit % WORD_BITS)) & 1UL;
}
static void set_bit(unsigned int bit, unsigned long *bitmap) {
    bitmap[bit / WORD_BITS] |= 1UL << (bit % WORD_BITS);
}
struct tas58xx_ppc3_restore {
    s32 values[TAS5825M_PPC3_TOTAL_COEFFS];
    DECLARE_BITMAP(valid, TAS5825M_PPC3_TOTAL_COEFFS);
};
struct tas58xx_priv {
    struct tas58xx_ppc3_restore *ppc3_restore;
};
struct tas58xx_profile_coeff_ctrl {
    unsigned int start_coeff, count;
};
struct tas58xx_meter_ctrl { u8 reg; };
struct snd_kcontrol_new {
    int (*put)(void);
    unsigned long private_value;
};
static int tas58xx_profile_coeff_put(void) { return 0; }
#define TAS58XX_PROFILE_COEFF_CTRL(name, ctrl) \
    { .put = tas58xx_profile_coeff_put, .private_value = (unsigned long)&ctrl }
#define TAS58XX_PROFILE_METER_CTRL(name, ctrl) \
    { .put = NULL, .private_value = (unsigned long)&ctrl }
static int total_writes, single_writes, biquad_writes, page_restores, fail_at = -1;
static int tas5825m_ppc3_write_coeff(struct tas58xx_priv *p,
                                     unsigned int coeff, s32 value) {
    (void)p; (void)coeff; (void)value;
    if (total_writes++ == fail_at) return -EIO;
    single_writes++;
    return 0;
}
static int tas5825m_ppc3_write_biquad(struct tas58xx_priv *p,
                                      unsigned int coeff, const s32 values[5]) {
    (void)p; (void)coeff; (void)values;
    if (total_writes++ == fail_at) return -EIO;
    biquad_writes++;
    return 0;
}
static int tas58xx_select_page0(struct tas58xx_priv *p, int ret) {
    (void)p;
    page_restores++;
    return ret;
}
@CTRL_DECLARATIONS@
@CACHE_FUNCTIONS@
@CONTROL_ARRAY@
@REPLAY_FUNCTION@
static void clear_calls(void) {
    total_writes = single_writes = biquad_writes = page_restores = 0;
    fail_at = -1;
}
int main(int argc, char **argv) {
    struct tas58xx_ppc3_restore cache = {0};
    struct tas58xx_ppc3_restore other_cache = {0};
    struct tas58xx_priv dev = { &cache }, other_dev = { &other_cache };
    const struct tas58xx_profile_coeff_ctrl eq = {2010, 5};
    const struct tas58xx_profile_coeff_ctrl drc = {453, 10};
    const struct tas58xx_profile_coeff_ctrl flag = {307, 1};
    s32 eq_values[] = {1, 2, 3, 4, 5};
    s32 drc_values[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    s32 flag_value[] = {1};

    if (argc != 2) return 2;
    if (!strcmp(argv[1], "untouched")) {
        assert(tas58xx_ppc3_replay_cached(&dev) == 0);
        assert(total_writes == 0 && page_restores == 1);
        return 0;
    }
    assert(!tas58xx_ppc3_cache_has_all(&dev, &eq));
    assert(tas58xx_ppc3_cache_save(&dev, &eq, eq_values));
    assert(tas58xx_ppc3_cache_has_all(&dev, &eq));
    assert(!tas58xx_ppc3_cache_has_all(&other_dev, &eq));
    assert(!tas58xx_ppc3_cache_save(&dev, &eq, eq_values));
    assert(!tas58xx_ppc3_cache_has_all(&dev, &drc));
    if (!strcmp(argv[1], "single-biquad")) {
        assert(tas58xx_ppc3_replay_cached(&dev) == 0);
        assert(biquad_writes == 1 && single_writes == 0);
        assert(page_restores == 1);
        return 0;
    }
    assert(tas58xx_ppc3_cache_save(&dev, &drc, drc_values));
    assert(tas58xx_ppc3_cache_save(&dev, &flag, flag_value));
    if (!strcmp(argv[1], "mixed-controls")) {
        assert(tas58xx_ppc3_replay_cached(&dev) == 0);
        assert(biquad_writes == 1 && single_writes == 11);
        assert(page_restores == 1);
        return 0;
    }
    if (!strcmp(argv[1], "io-error")) {
        clear_calls();
        fail_at = 0;
        assert(tas58xx_ppc3_replay_cached(&dev) == -EIO);
        assert(total_writes == 1 && page_restores == 1);
        return 0;
    }
    return 3;
}
'''

class Ppc3Restore(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = DRIVER.read_text(encoding="utf-8")
        cls.tmp = tempfile.TemporaryDirectory(prefix="tas58xx-restore-")
        cls.addClassCleanup(cls.tmp.cleanup)
        root = Path(cls.tmp.name)
        start = cls.source.index("static const struct tas58xx_profile_coeff_ctrl ppc3_agl_enable")
        array = cls.source.index("static const struct snd_kcontrol_new tas58xx_snd_controls_drc3agl[]")
        replay = cls.source.index("static int tas58xx_ppc3_replay_cached(")
        declarations = cls.source[start:array]
        controls = cls.source[array:replay]
        helpers = "\n".join(function(cls.source, fn) for fn in (
            "tas58xx_ppc3_cache_has_all", "tas58xx_ppc3_cache_save"))
        ctext = (C_HARNESS.replace("@CTRL_DECLARATIONS@", declarations)
                 .replace("@CACHE_FUNCTIONS@", helpers)
                 .replace("@CONTROL_ARRAY@", controls)
                 .replace("@REPLAY_FUNCTION@", function(cls.source, "tas58xx_ppc3_replay_cached")))
        src = root / "restore.c"
        src.write_text(ctext, encoding="utf-8")
        cls.exe = root / ("restore.exe" if os.name == "nt" else "restore")
        cc = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
        if not cc:
            raise RuntimeError("A C compiler is required")
        command = [cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                   str(src), "-o", str(cls.exe)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f"Harness compilation failed:\n{result.stdout}{result.stderr}")

    def invoke(self, name):
        result = subprocess.run([str(self.exe), name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, f"{name}: {result.stderr}")

    def test_untouched_coefficients_not_replayed(self):
        self.invoke("untouched")

    def test_complete_biquad_cached_and_replayed(self):
        self.invoke("single-biquad")

    def test_mixed_multivalue_and_scalar_controls(self):
        self.invoke("mixed-controls")

    def test_replay_propagates_i2c_error_and_restores_page(self):
        self.invoke("io-error")

    def test_boot_path_is_wired_before_play(self):
        source = self.source
        self.assertIn("changed = tas58xx_ppc3_cache_save(tas58xx, ctrl, requested);", source)
        self.assertLess(source.index("tas58xx_ppc3_replay_cached(tas58xx);"),
                        source.index("tas58xx->dsp_initialized = true;", source.index("static void do_work(")))
        self.assertLess(source.index("tas58xx_ppc3_replay_cached(tas58xx);"),
                        source.index("tas58xx_refresh(tas58xx);", source.index("static void do_work(")))

if __name__ == "__main__":
    unittest.main(verbosity=2)
