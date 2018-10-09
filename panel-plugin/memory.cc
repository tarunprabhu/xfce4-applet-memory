#ifdef HAVE_CONFIG_H
#include <config.h>
#endif // HAVE_CONFIG_H

#include <libxfce4panel/xfce-panel-convenience.h>
#include <libxfce4panel/xfce-panel-plugin.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4util/libxfce4util.h>

#include <gtk/gtk.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef struct {
  gdouble min;
  gdouble max;
  gdouble step;
} range_t;

/* This is effectively a resource file for all the constants in the plugin */
/* Yes, this is C++, but I don't want to bring in STL */
typedef struct {
  const guint  monitors;
  const gchar* meminfo;
  struct {
    const gchar* base;
    const guint  count;
  } dials;
  struct {
    const gchar* period;
    const gchar* enable;
    const gchar* icon;
    const gchar* border;
    const gchar* padding;
  } rc;
  struct {
    const gulong   period;
    const gboolean enable;
    const gboolean icon;
    const guint    border;
    const guint    padding;
  } defaults;
  struct {
    struct {
      const guint border;  /* Border of the grid */
      const guint padding; /* Padding of the grid */
      const guint width;   /* Width of the labels */
    } display;
    const range_t border;
    const range_t padding;
    const range_t period;
  } config; /* Parameters for the config dialog */
} app_t;

static constexpr app_t app = {
    2,               /* 2 monitors, RAM and Swap */
    "/proc/meminfo", /* file */
    {
        "xfce-applet-memory-dial-%03d", /* base */
        21 /* The dial moves from 0-100 in steps of 5 (inclusive) */
    },     /* dials */
    {"period", "enable", "icon", "border", "padding"}, /* rc */
    {10000 /* 10 seconds */, TRUE, TRUE, 1, 1},        /* defaults */
    {
        {8, 4, 12}, /* config.display */
        {0, 16, 1}, /* config.border */
        {0, 16, 1}, /* config.padding */
        {1, 60, 1}  /* config.period */
    }               /* config */
};

typedef struct {
  gulong free;
  gulong buffered;
  gulong cached;
} stats_dram_t;

typedef struct {
  gulong cached;
} stats_swap_t;

typedef struct {
  gulong total;
  gulong available;
  union {
    stats_dram_t ram;
    stats_swap_t swap;
  };
} stats_t;

typedef struct {
  guint    border;
  guint    padding;
  guint    period;
  gboolean enable;
  gboolean icon;
} opts_t;

typedef struct {
  GtkWidget* grid;
  GtkWidget* img_icon;
  GtkWidget* img_dial;
} gui_t;

typedef struct {
  GdkPixbuf* icons[app.monitors];
  GdkPixbuf* tooltips[app.monitors];
  GdkPixbuf* dials[app.dials.count];
} pixbufs_t;

typedef struct {
  GtkWidget* grid;
  GtkWidget* chk_show;
  GtkWidget* chk_icon;
  GtkWidget* spin_period;
} config_t;

typedef struct {
  guint      id;
  int        timer;
  gui_t      gui;
  config_t   config;
  opts_t     opts;
  stats_t    stats;
  pixbufs_t* pixbufs;
} monitor_t;

typedef struct {
  XfcePanelPlugin* xfce;
  GtkWidget*       evt;
  GtkWidget*       box;
  pixbufs_t        pixbufs;
  monitor_t        monitors[app.monitors];
} plugin_t;

/* Config dialog callbacks */
static void cb_config_period_changed(GtkWidget*, void*);
static void cb_config_enable_toggled(GtkWidget*, void*);
static void cb_config_icon_toggled(GtkWidget*, void*);
static void cb_config_border_changed(GtkWidget*, void*);
static void cb_config_padding_changed(GtkWidget*, void*);
static void cb_config_response(GtkWidget*, int, plugin_t*);

/* Plugin callbacks */
static void cb_plugin_about(XfcePanelPlugin*);
static void cb_plugin_configure_plugin(XfcePanelPlugin*, plugin_t*);
static void cb_plugin_free_data(XfcePanelPlugin*, plugin_t*);
static void
                cb_plugin_orientation_changed(XfcePanelPlugin*, GtkOrientation, plugin_t*);
static gboolean cb_plugin_remote_event(XfcePanelPlugin*,
                                       const gchar*,
                                       const GValue*,
                                       plugin_t*);
static void     cb_plugin_save(XfcePanelPlugin*, plugin_t*);
static void     cb_plugin_size_changed(XfcePanelPlugin*, int, plugin_t*);

/* Monitor callbacks */
static int cb_monitor_timer_tick(void*);
static gboolean cb_monitor_gen_tooltip(
    GtkWidget*, gint, gint, gboolean, GtkTooltip*, monitor_t*);

/* Pixbufs functions */
static void pixbufs_update(pixbufs_t*, plugin_t*);
static void pixbufs_delete(pixbufs_t*);

