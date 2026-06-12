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

#include <ctime>

#include "odamex.h"

#include "g_levelstate.h"
#include "m_wdlstats.h"
#include "m_wdlstats_agg.h"

#include "c_dispatch.h"
#include "p_local.h"
#include "teaminfo.h"

#define WDLSTATS_VERSION 6

// [auth] §3 (B) native capture: a player carries a flag iff they are some team's
// recorded flagger. Read off live engine state when an event is logged.
static bool PlayerCarriesFlag(const player_t* p)
{
	if (p == NULL)
		return false;
	for (size_t i = 0; i < NUMTEAMS; i++)
	{
		TeamInfo* ti = GetTeamInfo(static_cast<team_t>(i));
		if (ti != NULL && &idplayer(ti->FlagData.flagger) == p)
			return true;
	}
	return false;
}

// The CARRIER* event variants are the engine-authoritative signal that the
// target held a flag at the instant the event fired (timing-safe vs. reading
// flag state after, e.g., a death has already dropped it).
static bool WDLEventTargetIsCarrier(WDLEvents event)
{
	return event == WDL_EVENT_CARRIERDAMAGE || event == WDL_EVENT_ENVIROCARRIERDAMAGE ||
	       event == WDL_EVENT_CARRIERKILL || event == WDL_EVENT_ENVIROCARRIERKILL;
}

EXTERN_CVAR(sv_gametype)
EXTERN_CVAR(sv_hostname)
EXTERN_CVAR(sv_teamspawns)
EXTERN_CVAR(sv_playerbeacons)
EXTERN_CVAR(g_sides)
EXTERN_CVAR(g_lives)

//// Strings for WDL events
//static const char* wdlevstrings[] = {
//    "DAMAGE",         "CARRIERDAMAGE",     "KILL",
//    "CARRIERKILL",    "ENVIRODAMAGE",      "ENVIROCARRIERDAMAGE",
//    "ENVIROKILL",     "ENVIROCARRIERKILL", "TOUCH",
//    "PICKUPTOUCH",    "CAPTURE",           "PICKUPCAPTURE",
//    "ASSIST",         "RETURNFLAG",        "PICKUPITEM",
//    "SPREADACCURACY", "SSACCURACY",        "TRACERACCURACY",
//    "PROJACCURACY",   "SPAWNPLAYER",       "SPAWNITEM",
//    "JOINGAME",       "DISCONNECT",        "PLAYERBEACON",
//    "CARRIERBEACON",  "PROJFIRE",
//    //"RJUMPGO",
//    //"RJUMPLAND",
//    //"RJUMPAPEX",
//    //"MOBBEACON",
//    //"SPAWNMOB",
//};

std::string M_GetCurrentWadHashes();

static struct WDLState
{
	// Directory to log stats to.
	std::string logdir;

	// True if we're recording stats for this game.
	bool recording;

	// The starting gametic of the most recent log.
	int begintic;

	// [Blair] Toggle for whether that recording has playerbeacons enabled.
	bool enablebeacons;
} wdlstate;

// The recording-table record shapes (WDLPlayer/WDLPlayerSpawn/WDLItemSpawn/
// WDLFlagLocation/WDLEvent) and their typedefs now live in m_wdlstats.h so the
// v6 aggregation module can consume them directly. Only the recorder-owned
// singleton instances and their helpers live here.

// WDL Players that we're keeping track of.
static WDLPlayers wdlplayers;

[[nodiscard]]
bool operator==(const WDLPlayerSpawn& lhs, const WDLPlayerSpawn& rhs)
{
	return lhs.id == rhs.id && lhs.team == rhs.team && lhs.x == rhs.x && lhs.y == rhs.y &&
	       lhs.z == rhs.z;
}

// WDL player spawns that we're keeping track of.
static WDLPlayerSpawns wdlplayerspawns;

// WDL item spawns that we're keeping track of.
static WDLItemSpawns wdlitemspawns;

// Flags that we're keeping track of.
static WDLFlagLocations wdlflaglocations;

auto inline format_as(const WDLEvent& ev)
{
	//                 "ev,ac,tg,gt,ax,ay,az,tx,ty,tz,a0,a1,a2,a3"
	return fmt::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{}", ev.ev,
	                   ev.activator, ev.target, ev.gametic, ev.apos[0], ev.apos[1],
	                   ev.apos[2], ev.tpos[0], ev.tpos[1], ev.tpos[2], ev.arg0,
	                   ev.arg1, ev.arg2, ev.arg3);
}

// Events that we're keeping track of.
static WDLEventLog wdlevents;

// [auth] §3 (B) the v6 game is compiled live, as part of normal game flow: each
// M_Log* event below updates this accumulator directly at the source — there is
// no event-dispatch / re-parse layer and no end-of-match replay. Reset to a fresh
// game on each M_StartWDLLog; ::wdlstate.recording gates whether it is being fed.
// (::wdlevents and the text-log writer are kept only so the C# parser can be run
// over the same match for JSON parity comparison.)
static WDLAggGame g_liveGame;

// Resolve the live-game accumulator entry for a player, or NULL when not
// recording / the player isn't one we're tracking. Keeps the game's player table
// in sync with the recorder's first (players join mid-match).
static WDLAggPlayer* WDLAgg(const player_t* p)
{
	if (!::wdlstate.recording || p == NULL)
		return NULL;
	::g_liveGame.SyncPlayers(::wdlplayers);
	return ::g_liveGame.PlayerByNetId(p->id);
}

// Match-relative tic for an event happening now.
static int WDLTics()
{
	return ::gametic - ::wdlstate.begintic;
}

// A player's body position in the units the stats use (fixed_t >> FRACBITS).
static void WDLBodyPos(const player_t* p, int& x, int& y, int& z)
{
	x = y = z = 0;
	if (p != NULL && p->mo)
	{
		x = p->mo->x >> FRACBITS;
		y = p->mo->y >> FRACBITS;
		z = p->mo->z >> FRACBITS;
	}
}

// Which accuracy event a weapon's means-of-death maps to (false = the mod is not
// accuracy-tracked, mirroring the legacy switch that had no default case).
static bool WDLAccuracyEventForMod(int mod, WDLEvents& out)
{
	switch (mod)
	{
	case MOD_CHAINSAW:
	case MOD_FIST:
	case MOD_PISTOL:
	case MOD_CHAINGUN:
	case MOD_RAILGUN:
		out = WDL_EVENT_SSACCURACY;
		return true;
	case MOD_SHOTGUN:
	case MOD_SSHOTGUN:
		out = WDL_EVENT_SPREADACCURACY;
		return true;
	case MOD_ROCKET:
	case MOD_R_SPLASH:
	case MOD_BFG_BOOM:
	case MOD_PLASMARIFLE:
		out = WDL_EVENT_PROJACCURACY;
		return true;
	case MOD_BFG_SPLASH:
		out = WDL_EVENT_TRACERACCURACY;
		return true;
	default:
		return false;
	}
}

