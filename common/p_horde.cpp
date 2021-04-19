// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id$
//
// Copyright (C) 1998-2006 by Randy Heit (ZDoom).
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
//  "Horde" game mode.
//
//-----------------------------------------------------------------------------

#include "p_horde.h"

#include <math.h>

#include "c_dispatch.h"
#include "d_player.h"
#include "doomstat.h"
#include "doomtype.h"
#include "m_random.h"
#include "m_vectors.h"
#include "p_local.h"
#include "p_tick.h"
#include "s_sound.h"

bool P_LookForPlayers(AActor* actor, bool allaround);

const int SPAWN_GROUND = MTF_EASY;
const int SPAWN_AIR = MTF_NORMAL;
const int SPAWN_BOSS = MTF_HARD;
const int SPAWN_AMBUSH = MTF_AMBUSH;

struct roundDefine_t
{
	int monsterHealth;  // Maximum health of a single monster spawn.
	int bossHealth;     // Maximum health of a single "boss" monster spawn.
	int minGroupHealth; // Minimum health of a group of monsters to spawn.
	int maxGroupHealth; // Maximum health of a group of monsters to spawn.
	int minTotalHealth; // Lower bound on the amount of health in the map at once, aside
	                    // from round end.
	int maxTotalHealth; // Upper bound on the amount of health in the map at once.
	int goalHealth;     // Base target health to win the round.

	roundDefine_t(int monsterHealth, int bossHealth, int minGroupHealth,
	              int maxGroupHealth, int minTotalHealth, int maxTotalHealth,
	              int goalHealth)
	    : monsterHealth(monsterHealth), bossHealth(bossHealth),
	      minGroupHealth(minGroupHealth), maxGroupHealth(maxGroupHealth),
	      minTotalHealth(minTotalHealth), maxTotalHealth(maxTotalHealth),
	      goalHealth(goalHealth)
	{
	}
};

class HordeRoundState
{
	const roundDefine_t ROUND_DEFINES[3] = {
	    // Round 1
	    roundDefine_t(150, 150, 150, 300, 600, 1200, 2400),
	    // Round 2
	    roundDefine_t(600, 700, 600, 1200, 2000, 4800, 9600),
	    // Round 3
	    roundDefine_t(1000, 4000, 1000, 2000, 4000, 8000, 16000)};

	int m_round; // 0-indexed

  public:
	int getRound() const
	{
		return m_round + 1;
	}

	const roundDefine_t& getDefine() const
	{
		return ROUND_DEFINES[m_round];
	}

	void setRound(const int round)
	{
		m_round = round - 1;
	}
};

class HordeState
{
	hordeState_e m_state;
	int m_stateTime;
	HordeRoundState m_roundState;
	int m_spawnedHealth;
	int m_killedHealth;

	void setState(const hordeState_e state)
	{
		m_state = state;
		m_stateTime = ::gametic;
	}

  public:
	void reset()
	{
		setState(HS_STARTING);
		m_roundState.setRound(1);
		m_spawnedHealth = 0;
		m_killedHealth = 0;
	}

	/**
	 * @brief Returns health, like for the HUD.
	 */
	hordeInfo_t info() const
	{
		hordeInfo_t info;
		info.state = m_state;
		info.round = m_roundState.getRound();
		info.spawned = m_spawnedHealth;
		info.killed = m_killedHealth;
		info.goal = m_roundState.getDefine().goalHealth;
		return info;
	}

	void addSpawnHealth(const int health)
	{
		m_spawnedHealth += health;
	}

	void addKilledHealth(const int health)
	{
		m_killedHealth += health;
	}

	HordeRoundState& getRoundState()
	{
		return m_roundState;
	}

	const HordeRoundState& getRoundState() const
	{
		return m_roundState;
	}

	void stateSwitch()
	{
		const roundDefine_t& define = m_roundState.getDefine();
		int aliveHealth = m_spawnedHealth - m_killedHealth;

		switch (m_state)
		{
		case HS_STARTING:
			setState(HS_PRESSURE);
		case HS_PRESSURE: {
			if (m_killedHealth > define.goalHealth)
			{
				setState(HS_BOSS);
				return;
			}
			else if (aliveHealth > define.maxTotalHealth)
			{
				setState(HS_RELAX);
				return;
			}
			return;
		}
		case HS_RELAX: {
			if (m_killedHealth > define.goalHealth)
			{
				setState(HS_BOSS);
				return;
			}
			else if (aliveHealth < define.minTotalHealth)
			{
				setState(HS_PRESSURE);
				return;
			}
			return;
		case HS_BOSS: {
			return;
		}
		}
		default:
			return;
		}
	}

