#include <cstdio>
#include <cmath>
#include <vector>
#include "altitude_reference.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static bool near(float a, float b, float eps = 1e-4f) { return fabsf(a - b) < eps; }
int main() {
    AltitudeReferences r;
    CHECK(!r.has_start() && !r.has_end());
    r.start_ft = 213.0f;
    CHECK(r.has_start() && !r.has_end());
    r.end_same_as_start = true;
    CHECK(r.has_end() && near(r.end_m(), r.start_m()));
    r.end_same_as_start = false; r.end_ft = 200.0f;
    CHECK(r.has_end() && near(r.end_ft, 200.0f));
    r.start_ft = 0.0f;  // sea level is a legitimate altitude
    CHECK(r.has_start());
    r.start_ft = -50.0f; // below sea level too
    CHECK(r.has_start());

    AltitudeOffsetModel m;
    CHECK(!m.active() && near(m.offset_at(5), 0));
    m.set_start(100, 2.0f);
    CHECK(near(m.offset_at(0), 2) && near(m.offset_at(9999), 2));
    m.set_end(1100, 4.0f);
    CHECK(near(m.offset_at(0), 2));        // held before start
    CHECK(near(m.offset_at(100), 2));
    CHECK(near(m.offset_at(600), 3));      // halfway
    CHECK(near(m.offset_at(1100), 4));
    CHECK(near(m.offset_at(5000), 4));     // held after end
    AltitudeOffsetModel same; same.set_start(50, 1); same.set_end(50, 3);
    CHECK(near(same.offset_at(10), 1));    // degenerate: no divide by zero
    AltitudeOffsetModel endonly; endonly.set_end(10, 7);
    CHECK(near(endonly.offset_at(0), 7));
    m.clear(); CHECK(!m.active());

    AltitudeCorrectionInfo info; info.has_start = info.has_end = true; info.start_offset_m = 1.0f; info.end_offset_m = 3.5f;
    CHECK(near(info.drift_m(), 2.5f));
    info.has_end = false; CHECK(near(info.drift_m(), 0));
    printf(fails ? "FAILED\n" : "all ok\n");
    return fails;
}
