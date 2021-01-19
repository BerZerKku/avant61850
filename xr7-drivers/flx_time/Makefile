# Comment/uncomment the following line to disable/enable debugging
#DEBUG = y


# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -O -g # "-O" is needed to expand inlines
else
  DEBFLAGS = -O2
endif

EXTRA_CFLAGS += $(DEBFLAGS)
EXTRA_CFLAGS += -I$(LDDINC) -Wall

ifneq ($(KERNELRELEASE),)
# call from kernel build system

obj-m   += flx_time.o
flx_time-objs := flx_time_main.o flx_time_proc.o 

else

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) LDDINC=$(PWD)/../include $@

endif

modules_install:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) $@

clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) $@
	rm -f *.o *.ko *~ Module.symvers

