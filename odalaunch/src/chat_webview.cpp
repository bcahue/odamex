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
//   Reusable HTML chat scrollback. See chat_webview.h.
//
//-----------------------------------------------------------------------------

#include "chat_webview.h"

#include <wx/colour.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/webview.h>

#ifdef __WXMSW__
#include <wx/msw/webview_ie.h>
#endif

#include <cstdio>

namespace
{
// The page shell lives in chat_view.inc (a single raw-string literal) so the
// markup can grow on its own. Colours (@BG@/@FG@) are substituted from the
// system theme before load; see that file for the JS contract.
const char* const kChatHtml =
#include "chat_view.inc"
    ;
} // namespace

ChatWebView::ChatWebView() = default;

std::string ChatWebView::JsonStr(const std::string& s)
{
	std::string out = "\"";
	for (char c : s)
	{
		switch (c)
		{
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (static_cast<unsigned char>(c) < 0x20)
			{
				char buf[8];
				std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
				out += buf;
			}
			else
			{
				out += c;
			}
		}
	}
	out += "\"";
	return out;
}

void ChatWebView::Create(wxWindow* host)
{
	// Prefer the Edge (Chromium) backend when the wx build provides it; otherwise
	// the default backend is used (legacy on Windows until wx is rebuilt with
	// Edge).
	wxString backend = wxWebViewBackendDefault;
	if (wxWebView::IsBackendAvailable(wxWebViewBackendEdge))
	{
		backend = wxWebViewBackendEdge;
		m_isEdge = true;
	}
#ifdef __WXMSW__
	else
		// Falling back to the legacy IE control: by default it emulates IE7, where
		// modern JS/CSS silently fail (so setMessages() would throw and nothing
		// renders). Opt this exe into IE11 mode before the control is created.
		// Harmless/ignored once the wx build provides the Edge backend.
		wxWebViewIE::MSWSetEmulationLevel(wxWEBVIEWIE_EMU_IE11);
#endif
	m_web = wxWebView::New(host, wxID_ANY, wxWebViewDefaultURLStr, wxDefaultPosition,
	                       wxDefaultSize, backend);
	m_web->EnableContextMenu(false);

	// Let the page hand us a username's subject on right-click and clicked links.
	// Must be registered before the page loads. Only the Edge backend supports
	// script message handlers; where it isn't available the page's window.oda
	// check makes it a no-op.
	if (m_web->AddScriptMessageHandler("oda"))
		m_web->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &ChatWebView::OnScriptMessage, this);

	wxBoxSizer* hostSizer = new wxBoxSizer(wxVERTICAL);
	hostSizer->Add(m_web, 1, wxEXPAND);
	host->SetSizer(hostSizer);
	host->Layout();

	// Theme the page from the system colours, then load it.
	const wxColour bg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
	const wxColour fg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
	wxString html(kChatHtml, wxConvUTF8);
	html.Replace("@BG@", bg.GetAsString(wxC2S_HTML_SYNTAX));
	html.Replace("@FG@", fg.GetAsString(wxC2S_HTML_SYNTAX));
	m_web->Bind(wxEVT_WEBVIEW_LOADED, &ChatWebView::OnLoaded, this);
	m_web->SetPage(html, wxString());
}

void ChatWebView::OnLoaded(wxWebViewEvent& WXUNUSED(event))
{
	m_ready = true;
	if (!m_pending.empty())
	{
		RunJs("setMessages(" + m_pending + ");");
		m_pending.clear();
	}
	if (OnReady)
		OnReady();
}

void ChatWebView::SetMessages(const wxString& messagesJson)
{
	if (!m_ready)
	{
		// Hold the latest payload; OnLoaded() replays it once the page is ready.
		m_pending = messagesJson;
		return;
	}
	RunJs("setMessages(" + messagesJson + ");");
}

void ChatWebView::RunJs(const wxString& script)
{
	if (!m_web || !m_ready)
		return;
	// Edge (Chromium) supports the async, non-blocking variant; the legacy IE
	// backend only implements the synchronous RunScript. We never use the result.
	if (m_isEdge)
		m_web->RunScriptAsync(script);
	else
		m_web->RunScript(script);
}

void ChatWebView::OnScriptMessage(wxWebViewEvent& event)
{
	// Tab-delimited command from the page (see chat_view.inc):
	//   "open\t<url>" -> open a clicked chat link in the system browser
	//   "menu\t<sub>" -> show the host's menu for a right-clicked username
	const wxString msg = event.GetString();
	if (msg.StartsWith("open\t"))
	{
		const wxString url = msg.Mid(5);
		// The page only ever builds http(s) targets; re-check before handing it
		// to the host.
		if ((url.StartsWith("http://") || url.StartsWith("https://")) && OnOpenUrl)
			OnOpenUrl(url);
	}
	else if (msg.StartsWith("menu\t"))
	{
		if (OnUserMenu)
			OnUserMenu(msg.Mid(5).utf8_string());
	}
}
