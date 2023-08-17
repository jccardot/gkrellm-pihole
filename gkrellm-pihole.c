/*
 * pihole monitor gkrellm plugin
 * version 0.1
 * shows number of queries in the last 24 hours and number blocked
 * and a nice pihole icon
 * (c) 2023 JCC gkrellm@cardot.net
 */

#include <gkrellm2/gkrellm.h>
#include <stdio.h>
#include <curl/curl.h>

#include "pihole.xpm"

#define CONFIG_NAME "gkrellm-pihole"
#define STYLE_NAME  "gkrellm-pihole"

#define PIHOLE_URL_PATTERN       "http://%s/admin/api.php?summaryRaw&auth=%s"
#define PIHOLE_DEFAULT_FREQ      10

#define SPACING_BETWEEN_ROWS     4
#define SPACING_BETWEEN_COLUMNS  6

#define PIHOLE_ONLINE  0
#define PIHOLE_OFFLINE 1

static GkrellmMonitor *monitor;
static GkrellmPanel *panel;
static GkrellmDecal *decal_pihole_icon;
static GkrellmDecal *decal_label1;
static GkrellmDecal *decal_text1;
static GkrellmDecal *decal_label2;
static GkrellmDecal *decal_text2;
static GdkPixmap *pihole_gdkpixmap;
static gchar *pihole_URL, *dns_queries_today, *ads_blocked_today; 
static gint style_id;
static gint update=-1;

static GtkWidget  *pihole_hostname_fillin;
static gchar      *pihole_hostname;
static GtkWidget  *pihole_API_key_fillin;
static gchar      *pihole_API_key;
static GtkWidget  *pihole_freq_spinner;
static gint        pihole_freq = PIHOLE_DEFAULT_FREQ;
static GtkWidget  *pihole_url_pattern_fillin;
static gchar      *pihole_url_pattern;

struct MemoryStruct {
  char *memory;
  size_t size;
};
 
static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;
 
  char *ptr = realloc(mem->memory, mem->size + realsize + 1);
  if(!ptr) {
    /* out of memory! */
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
  }
 
  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;
 
  return realsize;
}

int
pihole(void)
{
  CURL *curl;
  CURLcode res=0;

// TODO: move to init  
  struct MemoryStruct chunk;

  chunk.memory = malloc(1); /* will be grown as needed by the realloc above */
  chunk.size = 0;           /* no data at this point */

  if (pihole_URL == NULL || pihole_URL[0] == 0) {
    gkrellm_draw_decal_pixmap(panel, decal_pihole_icon, PIHOLE_OFFLINE);
    puts("No URL defined");
    return -1;
  }

  curl = curl_easy_init();
// TODO: until there

  if(curl) {
    curl_easy_setopt(curl, CURLOPT_URL, pihole_URL);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    /* send all data to this function  */
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    /* we pass our 'chunk' struct to the callback function */
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    res = curl_easy_perform(curl);
    //printf("%lu bytes retrieved\n", (unsigned long)chunk.size);
    if(res != CURLE_OK || chunk.size == 0) {
      gkrellm_draw_decal_pixmap(panel, decal_pihole_icon, PIHOLE_OFFLINE);
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));
      return -1;
    }
    
    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code != 200) {
      gkrellm_draw_decal_pixmap(panel, decal_pihole_icon, PIHOLE_OFFLINE);
      fprintf(stderr, "curl_easy_perform() response code: %lu\n", response_code);
      return -1;
    }
    
    /* if the api_key is incorrect, the api answers with "[]" */
    if (!strcmp(chunk.memory, "[]")) {
      gkrellm_draw_decal_pixmap(panel, decal_pihole_icon, PIHOLE_OFFLINE);
      puts("Incorrect API key");
      return -1;
    }

    gkrellm_draw_decal_pixmap(panel, decal_pihole_icon, PIHOLE_ONLINE);

    // find the data in chunk.memory, named "dns_queries_today", "ads_blocked_today"
    char* mystr;

    mystr = strstr(chunk.memory, "dns_queries_today");
    if (mystr) {
      mystr += strlen("dns_queries_today") + 2;
      strtok(mystr, ",");
      g_free(dns_queries_today);
      dns_queries_today = g_strdup(mystr);

      mystr = strstr(mystr + strlen(mystr) + 1, "ads_blocked_today");
      if (mystr) {
        mystr += strlen("ads_blocked_today") + 2;
        strtok(mystr, ",");
        g_free(ads_blocked_today);
        ads_blocked_today = g_strdup(mystr);
      }
    }

