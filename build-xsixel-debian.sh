#!/bin/sh

sudo apt install -y libsixel-dev \
                    libchafa-dev \
                    libglib2.0-dev \
                    libpixman-1-dev \
                    xserver-xorg-dev \
                    xkb-data \
                    x11-xkb-utils

# Regenerate autotools files to pick up Makefile.am changes
autoreconf -fi || exit 1

SIXEL=yes CFLAGS="-Wno-array-bounds -O3" LDFLAGS="-Wl,--no-as-needed" ./configure \
    --with-xkb-path=/usr/share/X11/xkb \
    --with-xkb-bin-directory=/usr/bin \
    --with-default-font-path="built-ins" \
    --enable-debug \
    --disable-xorg \
    --disable-dmx \
    --disable-xvfb \
    --disable-xnest \
    --disable-xquartz \
    --disable-xwin \
    --disable-xephyr \
    --disable-xfake \
    --disable-xfbdev \
    --disable-unit-tests \
    --enable-mitshm \
    --disable-dri \
    --disable-dri2 \
    --disable-dri3 \
    --disable-present \
    --disable-glx \
    --disable-xf86vidmode \
    --disable-xquartz \
    --disable-composite \
    --disable-xv \
    --disable-dga \
    --disable-screensaver \
    --disable-xdmcp \
    --disable-xdm-auth-1 \
    --disable-xinerama \
    --disable-docs \
    --disable-xtrans-send-fds \
    --enable-xsixel \
    --enable-kdrive \
    --disable-kdrive-kbd \
    --disable-kdrive-mouse \
    --disable-kdrive-evdev \

make -j$(nproc)
#make install