// Turn an event enum into a string.
//static const char* WDLEventString(WDLEvents i)
//{
//	if (i >= ARRAY_LENGTH(::wdlevstrings) || i < 0)
//		return "UNKNOWN";
//	return ::wdlevstrings[i];
//}

static void AddWDLPlayer(const player_t& player)
{
	// Don't add player if their name is already in the vector.
	// [Blair] Check the player's team too as version six tracks all
	// connects/disconnects/team switches
	for (const auto& wdlplayer : ::wdlplayers)
	{
		if (wdlplayer.netname == player.userinfo.netname &&
		    wdlplayer.team == player.userinfo.team && wdlplayer.pid == player.id)
			return;
	}

	WDLPlayer wdlplayer = {
	    static_cast<int>(::wdlplayers.size() + 1),
	    player.id,
	    player.userinfo.netname,
	    player.userinfo.team,
	    player.client.auth_sub,
	};
	::wdlplayers.push_back(wdlplayer);
}

static void AddWDLPlayerSpawn(const mapthing2_t& mthing)
{

	team_t team = TEAM_NONE;

	if (sv_teamspawns != 0)
	{
		if (mthing.type == 5080)
			team = TEAM_BLUE;
		else if (mthing.type == 5081)
			team = TEAM_RED;
		else if (mthing.type == 5083)
			team = TEAM_GREEN;
	}

	// [Blair] Add player spawns to the table with team info.
	for (const auto& spawn : ::wdlplayerspawns)
	{
		if (spawn.x == mthing.x && spawn.y == mthing.y && spawn.z == mthing.z &&
		    spawn.team == team)
			return;
	}

	WDLPlayerSpawn wdlplayerspawn = {static_cast<int>(::wdlplayerspawns.size() + 1), mthing.x, mthing.y,
	                                 mthing.z, team};
	::wdlplayerspawns.push_back(wdlplayerspawn);
}

static void AddWDLFlagLocation(const mapthing2_t& mthing, team_t team)
{
	// [Blair] Add flag pedestals to the table.
	for (const auto& loc : ::wdlflaglocations)
	{
		if (loc.x == mthing.x && loc.y == mthing.y && loc.z == mthing.z &&
		    loc.team == team)
			return;
	}

	::wdlflaglocations.push_back({team, mthing.x, mthing.y, mthing.z});
}

static void RemoveWDLPlayerSpawn(const mapthing2_t& mthing)
{
	const auto it = std::find_if(::wdlplayerspawns.begin(), ::wdlplayerspawns.end(), [&mthing](const auto& spawn){
		return spawn.x == mthing.x && spawn.y == mthing.y && spawn.z == mthing.z;
	});

	if (it != ::wdlplayerspawns.end())
		::wdlplayerspawns.erase(it);
}

int GetItemSpawn(int x, int y, int z, WDLPowerups item)
{
	for (const auto& spawn : ::wdlitemspawns)
	{
		if (spawn.x == x && spawn.y == y && spawn.z == z)
			return spawn.id;
	}
	return 0;
}

void M_LogWDLItemSpawn(const AActor& target, WDLPowerups type)
{
	// [Blair] Add item spawn to the table.
	// Don't add an overlapping item spawn, treat it as one.
	for (const auto& spawn : ::wdlitemspawns)
	{
		if (spawn.x == target.x && spawn.y == target.y && spawn.z == target.z &&
		    spawn.item == type)
			return;
	}

	WDLItemSpawn wdlitemspawn = {static_cast<int>(::wdlitemspawns.size() + 1), target.x, target.y,
	                             target.z, type};
	::wdlitemspawns.push_back(wdlitemspawn);
}

WDLPowerups M_GetWDLItemByMobjType(const mobjtype_t type)
{
	// [Blair] Return a WDL item based on the actor that was spawned.
	// Helps with pickup table lookups.

	WDLPowerups itemid;

	switch (type)
	{
	case MT_MISC0:
		itemid = WDL_PICKUP_GREENARMOR;
		break;
	case MT_MISC1:
		itemid = WDL_PICKUP_BLUEARMOR;
		break;
	case MT_MISC2:
		itemid = WDL_PICKUP_HEALTHBONUS;
		break;
	case MT_MISC3:
		itemid = WDL_PICKUP_ARMORBONUS;
		break;
	case MT_MISC10:
		itemid = WDL_PICKUP_STIMPACK;
		break;
	case MT_MISC11:
		itemid = WDL_PICKUP_MEDKIT;
		break;
	case MT_MISC12:
		itemid = WDL_PICKUP_SOULSPHERE;
		break;
	case MT_INV:
		itemid = WDL_PICKUP_INVULNSPHERE;
		break;
	case MT_MISC13:
		itemid = WDL_PICKUP_BERSERK;
		break;
	case MT_INS:
		itemid = WDL_PICKUP_INVISSPHERE;
		break;
	case MT_MISC14:
		itemid = WDL_PICKUP_RADSUIT;
		break;
	case MT_MISC15:
		itemid = WDL_PICKUP_COMPUTERMAP;
		break;
	case MT_MISC16:
		itemid = WDL_PICKUP_GOGGLES;
		break;
	case MT_MEGA:
		itemid = WDL_PICKUP_MEGASPHERE;
		break;
	case MT_CLIP:
		itemid = WDL_PICKUP_CLIP;
		break;
	case MT_MISC17:
		itemid = WDL_PICKUP_AMMOBOX;
		break;
	case MT_MISC18:
		itemid = WDL_PICKUP_ROCKET;
		break;
	case MT_MISC19:
		itemid = WDL_PICKUP_ROCKETBOX;
		break;
	case MT_MISC20:
		itemid = WDL_PICKUP_CELL;
		break;
	case MT_MISC21:
		itemid = WDL_PICKUP_CELLPACK;
		break;
	case MT_MISC22:
		itemid = WDL_PICKUP_SHELLS;
		break;
	case MT_MISC23:
		itemid = WDL_PICKUP_SHELLBOX;
		break;
	case MT_MISC24:
		itemid = WDL_PICKUP_BACKPACK;
		break;
	case MT_MISC25:
		itemid = WDL_PICKUP_BFG;
		break;
	case MT_CHAINGUN:
		itemid = WDL_PICKUP_CHAINGUN;
		break;
	case MT_MISC26:
		itemid = WDL_PICKUP_CHAINSAW;
		break;
	case MT_MISC27:
		itemid = WDL_PICKUP_ROCKETLAUNCHER;
		break;
	case MT_MISC28:
		itemid = WDL_PICKUP_PLASMAGUN;
		break;
	case MT_SHOTGUN:
		itemid = WDL_PICKUP_SHOTGUN;
		break;
	case MT_SUPERSHOTGUN:
		itemid = WDL_PICKUP_SUPERSHOTGUN;
		break;
	case MT_CAREPACK:
		itemid = WDL_PICKUP_CAREPACKAGE;
		break;
	case MT_EXTRALIFE:
		itemid = WDL_PICKUP_EXTRALIFE;
		break;
	case MT_RESTEAMMATE:
		itemid = WDL_PICKUP_RESTEAMMATE;
		break;
	default:
		itemid = WDL_PICKUP_UNKNOWN;
		break;
	}

	return itemid;
}

