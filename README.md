# Monday Night Net

A check-in log for [RACES/ARES Mountain
View](https://www.k6mtv.org/monday-net)'s Monday Night net, built with GTK 4 and
libadwaita.

As the net runs, I mark the stations I can hear directly, or those that need a relay. A pin pops up on the map based on the FCC license data; the stations needing a relay are in yellow. This helps me understand how well my antenna can hear throughout the city.

If you'd like a Flatpak, you can click this button:

<a href='https://flatpak.39music.dance/repo/stable/appstream/radio.ki6kvz.MondayNightNet.flatpakref'><img height='51' alt='Download on 39music' src='https://flatpak.39music.dance/repo/stable/appstream/39music-badge.png'/></a>

Otherwise, to build it yourself you'll need gtk4, libadwaita, libshumate,
libsoup 3 and a C++26 compiler (GCC 15 or newer). Meson fetches the rest
(peel, gom, libdex and friends) as subprojects.

```
meson setup build
ninja -C build
meson test -C build
meson devenv -C build src/monday-night-net
```
