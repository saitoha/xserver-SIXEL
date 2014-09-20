#!/bin/sh

prefix=/usr/local
x11prefix=/usr

env SIXEL=yes ACLOCAL="aclocal -I ${x11prefix}/share/aclocal" PKG_CONFIG_PATH="${prefix}/lib/pkgconfig:${x11prefix}/lib/pkgconfig" CFLAGS="-O3 -Ofast" \
./autogen.sh \
    --prefix=${prefix} \
    --with-xkb-path=${x11prefix}/share/X11/xkb \
    --with-xkb-bin-directory=${x11prefix}/bin \
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

nice -n19 make -j8
#make install
