#!/usr/bin/env python3
"""Run the playback trigger and the start-up work item on the host.

tas58xx_trigger, do_work, send_cfg and send_cfg_ops are compiled from
tas58xx.c unchanged. The stubs model atomic context: the harness calls the
trigger while holding a stand-in for the PCM stream lock, and any call that
may sleep (mutex_lock, usleep_range, regmap_write) aborts the run if it
happens while a spinlock is held. This is the runtime counterpart of
test_trigger_locking.py, which only checks the source text.
"""
import re
import unittest

from hostc import SOURCE, Program, function

PRELUDE = r"""
#define _GNU_SOURCE
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;

static int atomic_depth;	/* > 0 while any spinlock is held */
static int sleeps_in_atomic;
static void may_sleep(const char *what)
{
	if (atomic_depth) {
		sleeps_in_atomic++;
		fprintf(stderr, "sleeping call in atomic context: %s\n", what);
	}
}

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD(name) struct list_head name = { &(name), &(name) }
static void list_add_tail(struct list_head *n, struct list_head *head)
{
	n->prev = head->prev; n->next = head;
	head->prev->next = n; head->prev = n;
}
#define list_for_each_entry(pos, head, member) \
	for (pos = container_of((head)->next, __typeof__(*pos), member); \
	     &pos->member != (head); \
	     pos = container_of(pos->member.next, __typeof__(*pos), member))

typedef struct { int held; } spinlock_t;
#define DEFINE_SPINLOCK(name) spinlock_t name = { 0 }
#define spin_lock_irqsave(lock, flags) \
	do { (flags) = 0; (lock)->held++; atomic_depth++; } while (0)
#define spin_unlock_irqrestore(lock, flags) \
	do { (void)(flags); (lock)->held--; atomic_depth--; } while (0)

struct mutex { int held; };
static void mutex_lock(struct mutex *m)
{
	may_sleep("mutex_lock");
	if (m->held) {
		fprintf(stderr, "mutex_lock on a held mutex: would deadlock\n");
		exit(3);
	}
	m->held = 1;
}
static void mutex_unlock(struct mutex *m) { m->held = 0; }

struct work_struct { int scheduled; };
static int schedule_calls;
static bool schedule_work(struct work_struct *w)
{
	schedule_calls++;
	if (w->scheduled)
		return false;
	w->scheduled = 1;
	return true;
}

static int sleeps;
static void usleep_range(unsigned long min, unsigned long max)
{
	(void)min; (void)max;
	may_sleep("usleep_range");
	sleeps++;
}

struct regmap { int unused; };
#define LOG_MAX 4096
static unsigned int log_reg[LOG_MAX], log_val[LOG_MAX];
static int writes;
static int regmap_write(struct regmap *rm, unsigned int reg, unsigned int val)
{
	(void)rm;
	may_sleep("regmap_write");
	if (writes < LOG_MAX) {
		log_reg[writes] = reg;
		log_val[writes] = val;
	}
	writes++;
	return 0;
}

struct device { int unused; };
struct i2c_client { struct device dev; unsigned short addr; };
struct snd_soc_component { struct device *dev; };
struct snd_soc_dai { struct snd_soc_component *component; };
struct snd_pcm_substream { int unused; };
#define dev_dbg(dev, ...) ((void)(dev))
#define dev_info(dev, ...) ((void)(dev))
#define pr_debug(...) ((void)0)
#define EINVAL 22

/* The values in include/sound/pcm.h */
#define SNDRV_PCM_TRIGGER_STOP 0
#define SNDRV_PCM_TRIGGER_START 1
#define SNDRV_PCM_TRIGGER_PAUSE_PUSH 3
#define SNDRV_PCM_TRIGGER_PAUSE_RELEASE 4
#define SNDRV_PCM_TRIGGER_SUSPEND 5
#define SNDRV_PCM_TRIGGER_RESUME 6

/* Register names: only their identity matters here. */
#define TAS58XX_REG_PAGE_SET 0x00
#define TAS58XX_REG_BOOK_SET 0x7f
#define TAS58XX_REG_PAGE_0 0x00
#define TAS58XX_BOOK_CONTROL_PORT 0x00
#define TAS58XX_REG_DEVICE_CTRL_1 0x02
#define PREBOOT_MARK 0xa1
#define GPIO_MARK 0xa2
static const uint8_t dsp_cfg_preboot[] = { PREBOOT_MARK, 0x01, PREBOOT_MARK, 0x02 };
static const uint8_t dsp_cfg_preboot_gpio_config[] = { GPIO_MARK, 0x03 };

struct tas58xx_priv;
static int refreshes;
static void tas58xx_refresh(struct tas58xx_priv *tas58xx);
"""

PRIV = r"""
struct tas58xx_priv {
	struct i2c_client	*i2c;
	struct tas58xx_cfg_op	*dsp_cfg_ops;
	int			dsp_cfg_num_ops;
	enum tas58xx_variant	variant;
	struct regmap		*regmap;
	unsigned int		modulation_mode;
	unsigned int		switch_freq;
	unsigned int		bridge_mode;
	bool			is_powered;
	bool			dsp_initialized;
	struct work_struct	work;
	struct mutex		lock;
	struct list_head	list;
};
static void tas58xx_refresh(struct tas58xx_priv *tas58xx)
{
	may_sleep("tas58xx_refresh");
	if (!tas58xx->lock.held) {
		fprintf(stderr, "refresh without the device mutex\n");
		exit(4);
	}
	refreshes++;
}
"""