// Generate a log filename based on the current time.
static std::string GenerateTimestamp()
{
	time_t ti = time(NULL);
	struct tm* lt = localtime(&ti);

	char buf[128];
	if (!strftime(&buf[0], ARRAY_LENGTH(buf), "%Y.%m.%d.%H.%M.%S", lt))
		return "";

	return std::string(buf, strlen(&buf[0]));
}

static void WDLStatsHelp()
{
	PrintFmt(PRINT_HIGH,
	         "wdlstats - Starts logging WDL statistics to the given directory.  Unless "
	         "you are running a WDL server, you probably are not interested in this.\n\n"
	         "Usage:\n"
	         "  ] wdlstats <DIRNAME>\n"
	         "  Starts logging WDL statistics in the directory DIRNAME.\n");
}

BEGIN_COMMAND(wdlstats)
{
	if (argc < 2)
	{
		WDLStatsHelp();
		return;
	}

	// Setting the stats dir tells us that we intend to log.
	::wdlstate.logdir = argv[1];

	// Ensure our path ends with a slash.
	if (::wdlstate.logdir.back() != PATHSEPCHAR)
		::wdlstate.logdir += PATHSEPCHAR;

	PrintFmt(PRINT_HIGH,
	         "wdlstats: Enabled, will log to directory \"{}\" on next map change.\n",
	         wdlstate.logdir);
}
END_COMMAND(wdlstats)

void M_StartWDLLog(bool newmap)
{
	if (::wdlstate.logdir.empty())
	{
		::wdlstate.recording = false;
		::g_liveGame = WDLAggGame();
		return;
	}

	/* This used to only support CTF
	*  but now, this supports all game modes.
	*  Also, we now require data that is created in
	*  game states that aren't levelstate, so we grab the
	*  levelstate from before the game finished.
	if (sv_gametype != 3)
	{
	    ::wdlstate.recording = false;
	    Printf(
	        PRINT_HIGH,
	        "wdlstats: Not logging, incorrect gametype.\n"
	    );
	    return;
	}


	// Ensure that we're not in an invalid warmup state.
	if (::levelstate.getState() != LevelState::INGAME)
	{
	    // [AM] This is a little too much inside baseball to print about.
	    ::wdlstate.recording = false;
	    return;
	}
	*/

	// Start with a fresh slate of events.
	::wdlevents.clear();

	// And a fresh set of players.
	::wdlplayers.clear();

	if (newmap)
	{
		::wdlflaglocations.clear();
		::wdlitemspawns.clear();
		::wdlplayerspawns.clear();
	}

	// set playerbeacons
	if (sv_playerbeacons)
		::wdlstate.enablebeacons = true;
	else
		::wdlstate.enablebeacons = false;

	// Turn on recording.
	::wdlstate.recording = true;

	// Set our starting tic.
	::wdlstate.begintic = ::gametic;

	// Spin up a fresh live aggregator for this match. Players are added lazily as
	// they generate events (WDLAgg -> SyncPlayers). endGameTic is unknown until the
	// match ends and is unused while accumulating, so pass 0.
	::g_liveGame = WDLAggGame(static_cast<WDLGameTypeV6>(::sv_gametype.asInt()),
	                          ::wdlstate.begintic, 0);

	PrintFmt(PRINT_HIGH, "wdlstats: Started, will log to directory \"{}\".\n",
	       wdlstate.logdir);
}

/**
 * Log a damage event.
 *
 * Because damage can come in multiple pieces, this checks for an existing
 * event this tic and adds to it if it finds one.
 *
 * Returns true if the function successfully appended to an existing event,
 * otherwise false if we need to generate a new event.
 */
static bool LogDamageEvent(WDLEvents eventtype, const player_t& activator, const player_t& target,
                           int arg0, int arg1, int arg2)
{
	for (auto& event : OUtil::reverse(::wdlevents))
	{
		if (event.gametic != ::gametic)
		{
			// We're too late for events from last tic, so we must have a
			// new event.
			return false;
		}

		// Event type is the same?
		if (event.ev != eventtype)
			continue;

		// Activator is the same?
		if (event.activator != activator.id)
			continue;

		// Target is the same?
		if (event.target != target.id)
			continue;

		// Update our existing event.
		event.arg0 += arg0;
		event.arg1 += arg1;
		return true;
	}

	// We ran through all our events, must be a new event.
	return false;
}

/**
 * Log a shot attempt made by a player.
 *
 * If there's already an accuracy record for this gametic with a populated actor
 * then create a new one because the shot hit more than 1 player.
 */
bool LogAccuracyShot(WDLEvents eventtype, const player_t& activator, int mod, angle_t angle)
{
	// See if we have an existing accuracy event for this tic.
	// If not, we need to create a new one
	// If there is an existing accuracy event for this tic and it has a target,
	// then there were more than 1 hits, create a new event.
	for (auto& event : OUtil::reverse(::wdlevents))
	{
		if (event.gametic != ::gametic)
		{
			// Whoops, we went a whole gametic without seeing an accuracy
			// to our name.
			break;
		}

		// Event type is the same?
		if (event.ev != eventtype)
			continue;

		// Activator is the same?
		if (event.activator != activator.id)
			continue;

		// We found an existing accuracy event for this tic.
		// Do nothing.
		return true;
	}

	return false;
}

/**
 * Log a hit shot by a player
 *
 * Looks for an accuracy log somewhere in the backlog, if there is none, it
 * logs a message but continues.
 */
