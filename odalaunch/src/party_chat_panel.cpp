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
//   Party-chat panel. See party_chat_panel.h.
//
//-----------------------------------------------------------------------------

#include "party_chat_panel.h"

#include <wx/button.h>
#include <wx/fileconf.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/string.h>
#include <wx/textctrl.h>
#include <wx/utils.h>
#include <wx/xrc/xmlres.h>

#include <string>

#include "oda_defs.h"
#include "profanity_filter.h"
#include "social_controller.h"

wxIMPLEMENT_DYNAMIC_CLASS(PartyChatPanel, wxPanel);

PartyChatPanel::PartyChatPanel() = default;

void PartyChatPanel::PostInit()
{
	// Shared HTML scrollback (same control as the global-chat tab): clicked links
	// open in the system browser, a right-clicked username shows the Friend/Block
	// menu, and the page re-renders once it reports ready.
	wxPanel* chatHost = XRCCTRL(*this, "Id_PartyChatHost", wxPanel);
	m_chat.OnOpenUrl = [](const wxString& url) { wxLaunchDefaultBrowser(url); };
	m_chat.OnUserMenu = [this](const std::string& sub) { ShowUserContextMenu(sub); };
	m_chat.OnReady = [this]() {
		if (m_controller)
			RenderChat(m_controller->State());
	};
	m_chat.Create(chatHost);

	m_chatInput = XRCCTRL(*this, "Id_PartyChatInput", wxTextCtrl);
	m_sendButton = XRCCTRL(*this, "Id_PartyChatSendBtn", wxButton);

	m_sendButton->Bind(wxEVT_BUTTON, &PartyChatPanel::OnSend, this);
	m_chatInput->Bind(wxEVT_TEXT_ENTER, &PartyChatPanel::OnEnter, this);

	m_chatInput->Enable(false);
	m_sendButton->Enable(false);
}

void PartyChatPanel::SetController(SocialController* controller)
{
	m_controller = controller;
	if (!controller)
	{
		m_chat.SetMessages("[]");
		m_chatSig.clear();
		m_chatInput->Clear();
		m_chatInput->Enable(false);
		m_sendButton->Enable(false);
		return;
	}
	Refresh();
}

void PartyChatPanel::Refresh()
{
	if (!m_controller)
		return;
	const SocialState& state = m_controller->State();

	// Chat is usable whenever you're actually in a party.
	const bool active = state.party.active;
	m_chatInput->Enable(active);
	m_sendButton->Enable(active);

	RenderChat(state);
}

void PartyChatPanel::RenderChat(const SocialState& state)
{
	std::string sig;
	for (const auto& line : state.party.chat)
	{
		sig += line.sentAt;
		sig += '|';
		sig += line.fromSubject;
		sig += '|';
		sig += line.text;
		sig += '\n';
	}
	if (sig == m_chatSig)
		return;
	m_chatSig = sig;

	// Match the global-chat tab's profanity preference (default on).
	bool filterProfanity = true;
	{
		wxFileConfig cfg;
		cfg.Read(FILTERPROFANITY, &filterProfanity, ODA_UIFILTERPROFANITY);
	}

	// Build the same setMessages() payload the global-chat tab uses. Party chat
	// has no per-message id and is never "blocked", so the id is just a render
	// key (the row index) and ts is left empty (no timestamps in party chat).
	std::string json = "[";
	bool first = true;
	int idx = 0;
	for (const auto& line : state.party.chat)
	{
		if (!first)
			json += ",";
		first = false;

		const std::string user = line.fromUsername.empty() ? line.fromSubject : line.fromUsername;
		const std::string text = filterProfanity ? CensorProfanity(line.text) : line.text;

		json += "{\"id\":" + ChatWebView::JsonStr(std::to_string(idx++));
		json += ",\"user\":" + ChatWebView::JsonStr(user);
		json += ",\"sub\":" + ChatWebView::JsonStr(line.fromSubject);
		json += ",\"text\":" + ChatWebView::JsonStr(text);
		json += ",\"ts\":\"\"";
		json += ",\"blocked\":false";
		json += ",\"deleted\":false";
		json += "}";
	}
	json += "]";

	m_chat.SetMessages(wxString::FromUTF8(json));
}

void PartyChatPanel::ShowUserContextMenu(const std::string& sub)
{
	if (!m_controller || sub.empty())
		return;

	const SocialState& state = m_controller->State();
	// No friend/block actions on yourself.
	if (sub == state.selfSubject)
		return;

	const bool blocked = state.IsBlocked(sub);

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

void PartyChatPanel::OnSend(wxCommandEvent& WXUNUSED(event))
{
	SendCurrent();
}

void PartyChatPanel::OnEnter(wxCommandEvent& WXUNUSED(event))
{
	SendCurrent();
}

void PartyChatPanel::SendCurrent()
{
	if (!m_controller)
		return;
	const wxString text = m_chatInput->GetValue();
	if (text.empty())
		return;
	m_controller->SendPartyMessage(text.utf8_string());
	m_chatInput->Clear();
}
