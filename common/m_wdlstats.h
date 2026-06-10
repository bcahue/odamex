// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id$
//
// Copyright (C) 1993-1996 by id Software, Inc.
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
//     Defines needed by the WDL stats logger.
//
//-----------------------------------------------------------------------------

#pragma once

#include <string>
#include <vector>

#include "d_player.h"

enum WDLEvents {
	WDL_EVENT_DAMAGE,
	WDL_EVENT_CARRIERDAMAGE,
	WDL_EVENT_KILL,
	WDL_EVENT_CARRIERKILL,
	WDL_EVENT_ENVIRODAMAGE,
	WDL_EVENT_ENVIROCARRIERDAMAGE,
	WDL_EVENT_ENVIROKILL,
	WDL_EVENT_ENVIROCARRIERKILL,
	WDL_EVENT_TOUCH,
	WDL_EVENT_PICKUPTOUCH,
	WDL_EVENT_CAPTURE,
	WDL_EVENT_PICKUPCAPTURE,
	WDL_EVENT_ASSIST,
	WDL_EVENT_RETURNFLAG,
	WDL_EVENT_PICKUPITEM,
	WDL_EVENT_SPREADACCURACY,
	WDL_EVENT_SSACCURACY,
	WDL_EVENT_TRACERACCURACY,
	WDL_EVENT_PROJACCURACY,
	WDL_EVENT_SPAWNPLAYER,
	WDL_EVENT_SPAWNITEM,
	WDL_EVENT_JOINGAME,
	WDL_EVENT_DISCONNECT,
	WDL_EVENT_PLAYERBEACON,
	WDL_EVENT_PROJFIRE,
	WDL_EVENT_CARRYRETURNFLAG,
	//WDL_EVENT_PLAYERSPECIAL,
	//WDL_EVENT_TELEPORTPLAYER,
	//WDL_EVENT_RJUMPGO,
	//WDL_EVENT_RJUMPLAND,
	//WDL_EVENT_RJUMPAPEX,
	//WDL_EVENT_SPAWNMOB,
	//WDL_EVENT_MOBBEACON,
	//WDL_EVENT_TRACERBEACON,
	//WDL_EVENT_MOBSHOOT,
	//WDL_EVENT_ARCHFIRE,
	//WDL_EVENT_MOBPROJ,
	//WDL_EVENT_MOBSPECIAL,
	//WDL_EVENT_TELEPORTMOB,
	//WDL_EVENT_EXITLEVEL,
};

inline auto format_as(WDLEvents eEvent)
{
	return fmt::underlying(eEvent);
}

enum WDLPowerups {
	WDL_PICKUP_SOULSPHERE,
	WDL_PICKUP_MEGASPHERE,
	WDL_PICKUP_BLUEARMOR,
	WDL_PICKUP_GREENARMOR,
	WDL_PICKUP_BERSERK,
	WDL_PICKUP_STIMPACK,
	WDL_PICKUP_MEDKIT,
	WDL_PICKUP_HEALTHBONUS,
	WDL_PICKUP_ARMORBONUS,
	WDL_PICKUP_YELLOWKEY,
	WDL_PICKUP_REDKEY,
	WDL_PICKUP_BLUEKEY,
	WDL_PICKUP_YELLOWSKULL,
	WDL_PICKUP_REDSKULL,
	WDL_PICKUP_BLUESKULL,
	WDL_PICKUP_INVULNSPHERE,
	WDL_PICKUP_INVISSPHERE,
	WDL_PICKUP_RADSUIT,
	WDL_PICKUP_COMPUTERMAP,
	WDL_PICKUP_GOGGLES,
	WDL_PICKUP_CLIP,
	WDL_PICKUP_AMMOBOX,
	WDL_PICKUP_ROCKET,
	WDL_PICKUP_ROCKETBOX,
	WDL_PICKUP_CELL,
	WDL_PICKUP_CELLPACK,
	WDL_PICKUP_SHELLS,
	WDL_PICKUP_SHELLBOX,
	WDL_PICKUP_BACKPACK,
	WDL_PICKUP_BFG,
	WDL_PICKUP_CHAINGUN,
	WDL_PICKUP_CHAINSAW,
	WDL_PICKUP_ROCKETLAUNCHER,
	WDL_PICKUP_PLASMAGUN,
	WDL_PICKUP_SHOTGUN,
	WDL_PICKUP_SUPERSHOTGUN,
	WDL_PICKUP_CAREPACKAGE,
	WDL_PICKUP_POWERUPSPAWNER,
	WDL_PICKUP_UNKNOWN,
	WDL_PICKUP_EXTRALIFE,
	WDL_PICKUP_RESTEAMMATE,
};

