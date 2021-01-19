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
ifneq ($(USE_FRS_TIMETRAILER),)
EXTRA_CFLAGS += -DUSE_FRS_TIMETRAILER
endif
ifeq ($(CONFIG_FLX_BUS),y)
EXTRA_CFLAGS += -DCONFIG_FLX_BUS
endif

ifneq ($(KERNELRELEASE),)
# call from kernel build system

obj-m   += flx_frs.o
flx_frs-objs := flx_frs_main.o flx_frs_proc.o flx_frs_mmio.o \
	flx_frs_indirect.o flx_frs_netdev.o flx_frs_netdevif.o \
	flx_frs_ethtool.o flx_frs_adapter.o flx_frs_sfp.o flx_frs_aux_netdev.o

else

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

modules: Module.symvers
	$(MAKE) -C $(KDIR) M=$(PWD) LDDINC=$(PWD)/.. $@

ifeq ($(CONFIG_FLX_BUS),y)
Module.symvers: ../flx_bus/Module.symvers
	@cat $^ > $@
else
Module.symvers:
	@echo -n > $@
endif

endif

modules_install:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) $@

clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) $@
	rm -f *.o *.ko *~ Module.symvers