// TODO: move to cleanup
    free(chunk.memory);
    curl_easy_cleanup(curl);
// TODO: until there
  }
  return 0;
}

/***********************************************************/

static gint
panel_expose_event(GtkWidget *widget, GdkEventExpose *ev) {
  gdk_draw_pixmap(widget->window,
    widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
    panel->pixmap, ev->area.x, ev->area.y, ev->area.x, ev->area.y,
    ev->area.width, ev->area.height);
  return FALSE;
}

static void
update_plugin() {
  static gint w;
  GkrellmTextstyle *ts /*, *ts_alt*/;

  // Do it only once every n seconds
  if (update >= 0) {
    if (GK.second_tick)
      update++;
    if (update < pihole_freq) return;
    update = 0;
  }
  else
    update = 0; // first time

  ts = gkrellm_meter_textstyle(style_id);
  //ts_alt = gkrellm_meter_alt_textstyle(style_id);

  w = gkrellm_chart_width();
 
  pihole();

  // right align values
  decal_text1->x_off =
    (w - gdk_string_width(gdk_font_from_description(ts->font), dns_queries_today)) - 4;
    //(w - gdk_string_width(decal_text1->text_style.font, dns_queries_today));
  if (decal_text1->x_off < 0)
    decal_text1->x_off = 0;

  decal_text2->x_off =
    (w - gdk_string_width(gdk_font_from_description(ts->font), ads_blocked_today)) - 4;
    //(w - gdk_string_width(decal_text1->text_style.font, ads_blocked_today));
  if (decal_text2->x_off < 0)
    decal_text2->x_off = 0;

  gkrellm_draw_decal_text(panel, decal_label1, "Total", 0);
  gkrellm_draw_decal_text(panel, decal_text1, dns_queries_today, 0);
  gkrellm_draw_decal_text(panel, decal_label2, "Ads", 0);
  gkrellm_draw_decal_text(panel, decal_text2, ads_blocked_today, 0);

  gkrellm_draw_panel_layers(panel);

}

static void
create_plugin(GtkWidget *vbox, gint first_create) {
  GkrellmStyle   *style;
  GkrellmTextstyle  *ts, *ts_alt;
  GdkBitmap  *mask;
  int y;
  gint w,h;

  if (first_create)
    panel = gkrellm_panel_new0();

  style = gkrellm_meter_style(style_id);

  ts = gkrellm_meter_textstyle(style_id);
  ts_alt = gkrellm_meter_alt_textstyle(style_id);
 
  y = -1; /* y = -1 places at top margin */

  pihole_gdkpixmap = gdk_pixmap_create_from_xpm_d(vbox->window, &mask, NULL, (char **)pihole_xpm);
  decal_pihole_icon = gkrellm_create_decal_pixmap(panel, pihole_gdkpixmap, mask, 2,  style, 4, 4);
  //gkrellm_draw_decal_pixmap(panel, decal_pihole_icon, 0);
  
  gdk_pixmap_get_size(pihole_gdkpixmap, &w, &h);

  decal_label1 = gkrellm_create_decal_text(panel,
                            "Total",                     /* string used for vertical sizing */
                            ts_alt,
                            style,
                            w + SPACING_BETWEEN_COLUMNS, /* x = -1 places at left margin */
                            y,                           /* y = -1 places at top margin */
                            -1);                         /* use full width */
  decal_text1 = gkrellm_create_decal_text(panel, "0", ts, style, -1, -1, -1);

  y += decal_text1->h + SPACING_BETWEEN_ROWS;
  decal_label2 = gkrellm_create_decal_text(panel,
                            "Ads",                       /* string used for vertical sizing */
                            ts_alt,
                            style,
                            w + SPACING_BETWEEN_COLUMNS, /* x = -1 places at left margin */
                            y,                           /* y = -1 places at top margin */
                            -1);                         /* use full width */
  decal_text2 = gkrellm_create_decal_text(panel, "0", ts, style, -1, y, -1);

  gkrellm_panel_configure(panel, NULL, style);
  gkrellm_panel_create(vbox, monitor, panel);

  if (first_create)
    g_signal_connect(G_OBJECT (panel->drawing_area), "expose_event",
                     G_CALLBACK (panel_expose_event), NULL);
}