bool LogAccuracyHit(WDLEvents eventtype, const player_t& activator, const player_t* target, int mod,
                    int hits)
{
	// See if we have an existing accuracy event for this tic.
	for (auto& event : OUtil::reverse(::wdlevents))
	{
		if (event.gametic != ::gametic)
		{
			// Whoops, we went a whole gametic without seeing an accuracy
			// to our name.
			break;
		}

		// Event type is the same?
		if (event.ev != eventtype)
			continue;

		// Activator is the same?
		if (event.activator != activator.id)
			continue;

		// Target exists?
		if (target == nullptr)
			return true; // Can't log a hit if it didn't hit anybody...

		// Target is the same?
		if (event.target != target->id && event.target != 0)
			continue;

		const int tx = target->mo->x;
		const int ty = target->mo->y;
		const int tz = target->mo->z;

		// We found an existing accuracy event for this tic - increment the number of
		// shots hit if its a spread type
		event.target = target->id;
		event.arg2 += hits;
		event.tpos[0] = tx;
		event.tpos[1] = ty;
		event.tpos[2] = tz;
		return true;
	}
	// Not sure what happened but it can't find the event. Create one.
	return false;
}

// [Blair] Helper function to determine max amount of shots that a mod shoots at a time.
int GetMaxShotsForMod(int mod)
{
	switch (mod)
	{
	case MOD_FIST:
	case MOD_PISTOL:
	case MOD_CHAINGUN:
	case MOD_ROCKET:
	case MOD_R_SPLASH:
	case MOD_CHAINSAW:
	case MOD_PLASMARIFLE:
	case MOD_BFG_BOOM:
		return 1;
	case MOD_SHOTGUN:
		return 7;
	case MOD_BFG_SPLASH:
		return 40;
	case MOD_SSHOTGUN:
		return 20;
	}

	return 1;
}

/**
 * Log a WDL flag location.
 *
 *
 * Logs the initial flag location on spawn and puts it in the flag locations table.
 */
void M_LogWDLFlagLocation(const mapthing2_t& activator, team_t team)
{
	AddWDLFlagLocation(activator, team);
}

/**
 * Log a WDL item respawn event.
 *
 *
 * Logs each time an item respawned during a WDL log recording.
 */
void M_LogWDLItemRespawnEvent(AActor* activator)
{
	if (!::wdlstate.recording)
		return;

	// Activator
	fixed_t itemspawnid = 0;
	WDLPowerups itemtype = WDL_PICKUP_UNKNOWN;

	int ax = 0;
	int ay = 0;
	int az = 0;
	if (activator != NULL)
	{
		itemtype = M_GetWDLItemByMobjType(static_cast<mobjtype_t>(activator->type));

		// Add the activator's body information.
		ax = activator->x;
		ay = activator->y;
		az = activator->z;

		// Add the id from the pickups table.
		itemspawnid = GetItemSpawn(ax, ay, az, itemtype);
	}

	// Add the event to the log.
	// Item respawns have no GameV6 stat, so nothing is awarded here — the text-log
	// record is kept only for parser parity.
	WDLEvent evt = {WDL_EVENT_SPAWNITEM, 0,     0,        ::gametic, {ax, ay, az},
	                {0, 0, 0},           itemtype, itemspawnid, 0,         0};
	::wdlevents.push_back(evt);
}

/**
 * Log a WDL pickup event.
 *
 *
 * This will log a player item or weapon pickup, and check it against the current pickup
 * spawn table to determine if it needs to be added. This does have a chance to record a
 * ton of moving pickups on a conveyer belt or something, but whatever consumes the data
 * can ignore item pickups that only get picked up at the same location once if item
 * respawn is on.
 */
// Pre-pickup health stashed by M_BeginWDLPickup, used to derive the heal delta
// the next pickup event awarded (§3 (B) native capture).
static int s_pickupPreHealth = 0;

void M_BeginWDLPickup(int preHealth)
{
	s_pickupPreHealth = preHealth;
}

void M_LogWDLPickupEvent(const player_t* activator, AActor* target, WDLPowerups pickuptype,
                         bool dropped)
{
	if (!::wdlstate.recording)
		return;

	int dropitem = 0;

	if (dropped)
		dropitem = 1;

	// Activator
	fixed_t aid = 0;
	int ax = 0;
	int ay = 0;
	int az = 0;
	if (activator != NULL)
	{
		// Add the activator.
		AddWDLPlayer(*activator);
		aid = activator->id;

		// Add the activator's body information.
		if (activator->mo)
		{
			ax = activator->mo->x >> FRACBITS;
			ay = activator->mo->y >> FRACBITS;
			az = activator->mo->z >> FRACBITS;
		}
	}

	// Target
	fixed_t tid = 0;
	fixed_t itemspawnid = 0;
	int tx = 0;
	int ty = 0;
	int tz = 0;
	if (target != NULL)
	{
		tx = target->x;
		ty = target->y;
		tz = target->z;

		// Add the target.
		if (!dropped)
			itemspawnid = GetItemSpawn(tx, ty, tz, pickuptype);
	}

	// [auth] §3 (B): award the pickup straight into the live game. The health it
	// actually added is ground truth — post-pickup health minus the pre-pickup
	// health stashed by M_BeginWDLPickup at the top of P_GiveSpecial.
	WDLAggPlayer* a = WDLAgg(activator);
	if (a != NULL)
		a->AwardPickup(static_cast<int>(pickuptype), itemspawnid, dropped, WDLTics(),
		               activator->health - s_pickupPreHealth);

	// Text-log record (kept only for parser parity).
	WDLEvent evt = {
	    WDL_EVENT_PICKUPITEM, aid,        tid,         ::gametic, {ax, ay, az},
	    {tx, ty, tz},         pickuptype, itemspawnid, dropitem,  0};
	::wdlevents.push_back(evt);
}

// Append one event to the text log only. The log is an enum-tagged serialization
// (and the only thing the C# parser still consumes), so it legitimately keeps the
// event enum; stat awarding is done directly by the typed M_LogWDL* functions
// below. Reproduces the original recorder's same-tic merge so the log stays
// byte-identical for parity.
static void WDLLogText(WDLEvents event, const player_t* activator, const player_t* target, int arg0,
                       int arg1, int arg2, int arg3)
{
	if (!::wdlstate.recording)
		return;

	fixed_t aid = 0;
	int ax = 0, ay = 0, az = 0;
	if (activator != NULL)
	{
		AddWDLPlayer(*activator);
		aid = activator->id;
		if (activator->mo)
		{
			ax = activator->mo->x >> FRACBITS;
			ay = activator->mo->y >> FRACBITS;
			az = activator->mo->z >> FRACBITS;
		}
	}

	fixed_t tid = 0;
	int tx = 0, ty = 0, tz = 0;
	if (target != NULL)
	{
		AddWDLPlayer(*target);
		tid = target->id;
		if (target->mo)
		{
			tx = target->mo->x >> FRACBITS;
			ty = target->mo->y >> FRACBITS;
			tz = target->mo->z >> FRACBITS;
		}
	}

	WDLEvent evt = {event,        aid,  tid,  ::gametic, {ax, ay, az},
	                {tx, ty, tz}, arg0, arg1, arg2,      arg3};

	// Damage events are handled specially: same-tic pieces are merged in place, so
	// the merged event already in ::wdlevents stands in for this piece.
	if (activator && target &&
	    (event == WDL_EVENT_DAMAGE || event == WDL_EVENT_CARRIERDAMAGE))
	{
		if (LogDamageEvent(event, *activator, *target, arg0, arg1, arg2))
			return;
	}

	if (activator && !target &&
	    (event == WDL_EVENT_SSACCURACY || event == WDL_EVENT_SPREADACCURACY ||
	     event == WDL_EVENT_PROJACCURACY || event == WDL_EVENT_TRACERACCURACY) &&
	    arg2 <= 0)
	{
		if (LogAccuracyShot(event, *activator, arg1, arg0))
			return;
	}

	if (activator && target &&
	    (event == WDL_EVENT_SSACCURACY || event == WDL_EVENT_SPREADACCURACY ||
	     event == WDL_EVENT_PROJACCURACY || event == WDL_EVENT_TRACERACCURACY) &&
	    arg2 > 0)
	{
		if (LogAccuracyHit(event, *activator, target, arg1, arg2))
			return;
	}

	::wdlevents.push_back(evt);
}

