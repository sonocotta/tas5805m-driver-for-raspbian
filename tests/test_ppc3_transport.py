#!/usr/bin/env python3

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


DRIVER = Path(__file__).resolve().parents[1] / "tas58xx.c"
VALUES = "01020304800000017ffffffffffffffe00a0b0c0"


def function(source, name):
    match = re.search(r"^static [^\n]*\b" + re.escape(name) + r"\(", source, re.M)
    if not match:
        raise RuntimeError(f"Could not find {name} in {DRIVER}")
    end = source.index("\n}\n", match.start()) + 3
    return source[match.start():end]


HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t s32;
#define EINVAL 22
#define EIO 5
#define TAS58XX_REG_PAGE_SET 0x00
#define TAS58XX_REG_BOOK_SET 0x7f
#define TAS58XX_REG_PAGE_0 0x00
#define TAS58XX_BOOK_CONTROL_PORT 0x00
#define TAS5825M_PPC3_BOOK0 0x8c
#define TAS5825M_PPC3_BOOK1 0xaa
#define TAS5825M_PPC3_COEFFS_PER_BANK 2000
#define TAS5825M_PPC3_COEFFS_PER_PAGE 30
#define TAS5825M_PPC3_FIRST_PAGE 1
#define TAS5825M_PPC3_FIRST_REG 0x08
struct regmap { int unused; };
struct tas58xx_priv { struct regmap *regmap; };
static int calls, fail_at, page, book;
static char logbuf[2048];
static size_t used;
static int regmap_write(struct regmap *m, unsigned int reg, unsigned int value) {
    (void)m;
    if (calls++ == fail_at) return -EIO;
    if (reg == TAS58XX_REG_PAGE_SET) page = value;
    else if (reg == TAS58XX_REG_BOOK_SET && page == 0) book = value;
    used += (size_t)snprintf(logbuf + used, sizeof(logbuf) - used, "w %02x %02x;", reg, value);
    return 0;
}
static int regmap_bulk_write(struct regmap *m, unsigned int reg, const void *data, size_t n) {
    const u8 *p = data;
    (void)m;
    if (calls++ == fail_at) return -EIO;
    used += (size_t)snprintf(logbuf + used, sizeof(logbuf) - used, "B %02x/%02x/%02x %zu:", book, page, reg, n);
    for (size_t i = 0; i < n; ++i)
        used += (size_t)snprintf(logbuf + used, sizeof(logbuf) - used, "%02x", p[i]);
    used += (size_t)snprintf(logbuf + used, sizeof(logbuf) - used, ";");
    return 0;
}
static int regmap_bulk_read(struct regmap *m, unsigned int reg, void *data, size_t n) {
    u8 *p = data;
    (void)m;
    if (calls++ == fail_at) return -EIO;
    used += (size_t)snprintf(logbuf + used, sizeof(logbuf) - used, "R %02x/%02x/%02x %zu;", book, page, reg, n);
    for (size_t i = 0; i < n; ++i) p[i] = (u8)(i + 1);
    return 0;
}
@FUNCTIONS@
int main(int argc, char **argv) {
    if (argc != 5) return 2;
    struct regmap rm = {0};
    struct tas58xx_priv dev = {&rm};
    const s32 values[5] = {0x01020304, (s32)0x80000001u, 0x7fffffff, -2, 0x00a0b0c0};
    unsigned int start = (unsigned int)strtoul(argv[2], NULL, 10);
    fail_at = atoi(argv[3]);
    s32 value = 0;
    int result;
    if (!strcmp(argv[1], "biquad")) result = tas5825m_ppc3_write_biquad(&dev, start, values);
    else if (!strcmp(argv[1], "read")) result = tas5825m_ppc3_read_coeff(&dev, start, &value);
    else if (!strcmp(argv[1], "write")) result = tas5825m_ppc3_write_coeff(&dev, start, values[0]);
    else if (!strcmp(argv[1], "restore")) result = tas58xx_select_page0(&dev, atoi(argv[4]));
    else return 2;
    printf("{\"result\":%d,\"calls\":%d,\"value\":%d,\"log\":\"%s\"}\n", result, calls, (int)value, logbuf);
    return 0;
}
'''


class Ppc3Transport(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="tas58xx-transport-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.dir = Path(cls.temp.name)
        cls.source = DRIVER.read_text(encoding="utf-8")
        funcs = "\n".join(function(cls.source, name) for name in (
            "tas58xx_select_page", "tas58xx_select_page0", "tas5825m_ppc3_coeff_addr",
            "tas5825m_ppc3_read_coeff", "tas5825m_ppc3_write_coeff", "tas5825m_ppc3_write_biquad"))
        cfile = cls.dir / "transport.c"
        cfile.write_text(HARNESS.replace("@FUNCTIONS@", funcs), encoding="utf-8")
        cls.exe = cls.dir / ("transport.exe" if os.name == "nt" else "transport")
        cc = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang") or shutil.which("zig")
        if not cc:
            raise RuntimeError("A C compiler is required: install gcc, clang or zig, or set CC")
        compiler = [cc, "cc"] if Path(cc).stem.lower() == "zig" else [cc]
        result = subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", str(cfile), "-o", str(cls.exe)],
                                capture_output=True, text=True, check=False)
        if result.returncode:
            raise RuntimeError(f"Could not compile test harness:\n{result.stdout}{result.stderr}")

    def invoke(self, mode, start=2000, fail_at=-1, prior=0):
        result = subprocess.run([str(self.exe), mode, str(start), str(fail_at), str(prior)],
                                capture_output=True, text=True, check=True)
        return json.loads(result.stdout)

    def write_biquad(self, start, fail_at=-1):
        return self.invoke("biquad", start, fail_at)

    def test_all_declared_pf3_biquads_use_one_burst_when_contiguous(self):
        starts = [int(x) for x in re.findall(r"tas58xx_profile_coeff_ctrl\s+\w+\s*=\s*\{\s*(\d+),\s*5\s*\}", self.source)]
        starts = [x for x in starts if x >= 2000 and (x - 2000) % 30 <= 25]
        self.assertGreaterEqual(len(starts), 30)
        for start in starts:
            with self.subTest(start=start):
                local = start - 2000
                page, reg = 1 + local // 30, 8 + 4 * (local % 30)
                result = self.write_biquad(start)
                self.assertEqual(result["result"], 0)
                self.assertEqual(result["log"], f"w 00 00;w 7f aa;w 00 {page:02x};B aa/{page:02x}/{reg:02x} 20:{VALUES};")

    def test_page_crossing_is_split_without_dropping_bytes(self):
        result = self.write_biquad(2027)
        self.assertEqual(result["result"], 0)
        self.assertEqual(result["log"], "w 00 00;w 7f aa;w 00 01;B aa/01/74 12:01020304800000017fffffff;"
                                       "w 00 00;w 7f aa;w 00 02;B aa/02/08 8:fffffffe00a0b0c0;")

    def test_each_single_page_failure_is_returned_and_stops_writing(self):
        for fail_at in range(4):
            with self.subTest(fail_at=fail_at):
                result = self.write_biquad(2010, fail_at)
                self.assertEqual(result["result"], -5)
                self.assertEqual(result["calls"], fail_at + 1)
                self.assertNotIn("B ", result["log"])

    def test_each_page_crossing_failure_stops_at_failed_operation(self):
        for fail_at in range(8):
            with self.subTest(fail_at=fail_at):
                result = self.write_biquad(2027, fail_at)
                self.assertEqual(result["result"], -5)
                self.assertEqual(result["calls"], fail_at + 1)
                self.assertEqual(result["log"].count("B "), 0 if fail_at < 4 else 1)

    def test_non_biquad_bank_and_out_of_range_are_rejected(self):
        for start in (1990, 3996, 4000):
            with self.subTest(start=start):
                result = self.write_biquad(start)
                self.assertEqual(result["result"], -22)
                self.assertEqual(result["calls"], 0)

    def test_control_page_restored_and_original_error_preserved(self):
        expected = "w 00 00;w 7f 00;w 00 00;"
        self.assertEqual(self.invoke("restore")["log"], expected)
        self.assertEqual(self.invoke("restore", prior=-17)["result"], -17)
        for fail_at in range(3):
            with self.subTest(fail_at=fail_at):
                failed = self.invoke("restore", fail_at=fail_at)
                self.assertEqual(failed["result"], -5)
                self.assertEqual(failed["calls"], fail_at + 1)
                self.assertEqual(self.invoke("restore", fail_at=fail_at, prior=-17)["result"], -17)

    def test_coeff_reads_and_writes_check_page_selection(self):
        read = self.invoke("read", start=2000)
        self.assertEqual((read["result"], read["value"]), (0, 0x01020304))
        self.assertEqual(read["log"], "w 00 00;w 7f aa;w 00 01;R aa/01/08 4;")
        write = self.invoke("write", start=2000)
        self.assertEqual(write["result"], 0)
        self.assertEqual(write["log"], "w 00 00;w 7f aa;w 00 01;B aa/01/08 4:01020304;")
        for mode in ("read", "write"):
            for fail_at in range(4):
                with self.subTest(mode=mode, fail_at=fail_at):
                    result = self.invoke(mode, start=2000, fail_at=fail_at)
                    self.assertEqual(result["result"], -5)
                    self.assertEqual(result["calls"], fail_at + 1)
                    self.assertNotIn("R ", result["log"])
                    self.assertNotIn("B ", result["log"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
