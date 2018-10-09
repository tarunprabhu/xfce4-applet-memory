#ifdef HAVE_CONFIG_H
#include <config.h>
#endif // HAVE_CONFIG_H

#include <libxfce4panel/xfce-panel-convenience.h>
#include <libxfce4panel/xfce-panel-plugin.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4util/libxfce4util.h>

#include <gtk/gtk.h>

void memory_monitor_construct_impl(XfcePanelPlugin* xfce);

static void memory_monitor_construct(XfcePanelPlugin* xfce) {
  memory_monitor_construct_impl(xfce);
}

XFCE_PANEL_PLUGIN_REGISTER(memory_monitor_construct)
