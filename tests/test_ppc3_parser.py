#!/usr/bin/env python3
"""Run the driver's PPC3 text-config parser on the host against real exports.

The parser functions are compiled from tas58xx.c unchanged. Every result is
compared with an independent expansion written here in Python.
"""
from pathlib import Path
import re
import tempfile
import unittest

from hostc import ROOT, SOURCE, Program, function

EXAMPLES = sorted((ROOT / "config-examples").glob("*.cfg"))

PRELUDE = r"""
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
struct device { int unused; };
#define GFP_KERNEL 0
static int warnings;
#define dev_warn(dev, ...) ((void)(dev), warnings++)
#define max_t(t, a, b) ((t)(a) > (t)(b) ? (t)(a) : (t)(b))
static char *kmemdup_nul(const char *s, size_t n, int gfp)
{
	char *p = malloc(n + 1);
	(void)gfp;
	if (!p)
		return NULL;
	memcpy(p, s, n);
	p[n] = 0;
	return p;
}
static void kfree(void *p) { free(p); }
static void *devm_kcalloc(struct device *d, size_t n, size_t size, int gfp)
{
	(void)d; (void)gfp;
	return calloc(n, size);
}
static char *strim(char *s)
{
	char *e;
	while (isspace((unsigned char)*s))
		s++;
	e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1]))
		*--e = 0;
	return s;
}
/* Like the kernel's: one optional '+', no '-', no junk, no overflow. */
static int kstrtouint(const char *s, unsigned int base, unsigned int *res)
{
	char *end;
	unsigned long v;
	if (*s == '+')
		s++;
	if (!*s || *s == '-' || *s == '+' || isspace((unsigned char)*s))
		return -EINVAL;
	errno = 0;
	v = strtoul(s, &end, base);
	if (*end || errno || v > UINT_MAX)
		return -ERANGE;
	*res = (unsigned int)v;
	return 0;
}
"""

MAIN = r"""
int main(int argc, char **argv)
{
	struct device dev;
	struct tas58xx_cfg_op *ops = NULL;
	int n = -1, i, ret;
	FILE *f = fopen(argv[1], "rb");
	static char text[1 << 20];
	size_t len = fread(text, 1, sizeof(text), f);
	fclose(f);
	ret = tas58xx_parse_text_config(&dev, (u8)strtoul(argv[2], NULL, 16),
					text, len, &ops, &n);
	printf("ret %d\ncount %d\nwarnings %d\n", ret, n, warnings);
	for (i = 0; i < n; i++) {
		if (ops[i].type == TAS58XX_CFG_OP_DELAY)
			printf("d %u\n", ops[i].delay_ms);
		else
			printf("w %02x %02x\n", ops[i].reg, ops[i].val);
	}
	free(ops);
	return 0;
}
"""


def definition(pattern):
    match = re.search(pattern, SOURCE, re.S | re.M)
    if match is None:
        raise AssertionError(f"Missing definition: {pattern}")
    return match.group(0) + "\n"


def build():
    parts = [PRELUDE,
             definition(r"^enum tas58xx_cfg_op_type \{.*?\};"),
             definition(r"^struct tas58xx_cfg_op \{.*?\};")]
    for name in ("tas58xx_cfg_next_token", "tas58xx_cfg_addr_matches",
                 "tas58xx_parse_config_line", "tas58xx_parse_text_config"):
        parts.append(function(name))
    parts.append(MAIN)
    return Program("".join(parts), "ppc3_parser")


