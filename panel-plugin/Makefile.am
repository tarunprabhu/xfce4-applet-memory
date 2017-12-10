plugindir = $(libdir)/xfce4/panel/plugins
plugin_LTLIBRARIES = libappletmemory.la

libappletmemory_la_CFLAGS =									\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"			\
	@LIBXFCE4UI_CFLAGS@												\
	@LIBXFCE4PANEL_CFLAGS@

libappletmemory_la_LIBADD =		\
	@LIBXFCE4UI_LIBS@						\
	@LIBXFCE4PANEL_LIBS@

libappletmemory_la_SOURCES =  \
	memory.c


libappletmemory_la_LDFLAGS = 																					\
	-avoid-version 																											\
	-module 																														\
	-no-undefined 																											\
	-export-symbols-regex '^xfce_panel_module_(preinit|init|construct)' \
	$(PLATFORM_LDFLAGS)

# .desktop file
#
desktop_in_files = applet-memory.desktop.in

desktopdir = $(datadir)/xfce4/panel/plugins
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

EXTRA_DIST = $(desktop_in_files)

DISTCLEANFILES = $(desktop_DATA)

%desktop: %desktop.in
	cp $< $@
