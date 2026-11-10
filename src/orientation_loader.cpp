#include "orientation_loader.h"
#include "dji_orientation.h"

OrientationLoader::OrientationLoader() : m_loading(false), m_loading_media(-1), m_seconds_per_tick(0.0), m_taken(0) {
}

void OrientationLoader::clear() {
	m_packets.stop();
	m_finished.clear();
	m_loading = false;
	m_loading_media = -1;
	m_taken = 0;
	m_building.clear();
}

void OrientationLoader::request(int media, const std::string& path, double seconds_per_tick, double start_seconds) {
	if (m_finished.find(media) != m_finished.end() || (m_loading && m_loading_media == media))
		return;
	if (seconds_per_tick <= 0.0)
		return;
	m_packets.start(path, camera_metadata::STREAM_DJMD, start_seconds);
	m_loading = true;
	m_loading_media = media;
	m_seconds_per_tick = seconds_per_tick;
	m_taken = 0;
	m_building.clear();
}

void OrientationLoader::update() {
	if (!m_loading || m_packets.running())
		return;
	// The packets are all in. A file with no djmd stream (or nothing readable) is finished with nothing.
	const size_t total = m_packets.count();
	const size_t CHUNK = 4000;
	size_t until = m_taken + CHUNK < total ? m_taken + CHUNK : total;
	for (; m_taken < until; m_taken++) {
		const CameraPacket* packet = m_packets.at(m_packets.index_at_position(m_taken));
		tracking::Quat orientation;
		if (packet != nullptr && dji_orientation::from_message((const uint8_t*)packet->bytes.data(), packet->bytes.size(), &orientation))
			m_building.add((int64_t)(packet->time / m_seconds_per_tick + 0.5), orientation);
	}
	if (m_taken < total)
		return;
	m_finished[m_loading_media].orientations = m_building;
	m_building.clear();
	m_packets.stop();   // The packets can be big; what was wanted from them is here.
	m_loading = false;
	m_loading_media = -1;
}

const tracking::CameraOrientations& OrientationLoader::orientations(int media) const {
	std::map<int, Finished>::const_iterator found = m_finished.find(media);
	return found != m_finished.end() ? found->second.orientations : m_none;
}

bool OrientationLoader::has_orientations(int media) const {
	std::map<int, Finished>::const_iterator found = m_finished.find(media);
	return found != m_finished.end() && !found->second.orientations.empty();
}

float OrientationLoader::progress() const {
	if (!m_loading)
		return 1.0f;
	if (m_packets.running())
		return 0.8f * m_packets.progress();
	const size_t total = m_packets.count();
	return total > 0 ? 0.8f + 0.2f * (float)m_taken / (float)total : 1.0f;
}