/* Opts functions */
static void opts_enable_toggled(opts_t*, gboolean);
static void opts_icon_toggled(opts_t*, gboolean);
static void opts_period_changed(opts_t*, double);
static void opts_border_changed(opts_t*, guint);
static void opts_padding_changed(opts_t*, guint);

/* Monitor functions */
static gboolean monitor_gen_tooltip_ram(monitor_t*, GtkTooltip*);
static gboolean monitor_gen_tooltip_swap(monitor_t*, GtkTooltip*);
static void monitor_update_gui(monitor_t*);
static void monitor_update_timer(monitor_t*);
static void monitor_construct(monitor_t*, guint, plugin_t*);
static void monitor_delete(monitor_t*);

/* Plugin functions */
static plugin_t* plugin_new(guint);
static void      plugin_construct(plugin_t*, XfcePanelPlugin*);
static void      plugin_update_gui(plugin_t*);
static void      plugin_update_timer(plugin_t*);
static void      plugin_update(plugin_t*);
static void      plugin_delete(plugin_t*);
static gboolean
            plugin_handle_remote_event(plugin_t*, const gchar*, const GValue*);
static void plugin_handle_reorient(plugin_t*, GtkOrientation);
static void plugin_handle_resize(plugin_t*, int);
static void plugin_opts_read(plugin_t*);
static void plugin_opts_write(plugin_t*);

/* Config dialog functions */
static void config_dialog_update_gui(config_t*);
static void config_dialog_add_appearance(plugin_t*, GtkWidget*);
static void config_dialog_add_monitor(monitor_t*, GtkWidget*);
static void config_dialog_construct(plugin_t*);
static void config_dialog_response(plugin_t*, GtkWidget*, int);

/* Stats functions */
static gboolean stats_read_ram(stats_t*);
static gboolean stats_read_swap(stats_t*);

/* Specifications for the monitors */
typedef struct {
  const gchar* name;
  const gchar* icon;
  gboolean (*stats_read)(stats_t*);
  gboolean (*gen_tooltip)(monitor_t*, GtkTooltip*);
  struct {
    struct {
      const gchar* label;
      const gchar* tooltip;
    } enable;
    struct {
      const gchar* label;
      const gchar* tooltip;
    } icon;
  } config;
} spec_t;

static constexpr spec_t spec[app.monitors] = {
    {
        "RAM",                    /* name */
        "xfce-applet-memory-ram", /* icon */
        stats_read_ram,           /* stats_read() */
        monitor_gen_tooltip_ram,  /* gen_tooltip() */
        {
            {"Enable RAM monitor", "Enable the RAM monitor"},    /* enable */
            {"Show RAM icon", "Show the RAM icon in the plugin"} /* icon */
        }                                                        /* config */
    },                                                           /* [0] */
    {
        "Swap",                    /* name */
        "xfce-applet-memory-swap", /* icon */
        stats_read_swap,           /* stats_read() */
        monitor_gen_tooltip_swap,  /* gen_tooltip() */
        {
            {"Enable Swap monitor", "Enable the Swap monitor"},    /* enable */
            {"Show swap icon", "Show the swap icon in the plugin"} /* icon */
        }                                                          /* config */
    }                                                              /* [1] */
};

static const guint RAM = 0;

static gboolean
match_field(const gchar* line, const gchar* label, gulong* out) {
  if(g_ascii_strncasecmp(line, label, strlen(label)) == 0) {
    sscanf(line, "%*s %lu", out);
    *out = *out * 1024; /* The values in the file are in kB */
    return TRUE;
  }
  return FALSE;
}

static guint get_pixbuf_index(gulong total, gulong available) {
  guint percent = 0;

  if(total)
    percent = ((total - available) * 100 / total);
  return percent / 5;
}

static GdkPixbuf*
get_pixbuf(const gchar* base, GtkIconTheme* theme, guint size) {
  GdkPixbuf*   pb   = NULL;
  GtkIconInfo* info = NULL;
  const gchar* icon = NULL;

  if((info = gtk_icon_theme_lookup_icon(theme, base, size,
                                        static_cast<GtkIconLookupFlags>(0)))) {
    icon = gtk_icon_info_get_filename(info);
    pb   = gdk_pixbuf_new_from_file_at_scale(icon, size, size, TRUE, NULL);

    g_object_unref(G_OBJECT(info));
  }

  return pb;
}

static const gchar* get_units(gulong val) {
  const gulong kb = 1024;
  const gulong mb = 1024 * kb;
  const gulong gb = 1024 * mb;
  const gulong tb = 1024 * gb;

  if(val > tb)
    return "TB";
  else if(val > gb)
    return "GB";
  else if(val > mb)
    return "MB";
  else if(val > kb)
    return "KB";
  else
    return "B";
}

static gdouble get_value(gdouble value) {
  if(value < 1024)
    return value;
  return get_value(value / 1024);
}

