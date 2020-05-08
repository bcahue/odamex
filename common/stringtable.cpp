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
//	String Abstraction Layer (StringTable)
//
//-----------------------------------------------------------------------------
//

#include "stringtable.h"

#include <cstring>
#include <stddef.h>

#include "cmdlib.h"
#include "errors.h"
#include "i_system.h"
#include "m_swap.h"
#include "oscanner.h"
#include "stringenums.h"
#include "w_wad.h"

// Contains every original string in its proper order.
static const OString* const stringOrder[] = {
    &D_DEVSTR,
    &D_CDROM,
    &PRESSKEY,
    &PRESSYN,
    &QUITMSG,
    &QUITMSG1,
    &QUITMSG2,
    &QUITMSG3,
    &QUITMSG4,
    &QUITMSG5,
    &QUITMSG6,
    &QUITMSG7,
    &QUITMSG8,
    &QUITMSG9,
    &QUITMSG10,
    &QUITMSG11,
    &QUITMSG12,
    &QUITMSG13,
    &QUITMSG14,
    &LOADNET,
    &QLOADNET,
    &QSAVESPOT,
    &SAVEDEAD,
    &QSPROMPT,
    &QLPROMPT,
    &NEWGAME,
    &NIGHTMARE,
    &SWSTRING,
    &MSGOFF,
    &MSGON,
    &NETEND,
    &ENDGAME,
    &DOSY,
    &EMPTYSTRING,
    &GOTARMOR,
    &GOTMEGA,
    &GOTHTHBONUS,
    &GOTARMBONUS,
    &GOTSTIM,
    &GOTMEDINEED,
    &GOTMEDIKIT,
    &GOTSUPER,
    &GOTBLUECARD,
    &GOTYELWCARD,
    &GOTREDCARD,
    &GOTBLUESKUL,
    &GOTYELWSKUL,
    &GOTREDSKUL,
    &GOTINVUL,
    &GOTBERSERK,
    &GOTINVIS,
    &GOTSUIT,
    &GOTMAP,
    &GOTVISOR,
    &GOTMSPHERE,
    &GOTCLIP,
    &GOTCLIPBOX,
    &GOTROCKET,
    &GOTROCKBOX,
    &GOTCELL,
    &GOTCELLBOX,
    &GOTSHELLS,
    &GOTSHELLBOX,
    &GOTBACKPACK,
    &GOTBFG9000,
    &GOTCHAINGUN,
    &GOTCHAINSAW,
    &GOTLAUNCHER,
    &GOTPLASMA,
    &GOTSHOTGUN,
    &GOTSHOTGUN2,
    &PD_BLUEO,
    &PD_REDO,
    &PD_YELLOWO,
    &PD_BLUEK,
    &PD_REDK,
    &PD_YELLOWK,
    &GGSAVED,
    &HUSTR_MSGU,
    &HUSTR_E1M1,
    &HUSTR_E1M2,
    &HUSTR_E1M3,
    &HUSTR_E1M4,
    &HUSTR_E1M5,
    &HUSTR_E1M6,
    &HUSTR_E1M7,
    &HUSTR_E1M8,
    &HUSTR_E1M9,
    &HUSTR_E2M1,
    &HUSTR_E2M2,
    &HUSTR_E2M3,
    &HUSTR_E2M4,
    &HUSTR_E2M5,
    &HUSTR_E2M6,
    &HUSTR_E2M7,
    &HUSTR_E2M8,
    &HUSTR_E2M9,
    &HUSTR_E3M1,
    &HUSTR_E3M2,
    &HUSTR_E3M3,
    &HUSTR_E3M4,
    &HUSTR_E3M5,
    &HUSTR_E3M6,
    &HUSTR_E3M7,
    &HUSTR_E3M8,
    &HUSTR_E3M9,
    &HUSTR_E4M1,
    &HUSTR_E4M2,
    &HUSTR_E4M3,
    &HUSTR_E4M4,
    &HUSTR_E4M5,
    &HUSTR_E4M6,
    &HUSTR_E4M7,
    &HUSTR_E4M8,
    &HUSTR_E4M9,
    &HUSTR_1,
    &HUSTR_2,
    &HUSTR_3,
    &HUSTR_4,
    &HUSTR_5,
    &HUSTR_6,
    &HUSTR_7,
    &HUSTR_8,
    &HUSTR_9,
    &HUSTR_10,
    &HUSTR_11,
    &HUSTR_12,
    &HUSTR_13,
    &HUSTR_14,
    &HUSTR_15,
    &HUSTR_16,
    &HUSTR_17,
    &HUSTR_18,
    &HUSTR_19,
    &HUSTR_20,
    &HUSTR_21,
    &HUSTR_22,
    &HUSTR_23,
    &HUSTR_24,
    &HUSTR_25,
    &HUSTR_26,
    &HUSTR_27,
    &HUSTR_28,
    &HUSTR_29,
    &HUSTR_30,
    &HUSTR_31,
    &HUSTR_32,
    &PHUSTR_1,
    &PHUSTR_2,
    &PHUSTR_3,
    &PHUSTR_4,
    &PHUSTR_5,
    &PHUSTR_6,
    &PHUSTR_7,
    &PHUSTR_8,
    &PHUSTR_9,
    &PHUSTR_10,
    &PHUSTR_11,
    &PHUSTR_12,
    &PHUSTR_13,
    &PHUSTR_14,
    &PHUSTR_15,
    &PHUSTR_16,
    &PHUSTR_17,
    &PHUSTR_18,
    &PHUSTR_19,
    &PHUSTR_20,
    &PHUSTR_21,
    &PHUSTR_22,
    &PHUSTR_23,
    &PHUSTR_24,
    &PHUSTR_25,
    &PHUSTR_26,
    &PHUSTR_27,
    &PHUSTR_28,
    &PHUSTR_29,
    &PHUSTR_30,
    &PHUSTR_31,
    &PHUSTR_32,
    &THUSTR_1,
    &THUSTR_2,
    &THUSTR_3,
    &THUSTR_4,
    &THUSTR_5,
    &THUSTR_6,
    &THUSTR_7,
    &THUSTR_8,
    &THUSTR_9,
    &THUSTR_10,
    &THUSTR_11,
    &THUSTR_12,
    &THUSTR_13,
    &THUSTR_14,
    &THUSTR_15,
    &THUSTR_16,
    &THUSTR_17,
    &THUSTR_18,
    &THUSTR_19,
    &THUSTR_20,
    &THUSTR_21,
    &THUSTR_22,
    &THUSTR_23,
    &THUSTR_24,
    &THUSTR_25,
    &THUSTR_26,
    &THUSTR_27,
    &THUSTR_28,
    &THUSTR_29,
    &THUSTR_30,
    &THUSTR_31,
    &THUSTR_32,
    &HUSTR_TALKTOSELF1,
    &HUSTR_TALKTOSELF2,
    &HUSTR_TALKTOSELF3,
    &HUSTR_TALKTOSELF4,
    &HUSTR_TALKTOSELF5,
    &HUSTR_MESSAGESENT,
    &AMSTR_FOLLOWON,
    &AMSTR_FOLLOWOFF,
    &AMSTR_GRIDON,
    &AMSTR_GRIDOFF,
    &AMSTR_MARKEDSPOT,
    &AMSTR_MARKSCLEARED,
    &STSTR_MUS,
    &STSTR_NOMUS,
    &STSTR_DQDON,
    &STSTR_DQDOFF,
    &STSTR_KFAADDED,
    &STSTR_FAADDED,
    &STSTR_NCON,
    &STSTR_NCOFF,
    &STSTR_BEHOLD,
    &STSTR_BEHOLDX,
    &STSTR_CHOPPERS,
    &STSTR_CLEV,
    &E1TEXT,
    &E2TEXT,
    &E3TEXT,
    &E4TEXT,
    &C1TEXT,
    &C2TEXT,
    &C3TEXT,
    &C4TEXT,
    &C5TEXT,
    &C6TEXT,
    &P1TEXT,
    &P2TEXT,
    &P3TEXT,
    &P4TEXT,
    &P5TEXT,
    &P6TEXT,
    &T1TEXT,
    &T2TEXT,
    &T3TEXT,
    &T4TEXT,
    &T5TEXT,
    &T6TEXT,
    &CC_ZOMBIE,
    &CC_SHOTGUN,
    &CC_HEAVY,
    &CC_IMP,
    &CC_DEMON,
    &CC_LOST,
    &CC_CACO,
    &CC_HELL,
    &CC_BARON,
    &CC_ARACH,
    &CC_PAIN,
    &CC_REVEN,
    &CC_MANCU,
    &CC_ARCH,
    &CC_SPIDER,
    &CC_CYBER,
    &CC_HERO,
    &PD_BLUEC,
    &PD_REDC,
    &PD_YELLOWC,
    &PD_BLUES,
    &PD_REDS,
    &PD_YELLOWS,
    &PD_ANY,
    &PD_ALL3,
    &PD_ALL6,
    &BGFLATE1,
    &BGFLATE2,
    &BGFLATE3,
    &BGFLATE4,
    &BGFLAT06,
    &BGFLAT11,
    &BGFLAT20,
    &BGFLAT30,
    &BGFLAT15,
    &BGFLAT31,
    &BGCASTCALL,
    &TXT_FRAGLIMIT,
    &TXT_TIMELIMIT,
    &SPREEKILLSELF,
    &SPREEOVER,
    &SPREE5,
    &SPREE10,
    &SPREE15,
    &SPREE20,
    &SPREE25,
    &MULTI2,
    &MULTI3,
    &MULTI4,
    &MULTI5,
    &OB_SUICIDE,
    &OB_FALLING,
    &OB_CRUSH,
    &OB_EXIT,
    &OB_WATER,
    &OB_SLIME,
    &OB_LAVA,
    &OB_BARREL,
    &OB_SPLASH,
    &OB_R_SPLASH,
    &OB_ROCKET,
    &OB_KILLEDSELF,
    &OB_STEALTHBABY,
    &OB_STEALTHVILE,
    &OB_STEALTHBARON,
    &OB_STEALTHCACO,
    &OB_STEALTHCHAINGUY,
    &OB_STEALTHDEMON,
    &OB_STEALTHKNIGHT,
    &OB_STEALTHIMP,
    &OB_STEALTHFATSO,
    &OB_STEALTHUNDEAD,
    &OB_STEALTHSHOTGUY,
    &OB_STEALTHZOMBIE,
    &OB_UNDEADHIT,
    &OB_IMPHIT,
    &OB_CACOHIT,
    &OB_DEMONHIT,
    &OB_SPECTREHIT,
    &OB_BARONHIT,
    &OB_KNIGHTHIT,
    &OB_ZOMBIE,
    &OB_SHOTGUY,
    &OB_VILE,
    &OB_UNDEAD,
    &OB_FATSO,
    &OB_CHAINGUY,
    &OB_SKULL,
    &OB_IMP,
    &OB_CACO,
    &OB_BARON,
    &OB_KNIGHT,
    &OB_SPIDER,
    &OB_BABY,
    &OB_CYBORG,
    &OB_WOLFSS,
    &OB_CHICKEN,
    &OB_BEAST,
    &OB_CLINK,
    &OB_DSPARIL1,
    &OB_DSPARIL1HIT,
    &OB_DSPARIL2,
    &OB_DSPARIL2HIT,
    &OB_HERETICIMP,
    &OB_HERETICIMPHIT,
    &OB_IRONLICH,
    &OB_IRONLICHHIT,
    &OB_BONEKNIGHT,
    &OB_BONEKNIGHTHIT,
    &OB_MUMMY,
    &OB_MUMMYLEADER,
    &OB_SNAKE,
    &OB_WIZARD,
    &OB_WIZARDHIT,
    &OB_MPFIST,
    &OB_MPCHAINSAW,
    &OB_MPPISTOL,
    &OB_MPSHOTGUN,
    &OB_MPSSHOTGUN,
    &OB_MPCHAINGUN,
    &OB_MPROCKET,
    &OB_MPR_SPLASH,
    &OB_MPPLASMARIFLE,
    &OB_MPBFG_BOOM,
    &OB_MPBFG_SPLASH,
    &OB_MPTELEFRAG,
    &OB_RAILGUN,
    &OB_DEFAULT,
    &OB_FRIENDLY1,
    &OB_FRIENDLY2,
    &OB_FRIENDLY3,
    &OB_FRIENDLY4,
    &SAVEGAMENAME,
    &STARTUP1,
    &STARTUP2,
    &STARTUP3,
    &STARTUP4,
    &STARTUP5,
    &HE1TEXT,
    &HE2TEXT,
    &HE3TEXT,
    &HE4TEXT,
    &HE5TEXT,
    &HHUSTR_E1M1,
    &HHUSTR_E1M2,
    &HHUSTR_E1M3,
    &HHUSTR_E1M4,
    &HHUSTR_E1M5,
    &HHUSTR_E1M6,
    &HHUSTR_E1M7,
    &HHUSTR_E1M8,
    &HHUSTR_E1M9,
    &HHUSTR_E2M1,
    &HHUSTR_E2M2,
    &HHUSTR_E2M3,
    &HHUSTR_E2M4,
    &HHUSTR_E2M5,
    &HHUSTR_E2M6,
    &HHUSTR_E2M7,
    &HHUSTR_E2M8,
    &HHUSTR_E2M9,
    &HHUSTR_E3M1,
    &HHUSTR_E3M2,
    &HHUSTR_E3M3,
    &HHUSTR_E3M4,
    &HHUSTR_E3M5,
    &HHUSTR_E3M6,
    &HHUSTR_E3M7,
    &HHUSTR_E3M8,
    &HHUSTR_E3M9,
    &HHUSTR_E4M1,
    &HHUSTR_E4M2,
    &HHUSTR_E4M3,
    &HHUSTR_E4M4,
    &HHUSTR_E4M5,
    &HHUSTR_E4M6,
    &HHUSTR_E4M7,
    &HHUSTR_E4M8,
    &HHUSTR_E4M9,
    &HHUSTR_E5M1,
    &HHUSTR_E5M2,
    &HHUSTR_E5M3,
    &HHUSTR_E5M4,
    &HHUSTR_E5M5,
    &HHUSTR_E5M6,
    &HHUSTR_E5M7,
    &HHUSTR_E5M8,
    &HHUSTR_E5M9,
    &TXT_GOTBLUEKEY,
    &TXT_GOTYELLOWKEY,
    &TXT_GOTGREENKEY,
    &TXT_ARTIHEALTH,
    &TXT_ARTIFLY,
    &TXT_ARTIINVULNERABILITY,
    &TXT_ARTITOMEOFPOWER,
    &TXT_ARTIINVISIBILITY,
    &TXT_ARTIEGG,
    &TXT_ARTISUPERHEALTH,
    &TXT_ARTITORCH,
    &TXT_ARTIFIREBOMB,
    &TXT_ARTITELEPORT,
    &TXT_ITEMHEALTH,
    &TXT_ITEMBAGOFHOLDING,
    &TXT_ITEMSHIELD1,
    &TXT_ITEMSHIELD2,
    &TXT_ITEMSUPERMAP,
    &TXT_AMMOGOLDWAND1,
    &TXT_AMMOGOLDWAND2,
    &TXT_AMMOMACE1,
    &TXT_AMMOMACE2,
    &TXT_AMMOCROSSBOW1,
    &TXT_AMMOCROSSBOW2,
    &TXT_AMMOBLASTER1,
    &TXT_AMMOBLASTER2,
    &TXT_AMMOSKULLROD1,
    &TXT_AMMOSKULLROD2,
    &TXT_AMMOPHOENIXROD1,
    &TXT_AMMOPHOENIXROD2,
    &TXT_WPNMACE,
    &TXT_WPNCROSSBOW,
    &TXT_WPNBLASTER,
    &TXT_WPNSKULLROD,
    &TXT_WPNPHOENIXROD,
    &TXT_WPNGAUNTLETS,
    &TXT_NEEDBLUEKEY,
    &TXT_NEEDGREENKEY,
    &TXT_NEEDYELLOWKEY,
    &TXT_CHEATHEALTH,
    &TXT_CHEATKEYS,
    &TXT_CHEATSOUNDON,
    &TXT_CHEATSOUNDOFF,
    &TXT_CHEATIDDQD,
    &TXT_CHEATIDKFA,
    &TXT_CHEATTICKERON,
    &TXT_CHEATTICKEROFF,
    &TXT_CHEATARTIFACTS3,
    &RAVENQUITMSG,
    &TXT_MANA_1,
    &TXT_MANA_2,
    &TXT_MANA_BOTH,
    &TXT_KEY_STEEL,
    &TXT_KEY_CAVE,
    &TXT_KEY_AXE,
    &TXT_KEY_FIRE,
    &TXT_KEY_EMERALD,
    &TXT_KEY_DUNGEON,
    &TXT_KEY_SILVER,
    &TXT_KEY_RUSTED,
    &TXT_KEY_HORN,
    &TXT_KEY_SWAMP,
    &TXT_KEY_CASTLE,
    &TXT_ARTIINVULNERABILITY2,
    &TXT_ARTISUMMON,
    &TXT_ARTIEGG2,
    &TXT_ARTIPOISONBAG,
    &TXT_ARTITELEPORTOTHER,
    &TXT_ARTISPEED,
    &TXT_ARTIBOOSTMANA,
    &TXT_ARTIBOOSTARMOR,
    &TXT_ARTIBLASTRADIUS,
    &TXT_ARTIHEALINGRADIUS,
    &TXT_ARTIPUZZSKULL,
    &TXT_ARTIPUZZGEMBIG,
    &TXT_ARTIPUZZGEMRED,
    &TXT_ARTIPUZZGEMGREEN1,
    &TXT_ARTIPUZZGEMGREEN2,
    &TXT_ARTIPUZZGEMBLUE1,
    &TXT_ARTIPUZZGEMBLUE2,
    &TXT_ARTIPUZZBOOK1,
    &TXT_ARTIPUZZBOOK2,
    &TXT_ARTIPUZZSKULL2,
    &TXT_ARTIPUZZFWEAPON,
    &TXT_ARTIPUZZCWEAPON,
    &TXT_ARTIPUZZMWEAPON,
    &TXT_ARTIPUZZGEAR,
    &TXT_USEPUZZLEFAILED,
    &TXT_ARMOR1,
    &TXT_ARMOR2,
    &TXT_ARMOR3,
    &TXT_ARMOR4,
    &TXT_WEAPON_F2,
    &TXT_WEAPON_F3,
    &TXT_WEAPON_F4,
    &TXT_WEAPON_C2,
    &TXT_WEAPON_C3,
    &TXT_WEAPON_C4,
    &TXT_WEAPON_M2,
    &TXT_WEAPON_M3,
    &TXT_WEAPON_M4,
    &TXT_QUIETUS_PIECE,
    &TXT_WRAITHVERGE_PIECE,
    &TXT_BLOODSCOURGE_PIECE,
    &BBA_BONED,
    &BBA_CASTRA,
    &BBA_CREAMED,
    &BBA_DECIMAT,
    &BBA_DESTRO,
    &BBA_DICED,
    &BBA_DISEMBO,
    &BBA_FLATTE,
    &BBA_JUSTICE,
    &BBA_MADNESS,
    &BBA_KILLED,
    &BBA_MINCMEAT,
    &BBA_MASSACR,
    &BBA_MUTILA,
    &BBA_REAMED,
    &BBA_RIPPED,
    &BBA_SLAUGHT,
    &BBA_SMASHED,
    &BBA_SODOMIZ,
    &BBA_SPLATT,
    &BBA_SQUASH,
    &BBA_THROTTL,
    &BBA_WASTED,
    &BBA_BODYBAG,
    &BBA_HELL,
    &BBA_TOAST,
    &BBA_SNUFF,
    &BBA_HOSED,
    &BBA_SPRAYED,
    &BBA_DOGMEAT,
    &BBA_BEATEN,
    &BBA_EXCREMENT,
    &BBA_HAMBURGER,
    &BBA_SCROTUM,
    &BBA_POPULATION,
    &BBA_SUICIDE,
    &BBA_DARWIN,
    &MUSIC_E1M1,
    &MUSIC_E1M2,
    &MUSIC_E1M3,
    &MUSIC_E1M4,
    &MUSIC_E1M5,
    &MUSIC_E1M6,
    &MUSIC_E1M7,
    &MUSIC_E1M8,
    &MUSIC_E1M9,
    &MUSIC_E2M1,
    &MUSIC_E2M2,
    &MUSIC_E2M3,
    &MUSIC_E2M4,
    &MUSIC_E2M5,
    &MUSIC_E2M6,
    &MUSIC_E2M7,
    &MUSIC_E2M8,
    &MUSIC_E2M9,
    &MUSIC_E3M1,
    &MUSIC_E3M2,
    &MUSIC_E3M3,
    &MUSIC_E3M4,
    &MUSIC_E3M5,
    &MUSIC_E3M6,
    &MUSIC_E3M7,
    &MUSIC_E3M8,
    &MUSIC_E3M9,
    &MUSIC_INTER,
    &MUSIC_INTRO,
    &MUSIC_BUNNY,
    &MUSIC_VICTOR,
    &MUSIC_INTROA,
    &MUSIC_RUNNIN,
    &MUSIC_STALKS,
    &MUSIC_COUNTD,
    &MUSIC_BETWEE,
    &MUSIC_DOOM,
    &MUSIC_THE_DA,
    &MUSIC_SHAWN,
    &MUSIC_DDTBLU,
    &MUSIC_IN_CIT,
    &MUSIC_DEAD,
    &MUSIC_STLKS2,
    &MUSIC_THEDA2,
    &MUSIC_DOOM2,
    &MUSIC_DDTBL2,
    &MUSIC_RUNNI2,
    &MUSIC_DEAD2,
    &MUSIC_STLKS3,
    &MUSIC_ROMERO,
    &MUSIC_SHAWN2,
    &MUSIC_MESSAG,
    &MUSIC_COUNT2,
    &MUSIC_DDTBL3,
    &MUSIC_AMPIE,
    &MUSIC_THEDA3,
    &MUSIC_ADRIAN,
    &MUSIC_MESSG2,
    &MUSIC_ROMER2,
    &MUSIC_TENSE,
    &MUSIC_SHAWN3,
    &MUSIC_OPENIN,
    &MUSIC_EVIL,
    &MUSIC_ULTIMA,
    &MUSIC_READ_M,
    &MUSIC_DM2TTL,
    &MUSIC_DM2INT,
};

