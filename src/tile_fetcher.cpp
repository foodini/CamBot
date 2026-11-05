#include "tile_fetcher.h"

#include <chrono>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <time.h>

#include "file_io.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace {

// Who is asking. The tile servers' usage policies want a name for the program that is distinct from a
// browser's, and somewhere to read about it.
const char* USER_AGENT = "CamBot (https://github.com/foodini/cambot)";

const size_t MAX_PICTURE_BYTES = 4 * 1024 * 1024;   // Tiles are a few tens of kilobytes; anything near this is not one.
const int    TRIM_AFTER_WRITES = 300;

double clock_seconds() {
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	return std::chrono::duration<double>(now.time_since_epoch()).count();
}

bool looks_like_picture(const std::string& bytes) {
	if (bytes.size() >= 8 && memcmp(bytes.data(), "\x89PNG\r\n\x1a\n", 8) == 0)
		return true;
	if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xD8 && (unsigned char)bytes[2] == 0xFF)
		return true;
	return false;
}

#ifdef _WIN32
std::wstring widen(const std::string& ascii_or_utf8) {
	if (ascii_or_utf8.empty())
		return std::wstring();
	const int count = MultiByteToWideChar(CP_UTF8, 0, ascii_or_utf8.c_str(), (int)ascii_or_utf8.size(), NULL, 0);
	std::wstring wide(count, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, ascii_or_utf8.c_str(), (int)ascii_or_utf8.size(), &wide[0], count);
	return wide;
}

std::string narrow(const std::wstring& wide) {
	if (wide.empty())
		return std::string();
	const int count = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), NULL, 0, NULL, NULL);
	std::string text(count, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), &text[0], count, NULL, NULL);
	return text;
}
#endif

}  // namespace

TileFetcher::TileFetcher(const std::string& cache_root, int64_t cache_limit_bytes)
	: m_cache_root(cache_root), m_cache_limit(cache_limit_bytes), m_mutex(), m_wake(), m_queue(), m_results(), m_in_flight(), m_stop(false),
	  m_started(false), m_trimming(false), m_writes_since_trim(0), m_session(nullptr) {
	for (int i = 0; i < MAX_WORKERS; i++)
		m_active_request[i] = nullptr;
}

TileFetcher::~TileFetcher() {
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop = true;
	}
	m_wake.notify_all();
	// A request in the middle of waiting for the network is cut short, so closing the program doesn't wait on it.
	abandon_requests();
	for (int i = 0; i < MAX_WORKERS; i++) {
		if (m_threads[i].joinable())
			m_threads[i].join();
	}
#ifdef _WIN32
	if (m_session != nullptr)
		WinHttpCloseHandle((HINTERNET)m_session);
#endif
}

void TileFetcher::start() {
	if (m_started)
		return;
	m_started = true;
#ifdef _WIN32
	HINTERNET session = WinHttpOpen(widen(USER_AGENT).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (session == NULL) {
		printf("Map tiles: WinHttpOpen failed (error %lu)\n", (unsigned long)GetLastError());
	} else {
		// Resolve, connect, send, receive (milliseconds).
		// (Some servers draw a picture when it is asked for, which can take a good while; it is waited for, and how
		// long it took is printed, so the wait can be judged.)
		WinHttpSetTimeouts(session, 8000, 8000, 10000, 45000);
		m_session = session;
	}
#endif
	for (int i = 0; i < MAX_WORKERS; i++)
		m_threads[i] = std::thread(&TileFetcher::worker_main, this, i);
}

void TileFetcher::begin_frame() {
	std::lock_guard<std::mutex> lock(m_mutex);
	for (size_t i = 0; i < m_queue.size(); i++)
		m_queue[i].wanted = false;
}

void TileFetcher::want(const TileRequest& request) {
	start();
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (size_t i = 0; i < m_in_flight.size(); i++) {
			if (m_in_flight[i].style == request.style && m_in_flight[i].key == request.key)
				return;   // Already out; its answer is on the way.
		}
		for (size_t i = 0; i < m_queue.size(); i++) {
			if (m_queue[i].request.style == request.style && m_queue[i].request.key == request.key) {
				m_queue[i].wanted = true;
				return;
			}
		}
		Queued queued;
		queued.request = request;
		queued.wanted = true;
		queued.queued_at = clock_seconds();
		m_queue.push_back(queued);
	}
	m_wake.notify_one();
}

