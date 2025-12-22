#include "ap_mode.h"
#include "config_manager.h"
#include "logging.h"

// AP mode only available on WiFi builds
#if defined(USE_WIFI)

#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>

// AP configuration
#define AP_PASSWORD "doorbell123"
#define AP_CHANNEL 1
#define AP_MAX_CONNECTIONS 4

// DNS server for captive portal
static DNSServer dnsServer;
static const byte DNS_PORT = 53;

// AP mode state
bool apModeActive = false;
static String apSsid;

bool shouldStartApMode() {
    // Start AP mode if no WiFi credentials are configured
    // (configured flag alone isn't enough - we need actual WiFi credentials)
    return !hasWifiCredentials();
}

String getApSsid() {
    if (apSsid.length() == 0) {
        // Generate unique SSID based on MAC address
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char ssid[32];
        snprintf(ssid, sizeof(ssid), "UniFi-Doorbell-%02X%02X", mac[4], mac[5]);
        apSsid = String(ssid);
    }
    return apSsid;
}

void setupApMode() {
    if (apModeActive) return;

    logPrintln("AP Mode: Starting...");

    // Configure WiFi as Access Point
    WiFi.mode(WIFI_AP);
    WiFi.softAP(getApSsid().c_str(), AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CONNECTIONS);

    // Wait for AP to start
    delay(100);

    IPAddress apIP = WiFi.softAPIP();
    logPrintln("AP Mode: SSID: " + getApSsid());
    logPrintln("AP Mode: Password: " + String(AP_PASSWORD));
    logPrintln("AP Mode: IP: " + apIP.toString());

    // Start DNS server for captive portal
    // Redirect all DNS requests to our IP
    dnsServer.start(DNS_PORT, "*", apIP);
    logPrintln("AP Mode: DNS server started (captive portal)");

    // Start mDNS responder
    if (MDNS.begin("doorbell")) {
        MDNS.addService("http", "tcp", 80);
        logPrintln("AP Mode: mDNS started: doorbell.local");
    }

    apModeActive = true;
    logPrintln("AP Mode: Ready for configuration at http://" + apIP.toString());
}

void apModeLoop() {
    if (!apModeActive) return;

    // Process DNS requests (for captive portal)
    dnsServer.processNextRequest();
}

void stopApMode() {
    if (!apModeActive) return;

    logPrintln("AP Mode: Stopping...");

    dnsServer.stop();
    MDNS.end();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);

    apModeActive = false;
    logPrintln("AP Mode: Stopped");
}

#else
// Ethernet builds - AP mode not available

bool apModeActive = false;

bool shouldStartApMode() {
    // Ethernet boards don't need AP mode
    return false;
}

String getApSsid() {
    return "";
}

void setupApMode() {
    // Not available on Ethernet
    logPrintln("AP Mode: Not available on Ethernet builds");
}

void apModeLoop() {
    // Nothing to do
}

void stopApMode() {
    // Nothing to do
}

#endif
