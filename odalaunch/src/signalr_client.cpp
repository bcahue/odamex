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
//   SignalR JSON-hub client over a WinHTTP WebSocket. See signalr_client.h.
//
//-----------------------------------------------------------------------------

// mongoose.h first: its <winsock2.h> must win the include-order race against
// the <windows.h> that <winhttp.h> drags in, matching the rest of odalaunch.
#include "mongoose.h"

#include "signalr_client.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <winhttp.h>

#include <wx/string.h>
#include <wx/webrequest.h>

#include "dpop_key.h"
#include "dpop_proof.h"

namespace
{
// SignalR's JSON hub protocol frames each message as JSON terminated by the
// ASCII record separator (0x1e). A single WebSocket message may carry several.
const char kRecordSeparator = '\x1e';

// Send a ping ~every 15s. The hub's default client-timeout is 30s, so this
// keeps a quiet connection alive with comfortable margin.
const int kPingIntervalMs = 15000;

// Reconnect backoff schedule (seconds), capped at the last entry.
const int kBackoff[] = {0, 2, 5, 10, 20, 30};

std::wstring Widen(const std::string& s)
{
	if (s.empty())
		return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
	std::wstring w(n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
	return w;
}

// Percent-encode a query-parameter value (e.g. the connection token) using
// mongoose's URL encoder.
std::string UrlEncode(const std::string& s)
{
	std::vector<char> buf(s.size() * 3 + 1);
	size_t n = mg_url_encode(s.data(), s.size(), buf.data(), buf.size());
	return std::string(buf.data(), n);
}

// POST with optional Bearer + DPoP headers via the same WinHTTP-backed wx
// stack used elsewhere for outbound HTTPS. Returns the body; sets statusOut to
// the HTTP status (0 on transport failure).
std::string HttpPost(const wxString& url, const std::string& bearer,
                     const std::string& dpop, int& statusOut)
{
	statusOut = 0;
	wxWebSessionSync& session = wxWebSessionSync::GetDefault();
	wxWebRequestSync request = session.CreateRequest(url);
	if (!request.IsOk())
		return std::string();

	request.SetMethod("POST");
	// Negotiate bodies are empty; send an empty JSON object to keep proxies and
	// content-type sniffers happy.
	request.SetData(wxString::FromUTF8("{}"), "application/json");
	if (!bearer.empty())
		request.SetHeader("Authorization", "Bearer " + wxString::FromUTF8(bearer));
	if (!dpop.empty())
		request.SetHeader("DPoP", wxString::FromUTF8(dpop));

	wxWebRequestSync::Result result = request.Execute();
	if (result.state != wxWebRequest::State_Completed)
		return std::string();

	wxWebResponse response = request.GetResponse();
	statusOut = response.GetStatus();
	return response.AsString().utf8_string();
}

// Insert "/negotiate" before the query string of an Azure SignalR redirect URL
// and append negotiateVersion. e.g.
//   https://x.service.signalr.net/client/?hub=party
// becomes
//   https://x.service.signalr.net/client/negotiate?hub=party&negotiateVersion=1
std::string InsertNegotiate(const std::string& url)
{
	std::string base = url, query;
	auto q = url.find('?');
	if (q != std::string::npos)
	{
		base = url.substr(0, q);
		query = url.substr(q); // includes leading '?'
	}
	if (!base.empty() && base.back() == '/')
		base += "negotiate";
	else
		base += "/negotiate";

	if (query.empty())
		return base + "?negotiateVersion=1";
	return base + query + "&negotiateVersion=1";
}
} // namespace

struct SignalRClient::Impl
{
	std::string apiBaseUrl;
	std::string hubPath;
	std::function<std::string()> tokenProvider;
	const DpopKey& key;

	ConnectedHandler onConnected;
	ClosedHandler onClosed;
	EventHandler onEvent;

	std::thread lifecycleThread;
	std::thread pingThread;

