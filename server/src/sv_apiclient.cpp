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
//   Asynchronous client for the Odamex API. Obtains a Keycloak service token
//   (client_credentials grant, B6) and posts player join/leave events (B5) on
//   a background worker thread so the game loop never blocks on network I/O.
//
//-----------------------------------------------------------------------------

#include "odamex.h"

#include "sv_apiclient.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <curl/curl.h>
#include "json/json.h"

EXTERN_CVAR(sv_auth_enabled)
EXTERN_CVAR(sv_auth_api_url)
EXTERN_CVAR(sv_auth_server_id)
EXTERN_CVAR(sv_auth_token_url)
EXTERN_CVAR(sv_auth_client_id)
EXTERN_CVAR(sv_auth_client_secret)

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
std::atomic<bool> s_running{false};
bool s_stop = false; // guarded by s_mutex

// Service-token cache. Only touched by the worker thread, so it needs no lock.
std::string s_token;
std::chrono::system_clock::time_point s_tokenExpiry;

// The queue is a safety valve, not a durable store: if the API is down we drop
// the oldest events rather than grow without bound.
const size_t MAX_QUEUE = 256;

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
// Service token (B6)
// ---------------------------------------------------------------------------

// Fetches a fresh client_credentials token from Keycloak. Returns true and
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
// Event POST (B5)
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

void DeliverEvent(const EventJob& job)
{
	const std::string& token = EnsureServiceToken();
	if (token.empty())
	{
		LogFmt("SV_Api: dropping {} event for {} (no service token)",
		       job.eventType, job.sub);
		return;
	}

	long code = PostEventOnce(job, token);

	// A 401 means the token was rejected (e.g. rotated/revoked). Force a refresh
	// and retry exactly once.
	if (code == 401)
	{
		s_token.clear();
		const std::string& fresh = EnsureServiceToken();
		if (!fresh.empty())
			code = PostEventOnce(job, fresh);
	}

	if (code < 200 || code >= 300)
		LogFmt("SV_Api: {} event for {} rejected (HTTP {})", job.eventType,
		       job.sub, code);
}

void WorkerLoop()
{
	while (true)
	{
		EventJob job;
		{
			std::unique_lock<std::mutex> lock(s_mutex);
			s_cv.wait(lock, [] { return s_stop || !s_queue.empty(); });

			if (s_stop && s_queue.empty())
				return;

			job = std::move(s_queue.front());
			s_queue.pop_front();
		}

		DeliverEvent(job);
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

void SV_ApiUploadMatchStats(const std::string& statsJson)
{
	// SCAFFOLD (B7): the upload path is intentionally inert until the stats
	// schema is designed. When implemented, this should mirror the event path:
	// build a StatsJob, enqueue it on the worker, and have the worker call
	// EnsureServiceToken() and POST `statsJson` to
	//   {sv_auth_api_url}/api/GameServerStats/game-servers/stats
	// with `Authorization: Bearer <token>` and `Content-Type: application/json`.
	(void)statsJson;
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
