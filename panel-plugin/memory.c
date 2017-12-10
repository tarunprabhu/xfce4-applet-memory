#include <libxfce4panel/xfce-panel-plugin.h>

#include <glib.h>

typedef struct {
  unsigned long total;
  unsigned long free;
  unsigned long avail;
  unsigned long buffered;
  unsigned long cached;
  int ok;
} MemStats;

static void get_memory_usage(MemStats* stats) {
  FILE* fp = NULL;
  gchar buf[256];
  unsigned long val = 0;
  int i;
  int line;
  int skipped;

  skipped = 0;
  lineno = 0;
  if((fp = fopen("/proc/meminfo"))) {
    while(fgets(buf, 256, fp)) {
      /* The format of the line is given below
       *
       *  <label>:\s*<size>\s*[units] 
       */

      /* Skip the label */
      i = 0;
      while(buf[i] != ':')
        i++;

      /* Skip the colon */
      i++;
      
      /* Skip whitespace */
      while(buf[i] == ' ')
        i++;
      
      /* Read the value */
      val = 0;
      while(isdigit(buf[i])) {
        val = (val * 10) + (buf[i] - (int)'0');
        i++;
      }

      lineno++;
      if(g_ascii_strncasecmp(buf, "MemTotal", 8) == 0)
        stats->total = val;
      else if(g_ascii_strncasecmp(buf, "MemFree", 7) == 0)
        stats->free = val;
      else if(g_ascii_strncasecmp(buf, "MemAvailable", 12) == 0)
        stats->avail = val;
      else if(g_ascii_strncasecmp(buf, "Buffers", 7) == 0)
        stats->buffered = val;
      else if(g_ascii_strncasecmp(buf, "Cached", 6) == 0)
        stats->cached = val;
      else
        skipped++;

      if(lineno - skipped == fields) {
        fclose(fp);
        stats->ok =  TRUE;
        break;
      }
    }
  }
}

static void memory(XfcePanelPlugin *plugin) {

}

XFCE_PANEL_PLUGIN_REGISTER_INTERNAL(constructor);
