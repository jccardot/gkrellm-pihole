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
#include <X11/Xlib.h>
#include <pthread.h>

#include "pihole.xpm"

#define CONFIG_NAME "gkrellm-pihole"
#define STYLE_NAME  "gkrellm-pihole"

#define PIHOLE_URL_PATTERN       "http://%s/admin/api.php?%s&auth=%s"
#define PIHOLE_DEFAULT_FREQ      10

#define SPACING_BETWEEN_ROWS     4
#define SPACING_BETWEEN_COLUMNS  6

#define PIHOLE_ONLINE  0
#define PIHOLE_OFFLINE 1

static GkrellmMonitor *monitor;
static GkrellmTicks *pGK;
static GkrellmPanel *panel;
static GkrellmDecal *decal_pihole_icon;
static GkrellmDecal *decal_label1;
static GkrellmDecal *decal_text1;
static GkrellmDecal *decal_label2;
static GkrellmDecal *decal_text2;
static GdkPixmap *pihole_gdkpixmap;
static gboolean resources_acquired;
static gchar *pihole_URL, *dns_queries_today, *ads_blocked_today, *status; 
static gint style_id;
static gint update=-1;
static gboolean blocking_disabled;
static gint blocking_disabled_time;

static GtkWidget  *pihole_hostname_fillin;
static gchar      *pihole_hostname;
static GtkWidget  *pihole_API_key_fillin;
static gchar      *pihole_API_key;
static GtkWidget  *pihole_freq_spinner;
static gint        pihole_freq = PIHOLE_DEFAULT_FREQ;
static GtkWidget  *pihole_url_pattern_fillin;
static gchar      *pihole_url_pattern;

CURL *curl;

struct MemoryStruct {
  char *memory;
  size_t size;
} chunk;

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

gboolean
callURL(gchar *pihole_URL) {
  CURLcode res=0;

  //printf("calling %s\n", pihole_URL);
  
  if(!curl) {
    fprintf(stderr, "curl is not initialized\n");
    return FALSE;
  }
  
  chunk.memory = malloc(1); /* will be grown as needed by the realloc above */
  chunk.size = 0;           /* no data at this point */
  
  curl_easy_setopt(curl, CURLOPT_URL, pihole_URL);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  /* send all data to this function  */
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  /* we pass our 'chunk' struct to the callback function */
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
  /* pihole must answer quickly, else there is a problem anyway */
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 500);

  res = curl_easy_perform(curl);
  //printf("%lu bytes retrieved\n", (unsigned long)chunk.size);
  if(res != CURLE_OK || chunk.size == 0) {
    fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
    return FALSE;
  }
  
  long response_code;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
  //printf("Response code: %lu\nBody: %s\n", response_code, chunk.memory);
  if (response_code >= 400) {
    fprintf(stderr, "curl_easy_perform() response code: %lu\n", response_code);
    return FALSE;
  }
  
  /* if the api_key is incorrect, the api answers with "[]" */
  if (!strcmp(chunk.memory, "[]")) {
    puts("Incorrect API key");
    return FALSE;
  }
  
  /* the call when well */
  return TRUE;
}

