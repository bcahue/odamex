// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id$
//
// Copyright (C) 2006-2015 by The Odamex Team.
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
//
// Game resource file abstractions
//
//-----------------------------------------------------------------------------

#include "resources/res_container.h"
#include "resources/res_main.h"
#include "resources/res_resourcepath.h"
#include "resources/res_fileaccessor.h"
#include "resources/res_identifier.h"

#include "m_ostring.h"
#include "w_ident.h"

//
// Res_IsMapLumpName
//
// Returns true if the given lump name is a name reserved for map lumps.
//
static bool Res_IsMapLumpName(const OString& name)
{
	static const OString things_lump_name("THINGS");
	static const OString linedefs_lump_name("LINEDEFS");
	static const OString sidedefs_lump_name("SIDEDEFS");
	static const OString vertexes_lump_name("VERTEXES");
	static const OString segs_lump_name("SEGS");
	static const OString ssectors_lump_name("SSECTORS");
	static const OString nodes_lump_name("NODES");
	static const OString sectors_lump_name("SECTORS");
	static const OString reject_lump_name("REJECT");
	static const OString blockmap_lump_name("BLOCKMAP");
	static const OString behavior_lump_name("BEHAVIOR");

	const OString uname = OStringToUpper(name);

	return uname == things_lump_name || uname == linedefs_lump_name || uname == sidedefs_lump_name ||
		uname == vertexes_lump_name || uname == segs_lump_name || uname == ssectors_lump_name ||
		uname == nodes_lump_name || uname == sectors_lump_name || uname == reject_lump_name ||
		uname == blockmap_lump_name || uname == behavior_lump_name;
}


// ============================================================================
//
// SingleLumpResourceContainer class implementation
//
// ============================================================================

//
// SingleLumpResourceContainer::SingleLumpResourceContainer
//
// Use the filename as the name of the lump and attempt to determine which
// directory to insert the lump into.
//
SingleLumpResourceContainer::SingleLumpResourceContainer(
	const OString& path,
	const ResourceContainerId& container_id,
	ResourceManager* manager) :
		ResourceContainer(container_id, manager),
		mFile(NULL),
		mResourceId(ResourceId::INVALID_ID)
{
	mFile = new DiskFileAccessor(path);
	if (mFile->valid())
	{
		const OString& filename = mFile->getFileName();

		// the filename serves as the lump_name
		std::string base_filename;
		M_ExtractFileBase(filename, base_filename);
		OString lump_name = OStringToUpper(base_filename.c_str(), 8);

		// rename lump_name to DEHACKED if it's a DeHackEd file
		if (Res_IsDehackedFile(filename))
			lump_name = "DEHACKED";

		ResourcePath path = Res_MakeResourcePath(lump_name, global_directory_name);
		mResourceId = manager->addResource(path, this);
	}
}


//
// SingleLumpResourceContainer::~SingleLumpResourceContainer
//
SingleLumpResourceContainer::~SingleLumpResourceContainer()
{
	delete mFile;
}


//
// SingleLumpResourceContainer::getResourceCount
//
uint32_t SingleLumpResourceContainer::getResourceCount() const
{
	return mFile->valid() ? 1 : 0;
}


//
// SingleLumpResourceContainer::getResourceSize
//
uint32_t SingleLumpResourceContainer::getResourceSize(const ResourceId res_id) const
{
	if (res_id == mResourceId)
		return mFile->size();
	return 0;
}


//
// SingleLumpResourceContainer::loadResource
//
uint32_t SingleLumpResourceContainer::loadResource(void* data, const ResourceId res_id, uint32_t size) const
{
	size = std::min(size, getResourceSize(res_id));
	if (size > 0)
		return mFile->read(data, size); 
	return 0;
}



// ============================================================================
//
// WadResourceContainer class implementation
//
// ============================================================================

//
// WadResourceContainer::WadResourceContainer
//
// Reads the lump directory from the WAD file and registers all of the lumps
// with the ResourceManager. If the WAD file has an invalid directory, no lumps
// will be registered and getResourceCount() will return 0.
//
WadResourceContainer::WadResourceContainer(
	const OString& path,
	const ResourceContainerId& container_id,
	ResourceManager* manager) :
		ResourceContainer(container_id, manager),
		mFile(NULL),
		mDirectory(256),
		mLumpIdLookup(256),
		mIsIWad(false)
{
	mIsIWad = W_IsIWAD(path);
	mFile = new DiskFileAccessor(path);

	if (!readWadDirectory())
		mDirectory.clear();

	buildMarkerRecords();

	// Examine each lump and decide which path it belongs in
	// and then register it with the resource manager.
	addResourcesToManager(manager);
}


