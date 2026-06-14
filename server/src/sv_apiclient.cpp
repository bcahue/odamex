// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 2006-2026 by The Odamex Team.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//   Asynchronous client for Odamex Server to send events to an external API.

//   Runs on worker threads to not block the main thread on network I/O.
//-----------------------------------------------------------------------------

#include "odamex.h"

#include "sv_apiclient.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <curl/curl.h>
#include "json/json.h"

#include "m_wdlstats.h"

EXTERN_CVAR(sv_auth_enabled)
EXTERN_CVAR(sv_auth_api_url)
EXTERN_CVAR(sv_auth_server_id)
EXTERN_CVAR(sv_auth_token_url)
EXTERN_CVAR(sv_auth_client_id)
EXTERN_CVAR(sv_auth_client_secret)

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

namespace
{

struct EventJob
{
	std::string sub;
	std::string jti;
	std::string eventType; // "join" or "leave"
	int64_t occurredAt;    // unix seconds
	std::string clientIp;
};

std::thread s_worker;
std::mutex s_mutex;
std::condition_variable s_cv;
std::deque<EventJob> s_queue;
// Finished-match stats uploads, as paths to spooled files under <spool>/unsent/.
// A separate queue (rather than a unified job) keeps the small/frequent event
// path and the large/infrequent stats path from jostling each other; the worker
// drains events first (see WorkerLoop). The stats file on disk - not this queue - is
// the durable source of truth; a dropped/lost queue entry is recovered by a
// rescan of unsent/.
std::deque<std::string> s_statsQueue;
std::atomic<bool> s_running{false};
bool s_stop = false; // guarded by s_mutex

// The match-stats spool lives under the wdlstats log directory, in a "stats"
// subtree with unsent/ sent/ rejected/ folders. Resolved on demand from
// M_GetWDLLogDir() rather than cached, since logging can be enabled after
// SV_ApiInit and the dir is only valid then.
struct SpoolDirs
{
	std::string unsent;
	std::string sent;
	std::string rejected;
};

// Service-token cache. Only touched by the worker thread, so it needs no lock.
std::string s_token;
std::chrono::system_clock::time_point s_tokenExpiry;

// The queue is a safety valve, not a durable store: if the API is down we drop
// the oldest events rather than grow without bound.
const size_t MAX_QUEUE = 256;

// Stats payloads are large (a full GameV6 blob) and infrequent (one per match),
// so a shallow queue is plenty — under sustained API outage we'd rather drop old
// match uploads than hold many multi-KB blobs in memory.
const size_t MAX_STATS_QUEUE = 8;

// Exponential-backoff tuning for transient API failures (transport error, 429,
// 5xx, or a momentarily unreachable token endpoint). Per-attempt delay is
// BACKOFF_BASE_MS << attempt, capped at BACKOFF_MAX_MS, with equal jitter. The
// wait is interruptible (parks on s_cv) so shutdown never waits out a backoff.
const int BACKOFF_BASE_MS = 500;
const int BACKOFF_MAX_MS = 30000;
const int MAX_DELIVERY_ATTEMPTS = 5;

// How often the worker rescans <spool>/unsent/ while otherwise idle, so a match
// left pending after a sustained outage retries within the session (not only on
// the next restart).
const int SPOOL_RESCAN_SECS = 300;

// Worker-thread logging goes straight to stderr. Odamex's PrintFmt touches the
// shared console state and is only safe on the main thread; fputs to stderr is
// the portable thread-safe choice for diagnostics off the game loop.
template <typename... Args>
void LogFmt(fmt::format_string<Args...> fmtstr, Args&&... args)
{
	std::string line = fmt::format(fmtstr, std::forward<Args>(args)...);
	std::fputs(line.c_str(), stderr);
	std::fputc('\n', stderr);
}

size_t CurlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
	std::string* response = static_cast<std::string*>(userdata);
	size_t total = size * nmemb;
	response->append(ptr, total);
	return total;
}

std::string Iso8601Utc(int64_t unixSeconds)
{
	std::time_t t = static_cast<std::time_t>(unixSeconds);
	std::tm tmv;
#if defined(_WIN32)
	gmtime_s(&tmv, &t);
#else
	gmtime_r(&t, &tmv);
#endif
	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
	return std::string(buf);
}

