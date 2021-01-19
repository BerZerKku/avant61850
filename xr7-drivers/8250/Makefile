#
# Makefile for the 8250 serial device drivers.
#

# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -O -g # "-O" is needed to expand inlines
else
  DEBFLAGS = -O2
endif

CONFIG_SERIAL_8250_BCM2835AUX = m

EXTRA_CFLAGS += $(DEBFLAGS)
EXTRA_CFLAGS += -I$(LDDINC) -Wall

ifneq ($(KERNELRELEASE),)
# call from kernel build system

$(info    CONFIG_SERIAL_8250 is $(CONFIG_SERIAL_8250))
$(info    CONFIG_SERIAL_8250_BCM2835AUX is $(CONFIG_SERIAL_8250_BCM2835AUX))

obj-$(CONFIG_SERIAL_8250)		+= 8250.o
8250-y					:= 8250_core.o
obj-$(CONFIG_SERIAL_8250_BCM2835AUX)	+= 8250_bcm2835aux.o

else

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) LDDINC=$(PWD)/.. $@

endif

modules_install:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) $@

clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) $@
	rm -f *.o *.ko *~ Module.symvers