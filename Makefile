PREFIX ?= /usr/local

CFLAGS=-Wall -Werror -std=gnu99 -D_GNU_SOURCE -O2
LDFLAGS=-liio -lpulse-simple

all: iio-fm-radio

iio-fm-radio: iio_fm_radio.c
	$(CC) $+ $(CFLAGS) $(LDFLAGS) -o $@

install:
	install -d $(DESTDIR)$(PREFIX)/bin
	install ./iio-fm-radio $(DESTDIR)$(PREFIX)/bin/iio-fm-radio