inline auto format_as(WDLPowerups ePowerup)
{
	return fmt::underlying(ePowerup);
}

// ---------------------------------------------------------------------------
// In-memory recording tables.
//
// Declared here (rather than file-locally in m_wdlstats.cpp) so the v6
// aggregation module (m_wdlstats_agg) can consume the recorded event stream and
// player table directly at match end — no text-file round-trip. The recorder
// owns the singleton instances; these are just the shared record shapes.
// ---------------------------------------------------------------------------

// A single tracked player.
struct WDLPlayer
{
	int id;                // 1-based id within this match's player table
	int pid;               // engine netid; events reference players by this
	std::string netname;
	team_t team;
	// [auth] OAuth sub (empty string if unauthenticated/bot)
	std::string sub;
};
typedef std::vector<WDLPlayer> WDLPlayers;

// A single tracked player spawn.
struct WDLPlayerSpawn
{
	int id;
	int x;
	int y;
	int z;
	team_t team;
};
typedef std::vector<WDLPlayerSpawn> WDLPlayerSpawns;

// A single tracked item spawn.
struct WDLItemSpawn
{
	int id;
	int x;
	int y;
	int z;
	WDLPowerups item;
};
typedef std::vector<WDLItemSpawn> WDLItemSpawns;

// A single tracked flag socket.
struct WDLFlagLocation
{
	team_t team;
	int x;
	int y;
	int z;
};
typedef std::vector<WDLFlagLocation> WDLFlagLocations;

// A single recorded event. apos/tpos units follow the recorder's per-event
// convention (M_LogWDLEvent stores map units, i.e. >> FRACBITS; the item-respawn
// path stores raw fixed_t). The aggregator passes them through unchanged so it
// receives exactly what the legacy parser received from the text log.
struct WDLEvent
{
	WDLEvents ev;
	int activator;         // activator netid (0 = none)
	int target;            // target netid (0 = none)
	int gametic;
	fixed_t apos[3];
	fixed_t tpos[3];
	int arg0;
	int arg1;
	int arg2;
	int arg3;
};
typedef std::vector<WDLEvent> WDLEventLog;

void M_StartWDLLog(bool newmap);
void M_LogWDLEvent(
	WDLEvents eventtype, const player_t* activator, const player_t* target,
	int arg0, int arg1, int arg2, int arg3
);
void M_LogActorWDLEvent(
	WDLEvents eventtype, AActor* activator, AActor* target,
	int arg0, int arg1, int arg2, int arg3
);
int M_GetPlayerId(const player_t& player, team_t team);
bool M_CheckIfPlayerInLogs(const int playerid);
void M_LogWDLPlayerSpawn(const mapthing2_t& mthing);
void M_RemoveWDLPlayerSpawn(const mapthing2_t& mthing);
void M_LogWDLItemRespawnEvent(AActor* activator);
void M_LogWDLFlagLocation(const mapthing2_t& activator, team_t team);
void M_LogWDLPickupEvent(const player_t* activator, AActor* target, WDLPowerups pickuptype, bool dropped);
void M_LogWDLItemSpawn(const AActor& target, WDLPowerups type);
int M_GetPlayerSpawn(int x, int y);
void M_HandleWDLNameChange(team_t team, std::string oldname, std::string newname, int netid);
int GetMaxShotsForMod(int mod);
void M_CommitWDLLog();
WDLPowerups M_GetWDLItemByMobjType(const mobjtype_t type);
