#MPY_CROSS = /home/sean/MicroPython_ESP32_psRAM_LoBo/MicroPython_BUILD/components/micropython/mpy-cross/mpy-cross

MPY_CROSS = /home/sean/micropython/esp32/micropython/mpy-cross/mpy-cross

PORT=/dev/ttyUSB0

%.mpy : %.py
	$(MPY_CROSS) $<

SRCS = font.py gpio_esp32.py lcd.py menu.py page.py upy_client.py wifi_esp32.py lcd_esp32.py config_esp32.py gettext_esp32.py
ALL = $(SRCS:.py=.mpy)

all: $(ALL)

upload: $(ALL)
	for f in $(ALL); do echo $$f;  ampy -p $(PORT) put $$f;	done
	ampy -p $(PORT) put boot.py

upload-locale:
	for f in `find locale -type d`; do echo $$f; ampy -p $(PORT) mkdir $$f; done
	for f in `find locale | grep po$$`; do echo put $$f; ampy -p $(PORT) put $$f $$f; done

clean:
	rm -rf *mpy