// ---------------------------------------------------------------------------
// Service token
// ---------------------------------------------------------------------------

// Fetches a fresh client_credentials token from the OAuth IDP. Returns true and
// populates s_token / s_tokenExpiry on success. Runs on the worker thread.
bool FetchServiceToken()
{
	if (strlen(sv_auth_token_url.cstring()) == 0 ||
	    strlen(sv_auth_client_id.cstring()) == 0 ||
	    strlen(sv_auth_client_secret.cstring()) == 0)
	{
		LogFmt("SV_Api: token URL / client id / client secret not configured");
		return false;
	}

	CURL* curl = curl_easy_init();
	if (!curl)
	{
		LogFmt("SV_Api: failed to init curl for token request");
		return false;
	}

	// URL-encode the credentials into the form body.
	char* encId = curl_easy_escape(curl, sv_auth_client_id.cstring(), 0);
	char* encSecret = curl_easy_escape(curl, sv_auth_client_secret.cstring(), 0);
	std::string body = "grant_type=client_credentials&client_id=" +
	                   std::string(encId ? encId : "") +
	                   "&client_secret=" + std::string(encSecret ? encSecret : "");
	if (encId) curl_free(encId);
	if (encSecret) curl_free(encSecret);

	std::string response;
	std::string useragent = std::string("Odamex/") + DOTVERSIONSTR;

	struct curl_slist* headers = nullptr;
	headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

	curl_easy_setopt(curl, CURLOPT_URL, sv_auth_token_url.cstring());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, useragent.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

	CURLcode res = curl_easy_perform(curl);
	long httpCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK)
	{
		LogFmt("SV_Api: token request failed: {}", curl_easy_strerror(res));
		return false;
	}
	if (httpCode != 200)
	{
		LogFmt("SV_Api: token endpoint returned HTTP {}", httpCode);
		return false;
	}

	Json::Value root;
	Json::CharReaderBuilder builder;
	std::string errs;
	std::istringstream stream(response);
	if (!Json::parseFromStream(builder, stream, &root, &errs))
	{
		LogFmt("SV_Api: failed to parse token response: {}", errs);
		return false;
	}

	if (!root.isMember("access_token") || !root["access_token"].isString())
	{
		LogFmt("SV_Api: token response missing access_token");
		return false;
	}

	s_token = root["access_token"].asString();

	// Refresh a little before the real expiry to avoid using a just-expired
	// token. Keycloak returns expires_in (seconds); default to 60s if absent.
	int expiresIn = root.get("expires_in", 60).asInt();
	int skew = 30;
	if (expiresIn <= skew)
		skew = expiresIn / 2;
	s_tokenExpiry = std::chrono::system_clock::now() +
	                std::chrono::seconds(expiresIn - skew);

	return true;
}

// Returns a usable bearer token, fetching one if the cache is empty/expired.
// Empty string on failure.
const std::string& EnsureServiceToken()
{
	static const std::string empty;
	if (s_token.empty() || std::chrono::system_clock::now() >= s_tokenExpiry)
	{
		if (!FetchServiceToken())
			return empty;
	}
	return s_token;
}

// ---------------------------------------------------------------------------
// Game Event POST
// ---------------------------------------------------------------------------

// POSTs one event with the given bearer token. Returns the HTTP status code,
// or 0 on a transport-level failure.
long PostEventOnce(const EventJob& job, const std::string& bearer)
{
	std::string url = std::string(sv_auth_api_url.cstring()) +
	                  "/api/GameServerEvents/game-servers/events";

	Json::Value payload;
	payload["serverId"] = sv_auth_server_id.asInt();
	payload["playerSubject"] = job.sub;
	payload["eventType"] = job.eventType;
	payload["occurredAt"] = Iso8601Utc(job.occurredAt);
	payload["clientIp"] = job.clientIp;
	payload["ticketJti"] = job.jti;

	Json::StreamWriterBuilder writer;
	writer["indentation"] = "";
	std::string bodyJson = Json::writeString(writer, payload);

	CURL* curl = curl_easy_init();
	if (!curl)
		return 0;

	std::string response;
	std::string useragent = std::string("Odamex/") + DOTVERSIONSTR;
	std::string authHeader = "Authorization: Bearer " + bearer;

	struct curl_slist* headers = nullptr;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, authHeader.c_str());

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, useragent.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyJson.c_str());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(bodyJson.size()));
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

	CURLcode res = curl_easy_perform(curl);
	long httpCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK)
	{
		LogFmt("SV_Api: event POST failed: {}", curl_easy_strerror(res));
		return 0;
	}
	return httpCode;
}

