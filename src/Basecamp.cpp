/*
   Basecamp - ESP32 library to simplify the basics of IoT projects
   Written by Merlin Schumacher (mls@ct.de) for c't magazin für computer technik (https://www.ct.de)
   Licensed under GPLv3. See LICENSE for details.
   */

#include <iomanip>
#include "Basecamp.hpp"
#include "lwip/apps/sntp.h"

namespace {
	const constexpr char* kLoggingTag = "Basecamp";
	const constexpr uint16_t defaultThreadStackSize = 3072;
	const constexpr UBaseType_t defaultThreadPriority = 0;
	// Default length for access point mode password
	const constexpr unsigned defaultApSecretLength = 8;
}

Basecamp::Basecamp(SetupModeWifiEncryption setupModeWifiEncryption, ConfigurationUI configurationUi) : 
	configuration(String{"/basecamp.json"}), 
	setupModeWifiEncryption_(setupModeWifiEncryption), 
	configurationUi_(configurationUi)
{
}

/**
 * This function generates a cleaned string from the device name set by the user.
 */
String Basecamp::_cleanHostname()
{
	String clean_hostname =	configuration.get(ConfigurationKey::deviceName); // Get device name from configuration

	// If hostname is not set, return default
	if (clean_hostname == "") {
		return "basecamp-device";
	}

	// Transform device name to lower case
	clean_hostname.toLowerCase();

	// Replace all non-alphanumeric characters in hostname to minus symbols
	for (int i = 0; i <= clean_hostname.length(); i++) {
		if (!isalnum(clean_hostname.charAt(i))) {
			clean_hostname.setCharAt(i,'-');
		};
	};
	ESP_LOGD(kLoggingTag, "clean_hostname: %s", clean_hostname.c_str());

	// return cleaned hostname String
	return clean_hostname;
};

/**
 * Returns true if a secret for the setup WiFi AP is set
 */
bool Basecamp::isSetupModeWifiEncrypted(){
	return (setupModeWifiEncryption_ == SetupModeWifiEncryption::secured);
}

/**
 * Returns the SSID of the setup WiFi network
 */
String Basecamp::getSetupModeWifiName(){
	return network.getAPName();
}

/**
 * Returns the secret of the setup WiFi network
 */
String Basecamp::getSetupModeWifiSecret(){
	return configuration.get(ConfigurationKey::accessPointSecret);
}

/**
 * This is the initialisation function for the Basecamp class.
 */