//
// WadResourceContainer::~WadResourceContainer
//
WadResourceContainer::~WadResourceContainer()
{
	delete mFile;
}


//
// WadResourceContainer::readWadDirectory
//
// Reads the directory of a WAD file and returns false if there
// are errors parsing the WAD directory or populates the instance's
// ContainerDirectory if successful.
//
bool WadResourceContainer::readWadDirectory()
{
	uint32_t magic;
	if (!mFile->read(&magic))
		return false;

	magic = LELONG(magic);
	if (magic != ('I' | ('W' << 8) | ('A' << 16) | ('D' << 24)) && 
		magic != ('P' | ('W' << 8) | ('A' << 16) | ('D' << 24)))
		return false;

	int32_t wad_lump_count;
	if (!mFile->read(&wad_lump_count))
		return false;

	wad_lump_count = LELONG(wad_lump_count);
	if (wad_lump_count < 1)
		return false;

	int32_t wad_table_offset;
	if (!mFile->read(&wad_table_offset) || wad_table_offset < 0)
		return false;

	wad_table_offset = LELONG(wad_table_offset);

	// The layout for a lump entry is:
	//    int32_t offset
	//    int32_t length
	//    char    name[8]
	const uint32_t wad_lump_record_length = 4 + 4 + 8;

	uint32_t wad_table_length = wad_lump_count * wad_lump_record_length;
	if (wad_table_offset < 12 || wad_table_length + wad_table_offset > mFile->size())
		return false;

	// read the WAD lump directory
	mFile->seek(wad_table_offset);

	uint8_t* wad_directory = new uint8_t[wad_table_length];

	if (mFile->read(wad_directory, wad_table_length) == wad_table_length)
	{
		mDirectory.reserve(wad_lump_count);

		const uint8_t* ptr = wad_directory;
		for (uint32_t wad_lump_num = 0; wad_lump_num < (uint32_t)wad_lump_count; wad_lump_num++, ptr += wad_lump_record_length)
		{
			int32_t offset = LELONG(*(int32_t*)(ptr + 0));
			int32_t length = LELONG(*(int32_t*)(ptr + 4));
			const char* name = (char*)(ptr + 8);
			mDirectory.addEntryInfo(OStringToUpper(name, 8), length, offset);
		}

		delete [] wad_directory;
		return true;
	}

	delete [] wad_directory;
	return false;
}


//
// WadResourceContainer::buildMarkerRecords
//
void WadResourceContainer::buildMarkerRecords()
{
	OString last_marker_prefix;
	LumpId last_lump_id = ContainerDirectory::INVALID_LUMP_ID;

	for (ContainerDirectory::const_iterator it = mDirectory.begin(); it != mDirectory.end(); ++it)
	{
		const OString& path = it->path;
		if (isMarker(path))
		{
			const OString current_marker_prefix(getMarkerPrefix(path));
			LumpId current_lump_id = mDirectory.getLumpId(it);

			// TODO: attempt to repair broken WADs that have missing markers

			if (getMarkerType(path) == END_MARKER)
			{
				MarkerRange range;
				range.start = last_lump_id;
				range.end = current_lump_id;
				mMarkers[current_marker_prefix] = range;
				DPrintf("Added markers %s_START (%d) and %s_END (%d)\n",
						current_marker_prefix.c_str(), range.start,
						current_marker_prefix.c_str(), range.end);
			}
			last_marker_prefix = current_marker_prefix;
			last_lump_id = current_lump_id;
		}
	}
}


//
// WadResourceContainer::assignPathBasedOnMarkers
//
// Determines which markers a lump is between (if any) and assigns a
// path for the resource.
//
const ResourcePath& WadResourceContainer::assignPathBasedOnMarkers(LumpId lump_id) const
{
	for (MarkerRangeLookupTable::const_iterator it = mMarkers.begin(); it != mMarkers.end(); ++it)
	{
		const MarkerRange& range = it->second;
		if (lump_id > range.start && lump_id < range.end)
		{
			const OString& prefix = it->first;
			if (prefix == "F" || prefix == "FF")
				return flats_directory_name;
			if (prefix == "S" || prefix == "SS")
				return sprites_directory_name;
			if (prefix == "P" || prefix == "PP")
				return patches_directory_name;
			if (prefix == "C")
				return colormaps_directory_name;
			if (prefix == "TX")
				return textures_directory_name;
			if (prefix == "HI")
				return hires_directory_name;
		}
	}
	return global_directory_name;
}


