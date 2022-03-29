// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id$
//
// Copyright (C) 1993-1996 by id Software, Inc.
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
//	Interpolation of moving ceiling/floor planes, scrolling texture, etc
//	for uncapped framerates.
//
//-----------------------------------------------------------------------------


#include "odamex.h"

#include "m_fixed.h"
#include "r_state.h"
#include "p_local.h"
#include "cl_demo.h"
#include "p_mapformat.h"


typedef std::pair<fixed_t, unsigned int> fixed_uint_pair;

// Sector interpolation
static std::vector<fixed_uint_pair> prev_ceilingheight;
static std::vector<fixed_uint_pair> saved_ceilingheight;
static std::vector<fixed_uint_pair> prev_floorheight;
static std::vector<fixed_uint_pair> saved_floorheight;

// Sector flat scrolling interpolation
static std::vector<fixed_uint_pair> prev_floor_xoffs;
static std::vector<fixed_uint_pair> saved_floor_xoffs;
static std::vector<fixed_uint_pair> prev_floor_yoffs;
static std::vector<fixed_uint_pair> saved_floor_yoffs;

static std::vector<fixed_uint_pair> prev_ceiling_xoffs;
static std::vector<fixed_uint_pair> saved_ceiling_xoffs;
static std::vector<fixed_uint_pair> prev_ceiling_yoffs;
static std::vector<fixed_uint_pair> saved_ceiling_yoffs;

// Texture scrolling interpolation
static std::vector<fixed_uint_pair> prev_rowoffset;
static std::vector<fixed_uint_pair> saved_rowoffset;
static std::vector<fixed_uint_pair> prev_textureoffset;
static std::vector<fixed_uint_pair> saved_textureoffset;

extern NetDemo netdemo;

//
// R_InterpolationTicker
//
// Records the current height of all moving planes and position of scrolling
// textures, which will be used as the previous position during iterpolation.
// This should be called once per gametic.
//
void R_InterpolationTicker()
{
	prev_ceilingheight.clear();
	prev_floorheight.clear();
	prev_floor_xoffs.clear();
	prev_floor_yoffs.clear();
	prev_ceiling_xoffs.clear();
	prev_ceiling_yoffs.clear();
	prev_rowoffset.clear();
	prev_textureoffset.clear();

	if (gamestate == GS_LEVEL)
	{
		for (int i = 0; i < numsectors; i++)
		{
			if (sectors[i].ceilingdata)
			{
				prev_ceilingheight.push_back(std::make_pair(P_CeilingHeight(&sectors[i]), i));
				prev_ceiling_xoffs.push_back(std::make_pair(sectors[i].ceiling_xoffs, i));
				prev_ceiling_yoffs.push_back(std::make_pair(sectors[i].ceiling_yoffs, i));
			}
			if (sectors[i].floordata)
			{
				prev_floorheight.push_back(std::make_pair(P_FloorHeight(&sectors[i]), i));
				prev_floor_xoffs.push_back(std::make_pair(sectors[i].floor_xoffs, i));
				prev_floor_yoffs.push_back(std::make_pair(sectors[i].floor_yoffs, i));
			}
		}

		for (int i = 0; i < numsides; i++)
		{
			if (P_IsScrollLine(sides[i].special))
			{
				prev_rowoffset.push_back(std::make_pair(sides[i].rowoffset, i));
				prev_textureoffset.push_back(std::make_pair(sides[i].textureoffset, i));
			}
		}
	}
}


//
// R_ResetInterpolation
//
// Clears any saved interpolation related data. This should be called whenever
// a map is loaded.
//
void R_ResetInterpolation()
{
	prev_ceilingheight.clear();
	prev_floorheight.clear();
	prev_floor_xoffs.clear();
	prev_floor_yoffs.clear();
	prev_ceiling_xoffs.clear();
	prev_ceiling_yoffs.clear();
	prev_rowoffset.clear();
	prev_textureoffset.clear();
	saved_ceilingheight.clear();
	saved_floorheight.clear();
	saved_floor_xoffs.clear();
	saved_floor_yoffs.clear();
	saved_ceiling_xoffs.clear();
	saved_ceiling_yoffs.clear();
	saved_rowoffset.clear();
	saved_textureoffset.clear();
	::localview.angle = 0;
	::localview.setangle = false;
	::localview.skipangle = false;
	::localview.pitch = 0;
	::localview.setpitch = false;
	::localview.skippitch = false;
}