bool Basecamp::begin(String fixedWiFiApEncryptionPassword)
{
	// Make sure we only accept valid passwords for ap
	if (fixedWiFiApEncryptionPassword.length() != 0) {
		if (fixedWiFiApEncryptionPassword.length() >= network.getMinimumSecretLength()) {
			setupModeWifiEncryption_ = SetupModeWifiEncryption::secured;
		} else {
			ESP_LOGE(kLoggingTag, "Error: Given fixed ap secret is too short. Refusing.");
		}
	}

	// Display a simple lifesign
	ESP_LOGW(kLoggingTag, "********************");
	ESP_LOGW(kLoggingTag, "Basecamp Startup");
	ESP_LOGW(kLoggingTag, "********************");

	// Load configuration from internal flash storage.
	// If configuration.load() fails, reset the configuration
	if (!configuration.load()) {
		ESP_LOGW(kLoggingTag, "Configuration is broken. Resetting.");
		configuration.reset();
	};

	// Get a cleaned version of the device name.
	// It is used as a hostname for DHCP and ArduinoOTA.
	hostname = _cleanHostname();
	ESP_LOGD(kLoggingTag, "hostname: %s", hostname.c_str());

	// Have checkResetReason() control if the device configuration
	// should be reset or not.
	checkResetReason();

#ifndef BASECAMP_NO_NETWORK
#ifndef BASECAMP_NETWORK_ETHERNET
	// If there is no access point secret set yet, generate one and save it.
	// It will survive the default config reset.
	if (!configuration.isKeySet(ConfigurationKey::accessPointSecret) ||
		fixedWiFiApEncryptionPassword.length() >= network.getMinimumSecretLength())
	{
		String apSecret = fixedWiFiApEncryptionPassword;
		if (apSecret.length() < network.getMinimumSecretLength()) {
			// Not set or too short. Generate a random one.
			ESP_LOGW(kLoggingTag, "Generating access point secret.");
			apSecret = network.generateRandomSecret(defaultApSecretLength);
		} else {
			ESP_LOGW(kLoggingTag, "Using fixed access point secret.");
		}
		configuration.set(ConfigurationKey::accessPointSecret, apSecret);
		configuration.save();
	}

	ESP_LOGD(kLoggingTag, "accessPointSecret: %s", configuration.get(ConfigurationKey::accessPointSecret).c_str());
#endif

	// Initialize Wifi with the stored configuration data.
	network.begin(
			configuration.get(ConfigurationKey::wifiEssid), // The (E)SSID or WiFi-Name
			configuration.get(ConfigurationKey::wifiPassword), // The WiFi password
			configuration.get(ConfigurationKey::wifiConfigured), // Has the WiFi been configured
			hostname, // The system hostname to use for DHCP
			(setupModeWifiEncryption_ == SetupModeWifiEncryption::none)?"":configuration.get(ConfigurationKey::accessPointSecret)
	);

	// Get WiFi MAC
	mac = network.getSoftwareMacAddress(":");
#endif
#ifndef BASECAMP_NOMQTT
	// Check if MQTT has been disabled by the user
	if (!configuration.get(ConfigurationKey::mqttActive).equalsIgnoreCase("false")) {
		const auto &mqttUri = configuration.get(ConfigurationKey::mqttHost);
		const auto &mqttHaDiscoveryPrefix = configuration.get(ConfigurationKey::haDiscoveryPrefix);
		mqtt.Begin(mqttUri, hostname, mqttHaDiscoveryPrefix);
	};
#endif

#ifndef BASECAMP_NOOTA
	// Set up Over-the-Air-Updates (OTA) if it hasn't been disabled.
	if (!configuration.get(ConfigurationKey::otaActive).equalsIgnoreCase("false")) {

		// Set OTA password
		String otaPass = configuration.get(ConfigurationKey::otaPass);
		if (otaPass.length() != 0) {
			ArduinoOTA.setPassword(otaPass.c_str());
		}

		// Set OTA hostname
		ArduinoOTA.setHostname(hostname.c_str());

		// The following code is copied verbatim from the ESP32 BasicOTA.ino example
		// This is the callback for the beginning of the OTA process
		ArduinoOTA
			.onStart([]() {
					String type;
					if (ArduinoOTA.getCommand() == U_FLASH)
					type = "sketch";
					else // U_SPIFFS
					type = "filesystem";
					SPIFFS.end();

					ESP_LOGW(kLoggingTag, "Start updating %s", type.c_str());
					})
		// When the update ends print it to serial
		.onEnd([]() {
				ESP_LOGW(kLoggingTag, "\nEnd");
				})
		// Show the progress of the update
		.onProgress([](unsigned int progress, unsigned int total) {
				ESP_LOGI(kLoggingTag, "Progress: %u%%\r", (progress / (total / 100)));
				})
		// Error handling for the update
		.onError([](ota_error_t error) {
				ESP_LOGE(kLoggingTag, "Error[%u]: ", error);
				if (error == OTA_AUTH_ERROR) ESP_LOGW(kLoggingTag, "Auth Failed");
				else if (error == OTA_BEGIN_ERROR) ESP_LOGW(kLoggingTag, "Begin Failed");
				else if (error == OTA_CONNECT_ERROR) ESP_LOGW(kLoggingTag, "Connect Failed");
				else if (error == OTA_RECEIVE_ERROR) ESP_LOGW(kLoggingTag, "Receive Failed");
				else if (error == OTA_END_ERROR) ESP_LOGW(kLoggingTag, "End Failed");
				});

		// Start the OTA service
		ArduinoOTA.begin();

	}
#endif

#ifndef BASECAMP_NOWEB
	if (shouldEnableConfigWebserver())
	{
		// Add a webinterface element for the h1 that contains the device name. It is a child of the #wrapper-element.
		web.addInterfaceElement("heading", "h1", "","#wrapper");
		web.setInterfaceElementAttribute("heading", "class", "fat-border");
		web.addInterfaceElement("logo", "img", "", "#heading");
		web.setInterfaceElementAttribute("logo", "src", "/logo.svg");
		String DeviceName = configuration.get(ConfigurationKey::deviceName);
		if (DeviceName == "") {
			DeviceName = "Unconfigured Basecamp Device";
		}
		web.addInterfaceElement("title", "title", DeviceName,"head");
		web.addInterfaceElement("devicename", "span", DeviceName,"#heading");
		// Set the class attribute of the element to fat-border.
		web.setInterfaceElementAttribute("heading", "class", "fat-border");
		// Add a paragraph with some basic information
		web.addInterfaceElement("infotext1", "p", "Configure your device with the following options (!!!space to clear!!!):","#wrapper");

		// Add the configuration form, that will include all inputs for config data
		web.addInterfaceElement("configform", "form", "","#wrapper");
		web.setInterfaceElementAttribute("configform", "action", "#");
		web.setInterfaceElementAttribute("configform", "onsubmit", "collectConfiguration()");

		web.addInterfaceElement("DeviceName", "input", "Device name","#configform" , "DeviceName");

#ifndef BASECAMP_NETWORK_ETHERNET
		// Add an input field for the WIFI data and link it to the corresponding configuration data
		web.addInterfaceElement("WifiEssid", "input", "WIFI SSID:","#configform" , "WifiEssid");
		web.addInterfaceElement("WifiPassword", "input", "WIFI Password:", "#configform", "WifiPassword");
		web.setInterfaceElementAttribute("WifiPassword", "type", "password");
#endif
		// Need to keep these even without WIFI as otherwise basecamp.js will crash
		web.addInterfaceElement("WifiConfigured", "input", "", "#configform", "WifiConfigured");
		web.setInterfaceElementAttribute("WifiConfigured", "type", "hidden");
		web.setInterfaceElementAttribute("WifiConfigured", "value", "true");

		// Add input fields for MQTT configurations if it hasn't been disabled
		if (!configuration.get(ConfigurationKey::mqttActive).equalsIgnoreCase("false")) {
			web.addInterfaceElement("MQTTHost", "input", "MQTT URI:","#configform" , "MQTTHost");
			web.addInterfaceElement("MQTTTopicPrefix", "input", "MQTT Topic Prefix (suggested 'esp-basecamp'):","#configform" , "MQTTTopicPrefix");
			web.addInterfaceElement("HaDiscoveryPrefix", "input", "Home Assistant MQTT Discovery Topic Prefix (suggested 'homeassistant', space/empty to disable):","#configform" , "HaDiscoveryPrefix");
		}

		web.addInterfaceElement("SyslogServer", "input", "Syslog Server (space/empty to disable):","#configform" , "SyslogServer");

		// Add a save button that calls the JavaScript function collectConfiguration() on click
		web.addInterfaceElement("saveform", "button", "Save","#configform");
		web.setInterfaceElementAttribute("saveform", "type", "submit");

		// Show the devices MAC in the Webinterface
		String infotext2 = "This device has the MAC-Address: " + mac;
		web.addInterfaceElement("infotext2", "p", infotext2,"#wrapper");

		web.addInterfaceElement("footer", "footer", "Powered by ", "body");
		web.addInterfaceElement("footerlink", "a", "Basecamp", "footer");
		web.setInterfaceElementAttribute("footerlink", "href", "https://github.com/merlinschumacher/Basecamp");
		web.setInterfaceElementAttribute("footerlink", "target", "_blank");
		#ifdef BASECAMP_USEDNS
		#ifdef DNSServer_h
		if (!configuration.get(ConfigurationKey::wifiConfigured).equalsIgnoreCase("true")) {
			dnsServer.start(53, "*", network.getSoftAPIP());
			xTaskCreatePinnedToCore(&DnsHandling, "DNSTask", 4096, (void*) &dnsServer, 5, NULL,0);
		}
		#endif
		#endif
		// Start webserver and pass the configuration object to it
		// Also pass a Lambda-function that restarts the device after the configuration has been saved.
		web.begin(configuration, [](){
			delay(2000);
			ESP.restart();
		});
	}
#endif

#ifndef BASECAMP_NO_SNTP
	sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    setenv("TZ", "CET-1CEST,M3.5.0/2:00,M10.5.0/3:00", 1);
    tzset();
#endif

	ESP_LOGW(kLoggingTag, "%s", showSystemInfo().c_str());

	// TODO: only return true if everything setup up correctly
	return true;
}