	hordeState_e getState()
	{
		return m_state;
	}
} gDirector;

namespace recipe
{
struct recipe_t
{
	mobjtype_t type;
	int count;
};

typedef std::vector<mobjtype_t> mobjTypes_t;

static bool CmpHealth(const mobjtype_t& a, const mobjtype_t& b)
{
	return ::mobjinfo[a].height < ::mobjinfo[b].height;
}

static const mobjTypes_t& GatherMonsters()
{
	// [AM] FIXME: This will break on WAD reload, this is just "for now".
	static mobjTypes_t all;

	if (all.empty())
	{
		for (size_t i = 0; i < ARRAY_LENGTH(::mobjinfo); i++)
		{
			// [AM] FIXME: Don't hardcode mobj exceptions, this basically makes
			//             that mobj unusable even when using DEH/BEX.
			if (i == MT_DOGS || i == MT_WOLFSS)
				continue;

			// Is this a monster? [AM] FIXME: Ditto.
			if (!(::mobjinfo[i].flags & MF_COUNTKILL) && i != MT_SKULL)
				continue;

			// Does this monster have any attacks?  (Skips keen)
			if (::mobjinfo[i].missilestate == S_NULL &&
			    ::mobjinfo[i].meleestate == S_NULL)
				continue;

			// Is this monster a flying monster?
			if (::mobjinfo[i].flags & (MF_FLOAT | MF_NOGRAVITY))
			{
				// Monster is a flying monster.
				all.push_back(static_cast<mobjtype_t>(i));
			}
			else if (::mobjinfo[i].missilestate == S_NULL &&
			         ::mobjinfo[i].meleestate != S_NULL)
			{
				// Monster is melee-only monster.
				all.push_back(static_cast<mobjtype_t>(i));
			}
			else
			{
				// Monster is a ranged monster.
				all.push_back(static_cast<mobjtype_t>(i));
			}
		}

		// Sort monsters by health.
		std::sort(all.begin(), all.end(), CmpHealth);
	}

	return all;
}

static recipe_t GetSpawnRecipe(const roundDefine_t& define, const int flags,
                               const int maxMonsterHealth)
{
	recipe_t result;
	mobjTypes_t types;

	// Figure out which monster we want to spawn.
	const mobjTypes_t& all = GatherMonsters();
	for (mobjTypes_t::const_iterator it = all.begin(); it != all.end(); ++it)
	{
		const mobjinfo_t& info = ::mobjinfo[*it];
		if (info.spawnhealth > maxMonsterHealth)
			continue;

		// Translate mobj flags into bitfield we can compare against.
		int mflags = 0;
		if (info.flags & (MF_NOGRAVITY | MF_FLOAT))
			mflags |= ::SPAWN_AIR;
		else
			mflags |= ::SPAWN_GROUND;
		if (info.spawnhealth > define.monsterHealth)
			mflags |= ::SPAWN_BOSS;

		if (!(mflags & flags))
			continue;

		types.push_back(*it);
	}

	size_t resultIdx = P_RandomInt(types.size());
	result.type = types.at(resultIdx);

	// Figure out how many monsters we can spawn of our given type - at least one.
	const int health = ::mobjinfo[types.at(resultIdx)].spawnhealth;
	const int upper = MAX(define.maxGroupHealth / health, 1);
	const int lower = MAX(define.minGroupHealth / health, 1);
	if (upper == lower)
	{
		// Only one possibility.
		result.count = upper;
		return result;
	}

	result.count = P_RandomInt(upper - lower) + lower;
	return result;
}
}; // namespace recipe

