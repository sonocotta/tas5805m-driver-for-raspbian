#!/bin/bash

dtc -I dts -O dtb -o /boot/overlays/tas5805m-1x.dtbo tas5805m-1x-overlay.dts
dtc -I dts -O dtb -o /boot/overlays/tas5805m-2x.dtbo tas5805m-2x-overlay.dts