// Award one accuracy record (shot: hits==0, or hit: hits>0) to the shooter,
// routing to the right recorder for the weapon. Shared by the shot/hit entries.
static void WDLAwardAccuracy(const player_t* shooter, const player_t* target, int angleBits, int mod,
                            unsigned hits)
{
	WDLAggPlayer* aAgg = WDLAgg(shooter);
	if (aAgg == NULL)
		return;

	WDLEvents ev;
	if (!WDLAccuracyEventForMod(mod, ev))
		return;

	WDLAggPlayer* tAgg = WDLAgg(target);
	const int targetId = tAgg ? tAgg->id : 0;
	const team_t enemyTeam = tAgg ? tAgg->team : TEAM_NONE;
	int ax, ay, az, tx, ty, tz;
	WDLBodyPos(shooter, ax, ay, az);
	WDLBodyPos(target, tx, ty, tz);
	const int tics = WDLTics();
	const unsigned maxShots = static_cast<unsigned>(GetMaxShotsForMod(mod));
	const bool hasFlag = PlayerCarriesFlag(shooter);

	switch (ev)
	{
	case WDL_EVENT_SSACCURACY:
	case WDL_EVENT_SPREADACCURACY:
		aAgg->RecordHitscanAccuracy(hits, maxShots, targetId, enemyTeam, angleBits, ax, ay, az, tx,
		                            ty, tz, mod, tics, hasFlag);
		break;
	case WDL_EVENT_PROJACCURACY:
		aAgg->RecordProjectileAccuracy(hits, maxShots, targetId, enemyTeam, angleBits, ax, ay, az,
		                               tx, ty, tz, mod, tics, hasFlag);
		break;
	case WDL_EVENT_TRACERACCURACY:
		aAgg->RecordTracerAccuracy(hits, maxShots, targetId, enemyTeam, angleBits, ax, ay, az, tx,
		                           ty, tz, mod, tics, hasFlag);
		break;
	default:
		break;
	}
}

// ===========================================================================
// Typed WDL event API. The game calls these at the point each event happens;
// each one awards the stat directly to the live game and records the text-log
// line. (There is no generic enum funnel and no central dispatch.)
// ===========================================================================

void M_LogWDLPlayerDamage(AActor* source, AActor* target, int hp, int armor, int mod,
                          bool targetHasFlag, team_t flagTeam)
{
	if (!::wdlstate.recording)
		return;

	player_t* sp = (source != NULL && source->type == MT_PLAYER) ? source->player : NULL;
	player_t* tp = (target != NULL && target->type == MT_PLAYER) ? target->player : NULL;
	const bool noSource = (source == NULL);

	// Text log: pick the variant the call site used to choose.
	WDLEvents ev;
	int arg3;
	if (noSource && !targetHasFlag)
	{
		ev = WDL_EVENT_ENVIRODAMAGE;
		arg3 = 0;
	}
	else if (noSource && targetHasFlag)
	{
		ev = WDL_EVENT_ENVIROCARRIERDAMAGE;
		arg3 = static_cast<int>(flagTeam);
	}
	else if (!noSource && targetHasFlag)
	{
		ev = WDL_EVENT_CARRIERDAMAGE;
		arg3 = static_cast<int>(flagTeam);
	}
	else
	{
		ev = WDL_EVENT_DAMAGE;
		arg3 = 0;
	}
	WDLLogText(ev, sp, tp, hp, armor, mod, arg3);

	// Award.
	WDLAggPlayer* tAgg = WDLAgg(tp);
	if (tAgg == NULL)
		return;
	WDLAggPlayer* aAgg = WDLAgg(sp);
	int ax, ay, az, tx, ty, tz;
	WDLBodyPos(sp, ax, ay, az);
	WDLBodyPos(tp, tx, ty, tz);
	const int tics = WDLTics();
	const WDLArmorV6 targetArmor = tp != NULL ? WDLMapArmorType(tp->armortype) : WDLArmorV6::None;

	if (aAgg == NULL)
	{
		// No player source: environmental damage is awarded to the victim; damage
		// from a non-player (monster) is dropped, as before.
		if (noSource)
			tAgg->AwardDamageToPlayer(-1, "World", hp, armor, targetArmor,
			                          WDLDamageTypeV6::EnvironmentalDamage, mod, targetHasFlag, false,
			                          tics, ax, ay, az, tx, ty, tz);
	}
	else if (aAgg->id == tAgg->id)
	{
		tAgg->AwardDamageToPlayer(tAgg->id, tAgg->name, hp, armor, targetArmor,
		                          WDLDamageTypeV6::SelfDamage, mod, targetHasFlag, false, tics, ax,
		                          ay, az, tx, ty, tz);
	}
	else
	{
		const WDLDamageTypeV6 dt = (::g_liveGame.IsTeamGame() && aAgg->team == tAgg->team)
		                               ? WDLDamageTypeV6::DamageByTeammate
		                               : WDLDamageTypeV6::DamageByEnemyPlayer;
		aAgg->AwardDamageToPlayer(tAgg->id, tAgg->name, hp, armor, targetArmor, dt, mod,
		                          PlayerCarriesFlag(sp), targetHasFlag, tics, ax, ay, az, tx, ty, tz);
	}
}