static gboolean stats_read_ram(stats_t* stats) {
  FILE* fp = NULL;
  gchar line[256];
  guint read   = 0;
  guint fields = 5;

  if((fp = fopen(app.meminfo, "r"))) {
    while(fgets(line, 256, fp) && read < fields) {
      read += (match_field(line, "MemTotal", &stats->total) ||
               match_field(line, "MemAvailable", &stats->available) ||
               match_field(line, "MemFree", &stats->ram.free) ||
               match_field(line, "Buffers", &stats->ram.buffered) ||
               match_field(line, "Cached", &stats->ram.cached));
    }
    fclose(fp);
  }

  return read == fields;
}

static gboolean stats_read_swap(stats_t* stats) {
  FILE* fp = NULL;
  gchar line[256];
  guint read   = 0;
  guint fields = 3;

  read = 0;
  if((fp = fopen(app.meminfo, "r"))) {
    while(fgets(line, 256, fp) && read < fields) {
      read += (match_field(line, "SwapTotal", &stats->total) ||
               match_field(line, "SwapFree", &stats->available) ||
               match_field(line, "SwapCached", &stats->swap.cached));
    }
    fclose(fp);
  }

  return read == fields;
}

static void pixbufs_create(pixbufs_t* pixbufs, plugin_t* plugin) {
  guint            size, i;
  GtkIconTheme*    theme = NULL;
  XfcePanelPlugin* xfce  = plugin->xfce;

  size = 96;
  theme =
      gtk_icon_theme_get_for_screen(gtk_widget_get_screen(GTK_WIDGET(xfce)));
  for(i = 0; i < app.monitors; i++)
    pixbufs->tooltips[i] = get_pixbuf(spec[i].icon, theme, size);
}

static void pixbufs_update(pixbufs_t* pixbufs, plugin_t* plugin) {
  guint            i       = 0;
  GtkIconTheme*    theme   = NULL;
  gchar*           base    = NULL;
  monitor_t*       monitor = &plugin->monitors[RAM];
  opts_t*          opts    = &monitor->opts;
  guint            border  = opts->border;
  guint            padding = opts->padding;
  XfcePanelPlugin* xfce    = plugin->xfce;
  guint            size, size_icon, size_dial;

  size      = xfce_panel_plugin_get_size(xfce);
  size_icon = size - border * 2 - padding * 2;
  size_dial = size - border * 2 - padding * 2;
  theme =
      gtk_icon_theme_get_for_screen(gtk_widget_get_screen(GTK_WIDGET(xfce)));

  pixbufs_delete(pixbufs);

  for(i = 0; i < app.monitors; i++)
    pixbufs->icons[i] = get_pixbuf(spec[i].icon, theme, size_icon);
  for(i = 0; i < app.dials.count; i++) {
    base              = g_strdup_printf(app.dials.base, i * 5);
    pixbufs->dials[i] = get_pixbuf(base, theme, size_dial);
    g_free(base);
  }
}

static void pixbufs_delete(pixbufs_t* pixbufs) {
  guint i;

  for(i = 0; i < app.monitors; i++)
    if(pixbufs->icons[i])
      g_object_unref(G_OBJECT(pixbufs->icons[i]));
  for(i = 0; i < app.dials.count; i++)
    if(pixbufs->dials[i])
      g_object_unref(G_OBJECT(pixbufs->dials[i]));
}

static gboolean monitor_gen_tooltip_ram(monitor_t*  monitor,
                                        GtkTooltip* tooltip) {
  stats_t* stats = &monitor->stats;
  pixbufs_t* pixbufs = monitor->pixbufs;
  GdkPixbuf* icon = pixbufs->tooltips[monitor->id];
  gchar* markup;

  markup = g_markup_printf_escaped(
      "<span><tt>"
      "<b>%-12s</b>%5.1f %s\n"
      "<b>%-12s</b>%5.1f %s\n"
      "<b>%-12s</b>%5.1f %s\n"
      "<b>%-12s</b>%5.1f %s\n\n"
      "<b>%-12s</b>%5.1f %s</tt></span>",
      "Available", get_value(stats->available), get_units(stats->available), //
      "Free", get_value(stats->ram.free), get_units(stats->ram.free),        //
      "Buffers", get_value(stats->ram.buffered), get_units(stats->ram.buffered),
      "Cached", get_value(stats->ram.cached), get_units(stats->ram.cached), //
      "Total", get_value(stats->total), get_units(stats->total));
  
  gtk_tooltip_set_icon(tooltip, icon);
  gtk_tooltip_set_markup(tooltip, markup);

  return TRUE;
}

static gboolean monitor_gen_tooltip_swap(monitor_t*  monitor,
                                         GtkTooltip* tooltip) {
  stats_t* stats = &monitor->stats;
  pixbufs_t* pixbufs = monitor->pixbufs;
  GdkPixbuf* icon = pixbufs->tooltips[monitor->id];
  gchar* markup;

  markup = g_markup_printf_escaped(
      "<span><tt>"
      "<b>%-12s</b>%5.1f %s\n"
      "<b>%-12s</b>%5.1f %s\n\n"
      "<b>%-12s</b>%5.1f %s</tt></span>",
      "Available", get_value(stats->available), get_units(stats->available), //
      "Cached", get_value(stats->swap.cached), get_units(stats->swap.cached), //
      "Total", get_value(stats->total), get_units(stats->total));

  gtk_tooltip_set_icon(tooltip, icon);
  gtk_tooltip_set_markup(tooltip, markup);

  return TRUE;
}