int
pihole(void)
{
  if (pihole_URL == NULL || pihole_URL[0] == 0) {
    gkrellm_draw_decal_pixmap(panel, decal_pihole_icon, PIHOLE_OFFLINE);
    puts("No URL defined");
    return -1;
  }
  
  if (!callURL(pihole_URL)) {
    gkrellm_draw_decal_pixmap(panel, decal_pihole_icon, PIHOLE_OFFLINE);
    return -1;
  }

  /* find the data in chunk.memory (parse the json),
   * named "dns_queries_today", "ads_blocked_today" & "status" */
  char* mystr;

  mystr = strstr(chunk.memory, "\"dns_queries_today\"");
  if (mystr) {
    mystr += strlen("\"dns_queries_today\"") + 1;
    strtok(mystr, ","); // change ',' to '\0'
    g_free(dns_queries_today);
    dns_queries_today = g_strdup(mystr);
    mystr += strlen(mystr);
    mystr[0] = ','; // restore the ',' char
  }
  
  mystr = strstr(chunk.memory, "\"ads_blocked_today\"");
  if (mystr) {
    mystr += strlen("\"ads_blocked_today\"") + 1;
    strtok(mystr, ",");
    g_free(ads_blocked_today);
    ads_blocked_today = g_strdup(mystr);
    mystr += strlen(mystr);
    mystr[0] = ',';
  }
  
  mystr = strstr(chunk.memory, "\"status\"");
  if (mystr) {
    mystr += strlen("\"status\"") + 1;
    strtok(mystr, ",");
    g_free(status);
    status = g_strdup(mystr);
    mystr += strlen(mystr);
    mystr[0] = ',';
    if (strcmp(status, "\"enabled\""))
      gkrellm_draw_decal_pixmap(panel, decal_pihole_icon, PIHOLE_OFFLINE);
    else
      gkrellm_draw_decal_pixmap(panel, decal_pihole_icon, PIHOLE_ONLINE);
  }
  
  free(chunk.memory);
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

void open_dashboard (void) {
  gchar *cmd;
  cmd = g_strdup_printf("xdg-open http://%s", pihole_hostname);
  system(cmd);
  free(cmd);
}

// Callback function for menu items
void
on_menu_item_clicked(GtkMenuItem *item, gpointer user_data) {
  if (!strcmp((char *)user_data, "open_dashboard")) {
    open_dashboard();
  }
  else if (!strncmp((char *)user_data, "api:", strlen("api:"))) { // command to send as-is to the API
    gchar *pihole_URL = g_strdup_printf(pihole_url_pattern, pihole_hostname, (char *)user_data + strlen("api:"), pihole_API_key);
    callURL(pihole_URL);
    g_free(pihole_URL);
    if (!strncmp((char *)user_data, "api:disable", strlen("api:disable"))) {
      blocking_disabled = TRUE;
      if (strlen((char *)user_data) == strlen("api:disable"))
        blocking_disabled_time = -1; // indefinitely
      else
        blocking_disabled_time = atoi((char *)user_data + strlen("api:disable") + 1);
      //printf("blocking_disabled_time = %i\n", blocking_disabled_time);
    }
    else if (!strcmp((char *)user_data, "api:enable")) {
      blocking_disabled = FALSE;
      blocking_disabled_time = 0;
    }
  }
  else if (!strcmp((char *)user_data, "config")) {
    gkrellm_open_config_window(monitor);
  }
}

gchar
*secondsToHHMMSS(int totalSeconds) {
    int hours, minutes, seconds;
    hours = totalSeconds / 3600;
    totalSeconds %= 3600;
    minutes = totalSeconds / 60;
    seconds = totalSeconds % 60;
    return g_strdup_printf("%02d:%02d:%02d", hours, minutes, seconds);
}

static gint
panel_button_press_event(GtkWidget *widget, GdkEventButton *ev, gpointer data) {
  GtkWidget *menu;
  switch (ev->button) {
    case 1:
      menu = gtk_menu_new();

      GtkWidget *title_item1 = gtk_menu_item_new_with_label("Disable blocking indefinitely");
      g_signal_connect(G_OBJECT(title_item1), "activate", G_CALLBACK(on_menu_item_clicked), "api:disable");
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), title_item1);

      GtkWidget *menu_item11 = gtk_menu_item_new_with_label("    or for 10 seconds");
      g_signal_connect(G_OBJECT(menu_item11), "activate", G_CALLBACK(on_menu_item_clicked), "api:disable=10");
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item11);

      GtkWidget *menu_item12 = gtk_menu_item_new_with_label("    or for 30 seconds");
      g_signal_connect(G_OBJECT(menu_item12), "activate", G_CALLBACK(on_menu_item_clicked), "api:disable=30");
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item12);

      GtkWidget *menu_item13 = gtk_menu_item_new_with_label("    or 5 minutes");
      g_signal_connect(G_OBJECT(menu_item13), "activate", G_CALLBACK(on_menu_item_clicked), "api:disable=300");
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item13);

      gchar *label;
      if (blocking_disabled && blocking_disabled_time > 0) {
        gchar *hhmmss;
        hhmmss = secondsToHHMMSS(blocking_disabled_time);
        label = g_strdup_printf("Enable blocking (%s)", hhmmss);
        g_free(hhmmss);
      }
      else if (blocking_disabled && blocking_disabled_time < 0) {
        label = g_strdup("Enable blocking (disabled)");
      }
      else {
        label = g_strdup("Enable blocking");
      }
      GtkWidget *menu_item2 = gtk_menu_item_new_with_label(label);
      g_signal_connect(G_OBJECT(menu_item2), "activate", G_CALLBACK(on_menu_item_clicked), "api:enable");
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item2);
      free(label);
      
      GtkWidget *separator1 = gtk_separator_menu_item_new ();
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator1);

      GtkWidget *menu_item3 = gtk_menu_item_new_with_label("Plugin configuration");
      g_signal_connect(G_OBJECT(menu_item3), "activate", G_CALLBACK(on_menu_item_clicked), "config");
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item3);

      GtkWidget *menu_item4 = gtk_menu_item_new_with_label("Open pi-hole dashboard");
      g_signal_connect(G_OBJECT(menu_item4), "activate", G_CALLBACK(on_menu_item_clicked), "open_dashboard");
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item4);

      gtk_widget_show_all(menu);

      // Popup the menu at the event's position
      gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, ev->button, ev->time);
      break;
    case 2:
      open_dashboard();
      break;
    case 3:
      gkrellm_open_config_window(monitor);
      break;
  }
  return TRUE;
}

