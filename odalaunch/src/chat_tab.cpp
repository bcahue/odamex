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
#include <wx/utils.h>
#include <wx/xrc/xmlres.h>

#include "lst_custom.h"
#include "oda_defs.h"
#include "profanity_filter.h"
#include "social_controller.h"

wxIMPLEMENT_DYNAMIC_CLASS(ChatTab, wxPanel);

ChatTab::ChatTab() = default;

void ChatTab::PostInit()
{
	m_status = XRCCTRL(*this, "Id_ChatStatus", wxStaticText);
	m_input = XRCCTRL(*this, "Id_ChatInput", wxTextCtrl);
	m_send = XRCCTRL(*this, "Id_ChatSend", wxButton);
	m_players = XRCCTRL(*this, "Id_ChatPlayers", wxAdvancedListCtrl);

	// Build the shared HTML scrollback into its host panel. Clicked links open
	// in the system browser; a right-clicked username shows the same Friend/Block
	// menu as the players list; the page renders once it reports ready.
	wxPanel* host = XRCCTRL(*this, "Id_ChatDisplayHost", wxPanel);
	m_chat.OnOpenUrl = [](const wxString& url) { wxLaunchDefaultBrowser(url); };
	m_chat.OnUserMenu = [this](const std::string& sub) { ShowUserContextMenu(sub); };
	m_chat.OnReady = [this]() {
		if (m_controller)
			RenderMessages(m_controller->State());
	};
	m_chat.Create(host);

	// Online-players list: not sortable (so a clicked row maps directly to a
	// subject), two columns, right-click for friend/block actions.
	m_players->HeaderUsable(false);
	m_players->InsertColumn(0, "Player", wxLIST_FORMAT_LEFT, 110);
	m_players->InsertColumn(1, "Status", wxLIST_FORMAT_LEFT, 64);
	m_players->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &ChatTab::OnPlayerRightClick, this);

	m_send->Bind(wxEVT_BUTTON, &ChatTab::OnSend, this);
	m_input->Bind(wxEVT_TEXT_ENTER, &ChatTab::OnEnter, this);

	m_input->Enable(false);
	m_send->Enable(false);
}

void ChatTab::SetController(SocialController* controller)
{
	m_controller = controller;
	if (!controller)
	{
		m_input->Enable(false);
		m_send->Enable(false);
		m_status->SetLabel("Sign in to use global chat.");
		m_chat.SetMessages("[]");
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

wxString ChatTab::FormatTimestamp(const std::string& isoUtc, bool use24Hour,
                                  bool showSeconds) const
{
	if (isoUtc.size() < 19)
		return wxEmptyString;
	// Take just "YYYY-MM-DDTHH:MM:SS" (drop fractional seconds / offset).
	wxString s = wxString::FromUTF8(isoUtc).Left(19);
	wxDateTime t;
	if (!t.ParseISOCombined(s, 'T'))
		return wxEmptyString;
	t.MakeFromTimezone(wxDateTime::UTC); // interpret components as UTC -> local

	// %H = 24-hour, %I = 12-hour (with %p AM/PM). Seconds are optional.
	wxString fmt;
	if (use24Hour)
		fmt = showSeconds ? "%H:%M:%S" : "%H:%M";
	else
		fmt = showSeconds ? "%I:%M:%S %p" : "%I:%M %p";
	return t.Format(fmt);
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
	if (!m_chat.IsReady())
		return; // the web view's OnReady will render once the page is ready

	// Read the chat display options fresh so a change in the settings dialog takes
	// effect on the next render. Profanity filtering defaults on, timestamps off,
	// 24-hour clock on, seconds off.
	bool filterProfanity = true;
	bool showTimestamps = false;
	bool use24Hour = true;
	bool showSeconds = false;
	{
		wxFileConfig cfg;
		cfg.Read(FILTERPROFANITY, &filterProfanity, ODA_UIFILTERPROFANITY);
		cfg.Read(SHOWCHATTIMESTAMPS, &showTimestamps, ODA_UISHOWCHATTIMESTAMPS);
		cfg.Read(CHAT24HOURTIME, &use24Hour, ODA_UICHAT24HOURTIME);
		cfg.Read(CHATSHOWSECONDS, &showSeconds, ODA_UICHATSHOWSECONDS);
	}

	std::string json = "[";
	bool first = true;
	for (const auto& line : state.chat)
	{
		if (!first)
			json += ",";
		first = false;

		const std::string user = line.authorUsername.empty() ? line.authorSubject : line.authorUsername;
		const std::string ts =
		    showTimestamps ? FormatTimestamp(line.sentAt, use24Hour, showSeconds).utf8_string()
		                   : std::string();
		const bool blocked = state.IsBlocked(line.authorSubject);
		const std::string text = filterProfanity ? CensorProfanity(line.text) : line.text;

		json += "{\"id\":" + ChatWebView::JsonStr(line.messageId);
		json += ",\"user\":" + ChatWebView::JsonStr(user);
		json += ",\"sub\":" + ChatWebView::JsonStr(line.authorSubject);
		json += ",\"text\":" + ChatWebView::JsonStr(text);
		json += ",\"ts\":" + ChatWebView::JsonStr(ts);
		json += ",\"blocked\":";
		json += blocked ? "true" : "false";
		json += ",\"deleted\":";
		json += line.deleted ? "true" : "false";
		json += "}";
	}
	json += "]";

	m_chat.SetMessages(wxString::FromUTF8(json));
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
	const long row = event.GetIndex();
	if (row < 0 || row >= static_cast<long>(m_playerSubjects.size()))
		return;
	ShowUserContextMenu(m_playerSubjects[row]);
}

void ChatTab::ShowUserContextMenu(const std::string& sub)
{
	if (!m_controller || sub.empty())
		return;

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
