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
ifeq ($(CONFIG_FLX_BUS),y)
EXTRA_CFLAGS += -DCONFIG_FLX_BUS
endif

ifneq ($(KERNELRELEASE),)
# call from kernel build system

obj-m   += flx_frtc.o
flx_frtc-objs := flx_time_nco_main.o flx_time_nco.o flx_time_nco_proc.o  

else

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

modules: Module.symvers
	$(MAKE) -C $(KDIR) M=$(PWD) LDDINC=$(PWD)/.. $@

Module.symvers: ../flx_time/Module.symvers
ifeq ($(CONFIG_FLX_BUS),y)
Module.symvers: ../flx_bus/Module.symvers
endif
Module.symvers:
	@cat $^ > $@

endif

modules_install:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) $@

clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) $@
	rm -f *.o *.ko *~ Module.symvers

