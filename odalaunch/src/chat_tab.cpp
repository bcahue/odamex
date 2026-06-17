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
//   Global-chat tab. See chat_tab.h.
//
//-----------------------------------------------------------------------------

#include "chat_tab.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/colour.h>
#include <wx/datetime.h>
#include <wx/fileconf.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/webview.h>
#include <wx/xrc/xmlres.h>

#ifdef __WXMSW__
#include <wx/msw/webview_ie.h>
#endif

#include <cstdio>

#include "lst_custom.h"
#include "social_controller.h"

wxIMPLEMENT_DYNAMIC_CLASS(ChatTab, wxPanel);

namespace
{
const char* const kShowTimestampsKey = "ShowChatTimestamps";

// The page shell. Colours (@BG@/@FG@) are substituted from the system theme so
// it matches light/dark. All user content is written with textContent (never
// innerHTML), so chat text cannot inject markup. Block reveal is local JS.
const char* const kChatHtml = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta http-equiv="X-UA-Compatible" content="IE=edge">
<style>
  html,body{margin:0;padding:0;height:100%;}
  body{font-family:'Segoe UI',sans-serif;font-size:13px;background:@BG@;color:@FG@;
       padding:4px 6px;box-sizing:border-box;overflow-y:auto;overflow-x:hidden;}
  .msg{margin:1px 0;line-height:1.35;word-wrap:break-word;overflow-wrap:anywhere;}
  .user{font-weight:bold;}
  .ts{opacity:0.55;}
  .sys{opacity:0.6;font-style:italic;}
  .blocked{color:#b06a6a;font-style:italic;cursor:pointer;}
</style></head>
<body><div id="log"></div>
<script>
  var current = [];
  var revealed = {};
  function buildMsg(m){
    var div = document.createElement('div');
    if(m.deleted){ div.className='msg sys'; div.textContent='[message removed by a moderator]'; return div; }
    div.className='msg';
    if(m.ts){ var s=document.createElement('span'); s.className='ts'; s.textContent='['+m.ts+'] '; div.appendChild(s); }
    if(m.blocked && !revealed[m.id]){
      div.className='msg blocked';
      var b=document.createElement('span'); b.textContent='[blocked user — click to show]'; div.appendChild(b);
      div.onclick=function(){ revealed[m.id]=true; rerender(); };
      return div;
    }
    var u=document.createElement('span'); u.className='user'; u.textContent=m.user+': '; div.appendChild(u);
    var t=document.createElement('span'); t.textContent=m.text; div.appendChild(t);
    return div;
  }
  function rerender(){
    var log=document.getElementById('log');
    log.innerHTML='';
    for(var i=0;i<current.length;i++) log.appendChild(buildMsg(current[i]));
    window.scrollTo(0, document.body.scrollHeight);
  }
  function setMessages(arr){ current=arr; rerender(); }
</script></body></html>
)HTML";

// JSON string literal (with quotes), escaping what a JS/JSON string requires.
std::string JsonStr(const std::string& s)
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
} // namespace

ChatTab::ChatTab() = default;

