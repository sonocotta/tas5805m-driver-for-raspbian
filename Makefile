# Comment/uncomment the appropriate line to select the desired DSP configuration
# All options are disabled by default.  Enable only ONE.

CFLAGS_tas5805m.o += -g

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

obj-m := tas5805m.o
# enable to compile with debug messages
#ccflags-y := -DDEBUG

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

install:
	sudo cp $(shell pwd)/tas5805m.ko /lib/modules/$(shell uname -r)/kernel/sound/soc/codecs/snd-soc-tas5805m.ko
	sudo depmod -a
