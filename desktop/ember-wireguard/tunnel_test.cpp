// SPDX-License-Identifier: BSD-2-Clause
#include "tunnel.h"

#include <cstdio>
#include <string>

static int fails = 0;

static void expect(bool ok, const char *msg)
{
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", msg);
		++fails;
	}
}

int main()
{
	expect(tunnel::validName("home"), "validName home");
	expect(tunnel::validName("mac-to_nick"), "validName dash underscore");
	expect(!tunnel::validName("../etc"), "reject path");
	expect(!tunnel::validName("a b"), "reject space");
	expect(tunnel::basenameStem("/usr/pkg/etc/wireguard/home.conf") == "home",
	    "basenameStem");

	const char *sample =
	    "[Interface]\n"
	    "PrivateKey = abcdefghijklmnopqrstuvwxyz0123456789ABCD=\n"
	    "Address = 10.77.0.2/32, fd00::2/128\n"
	    "DNS = 1.1.1.1\n"
	    "MTU = 1280\n"
	    "\n"
	    "[Peer]\n"
	    "PublicKey = ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcd=\n"
	    "Endpoint = 192.0.2.1:51820\n"
	    "AllowedIPs = 10.77.0.0/24, 192.0.2.0/24\n";

	tunnel::Conf c;
	expect(tunnel::parseConfText(sample, c), "parseConfText");
	expect(c.address == "10.77.0.2", "address host");
	expect(c.prefix == "32", "address prefix");
	expect(c.mtu == "1280", "mtu");
	expect(c.private_key == "abcdefghijklmnopqrstuvwxyz0123456789ABCD=",
	    "private_key");
	expect(c.public_key == "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcd=",
	    "peer public_key");
	expect(c.allowed_ips.size() == 2, "allowed_ips count");
	expect(c.wg_body.find("DNS") == std::string::npos, "DNS stripped");
	expect(c.wg_body.find("Address") == std::string::npos, "Address stripped");
	expect(c.wg_body.find("PrivateKey") != std::string::npos, "PrivateKey kept");
	expect(c.wg_body.find("AllowedIPs") != std::string::npos, "AllowedIPs kept");

	tunnel::Conf bad;
	expect(!tunnel::parseConfText("[Interface]\nListenPort = 1\n", bad),
	    "reject missing Address");

	expect(tunnel::formatBytes(0) == "0 B", "formatBytes 0");
	expect(tunnel::formatBytes(1024) == "1.00 KiB", "formatBytes 1024");
	expect(tunnel::formatBytes(1536) == "1.50 KiB", "formatBytes 1536");

	const std::string tmpl = tunnel::emptyTunnelTemplate("abc=");
	expect(tmpl.find("PrivateKey = abc=") != std::string::npos, "empty tmpl key");
	expect(tmpl.find("[Peer]") != std::string::npos, "empty tmpl peer");

	if (fails) {
		std::fprintf(stderr, "%d failed\n", fails);
		return 1;
	}
	std::puts("ok");
	return 0;
}