//
// WadResourceContainer::addResourcesToManager
//
void WadResourceContainer::addResourcesToManager(ResourceManager* manager)
{
	OString map_name;

	for (ContainerDirectory::const_iterator it = mDirectory.begin(); it != mDirectory.end(); ++it)
	{
		const LumpId lump_id = mDirectory.getLumpId(it);

		uint32_t offset = mDirectory.getOffset(lump_id);
		uint32_t length = mDirectory.getLength(lump_id);
		const OString& lump_name = mDirectory.getPath(lump_id); 
		ResourcePath base_path = global_directory_name;

		// check that the lump doesn't extend past the end of the file
		if (offset + length <= mFile->size())
		{
			// [SL] Determine if this is a map marker (by checking if a map-related lump such as
			// SEGS follows this one). If so, move all of the subsequent map lumps into
			// this map's directory
			bool is_map_lump = Res_IsMapLumpName(lump_name);
			bool is_map_marker = !is_map_lump && Res_IsMapLumpName(mDirectory.getPath(lump_id + 1));

			if (is_map_marker)
				map_name = lump_name;
			else if (!is_map_lump)
				map_name.clear();

			// add the lump to the appropriate namespace
			if (!map_name.empty())
				base_path = Res_MakeResourcePath(map_name, maps_directory_name);
			else
				base_path = assignPathBasedOnMarkers(lump_id);

			if (base_path == global_directory_name)
			{
				// Examine the lump to identify its type
				uint8_t* data = new uint8_t[length];
				mFile->seek(offset);
				if (mFile->read(data, length) == length)
				{
					WadResourceIdentifier identifier;
					base_path = identifier.identifyByContents(lump_name, data, length);
				}

				delete [] data;
			}
			
			const ResourcePath full_path = base_path + lump_name;
			DPrintf("Adding WAD lump %05d %s\n", lump_id, full_path.c_str());
			const ResourceId res_id = manager->addResource(full_path, this);
			mLumpIdLookup.insert(std::make_pair(res_id, lump_id));
		}
	}
}


//
// WadResourceContainer::getResourceCount
//
// Returns the number of lumps in the WAD file or returns 0 if
// the WAD file is invalid.
//
uint32_t WadResourceContainer::getResourceCount() const
{
	return mDirectory.size();
}


//
// WadResourceContainer::getResourceSize
//
uint32_t WadResourceContainer::getResourceSize(const ResourceId res_id) const
{
	LumpId lump_id = getLumpId(res_id);
	if (mDirectory.validate(lump_id))
		return mDirectory.getLength(lump_id);
	return 0;
}


//
// WadResourceContainer::loadResource
//
uint32_t WadResourceContainer::loadResource(void* data, const ResourceId res_id, uint32_t size) const
{
	size = std::min(size, getResourceSize(res_id));
	if (size > 0)
	{
		LumpId lump_id = getLumpId(res_id);
		uint32_t offset = mDirectory.getOffset(lump_id);
		mFile->seek(offset);
		return mFile->read(data, size); 
	}
	return 0;
}


// ============================================================================
//
// SingleMapWadResourceContainer class implementation
//
// ============================================================================

//
// SingleMapWadResourceContainer::SingleMapWadResourceContainer
//
// Reads the lump directory from the WAD file and registers all of the lumps
// with the ResourceManager. If the WAD file has an invalid directory, no lumps
// will be registered and getResourceCount() will return 0.
//
SingleMapWadResourceContainer::SingleMapWadResourceContainer(
	const OString& path,
	const ResourceContainerId& container_id,
	ResourceManager* manager) :
		WadResourceContainer(path, container_id, manager)
{
	// the filename serves as the name of the map (we ignore the map name in the WAD file)
	std::string base_filename;
	M_ExtractFileBase(path, base_filename);
	mMapName = OStringToUpper(base_filename.c_str(), 8);
}