void StringTable::clearStrings()
{
	_stringHash.empty();
	_indexes.empty();
}

void StringTable::loadLanguage(uint32_t code, bool exactMatch, char* lump, size_t lumpLen)
{
	OScannerConfig config = {
	    "LANGUAGE", // lumpName
	    false,      // semiComments
	    true,       // cComments
	};
	OScanner os = OScanner::openBuffer(config, lump, lump + lumpLen);
	while (os.scan())
	{
		// Parse a language section.
		bool shouldParseSection = false;

		os.assertTokenIs("[");
		while (os.scan())
		{
			if (os.compareToken("]"))
			{
				break;
			}
			else if (os.compareToken("default"))
			{
				// Default has a speical ID.
				if (code == (uint32_t)MAKE_ID('*', '*', 0, 0))
				{
					shouldParseSection = true;
				}
			}
			else
			{
				// Turn the language into an ID and compare it with the passed code.
				const std::string& lang = os.getToken();

				if (lang.length() == 2)
				{
					if (code == (uint32_t)MAKE_ID(lang[0], lang[1], '\0', '\0'))
					{
						shouldParseSection = true;
					}
				}
				else if (lang.length() == 3)
				{
					if (code == (uint32_t)MAKE_ID(lang[0], lang[1], lang[2], '\0'))
					{
						shouldParseSection = true;
					}
				}
				else
				{
					os.error("Language identifier must be 2 or 3 characters");
				}
			}
		}

		if (shouldParseSection)
		{
			// Parse all of the strings in this section.
			while (os.scan())
			{
				if (os.compareToken("["))
				{
					// We reached the end of the section.
					os.unScan();
					break;
				}

				// String name
				const std::string& name = os.getToken();

				// If we can find the token, skip past the string
				if (hasString(name))
				{
					while (os.scan())
					{
						if (os.compareToken(";"))
							break;
					}
					continue;
				}

				os.scan();
				os.assertTokenIs("=");

				// Grab the string value.
				std::string value;
				while (os.scan())
				{
					const std::string piece = os.getToken();
					if (piece.compare(";") == 0)
					{
						// Found the end of the string, next batter up.
						break;
					}

					value += piece;
				}

				setMissingString(name, value);
			}
		}
		else
		{
			// Skip past all of the strings in this section.
			while (os.scan())
			{
				if (os.compareToken("["))
				{
					// Found another section, parse it.
					os.unScan();
					break;
				}
			}
		}
	}
}