void TileFetcher::end_frame(std::vector<std::pair<int, map_tiles::TileKey> >* dropped) {
	std::lock_guard<std::mutex> lock(m_mutex);
	size_t kept = 0;
	for (size_t i = 0; i < m_queue.size(); i++) {
		if (m_queue[i].wanted) {
			if (kept != i)
				m_queue[kept] = m_queue[i];
			kept++;
		} else if (dropped != nullptr) {
			dropped->push_back(std::make_pair(m_queue[i].request.style, m_queue[i].request.key));
		}
	}
	m_queue.resize(kept);
}

bool TileFetcher::next_result(TileResult* result) {
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_results.empty())
		return false;
	*result = m_results.front();
	m_results.erase(m_results.begin());
	return true;
}

int TileFetcher::busy_count() {
	std::lock_guard<std::mutex> lock(m_mutex);
	return (int)m_queue.size() + (int)m_in_flight.size();
}

bool TileFetcher::is_busy(int style, const map_tiles::TileKey& key) {
	std::lock_guard<std::mutex> lock(m_mutex);
	for (size_t i = 0; i < m_in_flight.size(); i++) {
		if (m_in_flight[i].style == style && m_in_flight[i].key == key)
			return true;
	}
	for (size_t i = 0; i < m_queue.size(); i++) {
		if (m_queue[i].request.style == style && m_queue[i].request.key == key)
			return true;
	}
	return false;
}

int TileFetcher::next_to_start(double now) {
	// The first in line whose source has room: a source's limit is its base number of requests plus one for each
	// of those out that has been slow, so the ones held back by a full source don't hold back those of another.
	for (size_t i = 0; i < m_queue.size(); i++) {
		const int style = m_queue[i].request.style;
		int out = 0;
		int stalled = 0;
		for (size_t j = 0; j < m_in_flight.size(); j++) {
			if (m_in_flight[j].style != style)
				continue;
			out++;
			if (now - m_in_flight[j].started_at > map_tiles::STALL_SECONDS)
				stalled++;
		}
		if (out < map_tiles::requests_allowed(map_tiles::source(style), stalled))
			return (int)i;
	}
	return -1;
}

void TileFetcher::worker_main(int index) {
	// One of the workers sees to the cache's size before it starts on tiles, so a cache that has grown
	// past its limit (or been filled by an older version) is brought back down.
	if (index == MAX_WORKERS - 1)
		trim_cache();
	while (true) {
		TileRequest request;
		double queued_at = 0.0;
		int out_at_start = 0;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			int at = -1;
			while (true) {
				if (m_stop)
					return;
				at = next_to_start(clock_seconds());
				if (at >= 0)
					break;
				if (m_queue.empty())
					m_wake.wait(lock);
				else
					m_wake.wait_for(lock, std::chrono::milliseconds(250));   // Requests are held back: look again, as a slow one may have opened more room.
			}
			request = m_queue[at].request;
			queued_at = m_queue[at].queued_at;
			m_queue.erase(m_queue.begin() + at);
			InFlight started;
			started.style = request.style;
			started.key = request.key;
			started.started_at = clock_seconds();
			m_in_flight.push_back(started);
			for (size_t i = 0; i < m_in_flight.size(); i++) {
				if (m_in_flight[i].style == request.style)
					out_at_start++;
			}
		}
		const double started_at = clock_seconds();
		TileResult result = fetch(index, request, queued_at);
		result.total_seconds = clock_seconds() - started_at;
		result.out_at_start = out_at_start;
		bool trim = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_results.push_back(result);
			for (size_t i = 0; i < m_in_flight.size(); i++) {
				if (m_in_flight[i].style == request.style && m_in_flight[i].key == request.key) {
					m_in_flight.erase(m_in_flight.begin() + i);
					break;
				}
			}
			if (result.outcome == TileResult::LOADED && ++m_writes_since_trim >= TRIM_AFTER_WRITES) {
				m_writes_since_trim = 0;
				trim = true;
			}
		}
		if (trim)
			trim_cache();
	}
}

