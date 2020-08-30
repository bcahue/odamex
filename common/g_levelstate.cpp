// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id$
//
// Copyright (C) 2006-2020 by The Odamex Team.
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
//   Manage state for warmup and complicated gametype flows.
//
//-----------------------------------------------------------------------------

#include "g_levelstate.h"

#include <cmath>

#include "c_cvars.h"
#include "c_dispatch.h"
#include "d_player.h"
#include "g_game.h"
#include "g_level.h"
#include "i_net.h"
#include "i_system.h"

EXTERN_CVAR(sv_gametype)
EXTERN_CVAR(sv_warmup)
EXTERN_CVAR(sv_warmup_autostart)
EXTERN_CVAR(sv_countdown)
EXTERN_CVAR(sv_timelimit)

LevelState levelstate;

/**
 * @brief State getter.
 */
LevelState::States LevelState::getState() const
{
	return _state;
}

/**
 * @brief Get state as string.
 */
const char* LevelState::getStateString() const
{
	switch (_state)
	{
	case LevelState::INGAME:
		return "In-game";
	case LevelState::COUNTDOWN:
		return "Countdown";
	case LevelState::ENDGAME:
		return "Endgame";
	case LevelState::WARMUP:
		return "Warmup";
	case LevelState::WARMUP_COUNTDOWN:
		return "Warmup Countdown";
	case LevelState::WARMUP_FORCED_COUNTDOWN:
		return "Warmup Forced Countdown";
	default:
		return "Unknown";
	}
}

/**
 * @brief Countdown getter.
 */
short LevelState::getCountdown() const
{
	if (_state != LevelState::COUNTDOWN && _state != LevelState::WARMUP_COUNTDOWN &&
	    _state != LevelState::WARMUP_FORCED_COUNTDOWN)
		return 0;

	return ceil((_time_begin - level.time) / (float)TICRATE);
}

/**
 * @brief Check if the score should be allowed to change.
 */
bool LevelState::checkScoreChange() const
{
	return _state == LevelState::INGAME;
}

/**
 * @brief Check if timeleft should advance.
 */
bool LevelState::checkTimeLeftAdvance() const
{
	return _state == LevelState::INGAME;
}

/**
 * @brief Check if the player should be able to fire their weapon.
 */
bool LevelState::checkFireWeapon() const
{
	return _state == LevelState::INGAME || _state == LevelState::WARMUP;
}

/**
 * @brief Check to see if we should allow players to toggle their ready state.
 */
bool LevelState::checkReadyToggle() const
{
	return _state == LevelState::INGAME || _state == LevelState::COUNTDOWN ||
	       _state == LevelState::WARMUP || _state == LevelState::WARMUP_COUNTDOWN;
}

/**
 * @brief Set callback to call when changing state.
 */
void LevelState::setStateCB(LevelState::SetStateCB cb)
{
	_set_state_cb = cb;
}

/**
 * @brief Reset warmup to "factory defaults" for the level.
 */
void LevelState::reset(level_locals_t& level)
{
	if (sv_warmup && sv_gametype != GM_COOP &&
	    !(level.flags & LEVEL_LOBBYSPECIAL)) // do not allow warmup in lobby!
		setState(LevelState::WARMUP);
	else
		setState(LevelState::INGAME);

	_time_begin = 0;
}

/**
 * @brief We want to restart the map, so initialize a countdown that we can't
 *        bail out of.
 */
void LevelState::restart()
{
	setState(LevelState::WARMUP_FORCED_COUNTDOWN);
}

/**
 * @brief Force the start of the game.
 */
void LevelState::forceStart()
{
	// Useless outside of warmup.
	if (_state != LevelState::WARMUP)
		return;

	setState(LevelState::WARMUP_FORCED_COUNTDOWN);
}

/**
 * @brief Start or stop the countdown based on if players are ready or not.
 */