//
// R_BeginInterpolation
//
// Saves the current height of all moving planes and position of scrolling
// textures, which will be restored by R_EndInterpolation. The height of a
// moving plane will be interpolated between the previous height and this
// current height. This should be called every time a frame is rendered.
//
void R_BeginInterpolation(fixed_t amount)
{
	saved_ceilingheight.clear();
	saved_floorheight.clear();
	saved_floor_xoffs.clear();
	saved_floor_yoffs.clear();
	saved_ceiling_xoffs.clear();
	saved_ceiling_yoffs.clear();
	saved_rowoffset.clear();
	saved_textureoffset.clear();

	if (gamestate == GS_LEVEL)
	{
		for (std::vector<fixed_uint_pair>::const_iterator ceiling_it = prev_ceilingheight.begin();
			 ceiling_it != prev_ceilingheight.end(); ++ceiling_it)
		{
			unsigned int secnum = ceiling_it->second;
			sector_t* sector = &sectors[secnum];

			fixed_t old_value = ceiling_it->first;
			fixed_t cur_value = P_CeilingHeight(sector);

			saved_ceilingheight.push_back(std::make_pair(cur_value, secnum));
			
			fixed_t new_value = old_value + FixedMul(cur_value - old_value, amount);
			P_SetCeilingHeight(sector, new_value);
		}

		for (std::vector<fixed_uint_pair>::const_iterator floor_it = prev_floorheight.begin();
			 floor_it != prev_floorheight.end(); ++floor_it)
		{
			unsigned int secnum = floor_it->second;
			sector_t* sector = &sectors[secnum];

			fixed_t old_value = floor_it->first;
			fixed_t cur_value = P_FloorHeight(sector);

			saved_floorheight.push_back(std::make_pair(cur_value, secnum));
			
			fixed_t new_value = old_value + FixedMul(cur_value - old_value, amount);
			P_SetFloorHeight(sector, new_value);
		}

		for (std::vector<fixed_uint_pair>::const_iterator floor_xoffs_it =
		         prev_floor_xoffs.begin();
		     floor_xoffs_it != prev_floor_xoffs.end(); ++floor_xoffs_it)
		{
			unsigned int secnum = floor_xoffs_it->second;

			fixed_t old_value = floor_xoffs_it->first;
			fixed_t cur_value = sectors[secnum].floor_xoffs;

			saved_floor_xoffs.push_back(std::make_pair(cur_value, secnum));

			fixed_t new_value = old_value +FixedMul(cur_value - old_value, amount);
			sectors[secnum].floor_xoffs = new_value;
		}
		
		for (std::vector<fixed_uint_pair>::const_iterator floor_yoffs_it =
		         prev_floor_yoffs.begin();
		     floor_yoffs_it != prev_floor_yoffs.end(); ++floor_yoffs_it)
		{
			unsigned int secnum = floor_yoffs_it->second;

			fixed_t old_value = floor_yoffs_it->first;
			fixed_t cur_value = sectors[secnum].floor_yoffs;

			saved_floor_yoffs.push_back(std::make_pair(cur_value, secnum));

			fixed_t new_value = old_value + FixedMul(cur_value - old_value, amount);
			sectors[secnum].floor_yoffs = new_value;
		}

		for (std::vector<fixed_uint_pair>::const_iterator ceiling_xoffs_it =
		         prev_ceiling_xoffs.begin();
		     ceiling_xoffs_it != prev_ceiling_xoffs.end(); ++ceiling_xoffs_it)
		{
			unsigned int secnum = ceiling_xoffs_it->second;

			fixed_t old_value = ceiling_xoffs_it->first;
			fixed_t cur_value = sectors[secnum].ceiling_xoffs;

			saved_ceiling_xoffs.push_back(std::make_pair(cur_value, secnum));

			fixed_t new_value = old_value + FixedMul(cur_value - old_value, amount);
			sectors[secnum].ceiling_xoffs = new_value;
		}

		for (std::vector<fixed_uint_pair>::const_iterator ceiling_yoffs_it =
		         prev_ceiling_yoffs.begin();
		     ceiling_yoffs_it != prev_ceiling_yoffs.end(); ++ceiling_yoffs_it)
		{
			unsigned int secnum = ceiling_yoffs_it->second;

			fixed_t old_value = ceiling_yoffs_it->first;
			fixed_t cur_value = sectors[secnum].ceiling_yoffs;

			saved_ceiling_yoffs.push_back(std::make_pair(cur_value, secnum));

			fixed_t new_value = old_value + FixedMul(cur_value - old_value, amount);
			sectors[secnum].ceiling_yoffs = new_value;
		}
		
		for (std::vector<fixed_uint_pair>::const_iterator rowoffset_it =
		         prev_rowoffset.begin();
		     rowoffset_it != prev_rowoffset.end(); ++rowoffset_it)
		{
			int sidenum = rowoffset_it->second;

			fixed_t old_value = rowoffset_it->first;
			fixed_t cur_value = sides[sidenum].rowoffset;

			saved_rowoffset.push_back(std::make_pair(cur_value, sidenum));

			fixed_t new_value = old_value += FixedMul(cur_value - old_value, amount);
			sides[sidenum].rowoffset = new_value;
		}

		for (std::vector<fixed_uint_pair>::const_iterator textureoffset_it =
		         prev_textureoffset.begin();
		     textureoffset_it != prev_textureoffset.end(); ++textureoffset_it)
		{
			int sidenum = textureoffset_it->second;

			side_t* side = &sides[sidenum];

			fixed_t old_value = textureoffset_it->first;
			fixed_t cur_value = side->textureoffset;

			saved_textureoffset.push_back(std::make_pair(cur_value, sidenum));

			fixed_t new_value = old_value += FixedMul(cur_value - old_value, amount);
			side->textureoffset = new_value;
		}
	}
}