/**
 * This is the background task function for the Basecamp class. To be called from Arduino loop.
 */
void Basecamp::handle (void)
{
	#ifndef BASECAMP_NOOTA
		// This call takes care of the ArduinoOTA function provided by Basecamp
		ArduinoOTA.handle();
	#endif
}

bool Basecamp::shouldEnableConfigWebserver() const
{
	return (configurationUi_ == ConfigurationUI::always ||
	   (configurationUi_ == ConfigurationUI::accessPoint && network.getOperationMode() == NetworkControl::Mode::accessPoint));
}


#ifdef BASECAMP_USEDNS
#ifdef DNSServer_h
// This is a task that handles DNS requests from clients
void Basecamp::DnsHandling(void * dnsServerPointer)
{
		DNSServer * dnsServer = (DNSServer *) dnsServerPointer;
		while(1) {
			// handle each request
			dnsServer->processNextRequest();
			vTaskDelay(1000);
		}
};
#endif
#endif

// This function checks the reset reason returned by the ESP and resets the configuration if neccessary.
// It counts all system reboots that occured by power cycles or button resets.
// If the ESP32 receives an IP the boot counts as successful and the counter will be reset by Basecamps
// WiFi management.
void Basecamp::checkResetReason()
{
	// Instead of the internal flash it uses the somewhat limited, but sufficient preferences storage
	preferences.begin("basecamp", false);
	// Get the reset reason for the current boot
	int reason = rtc_get_reset_reason(0);
	ESP_LOGI(kLoggingTag, "Reset reason: %d", reason);
	// If the reason is caused by a power cycle (1) or a RTC reset / button press(16) evaluate the current
	// bootcount and act accordingly.
	if (reason == 1 || reason == 16) {
		// Get the current number of unsuccessful boots stored
		unsigned int bootCounter = preferences.getUInt("bootcounter", 0);
		// increment it
		bootCounter++;
		ESP_LOGI(kLoggingTag, "Unsuccessful boots: %d", bootCounter);

		// If the counter is bigger than 3 it will be the fifths consecutive unsucessful reboot.
		// This forces a reset of the WiFi configuration and the AP will be opened again
		if (bootCounter > 3){
			ESP_LOGW(kLoggingTag, "Configuration forcibly reset.");
			// Mark the WiFi configuration as invalid
			configuration.set(ConfigurationKey::wifiConfigured, "False");
			// Save the configuration immediately
			configuration.save();
			// Reset the boot counter
			preferences.putUInt("bootcounter", 0);
			// Call the destructor for preferences so that all data is safely stored befor rebooting
			preferences.end();
			ESP_LOGW(kLoggingTag, "Resetting the WiFi configuration.");
			// Reboot
			ESP.restart();

			// If the WiFi is unconfigured and the device is rebooted twice format the internal flash storage
		} else if (bootCounter > 2 && configuration.get(ConfigurationKey::wifiConfigured).equalsIgnoreCase("false")) {
			ESP_LOGW(kLoggingTag, "Factory reset was forced.");
			// Format the flash storage
			SPIFFS.format();
			// Reset the boot counter
			preferences.putUInt("bootcounter", 0);
			// Call the destructor for preferences so that all data is safely stored befor rebooting
			preferences.end();
			ESP_LOGW(kLoggingTag, "Rebooting.");
			// Reboot
			ESP.restart();

		// In every other case: store the current boot count
		} else {
			preferences.putUInt("bootcounter", bootCounter);
		};

	// if the reset has been for any other cause, reset the counter
	} else {
		preferences.putUInt("bootcounter", 0);
	};
	// Call the destructor for preferences so that all data is safely stored
	preferences.end();
};

// This shows basic information about the system. Currently only the mac
// TODO: expand infos
String Basecamp::showSystemInfo() {
	std::ostringstream info;
	info << "MAC-Address: " << mac.c_str();
	info << ", Hardware MAC: " << network.getHardwareMacAddress(":").c_str() << std::endl;

	if (configuration.isKeySet(ConfigurationKey::accessPointSecret)) {
			info << "*******************************************" << std::endl;
			info << "* ACCESS POINT PASSWORD: ";
			info << configuration.get(ConfigurationKey::accessPointSecret).c_str() << std::endl;
			info << "*******************************************" << std::endl;
	}

	return {info.str().c_str()};
}

