#!/usr/bin/env python3
"""Structural regression guard for the TAS58xx atomic playback trigger.

This checks lock ordering and scheduling; it is not a runtime concurrency test.
"""
from pathlib import Path
import re
import unittest

SOURCE = (Path(__file__).resolve().parents[1] / "tas58xx.c").read_text(encoding="utf-8")


def function(name):
    match = re.search(r"^static [^\n]*\b" + re.escape(name) + r"\(", SOURCE, re.M)
    if match is None:
        raise AssertionError(f"Missing function: {name}")
    return SOURCE[match.start():SOURCE.index("\n}\n", match.start()) + 3]


class TriggerLocking(unittest.TestCase):
    def test_trigger_schedules_work_without_sleeping_mutex(self):
        trigger = function("tas58xx_trigger")
        self.assertNotIn("mutex_lock(", trigger)
        self.assertNotIn("mutex_lock_interruptible(", trigger)
        self.assertRegex(trigger, re.compile(
            r"spin_lock_irqsave\(&tas58xx_list_lock, flags\);"
            r".*list_for_each_entry\(priv, &tas58xx_device_list, list\)"
            r".*schedule_work\(&priv->work\);"
            r".*spin_unlock_irqrestore\(&tas58xx_list_lock, flags\);", re.S))

    def test_powered_state_checked_under_device_mutex_in_worker(self):
        worker = function("do_work")
        self.assertRegex(worker, re.compile(
            r"mutex_lock\(&tas58xx->lock\);\s*"
            r"if \(tas58xx->is_powered\) \{\s*"
            r"mutex_unlock\(&tas58xx->lock\);\s*return;", re.S))

    def test_list_updates_use_same_spinlock(self):
        for name, operation in (("tas58xx_i2c_probe", "list_add_tail"),
                                ("tas58xx_i2c_remove", "list_del")):
            with self.subTest(name=name):
                body = function(name)
                self.assertRegex(body, re.compile(
                    r"spin_lock_irqsave\(&tas58xx_list_lock, flags\);"
                    r".*" + operation + r"\("
                    r".*spin_unlock_irqrestore\(&tas58xx_list_lock, flags\);", re.S))

if __name__ == "__main__":
    unittest.main(verbosity=2)