void ChatTab::PostInit()
{
	m_status = XRCCTRL(*this, "Id_ChatStatus", wxStaticText);
	m_input = XRCCTRL(*this, "Id_ChatInput", wxTextCtrl);
	m_send = XRCCTRL(*this, "Id_ChatSend", wxButton);
	m_timestamps = XRCCTRL(*this, "Id_ChatTimestamps", wxCheckBox);
	m_players = XRCCTRL(*this, "Id_ChatPlayers", wxAdvancedListCtrl);

	// Build the scrollback web view into its host panel. Prefer the Edge
	// (Chromium) backend when the wx build provides it; otherwise the default
	// backend is used (legacy on Windows until wx is rebuilt with Edge).
	wxPanel* host = XRCCTRL(*this, "Id_ChatDisplayHost", wxPanel);
	wxString backend = wxWebViewBackendDefault;
	if (wxWebView::IsBackendAvailable(wxWebViewBackendEdge))
		backend = wxWebViewBackendEdge;
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
	m_web->Bind(wxEVT_WEBVIEW_LOADED, &ChatTab::OnWebLoaded, this);
	m_web->SetPage(html, wxString());

	// Online-players list: not sortable (so a clicked row maps directly to a
	// subject), two columns, right-click for friend/block actions.
	m_players->HeaderUsable(false);
	m_players->InsertColumn(0, "Player", wxLIST_FORMAT_LEFT, 110);
	m_players->InsertColumn(1, "Status", wxLIST_FORMAT_LEFT, 64);
	m_players->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &ChatTab::OnPlayerRightClick, this);

	wxFileConfig cfg;
	cfg.Read(kShowTimestampsKey, &m_showTimestamps, false);
	m_timestamps->SetValue(m_showTimestamps);

	m_send->Bind(wxEVT_BUTTON, &ChatTab::OnSend, this);
	m_input->Bind(wxEVT_TEXT_ENTER, &ChatTab::OnEnter, this);
	m_timestamps->Bind(wxEVT_CHECKBOX, &ChatTab::OnToggleTimestamps, this);

	m_input->Enable(false);
	m_send->Enable(false);
}

void ChatTab::OnWebLoaded(wxWebViewEvent& WXUNUSED(event))
{
	m_webReady = true;
	if (m_controller)
		RenderMessages(m_controller->State());
}

void ChatTab::SetController(SocialController* controller)
{
	m_controller = controller;
	if (!controller)
	{
		m_input->Enable(false);
		m_send->Enable(false);
		m_status->SetLabel("Sign in to use global chat.");
		if (m_webReady)
			m_web->RunScript("setMessages([]);");
		m_players->DeleteAllItems();
		m_playerSubjects.clear();
		m_playersSig.clear();
		m_selfSubject.clear();
		return;
	}
	// Input enable + status are driven by the hub connection state in Refresh().
	Refresh();
}

void ChatTab::OnSend(wxCommandEvent& WXUNUSED(event))
{
	SendCurrent();
}

void ChatTab::OnEnter(wxCommandEvent& WXUNUSED(event))
{
	SendCurrent();
}

void ChatTab::OnToggleTimestamps(wxCommandEvent& WXUNUSED(event))
{
	m_showTimestamps = m_timestamps->GetValue();
	wxFileConfig cfg;
	cfg.Write(kShowTimestampsKey, m_showTimestamps);
	cfg.Flush();
	if (m_controller)
		RenderMessages(m_controller->State());
}

void ChatTab::SendCurrent()
{
	if (!m_controller)
		return;
	wxString text = m_input->GetValue();
	text.Trim(true).Trim(false); // strip trailing then leading whitespace
	if (text.IsEmpty())
		return;
	m_controller->SendGlobalMessage(text.utf8_string());
	m_input->Clear();
	m_input->SetFocus();
	// The message echoes back via GlobalMessageReceived, so don't render it here.
}

wxString ChatTab::FormatTimestamp(const std::string& isoUtc) const
{
	if (isoUtc.size() < 19)
		return wxEmptyString;
	// Take just "YYYY-MM-DDTHH:MM:SS" (drop fractional seconds / offset).
	wxString s = wxString::FromUTF8(isoUtc).Left(19);
	wxDateTime t;
	if (!t.ParseISOCombined(s, 'T'))
		return wxEmptyString;
	t.MakeFromTimezone(wxDateTime::UTC); // interpret components as UTC -> local
	return t.Format("%H:%M");
}

void ChatTab::Refresh()
{
	if (!m_controller)
		return;

	const SocialState& state = m_controller->State();

	// Connection state takes priority in the status line so a connection problem
	// is visible rather than failing silently.
	const bool connected = (state.hubState == SocialState::HubState::Connected);
	if (state.hubState == SocialState::HubState::Connecting)
		m_status->SetLabel("Connecting to chat...");
	else if (state.hubState == SocialState::HubState::Disconnected)
		m_status->SetLabel("Chat disconnected - reconnecting...");
	else if (state.slowModeSeconds > 0)
		m_status->SetLabel(wxString::Format("Slow mode: %d seconds between messages.",
		                                    state.slowModeSeconds));
	else
		m_status->SetLabel(wxEmptyString);

	// Only allow sending while actually connected.
	m_input->Enable(connected);
	m_send->Enable(connected);

	RebuildPlayers(state);
	RenderMessages(state);
}

