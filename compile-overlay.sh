#!/bin/bash

dtc -I dts -O dtb -o /boot/overlays/tas5805m.dtbo tas5805m-overlay.dts
dtc -I dts -O dtb -W no-unit_address_vs_reg -W no-graph_child_address -o /boot/overlays/tas5805m-dual.dtbo tas5805m-dual-overlay.dts
