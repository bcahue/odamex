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
//   HWID collection. See hwid.h.
//
//   Windows uses native APIs rather than WMI/COM: __cpuid (cpu_id),
//   IOCTL_STORAGE_QUERY_PROPERTY (disk_serial), GetAdaptersAddresses (mac),
//   and the Cryptography\MachineGuid registry value. These produce the same
//   per-machine fingerprints the design doc describes without OLE init.
//
//-----------------------------------------------------------------------------

#include "hwid.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
// Build the final payload from however many components were readable.
std::string BuildPayload(const std::string& cpu, const std::string& disk,
                         const std::string& mac, const std::string& guid);
} // namespace

// ===========================================================================
#ifdef _WIN32
// ===========================================================================

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>  // before windows.h / iphlpapi.h
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <ipifcons.h>  // IF_TYPE_* constants

#include <windows.h>

#include <bcrypt.h>
#include <winioctl.h>

#if defined(_M_X64) || defined(_M_IX86)
#include <intrin.h>
#define HWID_HAS_CPUID 1
#endif

namespace
{
std::string WideToUtf8(const wchar_t* w)
{
	if (w == nullptr)
		return std::string();
	int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
	if (n <= 1)
		return std::string();
	std::string s(static_cast<size_t>(n - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
	return s;
}

std::string Trim(const std::string& s)
{
	const char* ws = " \t\r\n";
	size_t a = s.find_first_not_of(ws);
	if (a == std::string::npos)
		return std::string();
	size_t b = s.find_last_not_of(ws);
	return s.substr(a, b - a + 1);
}

// SHA-256 -> lowercase hex, via the multi-step BCrypt API (Vista+).
std::string Sha256Hex(const std::string& in)
{
	unsigned char digest[32];
	BCRYPT_ALG_HANDLE hAlg = nullptr;
	if (!BCRYPT_SUCCESS(
	        BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
		return std::string();

	bool ok = false;
	BCRYPT_HASH_HANDLE hHash = nullptr;
	if (BCRYPT_SUCCESS(BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0)))
	{
		if (BCRYPT_SUCCESS(BCryptHashData(
		        hHash, reinterpret_cast<PUCHAR>(const_cast<char*>(in.data())),
		        static_cast<ULONG>(in.size()), 0)) &&
		    BCRYPT_SUCCESS(BCryptFinishHash(hHash, digest, sizeof(digest), 0)))
		{
			ok = true;
		}
		BCryptDestroyHash(hHash);
	}
	BCryptCloseAlgorithmProvider(hAlg, 0);
	if (!ok)
		return std::string();

	static const char* const hexd = "0123456789abcdef";
	std::string out;
	out.reserve(64);
	for (unsigned char b : digest)
	{
		out += hexd[b >> 4];
		out += hexd[b & 0x0F];
	}
	return out;
}

// cpu_id: CPUID leaf 1 (EAX signature + EDX feature flags) -- the same value
// Win32_Processor.ProcessorId exposes.
std::string ReadCpuId()
{
#ifdef HWID_HAS_CPUID
	int regs[4] = {0, 0, 0, 0};
	__cpuid(regs, 1);
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%08X%08X", static_cast<unsigned>(regs[0]),
	              static_cast<unsigned>(regs[3]));
	return std::string(buf);
#else
	return std::string();
#endif
}

// machine_guid: HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid. Force the
// 64-bit view so a 32-bit launcher reads the same value as a 64-bit one.
std::string ReadMachineGuid()
{
	HKEY hKey;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
	                  L"SOFTWARE\\Microsoft\\Cryptography", 0,
	                  KEY_READ | KEY_WOW64_64KEY, &hKey) != ERROR_SUCCESS)
		return std::string();

	wchar_t value[256];
	DWORD size = sizeof(value);
	DWORD type = 0;
	LONG r = RegQueryValueExW(hKey, L"MachineGuid", nullptr, &type,
	                          reinterpret_cast<LPBYTE>(value), &size);
	RegCloseKey(hKey);

	if (r != ERROR_SUCCESS || type != REG_SZ)
		return std::string();
	return WideToUtf8(value);
}

// disk_serial: the serial of PhysicalDrive0 via IOCTL_STORAGE_QUERY_PROPERTY.
// Opening with 0 access means no admin rights are needed for the query. (v1
// uses PhysicalDrive0 as the system disk approximation.)
std::string ReadDiskSerial()
{
	HANDLE h = CreateFileW(L"\\\\.\\PhysicalDrive0", 0,
	                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
	                       OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return std::string();

	std::string serial;
	STORAGE_PROPERTY_QUERY query;
	ZeroMemory(&query, sizeof(query));
	query.PropertyId = StorageDeviceProperty;
	query.QueryType = PropertyStandardQuery;

	STORAGE_DESCRIPTOR_HEADER header;
	ZeroMemory(&header, sizeof(header));
	DWORD bytes = 0;

	if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
	                    &header, sizeof(header), &bytes, nullptr) &&
	    header.Size >= sizeof(STORAGE_DEVICE_DESCRIPTOR))
	{
		std::vector<char> buf(header.Size);
		if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &query,
		                    sizeof(query), buf.data(), header.Size, &bytes,
		                    nullptr))
		{
			const STORAGE_DEVICE_DESCRIPTOR* desc =
			    reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buf.data());
			if (desc->SerialNumberOffset != 0 &&
			    desc->SerialNumberOffset < buf.size())
			{
				serial = Trim(std::string(buf.data() + desc->SerialNumberOffset));
			}
		}
	}

	CloseHandle(h);
	return serial;
}

