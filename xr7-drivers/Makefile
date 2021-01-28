# Makefile for building drivers.

SUBDIRS = \
	flx_bus flx_bus_i2c flx_bus_mdio \
	flx_time flx_frtc flx_frs flx_xrs flx_i2c_mdio \
	flx_gpio flx_i2c_gpio flx_fpts dp83848 m88e1512 \
	ttyUart0 ttyUart1
# 	8250

$(SUBDIRS): export CONFIG_FLX_BUS=y

bindir = $(INSTALL_MOD_PATH)

all modules modules_install clean: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

# Cannot build everything in parallel.
flx_frtc: flx_time
$(filter-out flx_bus, $(SUBDIRS)): $(filter flx_bus, $(SUBDIRS))

all modules: $(SUBDIRS)
	cp $(patsubst %,%/*.ko,$^) $(bindir)/

.PHONY: all modules modules_install clean $(SUBDIRS)
