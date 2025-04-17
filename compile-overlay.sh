#!/bin/bash

dtc -I dts -O dtb -o /boot/overlays/tas5805m.dtbo tas5805m-overlay.dts
dtc -I dts -O dtb -o /boot/overlays/tas5805m-duo.dtbo tas5805m-duo-overlay.dts