void M_LogWDLPlayerKill(AActor* source, AActor* target, int mod, bool targetHasFlag, team_t flagTeam)
{
	if (!::wdlstate.recording)
		return;

	player_t* sp = (source != NULL && source->type == MT_PLAYER) ? source->player : NULL;
	player_t* tp = (target != NULL && target->type == MT_PLAYER) ? target->player : NULL;
	const bool noSource = (source == NULL);

	WDLEvents ev;
	int arg0;
	if (noSource && targetHasFlag)
	{
		ev = WDL_EVENT_ENVIROCARRIERKILL;
		arg0 = static_cast<int>(flagTeam);
	}
	else if (noSource)
	{
		ev = WDL_EVENT_ENVIROKILL;
		arg0 = 0;
	}
	else if (targetHasFlag)
	{
		ev = WDL_EVENT_CARRIERKILL;
		arg0 = static_cast<int>(flagTeam);
	}
	else
	{
		ev = WDL_EVENT_KILL;
		arg0 = 0;
	}
	WDLLogText(ev, sp, tp, arg0, 0, mod, 0);

	WDLAggPlayer* tAgg = WDLAgg(tp);
	if (tAgg == NULL)
		return;
	WDLAggPlayer* aAgg = WDLAgg(sp);
	int ax, ay, az, tx, ty, tz;
	WDLBodyPos(sp, ax, ay, az);
	WDLBodyPos(tp, tx, ty, tz);
	const int tics = WDLTics();

	WDLDeathTypeV6 deathType;
	if (aAgg == NULL)
		deathType = noSource ? WDLDeathTypeV6::Environmental : WDLDeathTypeV6::Suicide;
	else if (aAgg->name == tAgg->name) // suicide(-with-flag) edge case
		deathType = WDLDeathTypeV6::Suicide;
	else
	{
		const bool isTeamKill = ::g_liveGame.IsTeamGame() && aAgg->team == tAgg->team;
		deathType = WDLDeathTypeV6::KilledByPlayer;
		aAgg->AwardKill(targetHasFlag, PlayerCarriesFlag(sp), isTeamKill, mod, tics, tAgg->id,
		                tAgg->name, tx, ty, tz, ax, ay, az);
	}
	tAgg->PlayerKilled(tics, deathType, mod, targetHasFlag, tx, ty, tz);
}

void M_LogWDLPlayerExitKill(player_t& player, bool targetHasFlag, team_t flagTeam)
{
	if (!::wdlstate.recording)
		return;

	WDLLogText(targetHasFlag ? WDL_EVENT_CARRIERKILL : WDL_EVENT_KILL, &player, &player,
	           targetHasFlag ? static_cast<int>(flagTeam) : 0, 0, MOD_EXIT, 0);

	WDLAggPlayer* tAgg = WDLAgg(&player);
	if (tAgg == NULL)
		return;
	int x, y, z;
	WDLBodyPos(&player, x, y, z);
	tAgg->PlayerKilled(WDLTics(), WDLDeathTypeV6::Suicide, MOD_EXIT, targetHasFlag, x, y, z);
}

// Internal flag-touch worker shared by the three named entry points below.
static void WDLFlagTouch(player_t& player, team_t flagTeam, WDLFlagTouchTypeV6 kind, WDLEvents ev)
{
	if (!::wdlstate.recording)
		return;
	WDLLogText(ev, &player, NULL, static_cast<int>(flagTeam), 0, 0, 0);
	WDLAggPlayer* aAgg = WDLAgg(&player);
	if (aAgg == NULL)
		return;
	int x, y, z;
	WDLBodyPos(&player, x, y, z);
	::g_liveGame.OnFlagTouch(aAgg, kind, WDLTics(), player.health, player.armorpoints,
	                         WDLMapArmorType(player.armortype), x, y, z);
}

void M_LogWDLFlagGrab(player_t& player, team_t flagTeam)
{
	WDLFlagTouch(player, flagTeam, WDLFlagTouchTypeV6::FlagTouch, WDL_EVENT_TOUCH);
}

void M_LogWDLFlagPickup(player_t& player, team_t flagTeam)
{
	WDLFlagTouch(player, flagTeam, WDLFlagTouchTypeV6::PickupFlagTouch, WDL_EVENT_PICKUPTOUCH);
}

void M_LogWDLFlagCarryReturn(player_t& player, team_t flagTeam)
{
	WDLFlagTouch(player, flagTeam, WDLFlagTouchTypeV6::CarryReturnFlagTouch,
	             WDL_EVENT_CARRYRETURNFLAG);
}

void M_LogWDLFlagReturn(player_t* player, team_t flagTeam)
{
	if (!::wdlstate.recording)
		return;
	WDLLogText(WDL_EVENT_RETURNFLAG, player, NULL, static_cast<int>(flagTeam), 0, 0, 0);
	WDLAggPlayer* aAgg = WDLAgg(player);
	if (aAgg == NULL)
		return;
	int x, y, z;
	WDLBodyPos(player, x, y, z);
	aAgg->AwardFlagReturn(x, y, z, WDLTics());
}

void M_LogWDLFlagCapture(player_t& player, team_t flagTeam, bool pickupCapture)
{
	if (!::wdlstate.recording)
		return;
	WDLLogText(pickupCapture ? WDL_EVENT_PICKUPCAPTURE : WDL_EVENT_CAPTURE, &player, NULL,
	           static_cast<int>(flagTeam), 0, 0, 0);
	WDLAggPlayer* aAgg = WDLAgg(&player);
	if (aAgg == NULL)
		return;
	int x, y, z;
	WDLBodyPos(&player, x, y, z);
	::g_liveGame.OnFlagCapture(aAgg, pickupCapture, WDLTics(), player.health, player.armorpoints,
	                           WDLMapArmorType(player.armortype), x, y, z);
}

void M_LogWDLPlayerSpawnEvent(player_t& player, team_t team, int spawnId)
{
	if (!::wdlstate.recording)
		return;
	WDLLogText(WDL_EVENT_SPAWNPLAYER, &player, NULL, static_cast<int>(team), 0, spawnId, 0);
	WDLAggPlayer* aAgg = WDLAgg(&player);
	if (aAgg != NULL)
		aAgg->RecordPlayerSpawn(spawnId, WDLTics());
}

void M_LogWDLPlayerBeacon(player_t& player, int angleBits)
{
	if (!::wdlstate.recording || !::wdlstate.enablebeacons)
		return;
	WDLLogText(WDL_EVENT_PLAYERBEACON, &player, NULL, angleBits, 0, 0, 0);
	WDLAggPlayer* aAgg = WDLAgg(&player);
	if (aAgg == NULL)
		return;
	int x, y, z;
	WDLBodyPos(&player, x, y, z);
	aAgg->RecordPlayerBeacon(angleBits, x, y, z, WDLTics());
}