void ChatTab::RenderMessages(const SocialState& state)
{
	if (!m_webReady)
		return; // OnWebLoaded() will render once the page is ready

	std::string json = "[";
	bool first = true;
	for (const auto& line : state.chat)
	{
		if (!first)
			json += ",";
		first = false;

		const std::string user = line.authorUsername.empty() ? line.authorSubject : line.authorUsername;
		const std::string ts = m_showTimestamps ? FormatTimestamp(line.sentAt).utf8_string() : std::string();
		const bool blocked = state.IsBlocked(line.authorSubject);

		json += "{\"id\":" + JsonStr(line.messageId);
		json += ",\"user\":" + JsonStr(user);
		json += ",\"text\":" + JsonStr(line.text);
		json += ",\"ts\":" + JsonStr(ts);
		json += ",\"blocked\":";
		json += blocked ? "true" : "false";
		json += ",\"deleted\":";
		json += line.deleted ? "true" : "false";
		json += "}";
	}
	json += "]";

	// RunScript (synchronous) rather than RunScriptAsync: the legacy IE backend
	// only implements the former, and we don't need the result. Edge supports both.
	wxString script = "setMessages(" + wxString::FromUTF8(json) + ");";
	m_web->RunScript(script);
}

void ChatTab::RebuildPlayers(const SocialState& state)
{
	// Skip the rebuild (which would drop selection) when nothing changed.
	std::string sig;
	for (const auto& p : state.participants)
	{
		sig += p.subject;
		sig += '|';
		sig += p.status;
		sig += state.IsBlocked(p.subject) ? "|b\n" : "|\n";
	}
	if (sig == m_playersSig)
		return;
	m_playersSig = sig;

	m_players->DeleteAllItems();
	m_playerSubjects.clear();
	m_selfSubject.clear();

	for (const auto& p : state.participants)
	{
		wxString name = wxString::FromUTF8(p.username.empty() ? p.subject : p.username);
		if (p.isSelf)
		{
			name += " (you)";
			m_selfSubject = p.subject;
		}
		const long row = m_players->ALCInsertItem(name);

		wxString statusText;
		if (state.IsBlocked(p.subject))
			statusText = "Blocked";
		else if (p.status == "InMatch")
			statusText = "In match";
		else
			statusText = wxString::FromUTF8(p.status); // "Online"
		m_players->SetItem(row, 1, statusText);

		m_playerSubjects.push_back(p.subject);
	}
}

void ChatTab::OnPlayerRightClick(wxListEvent& event)
{
	if (!m_controller)
		return;
	const long row = event.GetIndex();
	if (row < 0 || row >= static_cast<long>(m_playerSubjects.size()))
		return;
	const std::string sub = m_playerSubjects[row];

	// No friend/block actions on yourself.
	if (!m_selfSubject.empty() && sub == m_selfSubject)
		return;

	const bool blocked = m_controller->State().IsBlocked(sub);

	const int kAddFriend = wxID_HIGHEST + 1;
	const int kBlock = wxID_HIGHEST + 2;
	const int kUnblock = wxID_HIGHEST + 3;

	wxMenu menu;
	if (blocked)
	{
		menu.Append(kUnblock, "Unblock");
	}
	else
	{
		menu.Append(kAddFriend, "Add Friend");
		menu.Append(kBlock, "Block");
	}

	const int choice = GetPopupMenuSelectionFromUser(menu);
	if (choice == kAddFriend)
		m_controller->SendFriendRequest(sub);
	else if (choice == kBlock)
		m_controller->Block(sub);
	else if (choice == kUnblock)
		m_controller->Unblock(sub);
}
