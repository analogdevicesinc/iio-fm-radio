#!/bin/bash

install_location="${1:-/usr/bin}"

[ -e "$install_location/iio_fm_radio" ] \
&& [ -e "$install_location/iio_fm_radio_play" ] \
&& echo 'Installation successful' \
|| { echo 'Installation failed, some files are missing'; exit 1; }
