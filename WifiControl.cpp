/*
   Basecamp - ESP32 library to simplify the basics of IoT projects
   Written by Merlin Schumacher (mls@ct.de) for c't magazin für computer technik (https://www.ct.de)
   Licensed under GPLv3. See LICENSE for details.
   */

#include "WifiControl.hpp"
#ifdef BASECAMP_WIRED_NETWORK
#include <ETH.h>
#endif

namespace {
	// Minumum access point secret length to be generated (8 is min for ESP32)
	const constexpr unsigned minApSecretLength = 8;
#ifdef BASECAMP_WIRED_NETWORK
	static bool eth_connected = false;
#endif
}

void WifiControl::begin(String essid, String password, String configured,
												String hostname, String apSecret)
{
#ifdef BASECAMP_WIRED_NETWORK
	ESP_LOGI("Basecamp", "Connecting to Ethernet");
	operationMode_ = Mode::client;
	WiFi.onEvent(WiFiEvent);
	ETH.begin() ;
	ETH.setHostname(hostname.c_str());
	ESP_LOGD("Basecamp", "Ethernet initialized") ;
	ESP_LOGI("Basecamp", "Waiting for connection") ;
	while (!eth_connected) {
		Serial.print(".") ;
		delay(100) ;
	}
#else
	ESP_LOGI("Basecamp", "Connecting to Wifi");
	String _wifiConfigured = std::move(configured);
	_wifiEssid = std::move(essid);
	_wifiPassword = std::move(password);
	if (_wifiAPName.length() == 0) {
		_wifiAPName = "ESP32_" + getHardwareMacAddress();
	}

	WiFi.onEvent(WiFiEvent);
	if (_wifiConfigured.equalsIgnoreCase("true")) {
		operationMode_ = Mode::client;
		ESP_LOGI("Basecamp", "Wifi is configured, connecting to '%s'", _wifiEssid.c_str());

		WiFi.begin(_wifiEssid.c_str(), _wifiPassword.c_str());
		WiFi.setHostname(hostname.c_str());
		//WiFi.setAutoConnect ( true );
		//WiFi.setAutoReconnect ( true );
	} else {
		operationMode_ = Mode::accessPoint;
		ESP_LOGW("Basecamp", "Wifi is NOT configured, starting Wifi AP '%s'", _wifiAPName.c_str());

		WiFi.mode(WIFI_AP_STA);
		if (apSecret.length() > 0) {
			// Start with password protection
			ESP_LOGD("Basecamp", "Starting AP with password %s\n", apSecret.c_str());
			WiFi.softAP(_wifiAPName.c_str(), apSecret.c_str());
		} else {
			// Start without password protection
			WiFi.softAP(_wifiAPName.c_str());
		}
	}
#endif

}


bool WifiControl::isConnected()
{
#ifdef BASECAMP_WIRED_NETWORK
	return eth_connected ;
#else
	return WiFi.isConnected() ;
#endif
}

WifiControl::Mode WifiControl::getOperationMode() const
{
	return operationMode_;
}

int WifiControl::status() {
	return WiFi.status();

}
IPAddress WifiControl::getIP() {
#ifdef BASECAMP_WIRED_NETWORK
	return ETH.localIP() ;
#else
	return WiFi.localIP();
#endif
}
IPAddress WifiControl::getSoftAPIP() {
	return WiFi.softAPIP();
}

void WifiControl::setAPName(const String &name) {
	_wifiAPName = name;
}

String WifiControl::getAPName() {
	return _wifiAPName;
}

void WifiControl::WiFiEvent(WiFiEvent_t event)
{
	Preferences preferences;
	preferences.begin("basecamp", false);
	unsigned int __attribute__((unused)) bootCounter = preferences.getUInt("bootcounter", 0);
	// In case somebody wants to know this..
	ESP_LOGD("Basecamp", "WiFiEvent %d, Bootcounter is %d", event, bootCounter);
#ifdef BASECAMP_WIRED_NETWORK
	switch (event) {
    case SYSTEM_EVENT_ETH_START:
      ESP_LOGI("Basecamp", "ETH Started");
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      ESP_LOGI("Basecamp", "ETH Connected");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
	  ESP_LOGI("Basecamp", "ETH Got IPv4 %s (%d Mbps, full duplex: %d, MAC %s)", ETH.localIP(), ETH.linkSpeed(), ETH.fullDuplex(), ETH.macAddress());
      eth_connected = true;
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      ESP_LOGI("Basecamp", "ETH Disconnected");
      eth_connected = false;
      break;
    case SYSTEM_EVENT_ETH_STOP:
      ESP_LOGI("Basecamp", "ETH Stopped");
      eth_connected = false;
      break;
    default:
      break;
  }
#else
	IPAddress ip __attribute__((unused));
	switch(event) {
		case SYSTEM_EVENT_STA_GOT_IP:
			ip = WiFi.localIP();
			ESP_LOGI("Basecamp", "WIFI Got IPv4 address %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
			preferences.putUInt("bootcounter", 0);
			break;
		case SYSTEM_EVENT_STA_DISCONNECTED:
			ESP_LOGI("Basecamp", "WIFI Lost connection");
			WiFi.reconnect();
			break;
		default:
			// INFO: Default = do nothing
			break;
	}
#endif
}

namespace {
	template <typename BYTES>
	String format6Bytes(const BYTES &bytes, const String& delimiter)
	{
		std::ostringstream stream;
		for (unsigned int i = 0; i < 6; i++) {
			if (i != 0 && delimiter.length() > 0) {
				stream << delimiter.c_str();
			}
			stream << std::setfill('0') << std::setw(2) << std::hex << static_cast<unsigned int>(bytes[i]);
		}

		String mac{stream.str().c_str()};
		return mac;
	}
}

// TODO: This will return the default mac, not a manually set one
// See https://github.com/espressif/esp-idf/blob/master/components/esp32/include/esp_system.h
String WifiControl::getHardwareMacAddress(const String& delimiter)
{
#ifdef BASECAMP_WIRED_NETWORK
	return ETH.macAddress() ;
#else
	uint8_t rawMac[6];
	esp_efuse_mac_get_default(rawMac);
	return format6Bytes(rawMac, delimiter);
#endif
}

String WifiControl::getSoftwareMacAddress(const String& delimiter)
{
#ifdef BASECAMP_WIRED_NETWORK
	return ETH.macAddress() ;
#else
	uint8_t rawMac[6];
	WiFi.macAddress(rawMac);
	return format6Bytes(rawMac, delimiter);
#endif
	
}

unsigned WifiControl::getMinimumSecretLength() const
{
	return minApSecretLength;
}

String WifiControl::generateRandomSecret(unsigned length) const
{
	// There is no "O" (Oh) to reduce confusion
	const String validChars{"abcdefghjkmnopqrstuvwxyzABCDEFGHJKMNPQRSTUVWXYZ23456789.-,:$/"};
	String returnValue;

	unsigned useLength = (length < minApSecretLength)?minApSecretLength:length;
	returnValue.reserve(useLength);

	for (unsigned i = 0; i < useLength; i++)
	{
		auto randomValue = validChars[(esp_random() % validChars.length())];
		returnValue += randomValue;
	}

	return returnValue;
}
