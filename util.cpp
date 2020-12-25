#include <chrono>
#include <thread>

#include "stdio.h"

#include "util.h"

float ffsw::elapsed() {
	return (float)glfwGetTime();
}

char* ffsw::make_time(char* buf, float t, bool decimal) {
	int32_t divmod = t * 10;
	int32_t tenths = divmod % 10;
	divmod /= 10;
	int32_t sec = abs(divmod % 60);
	divmod /= 60;
	int32_t min = abs(divmod % 60);
	divmod /= 60;
	int32_t hours = abs(divmod);
	if (decimal)
		sprintf(buf, "%02d:%02d:%02d.%d", hours, min, sec, tenths);
	else
		sprintf(buf, "%02d:%02d:%02d", hours, min, sec);

	return buf;
}

void ffsw::sleep(uint32_t milliseconds) {
	std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}