static int monitor_timer_tick(monitor_t* monitor) {
  opts_t* opts = &monitor->opts;

  monitor_update_gui(monitor);
  if(monitor->timer == 0) {
    monitor->timer =
        g_timeout_add(opts->period, cb_monitor_timer_tick, monitor);
    return FALSE;
  }

  return TRUE;
}

static void monitor_update_timer(monitor_t* monitor) {
  opts_t* opts = &(monitor->opts);

  if(opts->enable) {
    if(monitor->timer) {
      g_source_remove(monitor->timer);
      monitor->timer = 0;
    }
    monitor_timer_tick(monitor);
  }
}

static void monitor_update_gui(monitor_t* monitor) {
  guint      index   = 0;
  stats_t*   stats   = &monitor->stats;
  gui_t*     gui     = &monitor->gui;
  opts_t*    opts    = &monitor->opts;
  pixbufs_t* pixbufs = monitor->pixbufs;

  if(spec[monitor->id].stats_read(stats)) {
    index = get_pixbuf_index(stats->total, stats->available);
    if(opts->enable) {
      gtk_image_set_from_pixbuf(GTK_IMAGE(gui->img_icon),
                                pixbufs->icons[monitor->id]);
      gtk_image_set_from_pixbuf(GTK_IMAGE(gui->img_dial),
                                pixbufs->dials[index]);
      gtk_container_set_border_width(GTK_CONTAINER(gui->grid), opts->border);
      gtk_grid_set_row_spacing(GTK_GRID(gui->grid), opts->padding);
      gtk_grid_set_column_spacing(GTK_GRID(gui->grid), opts->padding);
      gtk_widget_show(gui->grid);
      if(opts->icon)
        gtk_widget_show(gui->img_icon);
      else
        gtk_widget_hide(gui->img_icon);
    } else {
      gtk_widget_hide(gui->grid);
    }
  }
}

static void monitor_construct(monitor_t* monitor, guint id, plugin_t* plugin) {
  GtkWidget *      grid, *img_icon, *img_dial;
  GtkOrientation   orientation;
  XfcePanelPlugin* xfce    = plugin->xfce;
  gui_t*           gui     = &monitor->gui;
  opts_t*          opts    = &monitor->opts;
  pixbufs_t*       pixbufs = &plugin->pixbufs;

  orientation      = xfce_panel_plugin_get_orientation(xfce);
  monitor->id      = id;
  monitor->timer   = 0;
  monitor->pixbufs = pixbufs;

  grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), opts->padding);
  gtk_grid_set_column_spacing(GTK_GRID(grid), opts->padding);
  gtk_container_set_border_width(GTK_CONTAINER(grid), opts->border);
  gtk_widget_show(grid);

  img_icon = gtk_image_new();
  gtk_widget_set_halign(img_icon, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(img_icon, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), img_icon, 0, 0, 1, 1);
  gtk_widget_show(img_icon);

  img_dial = gtk_image_new();
  gtk_widget_set_halign(img_dial, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(img_dial, GTK_ALIGN_CENTER);
  gtk_grid_attach(GTK_GRID(grid), img_dial, 0, 1, 1, 1);
  gtk_widget_show(img_dial);

  gui->grid     = grid;
  gui->img_icon = img_icon;
  gui->img_dial = img_dial;

  g_object_set(G_OBJECT(gui->grid), "has-tooltip", TRUE, NULL);
  g_signal_connect(G_OBJECT(gui->grid), "query-tooltip",
                   G_CALLBACK(cb_monitor_gen_tooltip), monitor);
}

static void monitor_delete(monitor_t* monitor) {
  if(monitor->timer)
    g_source_remove(monitor->timer);
}

static void opts_enable_toggled(opts_t* opts, gboolean enabled) {
  opts->enable = enabled;
}

static void opts_icon_toggled(opts_t* opts, gboolean icon) {
  opts->icon = icon;
}

static void opts_period_changed(opts_t* opts, double period) {
  opts->period = period * 1000;
}

static void opts_border_changed(opts_t* opts, guint border) {
  opts->border = border;
}

static void opts_padding_changed(opts_t* opts, guint padding) {
  opts->padding = padding;
}

static void config_dialog_update_gui(config_t* config, gboolean enabled) {
  gtk_widget_set_sensitive(config->grid, enabled);
}

static void
config_dialog_response(plugin_t* plugin, GtkWidget* dialog, int response) {
  XfcePanelPlugin* xfce = plugin->xfce;

  gtk_widget_destroy(dialog);
  xfce_panel_plugin_unblock_menu(xfce);
  plugin_opts_write(plugin);
  plugin_update(plugin);
}