// ---------------------------------------------------------------------------
// Delivery with retry / exponential backoff
// ---------------------------------------------------------------------------

// Outcome of a logical delivery. Drives the stats spool's folder transitions:
// Delivered → sent/, Rejected → rejected/, Pending → left in unsent/ to retry.
enum class DeliveryResult
{
	Delivered, // 2xx
	Rejected,  // permanent non-retryable status (4xx incl. 409 = already uploaded)
	Pending,   // transient failures exhausted, or shutdown mid-retry — try later
};

// Whether an HTTP status warrants a retry. Transport failures (0), rate limits
// (429) and server errors (5xx) are transient; other 4xx are permanent (a bad
// payload, a server_id mismatch, or a 409 duplicate won't fix itself by retrying).
bool IsRetryableStatus(long code)
{
	return code == 0 || code == 429 || (code >= 500 && code < 600);
}

// Park for the backoff appropriate to a 0-based attempt number. Returns false if
// the worker was asked to stop while waiting (the caller should then abort the
// retry). Thread-safe + shutdown-aware: it waits on the same condition variable
// the queue uses, so SV_ApiShutdown's notify wakes it immediately instead of
// blocking the join for the full delay.
bool BackoffWait(int attempt)
{
	long long delay = static_cast<long long>(BACKOFF_BASE_MS) << attempt;
	if (delay > BACKOFF_MAX_MS)
		delay = BACKOFF_MAX_MS;

	// Equal jitter (half fixed + half random) spreads retries without a
	// thundering herd. The RNG is only ever touched by the worker thread.
	thread_local std::mt19937 rng(std::random_device{}());
	long long half = delay / 2;
	std::uniform_int_distribution<long long> dist(0, delay - half);
	long long waitMs = half + dist(rng);

	std::unique_lock<std::mutex> lock(s_mutex);
	// wait_for returns true iff the predicate (stop requested) became true.
	return !s_cv.wait_for(lock, std::chrono::milliseconds(waitMs),
	                      [] { return s_stop; });
}

// Drive one logical delivery to completion: acquire/refresh the service token,
// POST via `post`, and retry transient failures with exponential backoff. `post`
// performs a single attempt with the given bearer and returns the HTTP status
// (0 = transport failure). Runs entirely on the worker thread, so the token
// cache needs no lock.
template <typename PostFn>
DeliveryResult DeliverWithRetry(const std::string& what, PostFn&& post)
{
	for (int attempt = 0;; ++attempt)
	{
		const std::string& token = EnsureServiceToken();
		if (!token.empty())
		{
			long code = post(token);

			// A 401 means the token was rejected (e.g. rotated/revoked). Force a
			// refresh and retry in-place once, without consuming a backoff slot.
			if (code == 401)
			{
				s_token.clear();
				const std::string& fresh = EnsureServiceToken();
				if (!fresh.empty())
					code = post(fresh);
			}

			if (code >= 200 && code < 300)
				return DeliveryResult::Delivered;

			if (!IsRetryableStatus(code))
			{
				LogFmt("SV_Api: {} rejected (HTTP {}), not retrying", what, code);
				return DeliveryResult::Rejected;
			}

			LogFmt("SV_Api: {} failed (HTTP {}), attempt {}/{}", what, code,
			       attempt + 1, MAX_DELIVERY_ATTEMPTS);
		}
		else
		{
			// No token (Keycloak briefly unreachable / misconfigured) — transient.
			LogFmt("SV_Api: {} deferred (no service token), attempt {}/{}", what,
			       attempt + 1, MAX_DELIVERY_ATTEMPTS);
		}

		if (attempt + 1 >= MAX_DELIVERY_ATTEMPTS)
		{
			LogFmt("SV_Api: {} giving up after {} attempts", what,
			       MAX_DELIVERY_ATTEMPTS);
			return DeliveryResult::Pending;
		}

		if (!BackoffWait(attempt))
			return DeliveryResult::Pending; // shutting down — retry later
	}
}