void StringTable::loadStringsLump(int lump, const char* lumpname)
{
	// Can't use Z_Malloc this early, so we use raw new/delete.
	size_t len = W_LumpLength(lump);
	char* languageLump = new char[len + 1];
	W_ReadLump(lump, languageLump);
	languageLump[len] = '\0';

	// Load language-specific strings.
	for (size_t i = 0; i < ARRAY_LENGTH(LanguageIDs); i++)
	{
		loadLanguage(LanguageIDs[i], true, languageLump, len);
		loadLanguage(LanguageIDs[i] & MAKE_ID(0xff, 0xff, 0, 0), true, languageLump, len);
		loadLanguage(LanguageIDs[i], false, languageLump, len);
	}

	// Load string defaults.
	loadLanguage(MAKE_ID('*', '*', 0, 0), true, languageLump, len);

	delete[] languageLump;
}

void StringTable::prepareIndexes()
{
	// All of the default strings have index numbers that represent their
	// position in the now-removed enumeration.  This function simply sets
	// them all up.
	for (size_t i = 0; i < ARRAY_LENGTH(::stringOrder); i++)
	{
		OString name = *(::stringOrder[i]);
		StringHash::iterator it = _stringHash.find(name);
		if (it == _stringHash.end())
		{
			TableEntry entry = {std::make_pair(false, ""), i};
			_stringHash.insert(std::make_pair(name, entry));
		}
	}
}