// mac_hash source: the lexicographically smallest physical NIC MAC (stable
// regardless of adapter enumeration order). Skips loopback and non-6-byte
// addresses; restricts to Ethernet / Wi-Fi to avoid most virtual adapters.
std::string ReadPrimaryMac()
{
	ULONG flags = GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST |
	              GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
	ULONG size = 15 * 1024;
	std::vector<unsigned char> buffer(size);

	ULONG ret = GetAdaptersAddresses(
	    AF_UNSPEC, flags, nullptr,
	    reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
	if (ret == ERROR_BUFFER_OVERFLOW)
	{
		buffer.resize(size);
		ret = GetAdaptersAddresses(
		    AF_UNSPEC, flags, nullptr,
		    reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
	}
	if (ret != NO_ERROR)
		return std::string();

	std::string best;
	for (IP_ADAPTER_ADDRESSES* a =
	         reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
	     a != nullptr; a = a->Next)
	{
		if (a->PhysicalAddressLength != 6)
			continue;
		if (a->IfType != IF_TYPE_ETHERNET_CSMACD && a->IfType != IF_TYPE_IEEE80211)
			continue;

		char mac[16];
		std::snprintf(mac, sizeof(mac), "%02X%02X%02X%02X%02X%02X",
		              a->PhysicalAddress[0], a->PhysicalAddress[1],
		              a->PhysicalAddress[2], a->PhysicalAddress[3],
		              a->PhysicalAddress[4], a->PhysicalAddress[5]);
		std::string s(mac);
		if (best.empty() || s < best)
			best = s;
	}
	return best;
}
} // namespace

std::string Hwid::CollectPayloadJson()
{
	std::string cpu = ReadCpuId();
	std::string disk = ReadDiskSerial();
	std::string mac = ReadPrimaryMac();
	std::string guid = ReadMachineGuid();

	return BuildPayload(cpu.empty() ? std::string() : Sha256Hex(cpu),
	                    disk.empty() ? std::string() : Sha256Hex(disk),
	                    mac.empty() ? std::string() : Sha256Hex(mac),
	                    guid.empty() ? std::string() : Sha256Hex(guid));
}

// ===========================================================================
#else // non-Windows: stub until macOS (sysctl/ioreg) and Linux (/sys) land
// ===========================================================================

std::string Hwid::CollectPayloadJson()
{
	return BuildPayload(std::string(), std::string(), std::string(),
	                    std::string());
}

#endif

// ===========================================================================
// Shared payload assembly
// ===========================================================================

namespace
{
std::string BuildPayload(const std::string& cpu, const std::string& disk,
                         const std::string& mac, const std::string& guid)
{
	std::string out = "{\"hwid_v\":1,\"components\":{";
	bool first = true;
	auto add = [&](const char* name, const std::string& hashHex) {
		if (hashHex.empty())
			return;
		if (!first)
			out += ",";
		out += "\"";
		out += name;
		out += "\":\"";
		out += hashHex;
		out += "\"";
		first = false;
	};
	add("cpu_id", cpu);
	add("disk_serial", disk);
	add("mac_hash", mac);
	add("machine_guid", guid);
	out += "}}";
	return out;
}
} // namespace
