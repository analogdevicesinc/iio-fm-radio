DESTDIR=/usr/local
CFLAGS=-Wall -Werror -std=gnu99 -D_GNU_SOURCE -O2
LDFLAGS=-liio -lpulse-simple

all: iio_fm_radio

iio_fm_radio: iio_fm_radio.c
	$(CC) $+ $(CFLAGS) $(LDFLAGS) -o $@

install:
	install -d $(DESTDIR)/bin
	install ./iio_fm_radio $(DESTDIR)/bin/iio_fm_radio
