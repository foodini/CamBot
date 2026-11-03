#include "climb_filter.h"
#include <cstdio>
#include <random>
#include <vector>
int main() {
    const float hz = 10.0f, sigma = 0.12f;
    std::mt19937 rng(1);
    std::normal_distribution<float> nd(0.0f, sigma);
    // 1) noise on a constant-rate climb (2 m/s): measure std of output after warm-up.
    struct Case { int type; float p; } cases[] = {
        {0,0.5f},{0,1},{0,2},{0,3},{0,5},{0,10},{1,0.25f},{1,0.5f},{1,1},{1,1.5f},{1,2.5f},{1,5}};
    printf("type param  measured_noise_fpm  formula_fpm  | measured_lag_s formula_lag_s\n");
    for (Case c : cases) {
        ClimbFilterSettings s; s.type = c.type; s.window_seconds = c.p; s.time_constant_seconds = c.p;
        ClimbFilter f; f.configure(s, hz);
        double sum = 0, sum2 = 0; int cnt = 0;
        const int N = 200000;
        for (int i = 0; i < N; i++) {
            float alt = 1000.0f + nd(rng);
            float r = f.update(alt, true);
            if (i > 3000) { sum += r; sum2 += (r) * (r); cnt++; }
        }
        double mean = sum / cnt, sd = sqrt(sum2 / cnt - mean * mean);
        // 2) lag: noiseless step in rate from 0 to 1 m/s at t=0 (altitude ramp kink). Find the
        //    lag as the time the output's integral deficit corresponds to: lag = integral(1 - out) dt.
        ClimbFilter g; g.configure(s, hz);
        for (int i = 0; i < 2000; i++) g.update(100.0f, true);
        double deficit = 0;
        for (int i = 0; i < 4000; i++) {
            float alt = 100.0f + 1.0f * (i + 1) / hz;
            float r = g.update(alt, true);
            deficit += (1.0 - r) / hz;
        }
        printf("%d  %5.2f   %8.2f           %8.2f     | %8.2f        %8.2f\n", c.type, c.p, sd * 196.85,
            climb_filter_expected_noise_mps(c.type, c.p, sigma, hz) * 196.85,
            deficit, climb_filter_expected_lag_seconds(c.type, c.p, hz));
    }
    // 3) invalid handling
    ClimbFilter h; ClimbFilterSettings s; h.configure(s, hz);
    printf("invalid -> %f\n", h.update(99999.9f, false));
}
