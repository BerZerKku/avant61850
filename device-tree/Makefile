OUT_DIR = ./!build

MKDIR_P = mkdir -p

DTB = 	\
	bcm2710-rpi-3-b-xrs7003f-bvp-2v2-imr-1v1.dtb \
#	bcm2710-rpi-3-b-xrs7004e.dtb \
#  	bcm2710-rpi-3-b-xrs7003f.dtb 
	  	

DTBO = 	\
	i2c-rtc.dtbo \
#	bvp61850_2v0.dtbo \
#	bvp61850_2v1.dtbo \
#	bvp61850_2v2.dtbo \
#  	imr61850_1v0.dtbo \
#  	imr61850_1v1.dtbo \
#  	uart_4x.dtbo 
 	

FILES = \
	README

all: directories $(DTB)	$(DTBO)
	for u in $(FILES); do cp -f $$u $(OUT_DIR)/overlays/; done

%.dtb: %.dts *.dtsi
	dtc -I dts -O dtb -@ -o $(OUT_DIR)/$@ $<

%.dtbo: %-overlays.dts *.dtsi
	dtc -I dts -O dtb -@ -o $(OUT_DIR)/overlays/$@ $<

# ${OUT_DIR}:
#         ${MKDIR_P} ${OUT_DIR}

clean:
	find $(OUT_DIR) -name \*.dtb -type f -delete
	find $(OUT_DIR) -name \*.dtbo -type f -delete
	find $(OUT_DIR) -name ${FILES} -type f -delete

directories: 
	mkdir -p ${OUT_DIR}
	mkdir -p ${OUT_DIR}/overlays/

.PHONY: all clean install directories