//
// R_EndInterpolation
//
// Restores the saved height of all moving planes and position of scrolling
// textures. This should be called at the end of every frame rendered.
//
void R_EndInterpolation()
{
	if (gamestate == GS_LEVEL)
	{
		for (std::vector<fixed_uint_pair>::const_iterator ceiling_it = saved_ceilingheight.begin();
			 ceiling_it != saved_ceilingheight.end(); ++ceiling_it)
		{
			sector_t* sector = &sectors[ceiling_it->second];
			P_SetCeilingHeight(sector, ceiling_it->first);
		}

		for (std::vector<fixed_uint_pair>::const_iterator floor_it = saved_floorheight.begin();
			 floor_it != saved_floorheight.end(); ++floor_it)
		{
			sector_t* sector = &sectors[floor_it->second];
			P_SetFloorHeight(sector, floor_it->first);
		}

		for (std::vector<fixed_uint_pair>::const_iterator floor_xoffs_it =
		         saved_floor_xoffs.begin();
		     floor_xoffs_it != saved_floor_xoffs.end(); ++floor_xoffs_it)
		{
			sectors[floor_xoffs_it->second].floor_xoffs += floor_xoffs_it->first;
		}

		for (std::vector<fixed_uint_pair>::const_iterator floor_yoffs_it =
		         saved_floor_yoffs.begin();
		     floor_yoffs_it != saved_floor_yoffs.end(); ++floor_yoffs_it)
		{
			sectors[floor_yoffs_it->second].floor_yoffs += floor_yoffs_it->first;
		}

		for (std::vector<fixed_uint_pair>::const_iterator ceiling_xoffs_it =
		         saved_ceiling_xoffs.begin();
		     ceiling_xoffs_it != saved_ceiling_xoffs.end(); ++ceiling_xoffs_it)
		{
			sectors[ceiling_xoffs_it->second].ceiling_xoffs += ceiling_xoffs_it->first;
		}

		for (std::vector<fixed_uint_pair>::const_iterator ceiling_yoffs_it =
		         saved_ceiling_yoffs.begin();
		     ceiling_yoffs_it != saved_ceiling_yoffs.end(); ++ceiling_yoffs_it)
		{
			sectors[ceiling_yoffs_it->second].ceiling_yoffs += ceiling_yoffs_it->first;
		}

		for (std::vector<fixed_uint_pair>::const_iterator rowoffset_it =
		         saved_rowoffset.begin();
		     rowoffset_it != saved_rowoffset.end(); ++rowoffset_it)
		{
			side_t* side = &sides[rowoffset_it->second];
			side->rowoffset += rowoffset_it->first;
		}

		for (std::vector<fixed_uint_pair>::const_iterator textureoffset_it =
		         saved_textureoffset.begin();
		     textureoffset_it != saved_textureoffset.end(); ++textureoffset_it)
		{
			side_t* side = &sides[textureoffset_it->second];
			side->textureoffset += textureoffset_it->first;
		}
	}
}

//
// R_InterpolateCamera
//
// Interpolate between the current position and the previous position
// of the camera. If not using uncapped framerate / interpolation,
// render_lerp_amount will be FRACUNIT.
//

void R_InterpolateCamera(fixed_t amount)
{
	if (gamestate == GS_LEVEL && camera)
	{
		player_t& consolePlayer = consoleplayer();

		if (!::localview.skipangle && consolePlayer.id == displayplayer().id &&
		    consolePlayer.health > 0 && !consolePlayer.mo->reactiontime && 
			(!netdemo.isPlaying() && !demoplayback))
		{
			viewangle = camera->angle + ::localview.angle;
		}
		else
		{
			// Only interpolate if we are spectating
			// interpolate amount/FRACUNIT percent between previous value and current value
			viewangle = camera->prevangle + FixedMul(amount, camera->angle - camera->prevangle);
		}

		viewx = camera->prevx + FixedMul(amount, camera->x - camera->prevx);
		viewy = camera->prevy + FixedMul(amount, camera->y - camera->prevy);

		if (camera->player)
			viewz = camera->player->prevviewz + FixedMul(amount, camera->player->viewz - camera->player->prevviewz);
		else
			viewz = camera->prevz + FixedMul(amount, camera->z - camera->prevz);
	}
}

VERSION_CONTROL (r_interp_cpp, "$Id$")
