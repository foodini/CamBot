#include "map_zoom.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
int main(int argc, char** argv) {
    FILE* f = fopen(argv[1], "r"); char line[4096];
    MapZoomSettings s; if (argc > 2) s.min_width_km = (float)atof(argv[2]); if (argc > 3) s.max_width_km = (float)atof(argv[3]); if (argc > 4) s.transition_seconds = (float)atof(argv[4]);
    MapZoomTracker t(s, 10.0f);
    int i = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] != '{') continue;
        auto num = [&](const char* key, float def) { char* p = strstr(line, key); return p ? (float)atof(p + strlen(key)) : def; };
        float lat = num("\"gps_lat\":", 0), lon = num("\"gps_lon\":", 0), kts = num("\"gps_kts\":", 0), dir = num("\"gps_dir\":", 0), alt = num("\"ctr_alt_m\":", 0);
        if (strstr(line, "\"gps_lon_dir\":\"W\"")) lon = -lon;
        if (strstr(line, "\"gps_lat_dir\":\"S\"")) lat = -lat;
        float w = t.update(lat, lon, kts, dir);
        printf("%d %.1f %d %.0f %.1f\n", i, w, t.circling() ? 1 : 0, alt, kts);
        i++;
    }
}
