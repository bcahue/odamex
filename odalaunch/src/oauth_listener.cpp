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
//   One-shot loopback HTTP listener for the OIDC authorization-code callback.
//   See oauth_listener.h. Backed by Mongoose (single-file embedded HTTP),
//   which keeps the launcher's Windows floor at Vista -- the same as the rest
//   of Odamex -- rather than forcing a newer baseline.
//
//-----------------------------------------------------------------------------

#include "oauth_listener.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "mongoose.h"

namespace
{
// Parse a URL-encoded query string ("a=1&b=2") into a key->value map,
// percent-decoding both sides. Kept generic so the listener doesn't care
// whether it receives an OIDC callback (code/state) or the API's bounce-back
// (token/pending/error/ref).
void ParseQuery(const struct mg_str& query, std::map<std::string, std::string>& out)
{
	const char* s = query.buf;
	size_t len = query.len;
	size_t i = 0;
	char decoded[4096];

	while (i < len)
	{
		size_t amp = i;
		while (amp < len && s[amp] != '&')
			++amp;

		size_t eq = i;
		while (eq < amp && s[eq] != '=')
			++eq;

		const char* keyPtr = s + i;
		size_t keyLen = eq - i;
		const char* valPtr = (eq < amp) ? s + eq + 1 : s + amp;
		size_t valLen = (eq < amp) ? amp - eq - 1 : 0;

		if (keyLen > 0)
		{
			int kn = mg_url_decode(keyPtr, keyLen, decoded, sizeof(decoded), 1);
			std::string key = (kn > 0) ? std::string(decoded, static_cast<size_t>(kn))
			                           : std::string(keyPtr, keyLen);

			int vn = mg_url_decode(valPtr, valLen, decoded, sizeof(decoded), 1);
			std::string val = (vn > 0) ? std::string(decoded, static_cast<size_t>(vn))
			                           : std::string(valPtr, valLen);

			out.emplace(std::move(key), std::move(val));
		}

		i = amp + 1;
	}
}

// Minimal landing page shown in the user's browser after the redirect.
const char* const kClosePageHtml =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<title>Odamex</title></head>"
    "<body style=\"font-family:sans-serif;text-align:center;margin-top:4em\">"
    "<h2>Odamex sign-in complete</h2>"
    "<p>You can close this window and return to the launcher.</p>"
    "</body></html>";
} // namespace

struct OAuthLoopbackListener::Impl
{
	struct mg_mgr mgr;
	std::thread thread;

	std::mutex mutex;
	std::condition_variable cv;

	bool ready = false;       // worker has finished its bind attempt
	bool listenOk = false;    // bind/listen succeeded
	bool gotCallback = false; // the /auth request has been received
	std::atomic<bool> stop{false};

	std::map<std::string, std::string> params;
	int port = 0;

	static void EvHandler(struct mg_connection* c, int ev, void* ev_data);
	void WorkerMain();
};

void OAuthLoopbackListener::Impl::EvHandler(struct mg_connection* c, int ev,
                                            void* ev_data)
{
	if (ev != MG_EV_HTTP_MSG)
		return;

	Impl* d = static_cast<Impl*>(c->fn_data);
	struct mg_http_message* hm = static_cast<struct mg_http_message*>(ev_data);

	// Only the callback path captures parameters; ignore stray requests
	// (e.g. the browser's /favicon.ico) but still answer them politely.
	if (mg_strcmp(hm->uri, mg_str("/auth")) != 0)
	{
		mg_http_reply(c, 404, "", "not found\n");
		c->is_draining = 1;
		return;
	}

	{
		std::lock_guard<std::mutex> lock(d->mutex);
		if (!d->gotCallback)
		{
			ParseQuery(hm->query, d->params);
			d->gotCallback = true;
		}
	}

	mg_http_reply(c, 200, "Content-Type: text/html\r\n", "%s", kClosePageHtml);
	c->is_draining = 1; // flush the response, then close
	d->cv.notify_all();
}

void OAuthLoopbackListener::Impl::WorkerMain()
{
	mg_mgr_init(&mgr);

	// Bind an OS-chosen ephemeral port on the loopback interface only.
	struct mg_connection* lc =
	    mg_http_listen(&mgr, "http://127.0.0.1:0", &Impl::EvHandler, this);

	{
		std::lock_guard<std::mutex> lock(mutex);
		if (lc != nullptr)
		{
			// loc.port is filled from getsockname() after bind, in network
			// byte order -- convert to host order for display/use.
			port = mg_ntohs(lc->loc.port);
			listenOk = true;
		}
		ready = true;
	}
	cv.notify_all();

	if (lc == nullptr)
	{
		mg_mgr_free(&mgr);
		return;
	}

	// Event loop: poll until asked to stop. The 100ms timeout bounds how long
	// Stop() waits for the loop to notice the stop flag.
	while (!stop.load())
		mg_mgr_poll(&mgr, 100);

	mg_mgr_free(&mgr);
}

OAuthLoopbackListener::OAuthLoopbackListener() : m_impl(new Impl) {}

OAuthLoopbackListener::~OAuthLoopbackListener()
{
	Stop();
}

bool OAuthLoopbackListener::Start()
{
	Impl* d = m_impl.get();

	d->thread = std::thread([d]() { d->WorkerMain(); });

	// Wait for the worker to finish its bind attempt so Port()/RedirectUri()
	// are valid as soon as Start() returns.
	std::unique_lock<std::mutex> lock(d->mutex);
	d->cv.wait(lock, [d]() { return d->ready; });
	return d->listenOk;
}

int OAuthLoopbackListener::Port() const
{
	return m_impl->port;
}

std::string OAuthLoopbackListener::RedirectUri() const
{
	if (m_impl->port == 0)
		return std::string();
	return "http://127.0.0.1:" + std::to_string(m_impl->port) + "/auth";
}

bool OAuthLoopbackListener::WaitForCallback(
    int timeoutSeconds, std::map<std::string, std::string>& params,
    std::atomic<bool>* cancel)
{
	Impl* d = m_impl.get();

	std::unique_lock<std::mutex> lock(d->mutex);
	const auto deadline =
	    std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);

	// Poll in short slices so an external cancel request (set from another
	// thread) is noticed promptly rather than only when the callback arrives.
	while (!d->gotCallback)
	{
		if (cancel != nullptr && cancel->load())
			return false;
		if (std::chrono::steady_clock::now() >= deadline)
			return false;
		d->cv.wait_for(lock, std::chrono::milliseconds(200));
	}

	params = d->params;
	return true;
}

void OAuthLoopbackListener::Stop()
{
	Impl* d = m_impl.get();

	d->stop.store(true);
	if (d->thread.joinable())
		d->thread.join();
}
