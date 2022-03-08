LINUX_KERN=/usr/src/kernels/`uname -r`

MY_CFLAGS += -g -DDEBUG -O0
ccflags-y += $(MY_CFLAGS)
CC += $(MY_CFLAGS)


petmem-y := 	main.o \
		swap.o \
		buddy.o \
		file_io.o \
		on_demand.o 

petmem-objs := $(petmem-y)
obj-m := petmem.o


all:
	$(MAKE) -C $(LINUX_KERN) M=$(PWD) modules
	EXTRA_CFLAGS="$(MY_CFLAGS)"

clean:
	$(MAKE) -Wformat -C $(LINUX_KERN) M=$(PWD) clean

.PHONY: all clean