	std::atomic<bool> started{false};
	std::atomic<bool> stop{false};
	std::atomic<bool> connected{false};

	// Guards the three HINTERNET handles and serialises WebSocket sends (a send
	// may run concurrently with the lifecycle thread's blocking receive).
	std::mutex netMutex;
	HINTERNET hSession = nullptr;
	HINTERNET hConnect = nullptr;
	HINTERNET hWebSocket = nullptr;

	// Pending Invoke() completions keyed by invocation id.
	std::mutex invMutex;
	std::map<std::string, CompletionHandler> pending;
	std::atomic<long long> invCounter{0};

	// Sleep coordination so Stop() can wake the backoff/ping waits immediately.
	std::mutex waitMutex;
	std::condition_variable waitCv;

	// Set by a server-initiated close (type 7) record seen on the pump thread.
	bool serverClose = false;
	bool serverAllowReconnect = true;
	std::string serverCloseError;

	Impl(std::string base, std::string hub,
	     std::function<std::string()> tp, const DpopKey& k)
	    : apiBaseUrl(std::move(base)), hubPath(std::move(hub)),
	      tokenProvider(std::move(tp)), key(k)
	{
		while (!apiBaseUrl.empty() && apiBaseUrl.back() == '/')
			apiBaseUrl.pop_back();
	}

	// Interruptible sleep; returns early (false) if Stop() fired.
	bool SleepMs(int ms)
	{
		std::unique_lock<std::mutex> lk(waitMutex);
		waitCv.wait_for(lk, std::chrono::milliseconds(ms),
		                [this]() { return stop.load(); });
		return !stop.load();
	}

	void Lifecycle()
	{
		int attempt = 0;
		while (!stop.load())
		{
			std::string reason;
			bool handshakeOk = ConnectOnce(reason);

			// ConnectOnce returns when the connection is gone. Decide whether to
			// retry: never after Stop(); never after a server close that forbids
			// reconnect.
			bool willRetry = !stop.load() && serverAllowReconnect;
			connected.store(false);
			if (onClosed)
				onClosed(reason, willRetry);

			if (!willRetry)
				break;

			// Reset attempt counter after a connection that actually came up, so a
			// long-lived session that drops reconnects promptly.
			if (handshakeOk)
				attempt = 0;

			int idx = attempt < (int)(sizeof(kBackoff) / sizeof(kBackoff[0]))
			              ? attempt
			              : (int)(sizeof(kBackoff) / sizeof(kBackoff[0])) - 1;
			++attempt;
			if (!SleepMs(kBackoff[idx] * 1000))
				break;
		}
	}

	// One full connect: negotiate (with Azure redirect), WebSocket upgrade,
	// handshake, then pump records until the socket closes. Returns true if the
	// handshake completed (the connection was actually established).
	bool ConnectOnce(std::string& reason)
	{
		serverClose = false;
		serverAllowReconnect = true;
		serverCloseError.clear();

		std::string wsUrl, wsBearer, wsDpop;
		if (!Negotiate(wsUrl, wsBearer, wsDpop, reason))
			return false;

		if (!OpenWebSocket(wsUrl, wsBearer, wsDpop, reason))
			return false;

		// SignalR JSON handshake.
		if (!SendRaw(std::string("{\"protocol\":\"json\",\"version\":1}") +
		             kRecordSeparator))
		{
			reason = "handshake send failed";
			Cleanup();
			return false;
		}

		std::string hs;
		if (!ReceiveMessage(hs))
		{
			reason = "handshake receive failed";
			Cleanup();
			return false;
		}
		// The handshake response is a single record; an "error" property means
		// the server rejected us.
		{
			struct mg_str j = mg_str_n(hs.data(), hs.size());
			char* err = mg_json_get_str(j, "$.error");
			if (err)
			{
				reason = std::string("handshake rejected: ") + err;
				mg_free(err);
				Cleanup();
				return false;
			}
		}

		connected.store(true);
		if (onConnected)
			onConnected();

		PumpReceive();

		if (serverClose)
			reason = serverCloseError.empty() ? "server closed" : serverCloseError;
		else if (reason.empty())
			reason = "connection closed";

		Cleanup();
		return true;
	}