namespace spawn
{
struct SpawnPoint
{
	AActor::AActorPtr mo;
};
typedef std::vector<SpawnPoint> SpawnPoints;

struct SpawnPointWeight
{
	SpawnPoint* spawn;
	float score;
	int dist;
	bool visible;
};
typedef std::vector<SpawnPointWeight> SpawnPointWeights;

static SpawnPoints spawns;

static bool CmpWeights(const SpawnPointWeight& a, const SpawnPointWeight& b)
{
	return a.score > b.score;
}

/**
 * @brief Get a spawn point to place a monster at.
 */
static SpawnPoint* GetSpawnCandidate(player_t* player)
{
	float totalScore = 0.0f;

	SpawnPointWeights weights;
	for (SpawnPoints::iterator sit = spawns.begin(); sit != spawns.end(); ++sit)
	{
		SpawnPointWeight weight;
		weight.spawn = &*sit;

		int dist = P_AproxDistance2(sit->mo, player->mo) >> FRACBITS;
		bool visible = P_CheckSight(sit->mo, player->mo);

		weight.dist = dist;
		weight.visible = visible;

		// Base score is based on distance.
		float score;
		if (dist < 192)
			score = 0.125f; // Try not to spawn close by.
		else if (dist < 512)
			score = 1.0f;
		else if (dist < 1024)
			score = 0.75f;
		else if (dist < 2048)
			score = 0.5f;
		else if (dist < 3072)
			score = 0.25f;
		else
			score = 0.125f;

		// Having LoS gives a significant bonus, because intuitively there
		// are many more spanws in a map you can't see vs those you can.
		score *= visible ? 1.0f : 0.5f;
		weight.score = score;
		totalScore += score;

		weights.push_back(weight);
	}

	// Did we find any spawns?
	if (weights.empty())
		return NULL;

	// Sort by weights.
	std::sort(weights.begin(), weights.end(), CmpWeights);
	float choice = P_RandomFloat() * totalScore;

	SpawnPointWeights::const_iterator it = weights.begin();
	for (; it != weights.end(); ++it)
	{
		// If our choice number is less than the score, we found our spawn.
		if (choice < it->score)
			break;

		// Since scores origin at 0.0 we subtract the current score from our
		// choice so the next comparison lines up.
		choice -= it->score;
	}

	// This should not happen unless we were careless with our math.
	if (it == weights.end())
		return NULL;

	return it->spawn;
}

/**
 * @brief Spawn a monster.
 *
 * @param point Spawn point of the monster.
 * @param recipe Recipe of a monster to spawn.
 * @param offset Offset from the spawn point to spawn at.
 * @param player Player to target.
 * @param ambush True if the spawn should be an out-of-sight ambush without
 *               a teleport flash.
 * @return Number of successful spawns,
 */
static int SpawnMonster(spawn::SpawnPoint& spawn, const recipe::recipe_t& recipe,
                        const v2fixed_t offset, player_t* player, const bool ambush)
{
	// Spawn the monster - possibly.
	AActor* mo = new AActor(spawn.mo->x + offset.x, spawn.mo->y + offset.y, spawn.mo->z,
	                        recipe.type);
	if (mo)
	{
		if (P_TestMobjLocation(mo))
		{
			P_AddHealthPool(mo);

			// Don't respawn
			mo->flags |= MF_DROPPED;

			// This monster wakes up hating you and knows where you live.
			mo->target = player->mo->ptr();
			P_SetMobjState(mo, mo->info->seestate, true);

			// Play the see sound if we have one and it's not an ambush.
			if (!ambush && mo->info->seesound)
			{
				char sound[MAX_SNDNAME];

				strcpy(sound, mo->info->seesound);

				if (sound[strlen(sound) - 1] == '1')
				{
					sound[strlen(sound) - 1] = P_Random(mo) % 3 + '1';
					if (S_FindSound(sound) == -1)
						sound[strlen(sound) - 1] = '1';
				}

				S_Sound(mo, CHAN_VOICE, sound, 1, ATTN_NORM);
			}

			if (!ambush)
			{
				// Spawn a teleport fog if it's not an ambush.
				AActor* tele = new AActor(spawn.mo->x, spawn.mo->y, spawn.mo->z, MT_TFOG);
				S_Sound(tele, CHAN_VOICE, "misc/teleport", 1, ATTN_NORM);
			}
			return 1;
		}
		else
		{
			// Spawn blocked.
			mo->Destroy();
		}
	}
	return 0;
}

/**
 * @brief Spawn multiple monsters close to each other.
 *
 * @param point Spawn point of the monster.
 * @param recipe Recipe of a monster to spawn.
 * @param player Player to target.
 * @param ambush True if the spawn should be an out-of-sight ambush without
 *               a teleport flash.
 * @return Number of successful spawns.
 */
static int SpawnMonsterCount(spawn::SpawnPoint& spawn, const recipe::recipe_t& recipe,
                             player_t* player, const bool ambush)
{
	int ok = 0;

	// We might need the radius.
	fixed_t rad = ::mobjinfo[recipe.type].radius;

	int count = clamp(recipe.count, 1, 4);
	if (count == 4)
	{
		// A big square.
		ok += SpawnMonster(spawn, recipe, v2fixed_t(-rad, -rad), player, ambush);
		ok += SpawnMonster(spawn, recipe, v2fixed_t(rad, -rad), player, ambush);
		ok += SpawnMonster(spawn, recipe, v2fixed_t(-rad, rad), player, ambush);
		ok += SpawnMonster(spawn, recipe, v2fixed_t(rad, rad), player, ambush);
		return ok;
	}
	else if (count == 3)
	{
		// A wedge.
		ok += SpawnMonster(spawn, recipe, v2fixed_t(-rad, -rad), player, ambush);
		ok += SpawnMonster(spawn, recipe, v2fixed_t(rad, -rad), player, ambush);
		ok += SpawnMonster(spawn, recipe, v2fixed_t(0, rad), player, ambush);
		return ok;
	}
	else if (count == 2)
	{
		// Next to each other.
		ok += SpawnMonster(spawn, recipe, v2fixed_t(-rad, 0), player, ambush);
		ok += SpawnMonster(spawn, recipe, v2fixed_t(rad, 0), player, ambush);
		return ok;
	}

	// All by themselves :(
	ok += SpawnMonster(spawn, recipe, v2fixed_t(0, 0), player, ambush);
	return ok;
}

} // namespace spawn

