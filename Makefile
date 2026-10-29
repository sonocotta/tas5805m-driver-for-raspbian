
CFLAGS_tas58xx.o += -g

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

obj-m := tas58xx.o

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

install:
	sudo cp $(shell pwd)/tas58xx.ko /lib/modules/$(shell uname -r)/kernel/sound/soc/codecs/snd-soc-tas58xx.ko
	sudo depmod -a
