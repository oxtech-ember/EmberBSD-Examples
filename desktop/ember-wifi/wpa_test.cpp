// SPDX-License-Identifier: BSD-2-Clause
// Host test for wpa.h: parsing wpa_supplicant control replies.
#include "wpa.h"

#include <cstdio>

static unsigned checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void test_unescape()
{
	CHECK(wpa::unescape("OxTech") == "OxTech");
	CHECK(wpa::unescape("\\xd0\\x9e\\xd1\\x84") == "\xd0\x9e\xd1\x84");
	CHECK(wpa::unescape("a\\\\b\\\"c") == "a\\b\"c");
	CHECK(wpa::unescape("tab\\tx") == "tab\tx");
	CHECK(wpa::unescape("bad\\x4") == "bad\\x4");
}

static void test_scan()
{
	const char *text =
	    "bssid / frequency / signal level / flags / ssid\n"
	    "50:ff:20:fc:f3:b4\t5320\t56\t[WPA2-PSK+SAE-CCMP][SAE-H2E][ESS]\tOxTech\n"
	    "50:ff:20:fc:f3:b1\t2467\t46\t[WPA2-PSK+SAE-CCMP][SAE-H2E][ESS]\tOxTech\n"
	    "11:22:33:44:55:66\t2412\t30\t[ESS]\tCafe Free\n"
	    "11:22:33:44:55:67\t5180\t70\t[WPA2-EAP-CCMP][ESS]\tCorp\n"
	    "11:22:33:44:55:68\t5180\t40\t[SAE-CCMP][ESS]\tWpa3Only\n"
	    "11:22:33:44:55:69\t2437\t20\t[WPA2-PSK-CCMP][ESS]\t\n"
	    "garbage line\n";
	auto v = wpa::parseScan(text);
	CHECK(v.size() == 6);
	CHECK(v[0].bssid == "50:ff:20:fc:f3:b4" && v[0].freq == 5320 &&
	    v[0].signal == 56 && v[0].ssid == "OxTech");

	auto g = wpa::groupNetworks(v);
	// Hidden networks are dropped, one entry per SSID, strongest first.
	CHECK(g.size() == 4);
	CHECK(g[0].ssid == "Corp" && g[0].security == wpa::Security::Enterprise);
	CHECK(g[1].ssid == "OxTech" && g[1].signal == 56 && g[1].band5 &&
	    g[1].band24 && g[1].security == wpa::Security::Psk);
	CHECK(g[2].ssid == "Wpa3Only" && g[2].security == wpa::Security::SaeOnly);
	CHECK(g[3].ssid == "Cafe Free" && g[3].security == wpa::Security::Open &&
	    g[3].band24 && !g[3].band5);
	CHECK(wpa::security("[WPA-PSK-TKIP][ESS]") == wpa::Security::Psk);
	CHECK(wpa::security("[WEP][ESS]") == wpa::Security::Unsupported);
}

static void test_status_and_list()
{
	auto st = wpa::parseKeyValues(
	    "bssid=d6:9c:53:85:d0:c5\nfreq=5745\nssid=Netcraze-1387\nid=0\n"
	    "wpa_state=COMPLETED\nip_address=192.168.2.38\n");
	CHECK(st["wpa_state"] == "COMPLETED" && st["ssid"] == "Netcraze-1387");
	CHECK(st["ip_address"] == "192.168.2.38");

	auto nets = wpa::parseNetworks(
	    "network id / ssid / bssid / flags\n"
	    "0\tNetcraze-1387\tany\t[CURRENT]\n"
	    "1\tOxTech\tany\t[DISABLED]\n"
	    "2\t\\xd0\\x94\\xd0\\xbe\\xd0\\xbc\tany\t\n");
	CHECK(nets.size() == 3);
	CHECK(nets[0].id == 0 && nets[0].ssid == "Netcraze-1387" && nets[0].current);
	CHECK(nets[1].id == 1 && nets[1].ssid == "OxTech" && !nets[1].current);
	CHECK(nets[2].ssid == "\xd0\x94\xd0\xbe\xd0\xbc");
	CHECK(wpa::findNetwork(nets, "OxTech") == 1);
	CHECK(wpa::findNetwork(nets, "Nope") == -1);
}

static void test_commands()
{
	CHECK(wpa::hex("OxTech") == "4f7854656368");
	CHECK(wpa::hex("\xd0\x94") == "d094");
	CHECK(wpa::validPassphrase("12345678"));
	CHECK(!wpa::validPassphrase("1234567"));
	CHECK(!wpa::validPassphrase(std::string(64, 'z')));
	CHECK(wpa::validPassphrase(std::string(64, 'a')));	// raw PSK in hex
	CHECK(!wpa::validPassphrase("pass\nword"));

	auto cmds = wpa::addNetworkCommands(3, "OxTech", "secret pass",
	    wpa::Security::Psk);
	CHECK(cmds.size() == 3);
	CHECK(cmds[0] == "SET_NETWORK 3 ssid 4f7854656368");
	CHECK(cmds[1] == "SET_NETWORK 3 key_mgmt WPA-PSK");
	CHECK(cmds[2] == "SET_NETWORK 3 psk \"secret pass\"");
	cmds = wpa::addNetworkCommands(4, "x", std::string(64, 'a'),
	    wpa::Security::Psk);
	CHECK(cmds[2] == "SET_NETWORK 4 psk " + std::string(64, 'a'));
	cmds = wpa::addNetworkCommands(5, "Cafe", "", wpa::Security::Open);
	CHECK(cmds.size() == 2 && cmds[1] == "SET_NETWORK 5 key_mgmt NONE");
}

static void test_signal()
{
	CHECK(wpa::signalPercent(56) == 56);
	CHECK(wpa::signalPercent(130) == 100);
	CHECK(wpa::signalPercent(-50) == 100);	// dBm: strong
	CHECK(wpa::signalPercent(-90) == 20);
	CHECK(wpa::signalPercent(-100) == 0);
	CHECK(wpa::signalPercent(0) == 0);
}

static void test_scan_request()
{
	CHECK(wpa::scanRequest("COMPLETED") == "SCAN_RESULTS");
	CHECK(wpa::scanRequest("ASSOCIATING") == "SCAN_RESULTS");
	CHECK(wpa::scanRequest("4WAY_HANDSHAKE") == "SCAN_RESULTS");
	CHECK(wpa::scanRequest("SCANNING") == "SCAN_RESULTS");
	CHECK(wpa::scanRequest("") == "SCAN_RESULTS");
	CHECK(wpa::scanRequest("DISCONNECTED") == "SCAN");
	CHECK(wpa::scanRequest("INACTIVE") == "SCAN");
}

int main()
{
	test_scan_request();
	test_unescape();
	test_scan();
	test_status_and_list();
	test_commands();
	test_signal();
	std::printf("ember-wifi wpa_test: %u checks, %u failures\n", checks, failures);
	return failures != 0;
}