static bool DEBUG_enabled;

hordeInfo_t P_HordeInfo()
{
	return ::gDirector.info();
}

void P_ClearHordeSpawnPoints()
{
	spawn::spawns.clear();
}

void P_AddHordeSpawnPoint(AActor* mo)
{
	spawn::SpawnPoint sp;
	sp.mo = mo->self;
	spawn::spawns.push_back(sp);
}

void P_AddHealthPool(AActor* mo)
{
	// Already added?
	if (mo->oflags & MFO_HEALTHPOOL)
		return;

	// Counts as a monster?
	if (!(mo->flags & MF_COUNTKILL || mo->type == MT_SKULL))
		return;

	// Mark as part of the health pool for cleanup later
	mo->oflags |= MFO_HEALTHPOOL;

	::gDirector.addSpawnHealth(::mobjinfo[mo->type].spawnhealth);
}

void P_RemoveHealthPool(AActor* mo)
{
	// Not a part of the pool
	if (!(mo->oflags & MFO_HEALTHPOOL))
		return;

	::gDirector.addKilledHealth(::mobjinfo[mo->type].spawnhealth);
}

void P_RunHordeTics()
{
	// Move this function to tick inside someplace that can be paused.
	if (::paused)
		return;

	if (::level.time == 0)
		::gDirector.reset();

	// Only run logic once a second.
	if (!P_AtInterval(TICRATE))
		return;

	// Switch states?
	::gDirector.stateSwitch();

	// Should we spawn a monster?
	switch (::gDirector.getState())
	{
	case HS_PRESSURE: {
		// Spawn a monster near every player.
		PlayerResults pr = PlayerQuery().execute();
		PlayersView::const_iterator it;
		for (it = pr.players.begin(); it != pr.players.end(); ++it)
		{
			player_t* player = *it;

			// Do not consider dead players.
			if (player->health <= 0)
				continue;

			spawn::SpawnPoint* spawn = spawn::GetSpawnCandidate(player);
			if (spawn == NULL)
				continue;

			const roundDefine_t& define = ::gDirector.getRoundState().getDefine();
			const recipe::recipe_t recipe =
			    recipe::GetSpawnRecipe(define, spawn->mo->special1, define.monsterHealth);
			spawn::SpawnMonsterCount(*spawn, recipe, player, false);
		}
	}
	case HS_RELAX: {
		// Pick a monster that the player can't see right now and teleport
		// them close to the player.
	}
	case HS_BOSS: {
		// Spawn a boss if we don't have one, and
	}
	}
}

bool P_IsHordeMode()
{
	return sv_gametype == GM_HORDE;
}

BEGIN_COMMAND(horde_round)
{
	if (argc < 2)
	{
		Printf("horde_round <ROUND_NUMBER>\n");
		return;
	}

	int round = atoi(argv[1]);
	if (round < 1 || round > 3)
	{
		Printf("Round must be between 1-3.\n");
		return;
	}

	::gDirector.getRoundState().setRound(round);
}
END_COMMAND(horde_round)