static void config_dialog_add_appearance(plugin_t*  plugin,
                                         GtkWidget* notebook) {
  GtkWidget *lbl_border, *lbl_padding;
  GtkWidget *spin_border, *spin_padding;
  GtkWidget *grid, *frm, *lbl_title;
  monitor_t* monitor = &plugin->monitors[RAM];
  opts_t*    opts    = &monitor->opts;

  grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(grid), app.config.display.padding);
  gtk_grid_set_row_spacing(GTK_GRID(grid), app.config.display.padding);
  gtk_container_set_border_width(GTK_CONTAINER(grid),
                                 app.config.display.border);
  gtk_widget_show(grid);

  lbl_border = gtk_label_new("Border width");
  gtk_label_set_width_chars(GTK_LABEL(lbl_border), app.config.display.width);
  gtk_widget_set_tooltip_text(lbl_border, "Set space around plugin icons");
  gtk_misc_set_padding(GTK_MISC(lbl_border), app.config.display.padding,
                       app.config.display.padding);
  gtk_grid_attach(GTK_GRID(grid), lbl_border, 0, 0, 1, 1);
  gtk_widget_show(lbl_border);

  spin_border = gtk_spin_button_new_with_range(
      app.config.border.min, app.config.border.max, app.config.border.step);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_border), opts->border);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin_border), TRUE);
  gtk_grid_attach(GTK_GRID(grid), spin_border, 1, 0, 1, 1);
  gtk_widget_show(spin_border);

  lbl_padding = gtk_label_new("Padding");
  gtk_label_set_width_chars(GTK_LABEL(lbl_padding), app.config.display.width);
  gtk_widget_set_tooltip_text(lbl_padding, "Set padding between plugin icons");
  gtk_misc_set_padding(GTK_MISC(lbl_padding), app.config.display.padding,
                       app.config.display.padding);
  gtk_grid_attach(GTK_GRID(grid), lbl_padding, 0, 1, 1, 1);
  gtk_widget_show(lbl_padding);

  spin_padding = gtk_spin_button_new_with_range(
      app.config.padding.min, app.config.padding.max, app.config.padding.step);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_padding), opts->padding);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin_padding), TRUE);
  gtk_grid_attach(GTK_GRID(grid), spin_padding, 1, 1, 1, 1);
  gtk_widget_show(spin_padding);

  frm = gtk_frame_new(NULL);
  gtk_container_set_border_width(GTK_CONTAINER(frm), app.config.display.border);
  gtk_container_add(GTK_CONTAINER(frm), grid);
  gtk_widget_show(frm);

  lbl_title = gtk_label_new("Appearance");
  gtk_widget_show(lbl_title);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), frm, lbl_title);

  g_signal_connect(spin_border, "value_changed",
                   G_CALLBACK(cb_config_border_changed), plugin);
  g_signal_connect(spin_padding, "value_changed",
                   G_CALLBACK(cb_config_padding_changed), plugin);
}

static void config_dialog_add_monitor(monitor_t* monitor, GtkWidget* notebook) {
  GtkWidget* evt_enable;
  GtkWidget *chk_enable, *chk_icon;
  GtkWidget *lbl_period, *spin_period;
  GtkWidget *grid, *frm, *lbl_title;
  config_t*  config = &monitor->config;
  opts_t*    opts   = &monitor->opts;
  guint      i      = monitor->id;

  grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(grid), app.config.display.padding);
  gtk_grid_set_row_spacing(GTK_GRID(grid), app.config.display.padding);
  gtk_container_set_border_width(GTK_CONTAINER(grid),
                                 app.config.display.border);
  gtk_widget_show(grid);

  chk_icon = gtk_check_button_new_with_mnemonic(spec[i].config.icon.label);
  gtk_widget_set_tooltip_text(chk_icon, spec[i].config.icon.tooltip);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_icon), opts->icon);
  gtk_grid_attach(GTK_GRID(grid), chk_icon, 0, 0, 2, 1);
  gtk_widget_show(chk_icon);

  lbl_period = gtk_label_new("Period (s)");
  gtk_label_set_width_chars(GTK_LABEL(lbl_period), app.config.display.width);
  gtk_misc_set_padding(GTK_MISC(lbl_period), app.config.display.padding,
                       app.config.display.padding);
  gtk_widget_set_tooltip_text(lbl_period, "Update frequency");
  gtk_grid_attach(GTK_GRID(grid), lbl_period, 0, 1, 1, 1);
  gtk_widget_show(lbl_period);

  spin_period = gtk_spin_button_new_with_range(
      app.config.period.min, app.config.period.max, app.config.period.step);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_period), opts->period / 1000);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin_period), TRUE);
  gtk_grid_attach(GTK_GRID(grid), spin_period, 1, 1, 1, 1);
  gtk_widget_show(spin_period);

  evt_enable = gtk_event_box_new();
  gtk_widget_show(evt_enable);

  chk_enable = gtk_check_button_new_with_mnemonic(spec[i].config.enable.label);
  gtk_container_add(GTK_CONTAINER(evt_enable), chk_enable);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_enable), opts->enable);
  gtk_widget_set_tooltip_text(chk_enable, spec[i].config.enable.tooltip);
  gtk_widget_show(chk_enable);

  frm = gtk_frame_new(NULL);
  gtk_frame_set_label_widget(GTK_FRAME(frm), evt_enable);
  gtk_frame_set_label_align(GTK_FRAME(frm), 0.1, 0.5);
  gtk_container_set_border_width(GTK_CONTAINER(frm), app.config.display.border);
  gtk_widget_set_sensitive(frm, opts->enable);
  gtk_container_add(GTK_CONTAINER(frm), grid);
  gtk_widget_show(frm);

  lbl_title = gtk_label_new(spec[i].name);
  gtk_widget_show(lbl_title);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), frm, lbl_title);

  config->grid        = grid;
  config->spin_period = spin_period;
  config->chk_icon    = chk_icon;

  g_signal_connect(chk_enable, "toggled", G_CALLBACK(cb_config_enable_toggled),
                   monitor);
  g_signal_connect(chk_icon, "toggled", G_CALLBACK(cb_config_icon_toggled),
                   monitor);
  g_signal_connect(spin_period, "value_changed",
                   G_CALLBACK(cb_config_period_changed), monitor);
}