void M_LogWDLProjectileFire(player_t& player, int angleBits, int mod)
{
	if (!::wdlstate.recording)
		return;
	WDLLogText(WDL_EVENT_PROJFIRE, &player, NULL, angleBits, mod, 0, 0);
	WDLAggPlayer* aAgg = WDLAgg(&player);
	if (aAgg == NULL)
		return;
	int x, y, z;
	WDLBodyPos(&player, x, y, z);
	aAgg->RecordProjectileFire(angleBits, WDLTics(), mod, x, y, z);
}

void M_LogWDLAccuracyShot(player_t& shooter, int angleBits, int mod)
{
	if (!::wdlstate.recording)
		return;
	WDLEvents ev;
	if (!WDLAccuracyEventForMod(mod, ev))
		return;
	WDLLogText(ev, &shooter, NULL, angleBits, mod, 0, GetMaxShotsForMod(mod));
	WDLAwardAccuracy(&shooter, NULL, angleBits, mod, 0);
}

void M_LogWDLAccuracyHit(player_t* shooter, player_t* target, int angleBits, int mod)
{
	if (!::wdlstate.recording)
		return;
	WDLEvents ev;
	if (!WDLAccuracyEventForMod(mod, ev))
		return;
	WDLLogText(ev, shooter, target, angleBits, mod, 1, GetMaxShotsForMod(mod));
	WDLAwardAccuracy(shooter, target, angleBits, mod, 1);
}

void M_LogWDLPlayerJoin(player_t& player, team_t team, int playerId)
{
	// No GameV6 stat for joins; recorded for the text log / parser only.
	WDLLogText(WDL_EVENT_JOINGAME, &player, NULL, static_cast<int>(team), playerId, 0, 0);
}

void M_LogWDLPlayerDisconnect(player_t& player, team_t team, int playerId)
{
	// No GameV6 stat for disconnects; recorded for the text log / parser only.
	WDLLogText(WDL_EVENT_DISCONNECT, &player, NULL, static_cast<int>(team), playerId, 0, 0);
}

void M_LogWDLPlayerSpawn(const mapthing2_t& mthing)
{
	AddWDLPlayerSpawn(mthing);
}

void M_RemoveWDLPlayerSpawn(const mapthing2_t& mthing)
{
	RemoveWDLPlayerSpawn(mthing);
}

void M_HandleWDLNameChange(team_t team, std::string oldname, std::string newname, int pid)
{
	if (!::wdlstate.recording)
		return;

	for (auto& player : ::wdlplayers)
	{
		// Attempt a rename but don't go nuts.
		if (player.pid == pid && player.netname == oldname && player.team == team)
		{
			player.netname = newname;
			return;
		}
	}
}

int M_GetPlayerSpawn(int x, int y)
{
	if (!::wdlstate.recording)
		return 0;

	for (const auto& spawn : ::wdlplayerspawns)
	{
		if (spawn.x == x && spawn.y == y)
			return spawn.id;
	}
	return 0;
}

int M_GetPlayerId(const player_t& player, team_t team)
{
	if (!::wdlstate.recording)
		return 0;

	// This gets called before the log function itself, so add it.
	AddWDLPlayer(player);

	// Make real good sure its in there.
	const auto it = std::find_if(::wdlplayers.begin(), ::wdlplayers.end(), [&player, team](const auto& wp){
		return wp.pid == player.id && wp.netname == player.userinfo.netname && wp.team == team;
	});

	if (it != ::wdlplayers.end())
		return (*it).id;

	return 0;
}

bool M_CheckIfPlayerInLogs(const int playerid)
{
	if (!::wdlstate.recording)
		return false;

	auto it = std::find_if(::wdlplayers.begin(), ::wdlplayers.end(),
	                       [playerid](const auto& wp) { return wp.id == playerid; });

	return it != ::wdlplayers.end();
}