//
// SingleMapWadResourceContainer::~SingleMapWadResourceContainer
//
SingleMapWadResourceContainer::~SingleMapWadResourceContainer()
{
}


//
// SingleMapWadResourceContainer::~SingleMapWadResourceContainer
//
void SingleMapWadResourceContainer::addResourcesToManager(ResourceManager* manager)
{
}


// ============================================================================
//
// DirectoryResourceContainer class implementation
//
// ============================================================================

//
// DirectoryResourceContainer::DirectoryResourceContainer
//
//
DirectoryResourceContainer::DirectoryResourceContainer(
	const OString& path,
	const ResourceContainerId& container_id,
	ResourceManager* manager) :
		ResourceContainer(container_id, manager),
		mPath(path),
		mDirectory(256),
		mLumpIdLookup(256)
{
	// Examine each lump and decide which path it belongs in
	// and then register it with the resource manager.
	addResourcesToManager(manager);
}


//
// DirectoryResourceContainer::~DirectoryResourceContainer
//
DirectoryResourceContainer::~DirectoryResourceContainer()
{
}


//
// DirectoryResourceContainer::getResourceCount
//
// Returns the number of lumps in the WAD file or returns 0 if
// the WAD file is invalid.
//
uint32_t DirectoryResourceContainer::getResourceCount() const
{
	return mDirectory.size();
}


//
// DirectoryResourceContainer::getResourceSize
//
uint32_t DirectoryResourceContainer::getResourceSize(const ResourceId res_id) const
{
	LumpId lump_id = getLumpId(res_id);
	if (mDirectory.validate(lump_id))
		return mDirectory.getLength(lump_id);
	return 0;
}


//
// DirectoryResourceContainer::loadResource
//
uint32_t DirectoryResourceContainer::loadResource(void* data, const ResourceId res_id, uint32_t size) const
{
	LumpId lump_id = getLumpId(res_id);
	if (mDirectory.validate(lump_id))
	{
		const OString& path = mDirectory.getPath(lump_id);
		size = std::min(size, getResourceSize(res_id));
		if (size > 0)
		{
			DiskFileAccessor file_accessor(path);
			return file_accessor.read(data, size);
		}
	}
	return 0;
}


//
// DirectoryResourceContainer::addResourcesToManager
//
void DirectoryResourceContainer::addResourcesToManager(ResourceManager* manager)
{
	std::vector<std::string> files = M_ListDirectoryContents(mPath, 16);
	for (size_t i = 0 ; i < files.size(); i++)
	{
		const OString filepath = files[i];
		mDirectory.addEntryInfo(filepath, M_FileLength(filepath));
		const LumpId lump_id = mDirectory.getLumpId(filepath);

		std::string new_path(filepath);
		// Replace native filesystem path separator with the in-game path separator
		const char new_deliminator = ResourcePath::DELIMINATOR;
		std::replace(new_path.begin(), new_path.end(), PATHSEPCHAR, new_deliminator);
		// Remove the base path to produce the in-game resource path
		new_path.replace(0, mPath.length(), "");

		// Transform the filename portion of the path into a well-formed lump name.
		// eg, 8 chars, no filename extension, all caps.
		//
		// Note that the "/SCRIPTS/" directory will need the full filename preserved,
		// including extension and > 8 chars
		if (new_path.find("/SCRIPTS/") != 0)
		{
			size_t last_deliminator = new_path.find_last_of(new_deliminator);
			if (last_deliminator != std::string::npos)
			{
				size_t start_of_lump_name = last_deliminator + 1;
				// Trim the filename extension
				new_path.replace(new_path.find_first_of(".", start_of_lump_name), std::string::npos, "");
				// Is the new lump name empty?
				if (start_of_lump_name == new_path.size() - 1)
					continue;
				// Capitalize the lump name
				std::transform(new_path.begin() + start_of_lump_name, new_path.end(),
								new_path.begin() + start_of_lump_name, toupper);
				// Truncate the lump name to 8 chars
				new_path = new_path.substr(0, start_of_lump_name + 8);
			}
		}

		DPrintf("Adding lump %05d %s\n", lump_id, new_path.c_str());
		const ResourceId res_id = manager->addResource(new_path, this);
		mLumpIdLookup.insert(std::make_pair(res_id, lump_id));
	}
}


VERSION_CONTROL (res_container_cpp, "$Id$")