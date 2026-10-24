
#include "services/route_lookup.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <cctype>
#include <cstring>

#include "services/adsb_client.h"

namespace services::routes {
namespace {

constexpr size_t kCacheSize = 64;
constexpr unsigned long kSuccessTtlMs = 1800000UL;
constexpr unsigned long kMissTtlMs = 600000UL;
constexpr unsigned long kErrorRetryMs = 60000UL;

struct CacheEntry {
  char callsign[9] = {};
  char destination[5] = {};
  unsigned long checked_at = 0;
  unsigned long ttl_ms = 0;
  bool occupied = false;
};

CacheEntry s_cache[kCacheSize];
size_t s_next_slot = 0;

bool isValidCallsign(const char* callsign) {
  if (!callsign) return false;

  const size_t len = strlen(callsign);
  if (len < 3 || len > 8) return false;

  for (size_t i = 0; i < len; ++i) {
    if (!isalnum(static_cast<unsigned char>(callsign[i]))) {
      return false;
    }
  }

  return true;
}

CacheEntry* findEntry(const char* callsign) {
  for (auto& entry : s_cache) {
    if (entry.occupied &&
        strcmp(entry.callsign, callsign) == 0) {
      return &entry;
    }
  }
  return nullptr;
}

bool isFresh(const CacheEntry& entry) {
  return entry.occupied &&
         (millis() - entry.checked_at < entry.ttl_ms);
}

CacheEntry* allocateEntry(const char* callsign) {
  CacheEntry* entry = findEntry(callsign);

  if (!entry) {
    entry = &s_cache[s_next_slot];
    s_next_slot = (s_next_slot + 1) % kCacheSize;
  }

  memset(entry, 0, sizeof(*entry));
  strncpy(entry->callsign, callsign,
          sizeof(entry->callsign) - 1);
  entry->occupied = true;

  return entry;
}

unsigned long lookupDestination(const char* callsign,
                                char* out, size_t out_len) {
  if (out_len == 0) return kErrorRetryMs;
  out[0] = '\0';

    Serial.printf(
      "routes: heap free=%u, largest block=%u, min free=%u\n",
      ESP.getFreeHeap(),
      ESP.getMaxAllocHeap(),
      ESP.getMinFreeHeap());

    WiFiClientSecure client;
  client.setInsecure();



  HTTPClient http;

  String url = "https://api.adsbdb.com/v0/callsign/";
  url += callsign;

  if (!http.begin(client, url)) {
    Serial.println("routes: http.begin failed");
    return kErrorRetryMs;
  }

  http.setConnectTimeout(1500);
  http.setTimeout(3000);

  const int code = http.GET();

  if (code == HTTP_CODE_NOT_FOUND) {
    Serial.printf("routes: no route for %s\n", callsign);
    http.end();
    return kMissTtlMs;
  }

  if (code != HTTP_CODE_OK) {
    Serial.printf("routes: HTTP %d for %s\n",
                  code, callsign);
    http.end();
    return kErrorRetryMs;
  }

  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, http.getStream());

  http.end();

  if (err) {
    Serial.printf("routes: JSON error for %s: %s\n",
                  callsign, err.c_str());
    return kErrorRetryMs;
  }

  const char* airport =
      doc["response"]["flightroute"]
         ["destination"]["icao_code"] | "";

  if (strlen(airport) != 4) {
    Serial.printf("routes: destination unavailable for %s\n",
                  callsign);
    return kMissTtlMs;
  }

  for (size_t i = 0; i < 4; ++i) {
    if (!isalnum(static_cast<unsigned char>(airport[i]))) {
      return kMissTtlMs;
    }
  }

  snprintf(out, out_len, "%.4s", airport);

  Serial.printf("routes: %s -> %s\n", callsign, out);
  return kSuccessTtlMs;
}

}  // namespace

void getCachedDestination(const char* callsign,
                          char* out, size_t out_len) {
  if (!out || out_len == 0) return;
  out[0] = '\0';

  if (!isValidCallsign(callsign)) return;

  CacheEntry* entry = findEntry(callsign);

  if (!entry || !isFresh(*entry)) return;
  if (!entry->destination[0]) return;

  strncpy(out, entry->destination, out_len - 1);
  out[out_len - 1] = '\0';
}

void updateOne(adsb::Aircraft* aircraft, size_t count) {
  if (WiFi.status() != WL_CONNECTED) return;

  // Prefer new callsigns, but never make more than one
  // destination request per radar refresh.
  for (size_t i = 0; i < count; ++i) {
    const char* callsign = aircraft[i].callsign;

    if (!isValidCallsign(callsign)) continue;

    CacheEntry* entry = findEntry(callsign);

    if (entry && isFresh(*entry)) continue;

    if (!entry) entry = allocateEntry(callsign);

    entry->ttl_ms = lookupDestination(
        callsign, entry->destination,
        sizeof(entry->destination));

    entry->checked_at = millis();

    // Make the new result available immediately.
    for (size_t j = 0; j < count; ++j) {
      if (strcmp(aircraft[j].callsign, callsign) == 0) {
        getCachedDestination(
            callsign, aircraft[j].destination,
            sizeof(aircraft[j].destination));
      }
    }

    break;
  }
}

}  // namespace services::routes