void M_CommitWDLLog()
{
	if (!::wdlstate.recording || wdlevents.empty() ||
	    ::levelstate.getState() != LevelState::INGAME)
		return;

	// See if we can write a file.
	std::string timestamp = GenerateTimestamp();
	std::string filename = ::wdlstate.logdir + "wdl_" + timestamp + ".log";

	// [Blair] Make the in-file timestamp ISO 8601 instead of a homegrown one.
	// However, keeping the homegrown one for filename as ISO 8601 characters
	// aren't supported in Windows filenames.
	time_t now;
	time(&now);
	char iso8601buf[sizeof "2011-10-08T07:07:09Z"];
	strftime(iso8601buf, sizeof iso8601buf, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

	FILE* fh = fopen(filename.c_str(), "w+");
	if (fh == NULL)
	{
		::wdlstate.recording = false;
		PrintFmt(PRINT_HIGH, "wdlstats: Could not save\"{}\" for writing.\n",
		         filename);
		return;
	}

	// Header (metadata)
	fmt::print(fh, "version={}\n", WDLSTATS_VERSION);
	fmt::print(fh, "time={}\n", iso8601buf);
	fmt::print(fh, "levelnum={}\n", ::level.levelnum);
	fmt::print(fh, "levelname={}\n", ::level.level_name);
	fmt::print(fh, "levelhash={}\n", ::level.level_fingerprint.toString());
	fmt::print(fh, "gametype={}\n", ::sv_gametype.str());
	fmt::print(fh, "lives={}\n", ::g_lives.str());
	fmt::print(fh, "attackdefend={}\n", ::g_sides.str());
	fmt::print(fh, "duration={}\n", ::gametic - ::wdlstate.begintic);
	fmt::print(fh, "endgametic={}\n", ::gametic);
	fmt::print(fh, "round={}\n", ::levelstate.getRound());
	fmt::print(fh, "winresult={}\n", static_cast<int>(::levelstate.getWinInfo().type));
	fmt::print(fh, "winid={}\n", ::levelstate.getWinInfo().id);
	fmt::print(fh, "hostname={}\n", ::sv_hostname.str());

	// Players
	fmt::print(fh, "players\n");
	for (const auto& pl : ::wdlplayers)
		fmt::print(fh, "{},{},{},{}\n", pl.id, pl.pid, static_cast<int>(pl.team), pl.netname);

	// ItemSpawns
	fmt::print(fh, "itemspawns\n");
	for (const auto& is : ::wdlitemspawns)
		fmt::print(fh, "{},{},{},{},{}\n", is.id, is.x, is.y, is.z, static_cast<int>(is.item));

	// PlayerSpawns
	fmt::print(fh, "playerspawns\n");
	for (const auto& ps : ::wdlplayerspawns)
		fmt::print(fh, "{},{},{},{},{}\n", ps.id, static_cast<int>(ps.team), ps.x, ps.y, ps.z);

	if (sv_gametype == GM_CTF)
	{
		// FlagLocation
		fmt::print(fh, "flaglocations\n");
		for (const auto& fl : ::wdlflaglocations)
			fmt::print(fh, "{},{},{},{}\n", static_cast<int>(fl.team), fl.x, fl.y, fl.z);
	}

	// Wads
	fmt::print(fh, "wads\n");
	fmt::print(fh, "{}", M_GetCurrentWadHashes());

	// Events
	fmt::print(fh, "events\n");
	for (const auto& ev : ::wdlevents)
		fmt::print(fh, "{}\n", ev);

	fclose(fh);

	PrintFmt(PRINT_HIGH, "wdlstats: Log saved as \"{}\".\n", filename);

	// Dump the compiled v6 JSON next to the .log (wdl_<timestamp>.json) so the two
	// can be diffed for parity. Built from the same in-memory state, while it's
	// still valid (before recording is turned off below).
	const std::string json = M_GetWDLStatsV6Json();
	if (!json.empty())
	{
		const std::string jsonpath = ::wdlstate.logdir + "wdl_" + timestamp + ".json";
		FILE* jh = fopen(jsonpath.c_str(), "w");
		if (jh != NULL)
		{
			fwrite(json.data(), 1, json.size(), jh);
			fclose(jh);
			PrintFmt(PRINT_HIGH, "wdlstats: v6 JSON saved as \"{}\".\n", jsonpath);
		}
		else
		{
			PrintFmt(PRINT_HIGH, "wdlstats: Could not save \"{}\" for writing.\n", jsonpath);
		}
	}

	// Turn off stat recording global - it must be turned on again by the
	// log starter next go-around.
	::wdlstate.recording = false;
}

// Assemble a finished GameV6 from the recorder's current in-memory tables plus
// the match metadata (same header values M_CommitWDLLog writes). The aggregator
// consumes the in-memory event stream directly — no text-file round-trip.
WDLGameV6 M_BuildWDLGameV6()
{
	WDLAggMeta meta;
	meta.version = WDLSTATS_VERSION;

	// ISO-8601 UTC, the same source the text writer's time= header uses.
	time_t now;
	time(&now);
	char iso8601buf[sizeof "2011-10-08T07:07:09Z"];
	strftime(iso8601buf, sizeof iso8601buf, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
	meta.date = iso8601buf;

	meta.levelNum = ::level.levelnum;
	meta.levelName = ::level.level_name;
	meta.lives = ::g_lives.asInt();
	meta.gameType = ::sv_gametype.asInt();
	meta.attackDefend = ::g_sides.asInt();
	meta.durationTics = ::gametic - ::wdlstate.begintic;
	meta.round = ::levelstate.getRound();
	meta.winResult = static_cast<int>(::levelstate.getWinInfo().type);
	meta.winId = ::levelstate.getWinInfo().id;
	meta.hostName = ::sv_hostname.str();
	meta.originalLogFileName = ""; // no source file on the JSON-upload path

	// Wads: parse the "basename,hash\n" lines M_GetCurrentWadHashes() returns.
	std::vector<WDLWadV6> wads;
	const std::string hashes = M_GetCurrentWadHashes();
	for (size_t start = 0; start < hashes.size();)
	{
		const size_t nl = hashes.find('\n', start);
		const std::string line =
		    hashes.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
		start = (nl == std::string::npos) ? hashes.size() : nl + 1;
		if (line.empty())
			continue;
		const size_t comma = line.find(',');
		if (comma == std::string::npos)
			continue;
		wads.push_back(WDLWadV6{line.substr(0, comma), line.substr(comma + 1)});
	}

	// The match was compiled live (g_liveGame) as it was played — there is no
	// end-of-match replay. Just sync any last players, close out in-flight state,
	// and assemble. (If nothing was recorded this is a default-constructed game and
	// Build yields empty stats, which the serializer drops.)
	::g_liveGame.SyncPlayers(::wdlplayers);
	::g_liveGame.Finalize();
	return ::g_liveGame.Build(meta, ::wdlitemspawns, ::wdlplayerspawns, ::wdlflaglocations, wads);
}

static void PrintWDLEvent(const WDLEvent& evt)
{
	PrintFmt(PRINT_HIGH, "{}\n", evt);
}

static void WDLInfoHelp()
{
	PrintFmt(PRINT_HIGH,
	         "wdlinfo - Looks up internal information about logged WDL events\n\n"
	         "Usage:\n"
	         "  ] wdlinfo event <ID>\n"
	         "  Print the event by ID.\n\n"
	         "  ] wdlinfo size\n"
	         "  Return the size of the internal event array.\n\n"
	         "  ] wdlinfo state\n"
	         "  Return relevant WDL stats state.\n\n"
	         "  ] wdlinfo tail\n"
	         "  Print the last 10 events.\n");
}

BEGIN_COMMAND(wdlinfo)
{
	if (argc < 2)
	{
		WDLInfoHelp();
		return;
	}

	if (stricmp(argv[1], "size") == 0)
	{
		// Count total events.
		PrintFmt(PRINT_HIGH, "{} events found\n", ::wdlevents.size());
		return;
	}
	else if (stricmp(argv[1], "state") == 0)
	{
		// Count total events.
		PrintFmt(PRINT_HIGH, "Currently recording?: {}\n",
		         ::wdlstate.recording ? "Yes" : "No");
		PrintFmt(PRINT_HIGH, "Directory to write logs to: \"{}\"\n",
		         ::wdlstate.logdir);
		PrintFmt(PRINT_HIGH, "Log starting gametic: {}\n", ::wdlstate.begintic);
		return;
	}
	else if (stricmp(argv[1], "tail") == 0)
	{
		// [Blair] C++ doesn't like when you access an iterator on an empty vector.
		if (::wdlevents.empty())
		{
			PrintFmt(PRINT_HIGH, "No events to show.\n");
			return;
		}
		// Show last 10 events.
		WDLEventLog::const_iterator it = ::wdlevents.end() - 10;
		if (it < ::wdlevents.begin())
			it = wdlevents.begin();

		PrintFmt(PRINT_HIGH, "Showing last {} events:\n",
		         ::wdlevents.end() - it);
		for (; it != ::wdlevents.end(); ++it)
			PrintWDLEvent(*it);
		return;
	}

	if (argc < 3)
	{
		WDLInfoHelp();
		return;
	}

	if (stricmp(argv[1], "event") == 0)
	{
		int id = atoi(argv[2]);
		if (id >= static_cast<int>(::wdlevents.size()))
		{
			PrintFmt(PRINT_HIGH, "Event number {} not found\n", id);
			return;
		}
		WDLEvent evt = ::wdlevents.at(id);
		PrintWDLEvent(evt);
		return;
	}

	// Unknown command.
	WDLInfoHelp();
}
END_COMMAND(wdlinfo)
