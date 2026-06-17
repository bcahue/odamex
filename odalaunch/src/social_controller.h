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
//   Orchestrates the social features: owns the PartyClient (hub) + ApiClient
//   (REST) + SocialState. Hub events and REST results land on background
//   threads; everything is marshalled onto the UI thread (wxEvtHandler::
//   CallAfter) before touching SocialState, then SocialState::NotifyChanged()
//   tells the views to re-render. A shared "alive" flag makes in-flight
//   marshalled callbacks safe across teardown (sign-out).
//
//   REST calls run on a single dedicated worker thread (a task queue); hub
//   sends are fire-and-forget on the hub's own transport thread.
//
//-----------------------------------------------------------------------------

#pragma once

#include <wx/string.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "social_state.h"

class DpopKey;
class wxEvtHandler;
class PartyClient;
class ApiClient;

class SocialController
{
  public:
	// `key` and the LauncherSession behind `tokenProvider` must outlive this
	// controller (dlgMain resets the controller before the session goes away).
	// `ui` is the frame used to marshal callbacks onto the UI thread.
	SocialController(const wxString& apiBaseUrl,
	                 std::function<std::string()> tokenProvider, const DpopKey& key,
	                 wxEvtHandler* ui);
	~SocialController();

	SocialController(const SocialController&) = delete;
	SocialController& operator=(const SocialController&) = delete;

	void Start();
	void Stop();

	// Fast, non-blocking teardown for application exit: signals cancellation and
	// detaches the hub + worker threads instead of joining them, so the app can
	// quit immediately even mid-connect / mid-REST. The caller must then leak
	// this controller (its members stay alive for the detached threads until the
	// process exits); never call Stop()/destruct after Detach().
	void Detach();

	SocialState& State() { return m_state; }

	// UI-thread actions.
	void RefreshAll();
	void SendGlobalMessage(const std::string& text);
	void SendFriendRequest(const std::string& subject);
	// Add-by-username: resolves + sends server-side. `done` is invoked on the UI
	// thread with the outcome and a user-facing message (errors mapped to text).
	void SendFriendRequestByUsername(
	    const std::string& username,
	    std::function<void(bool ok, const std::string& message)> done);
	void AcceptRequest(long long requestId);
	void DeclineRequest(long long requestId);
	void CancelRequest(long long requestId);
	void RemoveFriend(const std::string& subject);
	void Block(const std::string& subject);
	void Unblock(const std::string& subject);

  private:
	void WireHubEvents();
	void WorkerLoop();
	void Enqueue(std::function<void()> work); // run on the worker thread
	void Marshal(std::function<void()> uiWork); // run on the UI thread, alive-guarded
	void DoRefreshAll();          // worker thread: fetch + parse + marshal-apply
	void DoRefreshParticipants(); // worker thread: refetch the participant roster only

	std::unique_ptr<ApiClient> m_api;
	std::unique_ptr<PartyClient> m_hub;
	SocialState m_state;
	wxEvtHandler* m_ui;

	std::shared_ptr<std::atomic<bool>> m_alive;

	std::thread m_worker;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	std::queue<std::function<void()>> m_tasks;
	bool m_running = false;
};
