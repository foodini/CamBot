#pragma once

// Gets map tiles from the web (and keeps them on the disk) on background threads, so the program
// never waits for the network. The main thread says which tiles it wants this frame (want), takes
// what has arrived (next_result), and anything it stopped wanting that hasn't been started is
// dropped. There are no callbacks: the main thread asks, every frame.
//
// Only a couple of requests to a server are out at a time, a polite number for a free service
// (OpenStreetMap asks for no more than two, and gets exactly that). A server that is slow to answer
// some of them is allowed one more for each slow one (see map_tiles::requests_allowed), up to its own
// limit, so a few stragglers don't hold up the rest. The requests go through WinHTTP, which is part of Windows (and uses the system's proxy settings). Every request
// says who is asking (a User-Agent naming CamBot and its home page), as the tile servers' usage
// policies ask.
//
// A tile that comes back is written to the disk cache here, on the worker thread, with its ETag and
// the time (see map_tiles.h's TileMeta); the picture itself comes back in the result for the main
// thread to turn into a texture. The cache is kept under a size limit, oldest tiles deleted first.
//
// Off Windows there is no network code: every request fails, which is good enough for the tests.

#include <condition_variable>
#include <mutex>
#include <stdint.h>
#include <string>
#include <thread>
#include <vector>

#include "map_tiles.h"

struct TileRequest {
	int                 style;        // map_tiles::Style: which source it is from.
	map_tiles::TileKey  key;
	std::string         url;
	std::string         etag;         // Not empty: ask only whether the tile has changed from that version.
	bool                deep;         // The address is the source's deep_url_template one (see map_tiles.h).
	std::string         picture_path; // Where to keep the picture...
	std::string         meta_path;    // ... and its details.
	TileRequest() : style(0), key(), url(), etag(), deep(false), picture_path(), meta_path() {}
};

struct TileResult {
	enum Outcome {
		LOADED,      // A new picture: `picture` has it (and it has been put in the cache).
		UNCHANGED,   // The server says the picture the request named by its ETag is still current.
		MISSING,     // The server answered but has no picture for it (not found, or not a picture).
		FAILED       // No answer, or an answer that says to try later.
	};
	int                 style;
	map_tiles::TileKey  key;
	Outcome             outcome;
	bool                conditional;   // The request carried an ETag.
	bool                deep;          // It was a deep_url_template one.
	int                 http_status;   // 0 if there was no reply.
	unsigned long       error_code;    // The system's code for what went wrong, if the request didn't get a reply.
	std::string         picture;
	map_tiles::TileMeta meta;          // What is now on the disk for it.
	// How long it took, in seconds: waiting in line for one of the workers, then from asking to the server starting to
	// answer, then from asking to the whole picture being in (or the failure).
	double              wait_seconds;
	double              reply_seconds;
	double              total_seconds;
	int                 out_at_start;  // How many requests to its source were out when it started, itself included.
	TileResult() : style(0), key(), outcome(FAILED), conditional(false), deep(false), http_status(0), error_code(0), picture(), meta(),
		wait_seconds(0.0), reply_seconds(0.0), total_seconds(0.0), out_at_start(0) {}
};

class TileFetcher {
public:
	// `cache_root` is the folder the cache is trimmed in, to `cache_limit_bytes`. Nothing starts until the first want().
	TileFetcher(const std::string& cache_root, int64_t cache_limit_bytes);
	~TileFetcher();

	// The tiles wanted in a frame go between begin_frame() and end_frame(). A tile already asked for
	// is left alone (a request already out isn't affected by any of this).
	void begin_frame();
	void want(const TileRequest& request);
	// Requests still waiting that weren't wanted this frame are dropped; `dropped` gets their (style, key)
	// as the style in `.first`.
	void end_frame(std::vector<std::pair<int, map_tiles::TileKey> >* dropped);

	// The next finished request, if there is one.
	bool next_result(TileResult* result);

	// How many requests are waiting or out.
	int  busy_count();
	// Whether this one is waiting or out.
	bool is_busy(int style, const map_tiles::TileKey& key);

	// The most requests out at once, to all sources together: the largest `max_out` any source has.
	static const int MAX_WORKERS = 6;
	// How big the cache is allowed to grow, unless the user's disk is tight (there is plenty of room on most).
	static const int64_t DEFAULT_CACHE_LIMIT_BYTES = 200LL * 1024 * 1024;

private:
	struct Queued {
		TileRequest request;
		bool        wanted;
		double      queued_at;   // Seconds on the program's clock.
	};

	// A request that is out.
	struct InFlight {
		int                style;
		map_tiles::TileKey key;
		double             started_at;   // Seconds on the program's clock.
	};

	void start();
	void worker_main(int index);
	// Which of the waiting requests may start now (an index into m_queue), or -1. Needs m_mutex held.
	int  next_to_start(double now);
	TileResult fetch(int worker, const TileRequest& request, double queued_at);
	void trim_cache();
	void abandon_requests();

	std::string              m_cache_root;
	int64_t                  m_cache_limit;
	std::mutex               m_mutex;
	std::condition_variable  m_wake;
	std::vector<Queued>      m_queue;
	std::vector<TileResult>  m_results;
	std::vector<InFlight>    m_in_flight;   // Out now, so the same one isn't asked for again, and so slow ones can be told.
	bool                     m_stop;
	bool                     m_started;
	bool                     m_trimming;
	int                      m_writes_since_trim;
	std::thread              m_threads[MAX_WORKERS];
	void*                    m_session;                // WinHTTP's, null when not on Windows.
	void*                    m_active_request[MAX_WORKERS];   // The WinHTTP request each worker is in the middle of, so shutting down can cut it short.
};
