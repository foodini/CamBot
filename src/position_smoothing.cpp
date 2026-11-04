#include "position_smoothing.h"

#include <math.h>

namespace position_smoothing {

bool has_fix(double latitude_degrees, double longitude_degrees) {
	return !(latitude_degrees == 0.0 && longitude_degrees == 0.0) &&
		fabs(latitude_degrees) <= 90.0 && fabs(longitude_degrees) <= 180.0;
}

void smooth(const std::vector<double>& latitude, const std::vector<double>& longitude, int half_window,
		std::vector<double>* smooth_latitude, std::vector<double>* smooth_longitude) {
	const int count = (int)(latitude.size() < longitude.size() ? latitude.size() : longitude.size());
	smooth_latitude->assign(latitude.begin(), latitude.begin() + count);
	smooth_longitude->assign(longitude.begin(), longitude.begin() + count);
	for (int i = 0; i < count; i++) {
		if (!has_fix(latitude[i], longitude[i]))
			continue;
		int reach = half_window;
		if (reach > i)
			reach = i;
		if (reach > count - 1 - i)
			reach = count - 1 - i;
		if (reach <= 0)
			continue;
		double total_weight = 0.0;
		double lat_sum = 0.0;
		double lon_sum = 0.0;
		for (int j = i - reach; j <= i + reach; j++) {
			if (!has_fix(latitude[j], longitude[j]))
				continue;
			const double weight = (double)(reach + 1 - (j > i ? j - i : i - j));
			total_weight += weight;
			lat_sum += weight * latitude[j];
			lon_sum += weight * longitude[j];
		}
		(*smooth_latitude)[i] = lat_sum / total_weight;
		(*smooth_longitude)[i] = lon_sum / total_weight;
	}
}

}  // namespace position_smoothing