/********************************************************/

static void
save_plugin_config(FILE *f) {
  if (pihole_hostname != NULL)
    fprintf(f, "%s pihole_hostname %s\n", CONFIG_NAME, pihole_hostname);
  if (pihole_API_key != NULL)
    fprintf(f, "%s pihole_api_key %s\n", CONFIG_NAME, pihole_API_key);
  if (pihole_freq > 0)
    fprintf(f, "%s pihole_freq %u\n", CONFIG_NAME, pihole_freq);
  if (pihole_url_pattern != NULL)
    fprintf(f, "%s pihole_url_pattern %s\n", CONFIG_NAME, pihole_url_pattern);
}

static void updateURL() {
  if (pihole_hostname != NULL && pihole_API_key != NULL && pihole_url_pattern != NULL) {
    if (pihole_URL != NULL)
      g_free(pihole_URL);
    pihole_URL = g_strdup_printf(pihole_url_pattern, pihole_hostname, pihole_API_key);
  }
}

static void
load_plugin_config(gchar *arg) {
  gchar config[64], item[256], value[256];
  gint n;

  n = sscanf(arg, "%s %[^\n]", config, item);
  if (n == 2) {
    if (strcmp(config, "pihole_hostname") == 0) {
      sscanf(item, "%s\n", value);
      pihole_hostname = g_strdup(value);
    }
    else if (strcmp(config, "pihole_api_key") == 0) {
      sscanf(item, "%s\n", value);
      pihole_API_key = g_strdup(value);
    }
    else if (strcmp(config, "pihole_freq") == 0) {
      sscanf(item, "%u\n", &pihole_freq);
    }
    else if (strcmp(config, "pihole_url_pattern") == 0) {
      sscanf(item, "%s\n", value);
      pihole_url_pattern = g_strdup(value);
    }
  }
  updateURL();
}

static void
apply_plugin_config() {
  if (pihole_hostname != NULL) g_free(pihole_hostname);
  pihole_hostname = g_strdup(gtk_entry_get_text(GTK_ENTRY(pihole_hostname_fillin)));
  if (pihole_API_key != NULL) g_free(pihole_API_key);
  pihole_API_key = g_strdup(gtk_entry_get_text(GTK_ENTRY(pihole_API_key_fillin)));
  pihole_freq = gtk_spin_button_get_value(GTK_SPIN_BUTTON(pihole_freq_spinner));
  if (pihole_url_pattern != NULL) g_free(pihole_url_pattern);
  pihole_url_pattern = g_strdup(gtk_entry_get_text(GTK_ENTRY(pihole_url_pattern_fillin)));
  updateURL();
  //puts(pihole_URL);
  update = -1;
  update_plugin();
}

static gchar* plugin_info_text[] = {
"<h>Pihole monitor\n",
"\n\t(c) 2023 JC <gkrellm@cardot.net>\n",
"\n\tMonitors your Pihole activity.\n",
"\n\tReleased under the GNU General Public License\n",
"\n\t", "<ul>Note", ": the default URL called on the Pihole is:\n",
"\t" PIHOLE_URL_PATTERN
};

