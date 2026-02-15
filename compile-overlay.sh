#!/bin/bash

dtc -I dts -O dtb -o /boot/overlays/tas58xx.dtbo tas58xx-overlay.dts
dtc -I dts -O dtb -W no-unit_address_vs_reg -W no-graph_child_address -o /boot/overlays/tas58xx-dual.dtbo tas58xx-dual-overlay.dts
