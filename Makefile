# Comment/uncomment the appropriate line to select the desired DSP configuration
# All options are disabled by default.  Enable only ONE.

# CUSTOM DSP config flag
# CFLAGS_tas5805m.o += -DTAS5805M_DSP_CUSTOM

CFLAGS_snd-soc-tasdevice.o += -g

CONFIG_SND_SOC_TASDEVICE := m

snd-soc-tasdevice-objs	:= 	\
	src/tasdevice-regbin.o 	\
	src/tasdevice-core.o 	\
	src/tasdevice-node.o 	\
	src/tasdevice-rw.o  	\
	src/tas2780-irq.o		\
	src/tas2770-irq.o 		\
	src/tas2560-irq.o		\
	src/tas2564-irq.o		\
	src/tas257x-irq.o		\
	src/pcm9211-irq.o
obj-$(CONFIG_SND_SOC_TASDEVICE) += snd-soc-tasdevice.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

install: all
	sudo cp $(shell pwd)/*.ko /lib/modules/$(shell uname -r)/kernel/sound/soc/codecs/
	sudo cp $(shell pwd)json/cfg-2.0/tas5805-2.0-novolume.bin /lib/firmware/tas5805-1amp-reg.bin
	sudo depmod -a

uninstall:
	sudo rm /lib/modules/$(shell uname -r)/kernel/sound/soc/codecs/snd-soc-tasdevice.ko