static void config_dialog_construct(plugin_t* plugin) {
  XfcePanelPlugin* xfce = plugin->xfce;
  GtkWidget *      dialog, *notebook, *box;
  guint            i = 0;

  xfce_panel_plugin_block_menu(xfce);

  dialog = xfce_titled_dialog_new_with_buttons(
      "Configuration", GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(xfce))),
      GTK_DIALOG_DESTROY_WITH_PARENT, "gtk-close", GTK_RESPONSE_OK, NULL);
  gtk_window_set_icon_name(GTK_WINDOW(dialog), spec[RAM].icon);

  g_signal_connect(dialog, "response", G_CALLBACK(cb_config_response), plugin);

  xfce_titled_dialog_set_subtitle(XFCE_TITLED_DIALOG(dialog), "Memory Monitor");

  box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_set_border_width(GTK_CONTAINER(box), 0);
  gtk_widget_show(box);

  notebook = gtk_notebook_new();
  config_dialog_add_appearance(plugin, notebook);
  for(i = 0; i < app.monitors; i++)
    config_dialog_add_monitor(&plugin->monitors[i], notebook);
  gtk_box_pack_start(GTK_BOX(box), notebook, TRUE, TRUE,
                     app.config.display.border);
  gtk_widget_show(notebook);

  gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                     box, TRUE, TRUE, app.config.display.border);

  gtk_widget_show(dialog);
}

static void plugin_opts_read(plugin_t* plugin) {
  XfcePanelPlugin* xfce = plugin->xfce;
  gchar*           file = NULL;
  XfceRc*          rc   = NULL;
  opts_t*          opts = NULL;
  guint            i    = 0;

  if((file = xfce_panel_plugin_lookup_rc_file(xfce))) {
    if((rc = xfce_rc_simple_open(file, TRUE))) {
      for(i = 0; i < app.monitors; i++) {
        opts = &plugin->monitors[i].opts;
        xfce_rc_set_group(rc, spec[i].name);
        opts->enable =
            xfce_rc_read_bool_entry(rc, app.rc.enable, app.defaults.enable);
        opts->icon =
            xfce_rc_read_bool_entry(rc, app.rc.icon, app.defaults.icon);
        opts->period =
            xfce_rc_read_int_entry(rc, app.rc.period, app.defaults.period);
        opts->border =
            xfce_rc_read_int_entry(rc, app.rc.border, app.defaults.border);
        opts->padding =
            xfce_rc_read_int_entry(rc, app.rc.padding, app.defaults.padding);
      }
      xfce_rc_close(rc);
    }
    g_free(file);
  }
}

static void plugin_opts_write(plugin_t* plugin) {
  XfcePanelPlugin* xfce = plugin->xfce;
  gchar*           file = NULL;
  XfceRc*          rc   = NULL;
  opts_t*          opts = NULL;
  guint            i    = 0;

  if((file = xfce_panel_plugin_save_location(xfce, TRUE))) {
    if((rc = xfce_rc_simple_open(file, FALSE))) {
      for(i = 0; i < app.monitors; i++) {
        opts = &plugin->monitors[i].opts;
        xfce_rc_set_group(rc, spec[i].name);
        xfce_rc_write_bool_entry(rc, app.rc.enable, opts->enable);
        xfce_rc_write_bool_entry(rc, app.rc.icon, opts->icon);
        xfce_rc_write_int_entry(rc, app.rc.period, opts->period);
        xfce_rc_write_int_entry(rc, app.rc.border, opts->border);
        xfce_rc_write_int_entry(rc, app.rc.padding, opts->padding);
      }
      xfce_rc_close(rc);
    }
    g_free(file);
  }
}

