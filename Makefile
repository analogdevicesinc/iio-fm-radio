DESTDIR=/usr/local
CFLAGS=-Wall -Werror -std=gnu99 -D_GNU_SOURCE -O2

all: iio_fm_radio

iio_fm_radio: iio_fm_radio.c iio_utils.c
	$(CC) $+ $(CFLAGS) $(LDFLAGS) -o $@

install:
	install -d $(DESTDIR)/bin
	install ./iio_fm_radio $(DESTDIR)/bin/iio_fm_radio
	install ./iio_fm_radio_play $(DESTDIR)/bin/iio_fm_radio_play
	
clean: 
	rm -f iio_fm_radio
