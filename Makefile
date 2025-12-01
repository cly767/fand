.DEFAULT_GOAL = build
.PHONY: build clean install run uninstall

CFLAGS = -O2 -lwiringPi -lpthread

PREFIX = /usr/local
SD_PREFIX = /etc/systemd

fand: main.c config.h
	gcc main.c -o fand $(CFLAGS)

build: fand

run: fand
	sudo -u root chrt --fifo 1 ./fand

install: build
	cp fand $(PREFIX)/bin/fand
	cp fand.service $(SD_PREFIX)/system/fand.service
	chmod 755 $(PREFIX)/bin/fand
	chmod 644 $(SD_PREFIX)/system/fand.service
	systemctl daemon-reload

uninstall:
	rm -f $(PREFIX)/bin/fand $(SD_PREFIX)/system/fand.service

clean:
	rm -f fand