static void plugin_update_gui(plugin_t* plugin) {
  guint i = 0;

  for(i = 0; i < app.monitors; i++)
    monitor_update_gui(&plugin->monitors[i]);
}

static void plugin_update_timer(plugin_t* plugin) {
  guint i = 0;

  for(i = 0; i < app.monitors; i++)
    monitor_update_timer(&plugin->monitors[i]);
}

static void plugin_update(plugin_t* plugin) {
  plugin_update_gui(plugin);
  plugin_update_timer(plugin);
}

static plugin_t* plugin_new(guint num) {
  plugin_t* plugin = g_new(plugin_t, num);
  memset(plugin, 0, sizeof(plugin_t));
  return plugin;
}

static void plugin_about(XfcePanelPlugin* plugin) {
  GdkPixbuf*   icon;
  const gchar* auth[] = {"Tarun Prabhu <tarun.prabhu@gmail.com>", NULL};

  icon = xfce_panel_pixbuf_from_source(spec[RAM].icon, NULL, 32);
  gtk_show_about_dialog(
      NULL, "logo", icon, "license",
      xfce_get_license_text(XFCE_LICENSE_TEXT_GPL), "version", VERSION,
      "program-name", PACKAGE, "comments",
      _("Displays a dial on the panel showing the percentage of memory used"),
      "website", "http://github.com/tarunprabhu/xfce4-applet-memory",
      "copyright", _("Copyright \xc2\xa9 2018 Tarun Prabhu\n"), "authors", auth,
      NULL);

  if(icon)
    g_object_unref(G_OBJECT(icon));
}

static void plugin_handle_resize(plugin_t* plugin, int size) {
  XfcePanelPlugin* xfce    = plugin->xfce;
  pixbufs_t*       pixbufs = &plugin->pixbufs;
  monitor_t*       monitor = &plugin->monitors[RAM];

  pixbufs_update(pixbufs, plugin);
  plugin_update_gui(plugin);
}

static gboolean plugin_handle_remote_event(plugin_t*     plugin,
                                           const gchar*  name,
                                           const GValue* value) {
  if(value && G_IS_VALUE(value)) {
    if(strcmp(name, "refresh") == 0) {
      if(G_VALUE_HOLDS_BOOLEAN(value) && g_value_get_boolean(value)) {
        plugin_update(plugin);
      }
      return TRUE;
    }
  }

  return FALSE;
}

static void plugin_handle_reorient(plugin_t*      plugin,
                                   GtkOrientation orientation) {
  gtk_orientable_set_orientation(GTK_ORIENTABLE(plugin->box), orientation);
  plugin_update(plugin);
}

static void plugin_construct(plugin_t* plugin, XfcePanelPlugin* xfce) {
  GtkWidget *    evt, *box;
  GtkOrientation orientation;
  monitor_t*     monitor = NULL;
  gui_t*         gui     = NULL;
  opts_t*        opts    = NULL;
  pixbufs_t*     pixbufs = &plugin->pixbufs;
  guint          i       = 0;

  orientation = xfce_panel_plugin_get_orientation(xfce);

  plugin->xfce = xfce;
  plugin_opts_read(plugin);
  pixbufs_create(pixbufs, plugin);
  for(i = 0; i < app.monitors; i++)
    monitor_construct(&plugin->monitors[i], i, plugin);

  evt = gtk_event_box_new();
  gtk_event_box_set_visible_window(GTK_EVENT_BOX(evt), FALSE);
  gtk_widget_show(evt);

  monitor = &plugin->monitors[RAM];
  opts    = &monitor->opts;
  box     = gtk_box_new(orientation, opts->padding);
  gtk_container_set_border_width(GTK_CONTAINER(box), opts->border);
  for(i = 0; i < app.monitors; i++) {
    monitor = &plugin->monitors[i];
    opts    = &monitor->opts;
    gui     = &monitor->gui;
    gtk_box_pack_start(GTK_BOX(box), gui->grid, TRUE, TRUE, opts->padding);
  }
  gtk_widget_show(box);

  gtk_container_add(GTK_CONTAINER(evt), box);
  xfce_panel_plugin_add_action_widget(xfce, evt);

  plugin->evt = evt;
  plugin->box = box;
}

static void plugin_delete(plugin_t* plugin) {
  pixbufs_t* pixbufs = &plugin->pixbufs;
  guint      i       = 0;

  for(i = 0; i < app.monitors; i++)
    monitor_delete(&plugin->monitors[i]);
  pixbufs_delete(pixbufs);
  g_free(plugin);
}

/* Config callbacks */
static void cb_config_period_changed(GtkWidget* spin, void* data) {
  monitor_t* monitor = (monitor_t*)data;
  opts_t*    opts    = &monitor->opts;
  double     period  = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));

  opts_period_changed(opts, period);
  monitor_update_timer(monitor);
}