MAIN = r"""
static int failures;
#define CHECK(c) do { if (!(c)) { failures++; \
	fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); } } while (0)

static struct i2c_client clients[2];
static struct regmap maps[2];
static struct tas58xx_priv devs[2];
static struct snd_soc_component component;
static struct snd_soc_dai dai = { &component };
static struct tas58xx_cfg_op export_ops[3];

static void setup(void)
{
	int i;
	for (i = 0; i < 2; i++) {
		memset(&devs[i], 0, sizeof(devs[i]));
		clients[i].addr = 0x4c + i;
		devs[i].i2c = &clients[i];
		devs[i].regmap = &maps[i];
		devs[i].variant = TAS5825M;
		devs[i].dsp_cfg_ops = export_ops;
		devs[i].dsp_cfg_num_ops = 3;
		devs[i].modulation_mode = 1;
		devs[i].bridge_mode = 0;
		devs[i].switch_freq = 2;
		list_add_tail(&devs[i].list, &tas58xx_device_list);
	}
	component.dev = &clients[0].dev;
	export_ops[0] = (struct tas58xx_cfg_op){ .type = TAS58XX_CFG_OP_WRITE, .reg = 0x30, .val = 0x31 };
	export_ops[1] = (struct tas58xx_cfg_op){ .type = TAS58XX_CFG_OP_DELAY, .delay_ms = 5 };
	export_ops[2] = (struct tas58xx_cfg_op){ .type = TAS58XX_CFG_OP_WRITE, .reg = 0x32, .val = 0x33 };
}

/* Call the trigger as ALSA does: under the PCM stream spinlock. */
static int trigger_atomic(int cmd)
{
	spinlock_t stream_lock = { 0 };
	unsigned long flags;
	int ret;
	spin_lock_irqsave(&stream_lock, flags);
	ret = tas58xx_trigger(NULL, cmd, &dai);
	spin_unlock_irqrestore(&stream_lock, flags);
	return ret;
}

/* Let the workqueue run whatever was scheduled. */
static void run_work(void)
{
	int i;
	for (i = 0; i < 2; i++) {
		if (devs[i].work.scheduled) {
			devs[i].work.scheduled = 0;
			do_work(&devs[i].work);
		}
	}
}

static int count_reg(unsigned int reg)
{
	int i, n = 0;
	for (i = 0; i < writes && i < LOG_MAX; i++)
		if (log_reg[i] == reg)
			n++;
	return n;
}

static void scenario_trigger_starts(void)
{
	int cmds[] = { SNDRV_PCM_TRIGGER_START, SNDRV_PCM_TRIGGER_RESUME,
		       SNDRV_PCM_TRIGGER_PAUSE_RELEASE };
	unsigned int c;
	for (c = 0; c < ARRAY_SIZE(cmds); c++) {
		devs[0].work.scheduled = devs[1].work.scheduled = 0;
		schedule_calls = 0;
		CHECK(trigger_atomic(cmds[c]) == 0);
		CHECK(schedule_calls == 2);
		CHECK(devs[0].work.scheduled && devs[1].work.scheduled);
	}
	CHECK(writes == 0 && sleeps == 0 && refreshes == 0);
}

static void scenario_trigger_others(void)
{
	int cmds[] = { SNDRV_PCM_TRIGGER_STOP, SNDRV_PCM_TRIGGER_SUSPEND,
		       SNDRV_PCM_TRIGGER_PAUSE_PUSH };
	unsigned int c;
	for (c = 0; c < ARRAY_SIZE(cmds); c++)
		CHECK(trigger_atomic(cmds[c]) == 0);
	CHECK(trigger_atomic(99) == -EINVAL);
	CHECK(schedule_calls == 0 && writes == 0);
}

static void scenario_trigger_while_device_busy(void)
{
	/* A control write holds one device's mutex when playback starts. */
	devs[1].lock.held = 1;
	CHECK(trigger_atomic(SNDRV_PCM_TRIGGER_START) == 0);
	CHECK(schedule_calls == 2);
	devs[1].lock.held = 0;
}

static void scenario_first_start(void)
{
	CHECK(trigger_atomic(SNDRV_PCM_TRIGGER_START) == 0);
	run_work();
	for (int i = 0; i < 2; i++) {
		CHECK(devs[i].dsp_initialized && devs[i].is_powered);
		CHECK(!devs[i].lock.held);
	}
	/* Per device: preboot (2 writes), GPIO config (1), export (2 writes and
	 * a delay), book/page (3), DCTRL1 (1). */
	CHECK(count_reg(PREBOOT_MARK) == 4);
	CHECK(count_reg(GPIO_MARK) == 2);
	CHECK(count_reg(0x30) == 2 && count_reg(0x32) == 2);
	CHECK(count_reg(TAS58XX_REG_DEVICE_CTRL_1) == 2);
	CHECK(writes == 2 * 9);
	CHECK(refreshes == 2);
	/* Each device in turn: preboot, GPIO, the export in file order, then
	 * book/page and DCTRL1 from the device tree. */
	{
		static const unsigned int order[9] = {
			PREBOOT_MARK, PREBOOT_MARK, GPIO_MARK, 0x30, 0x32,
			TAS58XX_REG_PAGE_SET, TAS58XX_REG_BOOK_SET,
			TAS58XX_REG_PAGE_SET, TAS58XX_REG_DEVICE_CTRL_1 };
		for (int i = 0; i < writes && i < 18; i++)
			CHECK(log_reg[i] == order[i % 9]);
		CHECK(log_val[8] == (1 | (2 << 4)) && log_val[17] == (1 | (2 << 4)));
	}
}

static void scenario_start_while_powered(void)
{
	scenario_first_start();
	int before = writes, refreshed = refreshes;
	CHECK(trigger_atomic(SNDRV_PCM_TRIGGER_START) == 0);
	run_work();
	CHECK(writes == before && refreshes == refreshed);
	CHECK(!devs[0].lock.held && !devs[1].lock.held);
}

static void scenario_restart_after_power_down(void)
{
	scenario_first_start();
	int before = writes;
	devs[0].is_powered = devs[1].is_powered = false;	/* DAPM PRE_PMD */
	CHECK(trigger_atomic(SNDRV_PCM_TRIGGER_START) == 0);
	run_work();
	CHECK(writes == before);	/* no second preboot or export */
	CHECK(refreshes == 4);
	CHECK(devs[0].is_powered && devs[1].is_powered);
}

static void scenario_tas5805m_without_export(void)
{
	devs[0].variant = TAS5805M;
	devs[0].dsp_cfg_num_ops = 0;
	devs[0].dsp_cfg_ops = NULL;
	devs[0].work.scheduled = 1;
	run_work();
	CHECK(count_reg(GPIO_MARK) == 0);
	CHECK(count_reg(0x30) == 0);
	CHECK(count_reg(PREBOOT_MARK) == 2);
	CHECK(devs[0].is_powered && devs[0].dsp_initialized);
}

int main(int argc, char **argv)
{
	setup();
	if (!strcmp(argv[1], "trigger_starts")) scenario_trigger_starts();
	else if (!strcmp(argv[1], "trigger_others")) scenario_trigger_others();
	else if (!strcmp(argv[1], "trigger_while_device_busy")) scenario_trigger_while_device_busy();
	else if (!strcmp(argv[1], "first_start")) scenario_first_start();
	else if (!strcmp(argv[1], "start_while_powered")) scenario_start_while_powered();
	else if (!strcmp(argv[1], "restart_after_power_down")) scenario_restart_after_power_down();
	else if (!strcmp(argv[1], "tas5805m_without_export")) scenario_tas5805m_without_export();
	else { fprintf(stderr, "unknown scenario\n"); return 2; }
	if (sleeps_in_atomic)
		failures++;
	printf("failures %d\n", failures);
	return failures ? 1 : 0;
}
"""


