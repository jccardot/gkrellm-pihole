# gkrellm-pihole
gkrellm plugin for pihole monitoring

This gkrellm plugin sits under your network chart and monitors you Pihole:
total number of DNS queries and numbers of blocked queries in the last 24 hours.

*To compile*:
./build

*To use*:
copy gkrellm-pihole.so to your plusing directory (usually ~/.gkrellm2/plugins) and activate from the configuration.

Then you have to configure the Pihole hostname or IP, and the API key, in the plugin configuration tab.
You can check stdout for messages if the plugin cannot contact the Pihole.

When the Pihole is online and the plugin properly configured, the Pihole icon show in colours:

![pihole online](docs/gkrellm-pihole-online.png)

If gkrellm is not properly configured, or the Pihole is offline, the icon shows in black and white:

![pihole offline](docs/gkrellm-pihole-offline.png)

Clicking on the plugin makes a menu appear, from which you can disable blocking, or enable it. When blocking is disabled, the pi-hole icon is greyed out.

![pihole menu](docs/gkrellm-pihole-menu.png)

The "Enable" menu entry shows the remaining time for which the pi-hole blocking is disabled (when applicable).

You can force the refresh of the plugin using the "Update display" menu, or simply by middle-clicking on the plugin.

Right click on the plugin shows the plugin configuration panel, as is customary for gkrellm.

