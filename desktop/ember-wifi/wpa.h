// SPDX-License-Identifier: BSD-2-Clause
// ember-wifi: wpa_supplicant control-interface replies and commands.
// Plain C++17 so that wpa_test.cpp runs on any host.
#ifndef EMBER_WIFI_WPA_H
#define EMBER_WIFI_WPA_H

#include <algorithm>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace wpa {

inline std::string scanRequest(const std::string &state)
{
	// Foreground scans can interrupt association on station-mode drivers.
	return state == "DISCONNECTED" || state == "INACTIVE"
	    ? "SCAN" : "SCAN_RESULTS";
}

enum class Security { Open, Psk, SaeOnly, Enterprise, Unsupported };

struct ScanEntry {
	std::string bssid;
	int freq = 0;
	int signal = 0;
	std::string flags;
	std::string ssid;
};

struct Network {		// one SSID as shown to the user
	std::string ssid;
	int signal = 0;
	bool band24 = false;
	bool band5 = false;
	Security security = Security::Open;
};

struct Configured {		// a LIST_NETWORKS entry
	int id = -1;
	std::string ssid;
	bool current = false;
	bool disabled = false;
};

inline std::vector<std::string> split(const std::string &s, char sep)
{
	std::vector<std::string> out;
	std::string cur;
	for (char c : s) {
		if (c == sep) {
			out.push_back(cur);
			cur.clear();
		} else if (c != '\r') {
			cur += c;
		}
	}
	out.push_back(cur);
	return out;
}

// Undo wpa_supplicant's printf_encode() used for SSIDs.
inline std::string unescape(const std::string &s)
{
	std::string out;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] != '\\' || i + 1 >= s.size()) {
			out += s[i];
			continue;
		}
		char n = s[i + 1];
		auto hexval = [](char c) {
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		};
		if (n == 'x' && i + 3 < s.size() &&
		    hexval(s[i + 2]) >= 0 && hexval(s[i + 3]) >= 0) {
			out += char(hexval(s[i + 2]) * 16 + hexval(s[i + 3]));
			i += 3;
			continue;
		}
		switch (n) {
		case '\\': out += '\\'; i++; continue;
		case '"': out += '"'; i++; continue;
		case 'n': out += '\n'; i++; continue;
		case 'r': out += '\r'; i++; continue;
		case 't': out += '\t'; i++; continue;
		case 'e': out += '\033'; i++; continue;
		default: out += s[i]; continue;
		}
	}
	return out;
}

inline Security security(const std::string &flags)
{
	if (flags.find("EAP") != std::string::npos)
		return Security::Enterprise;
	if (flags.find("PSK") != std::string::npos)
		return Security::Psk;
	if (flags.find("SAE") != std::string::npos)
		return Security::SaeOnly;	// needs PMF, which iwx lacks
	if (flags.find("WEP") != std::string::npos)
		return Security::Unsupported;
	return Security::Open;
}

inline std::vector<ScanEntry> parseScan(const std::string &text)
{
	std::vector<ScanEntry> out;
	for (const auto &line : split(text, '\n')) {
		auto f = split(line, '\t');
		if (f.size() < 5 || f[0].size() != 17)
			continue;
		ScanEntry e;
		e.bssid = f[0];
		e.freq = std::atoi(f[1].c_str());
		e.signal = std::atoi(f[2].c_str());
		e.flags = f[3];
		e.ssid = unescape(f[4]);
		out.push_back(e);
	}
	return out;
}

inline std::vector<Network> groupNetworks(const std::vector<ScanEntry> &v)
{
	std::vector<Network> out;
	for (const auto &e : v) {
		if (e.ssid.empty() || e.ssid.find('\0') != std::string::npos)
			continue;
		auto it = std::find_if(out.begin(), out.end(),
		    [&](const Network &n) { return n.ssid == e.ssid; });
		if (it == out.end()) {
			out.push_back(Network{e.ssid, e.signal, false, false,
			    security(e.flags)});
			it = out.end() - 1;
		}
		it->signal = std::max(it->signal, e.signal);
		if (e.freq >= 4900)
			it->band5 = true;
		else
			it->band24 = true;
	}
	std::stable_sort(out.begin(), out.end(),
	    [](const Network &a, const Network &b) { return a.signal > b.signal; });
	return out;
}

inline std::map<std::string, std::string> parseKeyValues(const std::string &text)
{
	std::map<std::string, std::string> out;
	for (const auto &line : split(text, '\n')) {
		auto eq = line.find('=');
		if (eq != std::string::npos)
			out[line.substr(0, eq)] = line.substr(eq + 1);
	}
	return out;
}

inline std::vector<Configured> parseNetworks(const std::string &text)
{
	std::vector<Configured> out;
	for (const auto &line : split(text, '\n')) {
		auto f = split(line, '\t');
		if (f.size() < 4 || f[0].empty() ||
		    f[0].find_first_not_of("0123456789") != std::string::npos)
			continue;
		Configured c;
		c.id = std::atoi(f[0].c_str());
		c.ssid = unescape(f[1]);
		c.current = f[3].find("[CURRENT]") != std::string::npos;
		c.disabled = f[3].find("[DISABLED]") != std::string::npos;
		out.push_back(c);
	}
	return out;
}

inline int findNetwork(const std::vector<Configured> &v, const std::string &ssid)
{
	for (const auto &c : v)
		if (c.ssid == ssid)
			return c.id;
	return -1;
}

inline std::string hex(const std::string &s)
{
	static const char d[] = "0123456789abcdef";
	std::string out;
	for (unsigned char c : s) {
		out += d[c >> 4];
		out += d[c & 15];
	}
	return out;
}

// WPA passphrase: 8..63 printable ASCII, or a 64-digit hex PSK.
inline bool validPassphrase(const std::string &p)
{
	if (p.size() == 64)
		return p.find_first_not_of("0123456789abcdefABCDEF") ==
		    std::string::npos;
	if (p.size() < 8 || p.size() > 63)
		return false;
	for (unsigned char c : p)
		if (c < 32 || c > 126)
			return false;
	return true;
}

// SET_NETWORK commands for a network created with ADD_NETWORK as `id'.
inline std::vector<std::string> addNetworkCommands(int id,
    const std::string &ssid, const std::string &pass, Security sec)
{
	std::string pre = "SET_NETWORK " + std::to_string(id) + " ";
	std::vector<std::string> out{pre + "ssid " + hex(ssid)};
	if (sec == Security::Open) {
		out.push_back(pre + "key_mgmt NONE");
		return out;
	}
	out.push_back(pre + "key_mgmt WPA-PSK");
	if (pass.size() == 64)
		out.push_back(pre + "psk " + pass);
	else
		out.push_back(pre + "psk \"" + pass + "\"");
	return out;
}

// Driver levels are 0..100-ish; negative values are dBm.
inline int signalPercent(int level)
{
	int p = level < 0 ? 2 * (level + 100) : level;
	return std::clamp(p, 0, 100);
}

} // namespace wpa

#endif