static void
updateURL() {
  //puts(pihole_url_pattern);
  if (pihole_hostname != NULL && pihole_API_key != NULL && pihole_url_pattern != NULL) {
    if (pihole_URL != NULL)
      g_free(pihole_URL);
    pihole_URL = g_strdup_printf(pihole_url_pattern, pihole_hostname, "summaryRaw", pihole_API_key);
  }
  //puts(pihole_URL);
}

void
*update_thread(void *vargp) {
  gint w;
  GkrellmTextstyle *ts /*, *ts_alt*/;

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

  return NULL;
}

static void
update_plugin() {
  // Do it only once every n seconds
  if (update >= 0) {
    if (pGK->second_tick) {
      update++;
      if (blocking_disabled && blocking_disabled_time > 0) {
        blocking_disabled_time--;
        if (blocking_disabled_time == 0)
          blocking_disabled = FALSE;
        //printf("blocking_disabled_time = %i\n", blocking_disabled_time);
      }
    }
    if (update < pihole_freq) return;
    update = 0;
  }
  else
    update = 0; // first time

  /* we will run the update in a thread in order not to block refreshes of the other krells */
  //static pthread_t thread_id = 0;
  size_t i=0;

  /* in case it has not finished yet, wait for the previous thread */
  /*if (thread_id != 0)
    pthread_join(thread_id, NULL);

  pthread_create(&thread_id, NULL, update_thread, &i);*/
  update_thread(&i);
}

static void
enable_plugin(void) {
  //printf("plugin is being initialized.\n");
  curl = curl_easy_init();
  updateURL();
  /*
  // Initialize the XCB threading system
  if (!XInitThreads()) {
      fprintf(stderr, "Error: XInitThreads failed.\n");
      return;
  }
  */
  resources_acquired = TRUE;
}

static void
disable_plugin(void) {
  //printf("plugin is being disabled.\n");
  curl_easy_cleanup(curl);
  resources_acquired = FALSE;
}

static void
create_plugin(GtkWidget *vbox, gint first_create) {
  GkrellmStyle   *style;
  GkrellmTextstyle  *ts, *ts_alt;
  GdkBitmap  *mask;
  int y;
  gint w,h;

  if (!resources_acquired) {
    enable_plugin();
    gkrellm_disable_plugin_connect(monitor, disable_plugin);
  }

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

  if (first_create) {
    g_signal_connect(G_OBJECT (panel->drawing_area), "expose_event",
                     G_CALLBACK (panel_expose_event), NULL);
    g_signal_connect(G_OBJECT (panel->drawing_area), "button_press_event",
                     G_CALLBACK (panel_button_press_event), NULL );
  }
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

static void
load_plugin_config(gchar *arg) {
  gchar config[64], item[256], value[256];
  gint n;

  //printf("load_plugin_config(%s)\n", arg);
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
    //updateURL();
  }
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
  update = -1;
  update_plugin();
}

static gchar* plugin_info_text[] = {
"<h>Pi-hole monitor\n",
"\n\t(c) 2023 JC <gkrellm@cardot.net>\n",
"\n\tMonitors your Pihole activity.\n",
"\n\tThe menu (on click) allows to trigger actions on your Pi-hole,",
"\n\tsuch as disabling it for a given time or indefinitely, enabling it,",
"\n\tor opening the dashboard in your browser.\n",
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
  table = gtk_table_new(3, 2, FALSE);
    
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
  table = gtk_table_new(1, 2, FALSE);
    
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
  pGK = gkrellm_ticks();
  dns_queries_today = g_strdup("--");
  ads_blocked_today = g_strdup("--");
  pihole_url_pattern = g_strdup(PIHOLE_URL_PATTERN);
  style_id = gkrellm_add_meter_style(&plugin_mon, STYLE_NAME);
  monitor = &plugin_mon;
  return &plugin_mon;
}