static void
create_plugin_tab(GtkWidget *tab_vbox) {
  GtkWidget *tabs, *vbox, *table, *text, *label_hostname, *label_API_key, *label_freq, *label_url;
  gint i;

  /* Make a couple of tabs.  One for setup and one for info
  */
  tabs = gtk_notebook_new();
  gtk_notebook_set_tab_pos(GTK_NOTEBOOK(tabs), GTK_POS_TOP);
  gtk_box_pack_start(GTK_BOX(tab_vbox), tabs, TRUE, TRUE, 0);

  /* --Setup tab */
  vbox = gkrellm_gtk_framed_notebook_page(tabs, "Setup");

  /* configuration widgets */
  table = gtk_table_new(2, 2, FALSE);
    
  label_hostname = gtk_label_new("Pihole hostname:");
  gtk_misc_set_alignment (GTK_MISC (label_hostname), 1, 1);
  gtk_table_attach(GTK_TABLE(table), label_hostname,  0, 1, 0, 1, GTK_FILL, 0, 1, 1);
  pihole_hostname_fillin = gtk_entry_new_with_max_length(255);
  gtk_table_attach(GTK_TABLE(table), pihole_hostname_fillin, 1, 2, 0, 1, GTK_FILL|GTK_EXPAND, 0, 1, 1);
  if (pihole_hostname != NULL)
    gtk_entry_set_text(GTK_ENTRY(pihole_hostname_fillin), pihole_hostname);

  label_API_key = gtk_label_new("API key:");
  gtk_misc_set_alignment (GTK_MISC (label_API_key), 1, 1);
  gtk_table_attach(GTK_TABLE(table), label_API_key,  0, 1, 1, 2, GTK_FILL, 0, 1, 1);
  pihole_API_key_fillin = gtk_entry_new_with_max_length(255);
  gtk_table_attach(GTK_TABLE(table), pihole_API_key_fillin, 1, 3, 1, 2, GTK_FILL|GTK_EXPAND, 0, 1, 1);
  if (pihole_API_key != NULL)
    gtk_entry_set_text(GTK_ENTRY(pihole_API_key_fillin), pihole_API_key);

  label_freq = gtk_label_new("Refresh frequence (seconds):");
  gtk_misc_set_alignment (GTK_MISC (label_freq), 1, 1);
  gtk_table_attach(GTK_TABLE(table), label_freq,  0, 1, 2, 3, GTK_FILL, 0, 1, 1);
  pihole_freq_spinner = gtk_spin_button_new_with_range(1, 999, 1);
  gtk_table_attach(GTK_TABLE(table), pihole_freq_spinner, 1, 4, 2, 3, GTK_FILL|GTK_EXPAND, 0, 1, 1);
  if (pihole_freq > 0)
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(pihole_freq_spinner), pihole_freq);
  
  gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 2);

  /* --Advanced tab */
  vbox = gkrellm_gtk_framed_notebook_page(tabs, "Advanced");

  /* configuration widgets */
  table = gtk_table_new(2, 2, FALSE);
    
  label_url = gtk_label_new("URL pattern:");
  gtk_misc_set_alignment (GTK_MISC (label_url), 1, 1);
  gtk_table_attach(GTK_TABLE(table), label_url,  0, 1, 0, 1, GTK_FILL, 0, 1, 1);
  pihole_url_pattern_fillin = gtk_entry_new_with_max_length(255);
  gtk_table_attach(GTK_TABLE(table), pihole_url_pattern_fillin, 1, 2, 0, 1, GTK_FILL|GTK_EXPAND, 0, 1, 1);
  gtk_entry_set_text(GTK_ENTRY(pihole_url_pattern_fillin), pihole_url_pattern);

  gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 2);
  
  /* --Info tab */
  vbox = gkrellm_gtk_framed_notebook_page(tabs, "Info");
  text = gkrellm_gtk_scrolled_text_view(vbox, NULL,
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  for (i = 0; i < sizeof(plugin_info_text)/sizeof(gchar *); ++i)
    gkrellm_gtk_text_view_append(text, plugin_info_text[i]);
}

static GkrellmMonitor plugin_mon = {
  CONFIG_NAME,          /* Name, for config tab.    */
  0,                    /* Id,  0 if a plugin       */
  create_plugin,        /* The create function      */
  update_plugin,        /* The update function      */
  create_plugin_tab,    /* The config tab create function */
  apply_plugin_config,  /* Apply the config function      */

  save_plugin_config,   /* Save user config   */
  load_plugin_config,   /* Load user config   */
  CONFIG_NAME,          /* config keyword     */

  NULL,   /* Undefined 2 */
  NULL,   /* Undefined 1 */
  NULL,   /* private  */

  MON_INSERT_AFTER|MON_NET, /* Insert plugin before this monitor   */

  NULL,   /* Handle if a plugin, filled in by GKrellM     */
  NULL    /* path if a plugin, filled in by GKrellM       */
};

GkrellmMonitor*
gkrellm_init_plugin() {
  dns_queries_today = g_strdup("0");
  ads_blocked_today = g_strdup("0");
  pihole_url_pattern = g_strdup(PIHOLE_URL_PATTERN);
  style_id = gkrellm_add_meter_style(&plugin_mon, STYLE_NAME);
  monitor = &plugin_mon;
  return &plugin_mon;
}