	// Resolve the WebSocket URL + auth. Handles the Azure SignalR redirect: the
	// first negotiate to our API (Bearer + DPoP, satisfying DPoPMiddleware) is
	// answered with a redirect to *.service.signalr.net and a service token; a
	// second negotiate against that endpoint yields the connection token.
	bool Negotiate(std::string& wsUrl, std::string& wsBearer, std::string& wsDpop,
	               std::string& reason)
	{
		const std::string token = tokenProvider();
		if (token.empty())
		{
			reason = "no session token";
			return false;
		}

		const std::string negUrl =
		    apiBaseUrl + hubPath + "/negotiate?negotiateVersion=1";
		const std::string proof = DpopProof::Create(key, "POST", negUrl, token);
		if (proof.empty())
		{
			reason = "dpop build failed";
			return false;
		}

		int status = 0;
		std::string body =
		    HttpPost(wxString::FromUTF8(negUrl), token, proof, status);
		if (status != 200)
		{
			reason = "negotiate http " + std::to_string(status);
			return false;
		}

		struct mg_str j = mg_str_n(body.data(), body.size());
		char* redirectUrl = mg_json_get_str(j, "$.url");
		char* redirectToken = mg_json_get_str(j, "$.accessToken");

		if (redirectUrl && redirectToken)
		{
			// ---- Azure SignalR (Default mode): follow the redirect ----
			const std::string svcBase = redirectUrl;
			const std::string svcToken = redirectToken;
			mg_free(redirectUrl);
			mg_free(redirectToken);

			const std::string negUrl2 = InsertNegotiate(svcBase);
			int status2 = 0;
			std::string body2 =
			    HttpPost(wxString::FromUTF8(negUrl2), svcToken, "", status2);
			if (status2 != 200)
			{
				reason = "service negotiate http " + std::to_string(status2);
				return false;
			}

			struct mg_str j2 = mg_str_n(body2.data(), body2.size());
			char* connTok = mg_json_get_str(j2, "$.connectionToken");
			if (!connTok)
				connTok = mg_json_get_str(j2, "$.connectionId");
			if (!connTok)
			{
				reason = "service negotiate missing connectionToken";
				return false;
			}

			// svcBase already carries a query string (?hub=...), so append id.
			wsUrl = svcBase + "&id=" + UrlEncode(connTok);
			mg_free(connTok);

			// The Azure leg authenticates with the service token in the header;
			// DPoP was already enforced at the API negotiate above and does not
			// apply to the service endpoint.
			wsBearer = svcToken;
			wsDpop.clear();
			return true;
		}

		if (redirectUrl)
			mg_free(redirectUrl);
		if (redirectToken)
			mg_free(redirectToken);

		// ---- Direct (Kestrel, no Azure) ----
		char* connTok = mg_json_get_str(j, "$.connectionToken");
		if (!connTok)
			connTok = mg_json_get_str(j, "$.connectionId");
		if (!connTok)
		{
			reason = "negotiate missing connectionToken";
			return false;
		}

		wsUrl = apiBaseUrl + hubPath + "?id=" + UrlEncode(connTok);
		mg_free(connTok);

		// In direct mode the WebSocket upgrade IS the request that runs
		// DPoPMiddleware, so it needs its own proof bound to GET + the ws URL.
		wsBearer = token;
		wsDpop = DpopProof::Create(key, "GET", wsUrl, token);
		return true;
	}

