// SPDX-License-Identifier: BSD-2-Clause
// ember-wireguard: WireGuard config helpers shared by the tray and tests.
#ifndef EMBER_WIREGUARD_TUNNEL_H
#define EMBER_WIREGUARD_TUNNEL_H

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace tunnel {

inline bool validName(const std::string &name)
{
	if (name.empty() || name.size() > 64)
		return false;
	for (unsigned char c : name) {
		if (!(std::isalnum(c) || c == '-' || c == '_'))
			return false;
	}
	return true;
}

inline std::string trim(const std::string &s)
{
	size_t a = 0;
	while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
		++a;
	size_t b = s.size();
	while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
		--b;
	return s.substr(a, b - a);
}

inline std::string basenameStem(const std::string &path)
{
	size_t slash = path.find_last_of('/');
	std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
	if (base.size() > 5 && base.compare(base.size() - 5, 5, ".conf") == 0)
		base.resize(base.size() - 5);
	return base;
}

struct Conf {
	std::string address;		 // first Address= value (host part before /)
	std::string prefix = "32";	 // CIDR length from that Address=
	std::string mtu = "1420";
	std::string private_key;
	std::string public_key;		 // peer PublicKey, first peer only
	std::string listen_port;
	std::vector<std::string> allowed_ips;
	std::string wg_body;		 // conf for `wg setconf` (wg-quick keys removed)
};

inline bool isWgQuickKey(const std::string &key)
{
	return key == "Address" || key == "DNS" || key == "MTU" || key == "Table" ||
	    key == "PreUp" || key == "PostUp" || key == "PreDown" ||
	    key == "PostDown" || key == "SaveConfig";
}

inline bool parseAddress(const std::string &value, std::string &host, std::string &prefix)
{
	std::string first = value;
	size_t comma = first.find(',');
	if (comma != std::string::npos)
		first = first.substr(0, comma);
	first = trim(first);
	if (first.empty())
		return false;
	size_t slash = first.find('/');
	if (slash == std::string::npos) {
		host = first;
		prefix = "32";
		return true;
	}
	host = first.substr(0, slash);
	prefix = first.substr(slash + 1);
	return !host.empty() && !prefix.empty();
}

inline bool parseConfText(const std::string &text, Conf &out)
{
	out = Conf{};
	std::istringstream in(text);
	std::string line;
	std::ostringstream body;
	std::string section;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		std::string raw = line;
		std::string t = trim(line);
		if (t.empty() || t[0] == '#' || t[0] == ';') {
			body << raw << '\n';
			continue;
		}
		if (t.front() == '[' && t.back() == ']') {
			section = t.substr(1, t.size() - 2);
			body << raw << '\n';
			continue;
		}
		size_t eq = t.find('=');
		if (eq == std::string::npos) {
			body << raw << '\n';
			continue;
		}
		std::string key = trim(t.substr(0, eq));
		std::string val = trim(t.substr(eq + 1));
		if (section == "Interface" && key == "Address") {
			std::string host, pref;
			if (parseAddress(val, host, pref)) {
				out.address = host;
				out.prefix = pref;
			}
			continue;
		}
		if (section == "Interface" && key == "MTU") {
			out.mtu = val;
			continue;
		}
		if (section == "Interface" && key == "PrivateKey") {
			out.private_key = val;
			body << raw << '\n';
			continue;
		}
		if (section == "Interface" && key == "ListenPort") {
			out.listen_port = val;
			body << raw << '\n';
			continue;
		}
		if (section == "Peer" && key == "PublicKey") {
			if (out.public_key.empty())
				out.public_key = val;
			body << raw << '\n';
			continue;
		}
		if (section == "Peer" && key == "AllowedIPs") {
			std::istringstream ips(val);
			std::string ip;
			while (std::getline(ips, ip, ',')) {
				ip = trim(ip);
				if (!ip.empty())
					out.allowed_ips.push_back(ip);
			}
			body << raw << '\n';
			continue;
		}
		if (isWgQuickKey(key))
			continue;
		body << raw << '\n';
	}
	out.wg_body = body.str();
	return !out.address.empty();
}

inline bool parseConfFile(const std::string &path, Conf &out)
{
	std::ifstream f(path);
	if (!f)
		return false;
	std::ostringstream ss;
	ss << f.rdbuf();
	return parseConfText(ss.str(), out);
}

inline std::vector<std::string> splitCidr(const std::string &cidr)
{
	size_t slash = cidr.find('/');
	if (slash == std::string::npos)
		return {cidr, "32"};
	return {cidr.substr(0, slash), cidr.substr(slash + 1)};
}

// Format a byte count like the official WireGuard client transfer line.
inline std::string formatBytes(unsigned long long n)
{
	const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
	double v = static_cast<double>(n);
	int u = 0;
	while (v >= 1024.0 && u < 4) {
		v /= 1024.0;
		++u;
	}
	char buf[64];
	if (u == 0)
		std::snprintf(buf, sizeof(buf), "%llu B", n);
	else if (v >= 100.0)
		std::snprintf(buf, sizeof(buf), "%.0f %s", v, units[u]);
	else if (v >= 10.0)
		std::snprintf(buf, sizeof(buf), "%.1f %s", v, units[u]);
	else
		std::snprintf(buf, sizeof(buf), "%.2f %s", v, units[u]);
	return buf;
}

inline std::string emptyTunnelTemplate(const std::string &privateKey,
    const std::string &address = "10.0.0.2/32")
{
	std::ostringstream o;
	o << "[Interface]\n"
	  << "PrivateKey = " << privateKey << "\n"
	  << "Address = " << address << "\n"
	  << "\n"
	  << "[Peer]\n"
	  << "PublicKey = \n"
	  << "Endpoint = \n"
	  << "AllowedIPs = 0.0.0.0/0\n";
	return o.str();
}

} // namespace tunnel

#endif