void TileFetcher::abandon_requests() {
#ifdef _WIN32
	std::lock_guard<std::mutex> lock(m_mutex);
	for (int i = 0; i < MAX_WORKERS; i++) {
		if (m_active_request[i] != nullptr) {
			WinHttpCloseHandle((HINTERNET)m_active_request[i]);
			m_active_request[i] = nullptr;
		}
	}
#endif
}

void TileFetcher::trim_cache() {
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_trimming)
			return;
		m_trimming = true;
	}
	std::vector<fileio::FileEntry> files;
	fileio::list_files(m_cache_root, &files);
	// A tile is its picture (.png) and the details kept with it (.meta): the pair is one cache file.
	std::vector<std::string>        stems;
	std::vector<map_tiles::CacheFile> pictures;
	std::vector<std::string>        stray;
	for (size_t i = 0; i < files.size(); i++) {
		const std::string& path = files[i].path;
		if (path.size() > 4 && path.compare(path.size() - 4, 4, ".png") == 0) {
			map_tiles::CacheFile file;
			file.bytes = files[i].bytes;
			file.modified = files[i].modified;
			stems.push_back(path.substr(0, path.size() - 4));
			pictures.push_back(file);
		}
	}
	for (size_t i = 0; i < files.size(); i++) {
		const std::string& path = files[i].path;
		if (path.size() > 5 && path.compare(path.size() - 5, 5, ".meta") == 0) {
			const std::string stem = path.substr(0, path.size() - 5);
			bool found = false;
			for (size_t j = 0; j < stems.size() && !found; j++) {
				if (stems[j] == stem) {
					pictures[j].bytes += files[i].bytes;
					found = true;
				}
			}
			if (!found)
				stray.push_back(path);   // Details of a picture that is gone.
		}
	}
	for (size_t i = 0; i < stray.size(); i++)
		fileio::delete_file(stray[i]);
	const std::vector<size_t> evictions = map_tiles::choose_evictions(pictures, m_cache_limit);
	if (!evictions.empty())
		printf("Map tiles: the cache is over %d MB, deleting the %d oldest tiles\n", (int)(m_cache_limit / (1024 * 1024)), (int)evictions.size());
	for (size_t i = 0; i < evictions.size(); i++) {
		fileio::delete_file(stems[evictions[i]] + ".png");
		fileio::delete_file(stems[evictions[i]] + ".meta");
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	m_trimming = false;
}