	bool OpenWebSocket(const std::string& url, const std::string& bearer,
	                   const std::string& dpop, std::string& reason)
	{
		std::wstring wurl = Widen(url);

		URL_COMPONENTS uc;
		ZeroMemory(&uc, sizeof(uc));
		uc.dwStructSize = sizeof(uc);
		wchar_t scheme[16] = {0}, host[256] = {0};
		std::vector<wchar_t> path(8192, 0), extra(8192, 0);
		uc.lpszScheme = scheme;
		uc.dwSchemeLength = 16;
		uc.lpszHostName = host;
		uc.dwHostNameLength = 256;
		uc.lpszUrlPath = path.data();
		uc.dwUrlPathLength = (DWORD)path.size();
		uc.lpszExtraInfo = extra.data();
		uc.dwExtraInfoLength = (DWORD)extra.size();
		if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
		{
			reason = "bad ws url";
			return false;
		}
		const bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
		std::wstring pathAndQuery = std::wstring(uc.lpszUrlPath) + uc.lpszExtraInfo;

		HINTERNET session = WinHttpOpen(L"Odamex-Launcher/1.0",
		                                WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
		                                0);
		if (!session)
		{
			reason = "WinHttpOpen failed";
			return false;
		}
		// Bound the receive so a wedged socket can't park the lifecycle thread
		// past Stop() forever; the pump treats a timeout as a drop.
		WinHttpSetTimeouts(session, 30000, 30000, 30000, 35000);

		HINTERNET connect =
		    WinHttpConnect(session, host, uc.nPort, 0);
		if (!connect)
		{
			WinHttpCloseHandle(session);
			reason = "WinHttpConnect failed";
			return false;
		}

		HINTERNET req = WinHttpOpenRequest(
		    connect, L"GET", pathAndQuery.c_str(), nullptr, WINHTTP_NO_REFERER,
		    WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
		if (!req)
		{
			WinHttpCloseHandle(connect);
			WinHttpCloseHandle(session);
			reason = "WinHttpOpenRequest failed";
			return false;
		}

		// Ask WinHTTP to perform the WebSocket upgrade on this request.
		if (!WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0))
		{
			WinHttpCloseHandle(req);
			WinHttpCloseHandle(connect);
			WinHttpCloseHandle(session);
			reason = "ws upgrade option failed";
			return false;
		}

		std::wstring headers;
		if (!bearer.empty())
			headers += L"Authorization: Bearer " + Widen(bearer) + L"\r\n";
		if (!dpop.empty())
			headers += L"DPoP: " + Widen(dpop) + L"\r\n";

		BOOL sent = WinHttpSendRequest(
		    req, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
		    headers.empty() ? 0 : (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
		if (!sent || !WinHttpReceiveResponse(req, nullptr))
		{
			WinHttpCloseHandle(req);
			WinHttpCloseHandle(connect);
			WinHttpCloseHandle(session);
			reason = "ws send/receive failed";
			return false;
		}

		// Verify the 101 upgrade before completing it.
		DWORD code = 0, codeLen = sizeof(code);
		WinHttpQueryHeaders(req,
		                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		                    WINHTTP_HEADER_NAME_BY_INDEX, &code, &codeLen,
		                    WINHTTP_NO_HEADER_INDEX);
		if (code != 101)
		{
			WinHttpCloseHandle(req);
			WinHttpCloseHandle(connect);
			WinHttpCloseHandle(session);
			reason = "ws upgrade http " + std::to_string(code);
			return false;
		}

		HINTERNET ws = WinHttpWebSocketCompleteUpgrade(req, 0);
		// The request handle is no longer needed once the socket is established.
		WinHttpCloseHandle(req);
		if (!ws)
		{
			WinHttpCloseHandle(connect);
			WinHttpCloseHandle(session);
			reason = "CompleteUpgrade failed";
			return false;
		}

		std::lock_guard<std::mutex> lk(netMutex);
		hSession = session;
		hConnect = connect;
		hWebSocket = ws;
		return true;
	}

	// Read one complete WebSocket message (re-assembling fragments) into out.
	// Returns false if the socket closed or errored.
	bool ReceiveMessage(std::string& out)
	{
		out.clear();
		BYTE buf[4096];
		for (;;)
		{
			DWORD read = 0;
			WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
			HINTERNET ws;
			{
				std::lock_guard<std::mutex> lk(netMutex);
				ws = hWebSocket;
			}
			if (!ws)
				return false;

			DWORD rc = WinHttpWebSocketReceive(ws, buf, sizeof(buf), &read, &type);
			if (rc != NO_ERROR)
				return false;
			if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
				return false;

			out.append(reinterpret_cast<char*>(buf), read);

			if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
			    type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
				return true; // end of message
			// otherwise a *_FRAGMENT_* type: keep reading.
		}
	}

	void PumpReceive()
	{
		while (!stop.load() && !serverClose)
		{
			std::string msg;
			if (!ReceiveMessage(msg))
				return;

			// A WebSocket message may pack several 0x1e-delimited records.
			size_t start = 0;
			while (start < msg.size())
			{
				size_t end = msg.find(kRecordSeparator, start);
				size_t len = (end == std::string::npos ? msg.size() : end) - start;
				if (len > 0)
					DispatchRecord(msg.substr(start, len));
				if (end == std::string::npos)
					break;
				start = end + 1;
			}
		}
	}

	void DispatchRecord(const std::string& rec)
	{
		struct mg_str j = mg_str_n(rec.data(), rec.size());
		long type = mg_json_get_long(j, "$.type", -1);

		switch (type)
		{
		case 1: // invocation: a server->client event
		{
			char* target = mg_json_get_str(j, "$.target");
			if (target)
			{
				int len = 0;
				int off = mg_json_get(j, "$.arguments", &len);
				std::string args =
				    (off >= 0 && len > 0) ? rec.substr(off, len) : std::string("[]");
				if (onEvent)
					onEvent(target, args);
				mg_free(target);
			}
			break;
		}
		case 3: // completion of one of our Invoke() calls
		{
			char* id = mg_json_get_str(j, "$.invocationId");
			if (id)
			{
				CompletionHandler cb;
				{
					std::lock_guard<std::mutex> lk(invMutex);
					auto it = pending.find(id);
					if (it != pending.end())
					{
						cb = it->second;
						pending.erase(it);
					}
				}
				if (cb)
				{
					char* err = mg_json_get_str(j, "$.error");
					if (err)
					{
						cb(false, err);
						mg_free(err);
					}
					else
					{
						int len = 0;
						int off = mg_json_get(j, "$.result", &len);
						cb(true, (off >= 0 && len > 0) ? rec.substr(off, len)
						                               : std::string());
					}
				}
				mg_free(id);
			}
			break;
		}
		case 6: // ping
			break;
		case 7: // close
		{
			char* err = mg_json_get_str(j, "$.error");
			serverCloseError = err ? err : "";
			if (err)
				mg_free(err);
			serverAllowReconnect = mg_json_get_long(j, "$.allowReconnect", 1) != 0;
			serverClose = true;
			break;
		}
		default:
			break;
		}
	}

	bool SendRaw(const std::string& payload)
	{
		std::lock_guard<std::mutex> lk(netMutex);
		if (!hWebSocket)
			return false;
		DWORD rc = WinHttpWebSocketSend(
		    hWebSocket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
		    const_cast<char*>(payload.data()), (DWORD)payload.size());
		return rc == NO_ERROR;
	}

	void PingLoop()
	{
		const std::string ping = std::string("{\"type\":6}") + kRecordSeparator;
		while (!stop.load())
		{
			if (!SleepMs(kPingIntervalMs))
				return;
			if (connected.load())
				SendRaw(ping);
		}
	}

	// Close + free the connection handles. Idempotent.
	void Cleanup()
	{
		HINTERNET ws, connect, session;
		{
			std::lock_guard<std::mutex> lk(netMutex);
			ws = hWebSocket;
			connect = hConnect;
			session = hSession;
			hWebSocket = hConnect = hSession = nullptr;
		}
		if (ws)
		{
			WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,
			                      nullptr, 0);
			WinHttpCloseHandle(ws);
		}
		if (connect)
			WinHttpCloseHandle(connect);
		if (session)
			WinHttpCloseHandle(session);
	}
};

SignalRClient::SignalRClient(std::string apiBaseUrl, std::string hubPath,
                             std::function<std::string()> tokenProvider,
                             const DpopKey& key)
    : m_impl(new Impl(std::move(apiBaseUrl), std::move(hubPath),
                      std::move(tokenProvider), key))
{
}

SignalRClient::~SignalRClient()
{
	Stop();
}

void SignalRClient::SetOnConnected(ConnectedHandler h)
{
	m_impl->onConnected = std::move(h);
}

void SignalRClient::SetOnClosed(ClosedHandler h)
{
	m_impl->onClosed = std::move(h);
}

void SignalRClient::SetOnEvent(EventHandler h)
{
	m_impl->onEvent = std::move(h);
}

void SignalRClient::Start()
{
	if (m_impl->started.exchange(true))
		return; // already running
	m_impl->stop.store(false);
	m_impl->lifecycleThread = std::thread([this]() { m_impl->Lifecycle(); });
	m_impl->pingThread = std::thread([this]() { m_impl->PingLoop(); });
}

void SignalRClient::Stop()
{
	if (!m_impl->started.exchange(false))
		return;

	m_impl->stop.store(true);
	m_impl->waitCv.notify_all();

	// Initiate the close so a blocking receive on the lifecycle thread unblocks
	// promptly (Cleanup() on that thread does the actual handle free).
	{
		std::lock_guard<std::mutex> lk(m_impl->netMutex);
		if (m_impl->hWebSocket)
			WinHttpWebSocketClose(m_impl->hWebSocket,
			                      WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
	}

	if (m_impl->lifecycleThread.joinable())
		m_impl->lifecycleThread.join();
	if (m_impl->pingThread.joinable())
		m_impl->pingThread.join();

	// Fail any still-pending invocations.
	std::map<std::string, CompletionHandler> leftover;
	{
		std::lock_guard<std::mutex> lk(m_impl->invMutex);
		leftover.swap(m_impl->pending);
	}
	for (auto& kv : leftover)
		kv.second(false, "stopped");
}

bool SignalRClient::IsConnected() const
{
	return m_impl->connected.load();
}

void SignalRClient::Send(const std::string& target, const std::string& argsJson)
{
	if (!m_impl->connected.load())
		return;
	std::string frame = "{\"type\":1,\"target\":\"" + target +
	                    "\",\"arguments\":" + argsJson + "}";
	frame += kRecordSeparator;
	m_impl->SendRaw(frame);
}

void SignalRClient::Invoke(const std::string& target, const std::string& argsJson,
                           CompletionHandler cb)
{
	if (!m_impl->connected.load())
	{
		if (cb)
			cb(false, "disconnected");
		return;
	}

	std::string id = std::to_string(m_impl->invCounter.fetch_add(1));
	{
		std::lock_guard<std::mutex> lk(m_impl->invMutex);
		m_impl->pending[id] = std::move(cb);
	}

	std::string frame = "{\"type\":1,\"invocationId\":\"" + id +
	                    "\",\"target\":\"" + target +
	                    "\",\"arguments\":" + argsJson + "}";
	frame += kRecordSeparator;

	if (!m_impl->SendRaw(frame))
	{
		CompletionHandler failed;
		{
			std::lock_guard<std::mutex> lk(m_impl->invMutex);
			auto it = m_impl->pending.find(id);
			if (it != m_impl->pending.end())
			{
				failed = it->second;
				m_impl->pending.erase(it);
			}
		}
		if (failed)
			failed(false, "send failed");
	}
}
