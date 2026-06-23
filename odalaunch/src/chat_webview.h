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
//   Reusable HTML chat scrollback shared by the global-chat and party-chat
//   tabs. Wraps a wxWebView loaded with the chat_view.inc page (color emoji +
//   good typography on the Edge backend): messages are pushed in as a JSON
//   array via SetMessages(); the page renders them safely (textContent, never
//   innerHTML) and posts back link-open / username-menu commands which surface
//   through the OnOpenUrl / OnUserMenu callbacks. Loading is async, so the
//   latest SetMessages() payload is replayed once the page reports ready.
//
//-----------------------------------------------------------------------------

#pragma once

#include <wx/string.h>

#include <functional>
#include <string>

class wxWindow;
class wxWebView;
class wxWebViewEvent;

class ChatWebView
{
  public:
	ChatWebView();

	// Build the web view into `host` (filling it via a sizer), pick the best
	// backend, register the "oda" script-message handler, theme + load the page.
	void Create(wxWindow* host);

	// Replace the scrollback with `messagesJson` — a JSON array string built by
	// the caller, shaped like the chat_view.inc setMessages() contract:
	// [{id,user,sub,text,ts,blocked,deleted}]. Held and replayed if the page
	// isn't loaded yet.
	void SetMessages(const wxString& messagesJson);

	bool IsReady() const { return m_ready; }

	// Host hooks. OnOpenUrl: a clicked (validated http/https) link. OnUserMenu:
	// a right-clicked username's subject. OnReady: page finished loading.
	std::function<void(const wxString& url)> OnOpenUrl;
	std::function<void(const std::string& subject)> OnUserMenu;
	std::function<void()> OnReady;

	// Build a JSON string literal (escaped, with surrounding quotes) for
	// embedding in a setMessages() payload.
	static std::string JsonStr(const std::string& s);

  private:
	void OnLoaded(wxWebViewEvent& event);
	void OnScriptMessage(wxWebViewEvent& event);
	void RunJs(const wxString& script);

	wxWebView* m_web = nullptr;
	bool m_ready = false;  // SetPage is async; only script once the page is loaded
	bool m_isEdge = false; // Edge backend -> RunScriptAsync; else legacy RunScript
	wxString m_pending;    // last SetMessages payload queued before the page loaded
};
