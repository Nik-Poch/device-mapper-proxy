# If KERNELRELEASE define it means that
# call was made from kernel build system
# and we can use its language
ifneq ($(KERNELRELEASE),)
	lifo-objs := dmp.o
	obj-m := lifo.o
else
	# Otherwise, the call is made directly from the command line;
	# Call the kernel build system
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)
modules:
	echo $(MAKE) -C $(KERNELDIR) M=$(PWD) LDDINC=$(PWD)/../include modules
	$(MAKE) -C $(KERNELDIR) M=$(PWD) LDDINC=$(PWD)/../include modules
endif

obj-m += dmp.o
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
remove:
	dmsetup remove dmp1
	dmsetup remove zero1
	rmmod dmp.ko
