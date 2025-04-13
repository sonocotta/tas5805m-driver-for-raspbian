# Comment/uncomment the appropriate line to select the desired DSP configuration
# All options are disabled by default.  Enable only ONE.

# CUSTOM DSP config
CFLAGS_tas5805m.o += -DTAS5805M_DSP_CUSTOM

CFLAGS_tas5805m.o += -g

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

obj-m := tas5805m.o

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

install:
	sudo cp $(shell pwd)/tas5805m.ko /lib/modules/$(shell uname -r)/kernel/sound/soc/codecs/snd-soc-tas5805m.ko
	sudo cp $(shell pwd)/startup/*.cfg /lib/firmware/
	sudo depmod -a