//
// Dump all strings to the console.
//
// Sometimes a blunt instrument is what is necessary.
//
void StringTable::dumpStrings()
{
	StringHash::const_iterator it = _stringHash.begin();
	for (; it != _stringHash.end(); ++it)
	{
		Printf(PRINT_HIGH, "%s (%d) = %s\n", (*it).first.c_str(), (*it).second.index,
		       (*it).second.string.second.c_str());
	}
}

//
// See if a string exists in the table.
//
bool StringTable::hasString(const OString& name) const
{
	StringHash::const_iterator it = _stringHash.find(name);
	if (it == _stringHash.end())
		return false;
	if ((*it).second.string.first == false)
		return false;

	return true;
}

//
// Load strings from all LANGUAGE lumps in all loaded WAD files.
//
void StringTable::loadStrings()
{
	clearStrings();
	prepareIndexes();

	int lump = -1;

	lump = -1;
	while ((lump = W_FindLump("LANGUAGE", lump)) != -1)
	{
		loadStringsLump(lump, "LANGUAGE");
	}
}

//
// Find a string with the same text.
//
const OString& StringTable::matchString(const OString& string) const
{
	for (StringHash::const_iterator it = _stringHash.begin(); it != _stringHash.end(); ++it)
	{
		if ((*it).second.string.first == false)
			continue;
		if ((*it).second.string.second == string)
			return (*it).first;
	}

	static OString empty = "";
	return empty;
}