void DeliverEvent(const EventJob& job)
{
	// Events are in-memory only; the outcome (delivered / rejected / pending) is
	// best-effort and not persisted.
	DeliverWithRetry(fmt::format("{} event for {}", job.eventType, job.sub),
	                 [&](const std::string& bearer) { return PostEventOnce(job, bearer); });
}

// ---------------------------------------------------------------------------
// Match stats POST
// ---------------------------------------------------------------------------

// POSTs the already-assembled match stats envelope with the given
// bearer token. Returns the HTTP status code, or 0 on a transport-level failure.
long PostStatsOnce(const std::string& body, const std::string& bearer)
{
	std::string url = std::string(sv_auth_api_url.cstring()) +
	                  "/api/GameServerStats/game-servers/stats";

	CURL* curl = curl_easy_init();
	if (!curl)
		return 0;

	std::string response;
	std::string useragent = std::string("Odamex/") + DOTVERSIONSTR;
	std::string authHeader = "Authorization: Bearer " + bearer;

	struct curl_slist* headers = nullptr;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, authHeader.c_str());

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, useragent.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
	// Larger ceiling than the event path: stats bodies are big and the API does
	// more work (parse + normalize + persist) per request.
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

	CURLcode res = curl_easy_perform(curl);
	long httpCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK)
	{
		LogFmt("SV_Api: stats POST failed: {}", curl_easy_strerror(res));
		return 0;
	}
	return httpCode;
}

// ---------------------------------------------------------------------------
// Match-stats spool (durable on-disk queue)
// ---------------------------------------------------------------------------

// Move a spooled file into one of the status folders (sent/ or rejected/),
// preserving its name. Atomic rename within the spool; falls back to copy+remove
// across devices. Best-effort: a failure just leaves the file where it was.
void MoveSpoolFile(const std::string& from, const std::string& destDir)
{
	std::error_code ec;
	fs::path src(from);
	fs::path dst = fs::path(destDir) / src.filename();

	fs::rename(src, dst, ec);
	if (ec)
	{
		// Cross-device or other rename failure — copy then remove the original.
		std::error_code copyEc;
		fs::copy_file(src, dst, fs::copy_options::overwrite_existing, copyEc);
		if (!copyEc)
			fs::remove(src, ec);
		else
			ec = copyEc;
	}

	if (ec)
		LogFmt("SV_Api: failed to move spool file {} -> {}: {}", from, destDir,
		       ec.message());
}

// Resolve the spool subdirs under the current wdlstats log directory and ensure
// they exist. Returns false if logging isn't enabled (no logdir) or the dirs
// can't be created.
bool ResolveSpool(SpoolDirs& out)
{
	const std::string& base = M_GetWDLLogDir(); // trailing path sep, or empty
	if (base.empty())
		return false;

	fs::path root = fs::path(base) / "stats";
	out.unsent = (root / "unsent").string();
	out.sent = (root / "sent").string();
	out.rejected = (root / "rejected").string();

	std::error_code ec;
	fs::create_directories(out.unsent, ec);
	fs::create_directories(out.sent, ec);
	fs::create_directories(out.rejected, ec);

	return fs::is_directory(out.unsent) && fs::is_directory(out.sent) &&
	       fs::is_directory(out.rejected);
}

