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
//   Party-chat panel: the shared HTML scrollback (ChatWebView, same control as
//   the global-chat tab) plus an input row. Lives in the launcher's collapsible
//   bottom strip, toggled with the rest of the social UI. Renders from
//   SocialState and acts through SocialController; XRC-subclassed like the tabs.
//
//-----------------------------------------------------------------------------

#pragma once

#include <wx/panel.h>

#include <string>

#include "chat_webview.h"

class SocialController;
class SocialState;
class wxButton;
class wxCommandEvent;
class wxTextCtrl;

class PartyChatPanel : public wxPanel
{
	wxDECLARE_DYNAMIC_CLASS(PartyChatPanel);

  public:
	// Default-constructed by XRC (subclass="PartyChatPanel"); dlgMain calls PostInit().
	PartyChatPanel();

	// Retrieve the XRC-built child controls, create the web view, bind events.
	void PostInit();

	// Set/clear the active controller on sign-in/out (null clears the scrollback).
	void SetController(SocialController* controller);

	// Re-render the party chat from the controller's SocialState.
	void Refresh();

  private:
	void OnSend(wxCommandEvent& event);
	void OnEnter(wxCommandEvent& event);
	void SendCurrent();

	void RenderChat(const SocialState& state);
	// Add Friend / Block(/Unblock) menu for a username right-clicked in the
	// scrollback.
	void ShowUserContextMenu(const std::string& subject);

	SocialController* m_controller = nullptr;

	ChatWebView m_chat; // HTML scrollback (shared with the global-chat tab)
	wxTextCtrl* m_chatInput = nullptr;
	wxButton* m_sendButton = nullptr;

	// Chat scrollback signature so we only re-render when it changes.
	std::string m_chatSig;
};