//
// Set a string to something specific by name.
//
// Overrides the existing string, if it exists.
//
void StringTable::setString(const OString& name, const OString& string)
{
	StringHash::iterator it = _stringHash.find(name);
	if (it == _stringHash.end())
	{
		// Stringtable entry does nto exist, insert it.
		TableEntry entry = {std::make_pair(true, string), -1};
		_stringHash.insert(std::make_pair(name, entry));
	}
	else
	{
		// Stringtable entry exists, update it.
		(*it).second.string.first = true;
		(*it).second.string.second = string;
	}
}

//
// Set a string to something specific by name.
//
// Does not set the string if it already exists.
//
void StringTable::setMissingString(const OString& name, const OString& string)
{
	StringHash::iterator it = _stringHash.find(name);
	if (it == _stringHash.end())
	{
		// Stringtable entry does not exist.
		TableEntry entry = {std::make_pair(true, string), -1};
		_stringHash.insert(std::make_pair(name, entry));
	}
	else if ((*it).second.string.first == true)
	{
		// We already set this string
		return;
	}
	else
	{
		// Stringtable entry exists, but has not been set yet.
		(*it).second.string.first = true;
		(*it).second.string.second = string;
	}
}

//
// Number of entries in the stringtable.
//
size_t StringTable::size() const
{
	return _stringHash.size();
}

VERSION_CONTROL(stringtable_cpp, "$Id$")