// Enqueue every *.json currently in unsent/ that isn't already queued. Called at
// startup (recover prior-session uploads) and on the idle rescan tick. Runs on
// the worker thread (or, at init, before it starts); locks s_mutex to push.
void ScanUnsentDir()
{
	SpoolDirs spool;
	if (!ResolveSpool(spool))
		return;

	std::error_code ec;
	std::vector<std::string> files;
	for (const auto& entry : fs::directory_iterator(spool.unsent, ec))
	{
		if (ec)
			break;
		if (!entry.is_regular_file())
			continue;
		if (entry.path().extension() != ".json") // skip *.json.tmp partials
			continue;
		files.push_back(entry.path().string());
	}
	if (files.empty())
		return;

	// Oldest first: the timestamped names sort chronologically.
	std::sort(files.begin(), files.end());

	std::lock_guard<std::mutex> lock(s_mutex);
	for (const auto& f : files)
	{
		// Skip anything already queued (e.g. a file enqueued at write time that
		// hasn't been processed yet) so a rescan never double-posts it.
		if (std::find(s_statsQueue.begin(), s_statsQueue.end(), f) != s_statsQueue.end())
			continue;
		if (s_statsQueue.size() >= MAX_STATS_QUEUE)
			break; // remaining files stay on disk; picked up on a later rescan
		s_statsQueue.push_back(f);
	}
}

// Read a spooled envelope, upload it (with retry/backoff), and transition the
// file by outcome: delivered → sent/, rejected → rejected/, pending → left in
// unsent/ for a later rescan or the next restart.
void ProcessStatsFile(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		// Transient FS hiccup — leave it for a later rescan rather than lose it.
		LogFmt("SV_Api: cannot open spool file {} (will retry)", path);
		return;
	}
	std::stringstream ss;
	ss << in.rdbuf();
	std::string body = ss.str();
	in.close();

	SpoolDirs spool;
	if (!ResolveSpool(spool))
	{
		// Logging dir vanished/disabled mid-flight — leave the file for later.
		LogFmt("SV_Api: spool dir unavailable, leaving {}", path);
		return;
	}

	if (body.empty())
	{
		LogFmt("SV_Api: empty spool file {}, rejecting", path);
		MoveSpoolFile(path, spool.rejected);
		return;
	}

	DeliveryResult result = DeliverWithRetry(
	    "match stats upload",
	    [&](const std::string& bearer) { return PostStatsOnce(body, bearer); });

	switch (result)
	{
	case DeliveryResult::Delivered:
		MoveSpoolFile(path, spool.sent);
		break;
	case DeliveryResult::Rejected:
		MoveSpoolFile(path, spool.rejected);
		break;
	case DeliveryResult::Pending:
		// Leave in unsent/ — recovered on the idle rescan or next startup.
		break;
	}
}

void WorkerLoop()
{
	while (true)
	{
		EventJob job;
		std::string statsPath;
		bool haveEvent = false;
		bool haveStats = false;
		bool idleTick = false;
		{
			std::unique_lock<std::mutex> lock(s_mutex);
			// Wake on stop / queued work, or time out to rescan the spool.
			bool signaled = s_cv.wait_for(
			    lock, std::chrono::seconds(SPOOL_RESCAN_SECS),
			    [] { return s_stop || !s_queue.empty() || !s_statsQueue.empty(); });

			// Exit promptly on shutdown without draining: stats are durable on
			// disk (recovered next start), and unsent events are best-effort, so
			// there's no reason to risk blocking shutdown on a slow/down API.
			if (s_stop)
				return;

			if (!signaled)
			{
				// Timed out with nothing queued: rescan unsent/ for stragglers.
				idleTick = true;
			}
			// Player join/leave events are small and latency-sensitive; drain them
			// ahead of the large stats payloads so an upload never starves event
			// delivery.
			else if (!s_queue.empty())
			{
				job = std::move(s_queue.front());
				s_queue.pop_front();
				haveEvent = true;
			}
			else if (!s_statsQueue.empty())
			{
				statsPath = std::move(s_statsQueue.front());
				s_statsQueue.pop_front();
				haveStats = true;
			}
		}

		if (haveEvent)
			DeliverEvent(job);
		else if (haveStats)
			ProcessStatsFile(statsPath);
		else if (idleTick)
			ScanUnsentDir();
	}
}

} // namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void SV_ApiInit()
{
	if (!sv_auth_enabled)
		return;

	// curl_global_init is performed by SV_AuthInit (also gated on sv_auth_enabled),
	// which runs before us, so the library is already initialized here.

	// Recover anything left unsent by a prior shutdown/crash before the worker
	// starts consuming it. No-op if logging (and thus the spool dir) isn't enabled
	// yet — the worker's idle rescan picks it up once `wdlstats <dir>` is set.
	ScanUnsentDir();

	s_stop = false;
	s_running = true;
	s_worker = std::thread(WorkerLoop);

	PrintFmt(PRINT_HIGH, "SV_Api: event/stats worker thread started\n");
}