TileResult TileFetcher::fetch(int worker, const TileRequest& request, double queued_at) {
	TileResult result;
	const double started_at = clock_seconds();
	result.wait_seconds = started_at - queued_at;
	result.style = request.style;
	result.key = request.key;
	result.conditional = !request.etag.empty();
	result.deep = request.deep;
#ifdef _WIN32
	map_tiles::UrlParts parts;
	if (m_session == nullptr || !map_tiles::split_url(request.url, &parts))
		return result;
	HINTERNET connection = WinHttpConnect((HINTERNET)m_session, widen(parts.host).c_str(), (INTERNET_PORT)parts.port, 0);
	if (connection == NULL) {
		result.error_code = GetLastError();
		return result;
	}
	HINTERNET http = WinHttpOpenRequest(connection, L"GET", widen(parts.path).c_str(), NULL, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, parts.secure ? WINHTTP_FLAG_SECURE : 0);
	if (http == NULL) {
		result.error_code = GetLastError();
		WinHttpCloseHandle(connection);
		return result;
	}
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_stop) {
			WinHttpCloseHandle(http);
			WinHttpCloseHandle(connection);
			return result;
		}
		m_active_request[worker] = http;
	}

	std::wstring headers = L"Accept: image/png,image/jpeg,image/*;q=0.8\r\n";
	if (!request.etag.empty())
		headers += L"If-None-Match: " + widen(request.etag) + L"\r\n";
	bool ok = WinHttpSendRequest(http, headers.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE &&
		WinHttpReceiveResponse(http, NULL) != FALSE;
	result.reply_seconds = clock_seconds() - started_at;
	if (!ok)
		result.error_code = GetLastError();

	std::string raw_headers;
	std::string picture;
	if (ok) {
		DWORD status = 0;
		DWORD size = sizeof(status);
		if (WinHttpQueryHeaders(http, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
				&status, &size, WINHTTP_NO_HEADER_INDEX))
			result.http_status = (int)status;
		DWORD header_bytes = 0;
		WinHttpQueryHeaders(http, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER,
			&header_bytes, WINHTTP_NO_HEADER_INDEX);
		if (header_bytes > 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
			std::wstring wide((header_bytes / sizeof(wchar_t)) + 1, L'\0');
			if (WinHttpQueryHeaders(http, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, &wide[0],
					&header_bytes, WINHTTP_NO_HEADER_INDEX))
				raw_headers = narrow(wide.c_str());
		}
		if (result.http_status == 200) {
			while (true) {
				DWORD available = 0;
				if (!WinHttpQueryDataAvailable(http, &available)) {
					result.error_code = GetLastError();
					ok = false;
					break;
				}
				if (available == 0)
					break;
				if (picture.size() + available > MAX_PICTURE_BYTES) {
					ok = false;
					break;
				}
				const size_t before = picture.size();
				picture.resize(before + available);
				DWORD got = 0;
				if (!WinHttpReadData(http, &picture[before], available, &got)) {
					result.error_code = GetLastError();
					ok = false;
					break;
				}
				picture.resize(before + got);
			}
		}
	}

	// Done with the request: close it, unless shutting down already did.
	bool close_http = true;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_active_request[worker] == nullptr)
			close_http = false;   // abandon_requests() closed it.
		m_active_request[worker] = nullptr;
	}
	if (close_http)
		WinHttpCloseHandle(http);
	WinHttpCloseHandle(connection);

	if (!ok)
		return result;   // FAILED.
	const int status = result.http_status;
	const int64_t now = (int64_t)time(NULL);
	if (status == 200) {
		if (!looks_like_picture(picture)) {
			result.outcome = TileResult::MISSING;   // The server's own "no tile here" page, say.
			return result;
		}
		result.meta.etag = map_tiles::find_header(raw_headers, "ETag");
		result.meta.fetched = now;
		fileio::make_directories(fileio::directory_of(request.picture_path));
		std::string error;
		// The picture first, then the details: a picture with no details is trusted (see TileLayer), details with no picture are junk.
		if (!fileio::write_text_atomic(request.picture_path, picture, &error) ||
				!fileio::write_text_atomic(request.meta_path, map_tiles::serialize_meta(result.meta), &error))
			printf("Map tiles: could not keep %d/%d/%d: %s\n", request.key.z, request.key.x, request.key.y, error.c_str());
		result.picture.swap(picture);
		result.outcome = TileResult::LOADED;
	} else if (status == 304 && result.conditional) {
		result.meta.etag = request.etag;
		result.meta.fetched = now;
		std::string error;
		fileio::write_text_atomic(request.meta_path, map_tiles::serialize_meta(result.meta), &error);
		result.outcome = TileResult::UNCHANGED;
	} else if (status == 404 || status == 410 || status == 400) {
		result.outcome = TileResult::MISSING;
	}
	// Anything else (403 when a server has blocked us, 429, 5xx, ...) stays FAILED: it is waited out, not asked again at once.
	return result;
#else
	(void)worker;
	(void)looks_like_picture;
	(void)MAX_PICTURE_BYTES;
	(void)USER_AGENT;
	// Off Windows there is no network, so the tests stand in for one: a request takes a moment to fail, the
	// way a real one takes a moment to answer, so asking for a tile again right away finds it still out.
	std::this_thread::sleep_for(std::chrono::milliseconds(30));
	return result;
#endif
}
