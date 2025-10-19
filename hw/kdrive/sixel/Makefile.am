AM_CPPFLAGS = \
	@KDRIVE_PURE_INCS@ \
	@KDRIVE_CFLAGS@ \
	@XSIXEL_INCS@ \
	@CHAFA_INCS@

AM_CFLAGS = -DHAVE_DIX_CONFIG_H

bin_PROGRAMS = Xsixel

# Provide a local main() wrapper like Xephyr to avoid --as-needed issues
Xsixel_SOURCES = sixel.c sixelinit.c

# Use the canonical KDRIVE_LIBS ordering from configure to satisfy
# static link dependencies on Debian (with --as-needed).
Xsixel_LDADD = \
	@KDRIVE_LIBS@ \
	@XSIXEL_LIBS@ \
	@CHAFA_LIBS@ \
	@KDRIVE_MAIN_LIB@ \
	$(XSERVER_SYS_LIBS)

Xsixel_DEPENDENCIES = @KDRIVE_LOCAL_LIBS@

Xsixel_LDFLAGS = $(LD_EXPORT_SYMBOLS_FLAG)

relink:
	rm -f $(bin_PROGRAMS) && make $(bin_PROGRAMS)
