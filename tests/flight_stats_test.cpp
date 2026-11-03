#include <cstdio>
#include <cmath>
#include <vector>
#include "flight_stats.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static bool near(float a, float b, float eps) { return fabsf(a - b) < eps; }

struct Sim {
    std::vector<FlightSample> s; float x = 0, y = 0, alt = 100, heading = 0, speed_kts = 20, dist_km = 0, climb_mps = 0;
    float lat0 = 37.6f, lon0 = -122.5f;
    void step(float turn_dps, float seconds, float climb) {
        int n = (int)(seconds * 10);
        for (int i = 0; i < n; i++) {
            heading += turn_dps / 10.0f; while (heading >= 360) heading -= 360; while (heading < 0) heading += 360;
            float v = speed_kts * 0.514444f;
            x += v * 0.1f * sinf(heading * 3.14159265f / 180); y += v * 0.1f * cosf(heading * 3.14159265f / 180);
            alt += climb * 0.1f; dist_km += v * 0.1f / 1000.0f;
            FlightSample f;
            f.lat_deg = lat0 + y / 111195.0f; f.lon_deg = lon0 + x / (111195.0f * cosf(lat0 * 3.14159265f / 180));
            f.alt_m = alt; f.climb_fpm = climb * 196.85f; f.speed_kts = speed_kts; f.course_deg = heading; f.total_distance_km = dist_km;
            s.push_back(f);
        }
    }
};
int main() {
    // 1: straight glide at 1 m/s sink, 10.29 m/s ground speed -> ~10.3:1
    { Sim a; a.step(0, 120, -1.0f);
      FlightSummary r = flight_stats::compute(a.s, 0, 10, true);
      CHECK(r.valid && r.has_glide && near(r.best_glide_ratio, 10.29f, 0.3f));
      CHECK(r.circle_count == 0 && near(r.circling_seconds, 0, 0.01f));
      CHECK(near(r.distance_flown_mi, 120 * 10.29f / 1609.34f, 0.02f));
      CHECK(near(r.distance_covered_mi, r.distance_flown_mi, 0.02f));
      CHECK(near(r.min_alt_m, 100 - 120, 0.2f) && near(r.max_alt_m, 99.9f, 0.2f));
      CHECK(near(r.max_sink_fpm, -196.85f, 0.5f)); }
    // 2: thermalling: 30s straight, 4 circles at 18 deg/s climbing 2 m/s, glide away; plus a ridge-end turn pair that must not count
    { Sim a; a.step(0, 30, 0);
      a.step(18, 80, 2.0f);        // 1440 deg = 4 circles, 80 s
      a.step(0, 30, -1.0f);
      a.step(22, 8.2f, 0);         // ~180 deg
      a.step(-22, 8.2f, 0);        // ~180 deg back the other way
      a.step(0, 20, 0);
      a.step(-15, 36, 1.0f);       // 540 deg = 1 circle (floor), 1.5 turns
      a.step(0, 20, 0);
      FlightSummary r = flight_stats::compute(a.s, 0, 10, true);
      printf("circles=%d time=%.1f glide=%.2f maxh=%.1f\n", r.circle_count, r.circling_seconds, r.best_glide_ratio, r.max_height_above_launch_m);
      CHECK(r.circle_count == 5);   // 4 + 1
      CHECK(near(r.circling_seconds, 80 + 36, 6.0f));
      CHECK(near(r.max_height_above_launch_m, 160 + 0 - 30 + 36 - 0, 40.0f) || r.max_height_above_launch_m > 100);
      CHECK(r.max_climb_fpm > 390 && r.max_climb_fpm < 400); }
    // 3: figure-eight weaving back and forth each less than a circle: nothing counted
    { Sim a; for (int k = 0; k < 10; k++) { a.step(20, 12, 0); a.step(-20, 12, 0); }
      FlightSummary r = flight_stats::compute(a.s, 0, 10, true);
      CHECK(r.circle_count == 0); }
    // 4: not moving (ground): course garbage must not count
    { Sim a; a.speed_kts = 0; a.step(50, 60, 0);
      FlightSummary r = flight_stats::compute(a.s, 0, 10, true);
      CHECK(r.circle_count == 0 && !r.has_glide); }
    // 5: launch offset: pre-launch samples don't count toward distance or circles
    { Sim a; a.step(0, 10, 0); size_t launch = a.s.size(); a.step(0, 60, -1.0f);
      FlightSummary r = flight_stats::compute(a.s, (int)launch, 10, false);
      CHECK(near(r.distance_flown_mi, 60 * 10.29f / 1609.34f, 0.02f));
      CHECK(!r.has_glide && r.circle_count == 0);  // XC off: not computed
      CHECK(near(r.launch_alt_m, 100.0f, 0.5f)); CHECK(near(r.max_height_above_launch_m, 0, 0.5f)); }
    // 6: empty and tiny inputs don't crash
    { std::vector<FlightSample> e; CHECK(!flight_stats::compute(e, 0, 10, true).valid);
      Sim a; a.step(0, 0.2f, 0); FlightSummary r = flight_stats::compute(a.s, 5, 10, true); CHECK(r.valid); }
    printf(fails ? "FAILED\n" : "all ok\n");
    return fails;
}