void SV_ApiPostPlayerEvent(const std::string& sub, const std::string& jti,
                           const std::string& eventType, int64_t occurredAt,
                           const std::string& clientIp)
{
	if (!s_running || sub.empty())
		return;

	EventJob job;
	job.sub = sub;
	job.jti = jti;
	job.eventType = eventType;
	job.occurredAt = occurredAt;
	job.clientIp = clientIp;

	{
		std::lock_guard<std::mutex> lock(s_mutex);
		if (s_queue.size() >= MAX_QUEUE)
			s_queue.pop_front(); // shed the oldest under sustained backpressure
		s_queue.push_back(std::move(job));
	}
	s_cv.notify_one();
}

void SV_ApiUploadMatchStats(const std::string& statsPayloadJson, int64_t startedAtUnix,
                            int64_t endedAtUnix)
{
	if (!s_running || statsPayloadJson.empty())
		return;

	SpoolDirs spool;
	if (!ResolveSpool(spool))
	{
		LogFmt("SV_Api: stats spool unavailable; dropping match upload");
		return;
	}

	// Wrap the v6 GameV6 payload in the stats envelope the API
	// expects (serverId + match window + raw payload). The payload is already
	// valid JSON, so it is embedded verbatim rather than parsed and re-emitted.
	std::string envelope =
	    "{\"serverId\":" + std::to_string(sv_auth_server_id.asInt()) +
	    ",\"matchStartedAt\":\"" + Iso8601Utc(startedAtUnix) + "\"" +
	    ",\"matchEndedAt\":\"" + Iso8601Utc(endedAtUnix) + "\"" +
	    ",\"payload\":" + statsPayloadJson + "}";

	// Persist to unsent/ BEFORE queueing so an immediate crash/restart still
	// recovers the upload. Write to a temp name then atomically rename into place,
	// so a partial write is never visible to the worker or a rescan. The name is
	// unique per match (end-time + server id + a process-local counter).
	static std::atomic<uint32_t> seq{0};
	std::string name = fmt::format("match_{}_{}_{}.json", endedAtUnix,
	                               sv_auth_server_id.asInt(),
	                               seq.fetch_add(1, std::memory_order_relaxed));
	fs::path finalPath = fs::path(spool.unsent) / name;
	fs::path tmpPath = fs::path(spool.unsent) / (name + ".tmp");

	{
		std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			LogFmt("SV_Api: cannot write spool file {}", tmpPath.string());
			return;
		}
		out << envelope;
		out.flush();
		if (!out)
		{
			LogFmt("SV_Api: error writing spool file {}", tmpPath.string());
			return;
		}
	}

	std::error_code ec;
	fs::rename(tmpPath, finalPath, ec);
	if (ec)
	{
		LogFmt("SV_Api: cannot finalize spool file {}: {}", finalPath.string(),
		       ec.message());
		fs::remove(tmpPath, ec);
		return;
	}

	{
		std::lock_guard<std::mutex> lock(s_mutex);
		if (s_statsQueue.size() >= MAX_STATS_QUEUE)
			s_statsQueue.pop_front(); // file stays on disk; recovered by a rescan
		s_statsQueue.push_back(finalPath.string());
	}
	s_cv.notify_one();
}

void STACK_ARGS SV_ApiShutdown()
{
	if (!s_running)
		return;

	{
		std::lock_guard<std::mutex> lock(s_mutex);
		s_stop = true;
	}
	s_cv.notify_one();

	if (s_worker.joinable())
		s_worker.join();

	s_running = false;
	s_token.clear();
}