static void cb_config_enable_toggled(GtkWidget* chk, void* data) {
  monitor_t* monitor = (monitor_t*)data;
  opts_t*    opts    = &monitor->opts;
  config_t*  config  = &monitor->config;
  gboolean   enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk));

  opts_enable_toggled(opts, enabled);
  config_dialog_update_gui(config, enabled);
  monitor_update_gui(monitor);
}

static void cb_config_icon_toggled(GtkWidget* chk, void* data) {
  monitor_t* monitor = (monitor_t*)data;
  opts_t*    opts    = &monitor->opts;
  gboolean   icon    = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk));

  opts_icon_toggled(opts, icon);
  monitor_update_gui(monitor);
}

static void cb_config_border_changed(GtkWidget* spin, void* data) {
  plugin_t*  plugin  = (plugin_t*)data;
  guint      border  = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
  monitor_t* monitor = NULL;
  opts_t*    opts    = NULL;
  pixbufs_t* pixbufs = &plugin->pixbufs;
  guint      i       = 0;

  for(i = 0; i < app.monitors; i++) {
    monitor = &plugin->monitors[i];
    opts    = &monitor->opts;
    opts_border_changed(opts, border);
  }
  pixbufs_update(pixbufs, plugin);
  plugin_update_gui(plugin);
}

static void cb_config_padding_changed(GtkWidget* spin, void* data) {
  plugin_t*  plugin  = (plugin_t*)data;
  guint      padding = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));
  monitor_t* monitor = NULL;
  opts_t*    opts    = NULL;
  pixbufs_t* pixbufs = &plugin->pixbufs;
  guint      i       = 0;

  for(i = 0; i < app.monitors; i++) {
    monitor = &plugin->monitors[i];
    opts    = &monitor->opts;
    opts_padding_changed(opts, padding);
  }
  pixbufs_update(pixbufs, plugin);
  monitor_update_gui(monitor);
}

static void
cb_config_response(GtkWidget* dialog, int response, plugin_t* plugin) {
  config_dialog_response(plugin, dialog, response);
}

/* Plugin callbacks */
static void cb_plugin_about(XfcePanelPlugin* plugin) {
  plugin_about(plugin);
}

static void cb_plugin_configure_plugin(XfcePanelPlugin* xfce,
                                       plugin_t*        plugin) {
  config_dialog_construct(plugin);
}

static void cb_plugin_free_data(XfcePanelPlugin* xfce, plugin_t* plugin) {
  plugin_delete(plugin);
}

static void cb_plugin_orientation_changed(XfcePanelPlugin* xfce,
                                          GtkOrientation   orientation,
                                          plugin_t*        plugin) {
  plugin_handle_reorient(plugin, orientation);
}

static gboolean cb_plugin_remote_event(XfcePanelPlugin* xfce,
                                       const gchar*     name,
                                       const GValue*    value,
                                       plugin_t*        plugin) {
  return plugin_handle_remote_event(plugin, name, value);
}

static void cb_plugin_save(XfcePanelPlugin* xfce, plugin_t* plugin) {
  plugin_opts_write(plugin);
}

static void cb_plugin_size_changed(XfcePanelPlugin* panel_plugin,
                                   int              size,
                                   plugin_t*        plugin) {
  plugin_handle_resize(plugin, size);
}

/* Monitor callbacks */
static int cb_monitor_timer_tick(void* p) {
  return monitor_timer_tick((monitor_t*)p);
}

static gboolean cb_monitor_gen_tooltip(GtkWidget*  widget,
                                       gint        x,
                                       gint        y,
                                       gboolean    keyboard_mode,
                                       GtkTooltip* tooltip,
                                       monitor_t*  monitor) {
  return spec[monitor->id].gen_tooltip(monitor, tooltip);
}

/* Main plugin constructor */
extern "C" void memory_monitor_construct_impl(XfcePanelPlugin* xfce) {
  plugin_t* plugin;

  plugin = plugin_new(1);
  plugin_construct(plugin, xfce);

  gtk_container_add(GTK_CONTAINER(xfce), plugin->evt);

  xfce_panel_plugin_menu_show_about(xfce);
  xfce_panel_plugin_menu_show_configure(xfce);

  g_signal_connect(xfce, "about", G_CALLBACK(cb_plugin_about), xfce);
  g_signal_connect(xfce, "configure-plugin",
                   G_CALLBACK(cb_plugin_configure_plugin), plugin);
  g_signal_connect(xfce, "free-data", G_CALLBACK(cb_plugin_free_data), plugin);
  g_signal_connect(xfce, "orientation-changed",
                   G_CALLBACK(cb_plugin_orientation_changed), plugin);
  g_signal_connect(xfce, "remote-event", G_CALLBACK(cb_plugin_remote_event),
                   plugin);
  g_signal_connect(xfce, "save", G_CALLBACK(cb_plugin_save), plugin);
  g_signal_connect(xfce, "size-changed", G_CALLBACK(cb_plugin_size_changed),
                   plugin);
}