def reference(text, i2c_addr):
    """An independent expansion of a PPC3 export: (ops, rejected lines)."""
    ops, rejected = [], 0
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        tokens = line.split()
        kind = tokens[0].lower()
        if kind == "w" and len(tokens[0]) == 1:
            try:
                addr = int(tokens[1], 16)
                reg = int(tokens[2], 16)
                data = [int(t, 16) for t in tokens[3:]]
                if (reg > 0xFF or not data or any(v > 0xFF for v in data)
                        or reg + len(data) - 1 > 0xFF
                        or any(t.startswith("-") for t in tokens[1:])):
                    raise ValueError
            except (IndexError, ValueError):
                rejected += 1
                continue
            if addr == i2c_addr or (addr <= 0xFF and addr % 2 == 0 and addr // 2 == i2c_addr):
                ops.extend(f"w {reg + k:02x} {v:02x}" for k, v in enumerate(data))
        elif kind == "d" and len(tokens[0]) == 1:
            try:
                ms = int(tokens[1], 10)
                if ms > 0xFFFF or tokens[1].startswith("-"):
                    raise ValueError
            except (IndexError, ValueError):
                rejected += 1
                continue
            ops.append(f"d {ms}")
        else:
            rejected += 1
    return ops, rejected


def as_bursts(text):
    """Merge runs of single-byte writes to consecutive registers into burst lines
    at the 8-bit address, as PPC3's burst export does. Delays end a run."""
    out, run = [], None  # run = [first_reg, [values]]

    def flush():
        nonlocal run
        if run:
            out.append("w 5a %02x %s" % (run[0], " ".join("%02x" % v for v in run[1])))
            run = None

    for raw in text.splitlines():
        line = raw.split("#", 1)[0].split()
        if len(line) == 4 and line[0] == "w" and line[1] == "2d":
            reg, val = int(line[2], 16), int(line[3], 16)
            if run and run[0] + len(run[1]) == reg:
                run[1].append(val)
                continue
            flush()
            run = [reg, [val]]
        elif line:
            flush()
            out.append(" ".join(line))
    flush()
    return "\n".join(out) + "\n"


class Parser(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.program = build()
        cls.tmp = tempfile.TemporaryDirectory(prefix="tas58xx-cfg-")

    @classmethod
    def tearDownClass(cls):
        cls.program.close()
        cls.tmp.cleanup()

    def parse(self, text, i2c_addr):
        path = Path(self.tmp.name) / "config.cfg"
        path.write_bytes(text.encode("ascii"))
        result = self.program.run(path, f"{i2c_addr:x}")
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[0], "ret 0")
        count = int(lines[1].split()[1])
        warnings = int(lines[2].split()[1])
        ops = lines[3:]
        self.assertEqual(len(ops), count)
        return ops, warnings

    def check(self, text, i2c_addr):
        ops, warnings = self.parse(text, i2c_addr)
        expected, rejected = reference(text, i2c_addr)
        self.assertEqual(ops, expected)
        self.assertEqual(warnings, rejected)
        return ops

    def test_examples_keep_every_write(self):
        self.assertTrue(EXAMPLES, "no config-examples/*.cfg found")
        for path in EXAMPLES:
            with self.subTest(example=path.name):
                text = path.read_text(encoding="ascii")
                ops = self.check(text, 0x2D)
                writes = sum(1 for l in text.splitlines() if l.startswith("w "))
                self.assertEqual(sum(1 for op in ops if op.startswith("w ")), writes)

    def test_burst_exports_match_single_byte_exports(self):
        # PPC3 can also export runs of consecutive registers on one line, at the
        # 8-bit address ("w 5a 08 00 00 00 01 ..."). Rewrite each example that
        # way and require the same operations.
        for path in EXAMPLES:
            with self.subTest(example=path.name):
                text = path.read_text(encoding="ascii")
                self.assertEqual(self.parse(as_bursts(text), 0x2D), self.parse(text, 0x2D))
                self.assertLess(len(as_bursts(text).splitlines()), len(text.splitlines()))

    def test_eight_bit_addresses_match_seven_bit(self):
        for path in EXAMPLES:
            with self.subTest(example=path.name):
                text = path.read_text(encoding="ascii")
                eight_bit = re.sub(r"^w 2d ", "w 5a ", text, flags=re.M)
                self.assertNotEqual(text, eight_bit)
                self.assertEqual(self.parse(eight_bit, 0x2D), self.parse(text, 0x2D))

    def test_other_devices_and_read_addresses_are_dropped(self):
        text = ("w 2d 00 00\n"     # this device, 7-bit
                "w 5a 01 11 22\n"  # this device, 8-bit write address
                "w 5b 02 33\n"     # 8-bit read address: never ours
                "w 2c 03 44\n"     # another device, 7-bit
                "w 58 04 55\n"     # another device, 8-bit
                "d 5\n")
        self.assertEqual(self.check(text, 0x2D),
                         ["w 00 00", "w 01 11", "w 02 22", "d 5"])
        self.assertEqual(self.check(text, 0x2C), ["w 03 44", "w 04 55", "d 5"])

    def test_comments_blank_lines_tabs_crlf_and_case(self):
        text = ("# header\r\n\r\n"
                "W\t2d\t10\t01 02   # trailing comment\r\n"
                "   w 2d 20 03\r\n"
                "D 10\r\n"
                "#w 2d 30 04\r\n")
        self.assertEqual(self.check(text, 0x2D),
                         ["w 10 01", "w 11 02", "w 20 03", "d 10"])

    def test_malformed_lines_are_dropped_whole(self):
        text = ("w 2d 00 01\n"
                "w 2d\n"              # no register
                "w 2d 10\n"           # no data
                "w 2d 10 01 zz\n"     # bad data after a good byte: nothing kept
                "w 2d 10 100\n"       # value above 0xff
                "w 2d 1ff 01\n"       # register above 0xff
                "w 2d fe 01 02 03\n"  # runs past register 0xff
                "w 2d -1 01\n"        # sign
                "w 2d 20 ff\n"
                "d\n"                 # no delay
                "d 70000\n"           # delay above 16 bits
                "d 0x10\n"            # delay is decimal
                "x 2d 00 00\n"        # unknown command
                "wr 2d 00 00\n"       # command must be one letter
                "d 3\n")
        self.assertEqual(self.check(text, 0x2D), ["w 00 01", "w 20 ff", "d 3"])

    def test_register_ff_is_the_last_register_a_burst_may_reach(self):
        self.assertEqual(self.check("w 2d fe 01 02\n", 0x2D), ["w fe 01", "w ff 02"])

    def test_empty_and_foreign_only_files(self):
        self.assertEqual(self.check("", 0x2D), [])
        self.assertEqual(self.check("# nothing\n\n", 0x2D), [])
        self.assertEqual(self.check("w 2c 00 00\n", 0x2D), [])

    def test_no_trailing_newline(self):
        self.assertEqual(self.check("w 2d 00 01 02", 0x2D), ["w 00 01", "w 01 02"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
