# Comment/uncomment the appropriate line to select the desired DSP configuration
# All options are disabled by default.  Enable only ONE.

# CUSTOM config
# CFLAGS_tas5805m.o += -DTAS5805M_DSP_CUSTOM

# or one of the BUILT IN
# CFLAGS_tas5805m.o += -DTAS5805M_DSP_STEREO
# CFLAGS_tas5805m.o += -DTAS5805M_DSP_MONO
# CFLAGS_tas5805m.o += -DTAS5805M_DSP_SUBWOOFER_40
# CFLAGS_tas5805m.o += -DTAS5805M_DSP_SUBWOOFER_60 
# CFLAGS_tas5805m.o += -DTAS5805M_DSP_SUBWOOFER_100
# CFLAGS_tas5805m.o += -DTAS5805M_DSP_BIAMP_60_MONO
# CFLAGS_tas5805m.o += -DTAS5805M_DSP_BIAMP_60_LEFT
# CFLAGS_tas5805m.o += -DTAS5805M_DSP_BIAMP_60_RIGHT

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

obj-m := tas5805m.o

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

install:
	sudo cp $(shell pwd)/tas5805m.ko /lib/modules/$(shell uname -r)/kernel/sound/soc/codecs/snd-soc-tas5805m.ko
	sudo depmod -a