void LevelState::readyToggle()
{
	// Rerun our checks to make sure we didn't skip them earlier.
	if (!checkReadyToggle())
		return;

	// Check to see if we satisfy our autostart setting.
	size_t ready = P_NumReadyPlayersInGame();
	size_t total = P_NumPlayersInGame();
	size_t needed;

	// We want at least one ingame ready player to start the game.  Servers
	// that start automatically with no ready players are handled by
	// Warmup::tic().
	if (ready == 0 || total == 0)
		return;

	float f_calc = total * sv_warmup_autostart;
	size_t i_calc = (int)floor(f_calc + 0.5f);
	if (f_calc > i_calc - MPEPSILON && f_calc < i_calc + MPEPSILON)
	{
		needed = i_calc + 1;
	}
	needed = (int)ceil(f_calc);

	if (ready >= needed)
	{
		if (_state == LevelState::WARMUP)
			setState(LevelState::WARMUP_COUNTDOWN);
	}
	else
	{
		if (_state == LevelState::WARMUP_COUNTDOWN)
		{
			setState(LevelState::WARMUP);
			Printf(PRINT_HIGH, "Countdown aborted: Player unreadied.\n");
		}
	}
}

/**
 * @brief Handle tic-by-tic maintenance of the level state.
 */
void LevelState::tic()
{
	// [AM] Clients are not authoritative on levelstate.
	if (!serverside)
		return;

	// If there aren't any more active players, go back to warm up mode [tm512 2014/04/08]
	if (sv_warmup && _state != LevelState::WARMUP && P_NumPlayersInGame() == 0)
	{
		setState(LevelState::WARMUP);
		return;
	}

	// Depending on our state, do something.
	switch (_state)
	{
	case LevelState::UNKNOWN:
		I_FatalError("Tried to tic unknown LevelState.\n");
		break;
	case LevelState::INGAME:
	case LevelState::COUNTDOWN:
	case LevelState::ENDGAME:
		// Nothing special happens.
		break;
	case LevelState::WARMUP: {
		// If autostart is zeroed out, start immediately.
		if (sv_warmup_autostart == 0.0f)
		{
			setState(LevelState::WARMUP_COUNTDOWN);
			return;
		}
		break;
	}
	case LevelState::WARMUP_COUNTDOWN:
	case LevelState::WARMUP_FORCED_COUNTDOWN: {
		// Once the timer has run out, start the game.
		if (level.time >= _time_begin)
		{
			setState(LevelState::INGAME);
			G_DeferedFullReset();
			Printf(PRINT_HIGH, "The match has started.\n");
			return;
		}
		break;
	}
	}
}

/**
 * @brief Serialize level state into a struct.
 *
 * @return Serialzied level state.
 */
SerializedLevelState LevelState::serialize() const
{
	SerializedLevelState serialized;
	serialized.state = _state;
	serialized.time_begin = _time_begin;
	return serialized;
}

/**
 * @brief Unserialize variables into levelstate.  Usually comes from a server.
 *
 * @param serialized New level state to set.
 */
void LevelState::unserialize(SerializedLevelState serialized)
{
	Printf(PRINT_HIGH, "LevelState::unserialize(%d, %d)\n", serialized.state,
	       serialized.time_begin);
	_state = serialized.state;
	_time_begin = serialized.time_begin;
}

// Set warmup status.
void LevelState::setState(LevelState::States new_state)
{
	Printf(PRINT_HIGH, "LevelState::setState(%d)\n", new_state);
	_state = new_state;

	// [AM] If we switch to countdown, set the correct time.
	if (_state == LevelState::COUNTDOWN || _state == LevelState::WARMUP_COUNTDOWN ||
	    _state == LevelState::WARMUP_FORCED_COUNTDOWN)
	{
		_time_begin = level.time + (sv_countdown.asInt() * TICRATE);
	}

	if (_set_state_cb)
	{
		_set_state_cb(serialize());
	}
}

BEGIN_COMMAND(forcestart)
{
	::levelstate.forceStart();
}
END_COMMAND(forcestart)