def definition(pattern):
    match = re.search(pattern, SOURCE, re.S | re.M)
    if match is None:
        raise AssertionError(f"Missing definition: {pattern}")
    return match.group(0) + "\n"


def build():
    parts = [PRELUDE,
             definition(r"^enum tas58xx_variant \{.*?\};"),
             definition(r"^enum tas58xx_cfg_op_type \{.*?\};"),
             definition(r"^struct tas58xx_cfg_op \{.*?\};"),
             definition(r"^#define SET_BOOK_AND_PAGE\(.*?while \(0\)"),
             definition(r"^static LIST_HEAD\(tas58xx_device_list\);"),
             definition(r"^static DEFINE_SPINLOCK\(tas58xx_list_lock\);"),
             PRIV]
    for name in ("send_cfg", "send_cfg_ops", "tas58xx_trigger", "do_work"):
        parts.append(function(name))
    parts.append(MAIN)
    return Program("".join(parts), "trigger_runtime")


class TriggerRuntime(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.program = build()

    @classmethod
    def tearDownClass(cls):
        cls.program.close()

    def run_scenario(self, name):
        result = self.program.run(name)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("failures 0", result.stdout)

    def test_start_resume_and_pause_release_schedule_every_device_atomically(self):
        self.run_scenario("trigger_starts")

    def test_stop_suspend_and_pause_push_do_nothing(self):
        self.run_scenario("trigger_others")

    def test_trigger_does_not_wait_for_a_busy_device(self):
        self.run_scenario("trigger_while_device_busy")

    def test_first_start_boots_dsp_then_sends_export(self):
        self.run_scenario("first_start")

    def test_start_while_powered_does_nothing(self):
        self.run_scenario("start_while_powered")

    def test_restart_after_power_down_only_refreshes(self):
        self.run_scenario("restart_after_power_down")

    def test_tas5805m_without_export(self):
        self.run_scenario("tas5805m_without_export")


if __name__ == "__main__":
    unittest.main(verbosity=2)
