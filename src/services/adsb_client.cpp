
#include "services/adsb_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <cstring>
#include <cmath>

#include "config.h"
#include "services/route_lookup.h"

namespace services::adsb {
namespace {

constexpr char kApiBase[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr float kKmPerNm = 1.852f;
constexpr int kConnectAttemptMs = 200;
constexpr unsigned long kRequestTimeoutMs = 10000;

Aircraft s_aircraft[kMaxAircraft] = {};
size_t s_aircraft_count = 0;
PollFn s_poll_fn = nullptr;

void pollNetwork() {
  if (s_poll_fn != nullptr) s_poll_fn();
}

int performGetWithPoll(HTTPClient& http) {
  http.setConnectTimeout(kConnectAttemptMs);
  const unsigned long start = millis();

  while (millis() - start < kRequestTimeoutMs) {
    pollNetwork();
    const int code = http.GET();

    if (code > 0) return code;

    if (code != HTTPC_ERROR_CONNECTION_REFUSED &&
        code != HTTPC_ERROR_NOT_CONNECTED) {
      return code;
    }
    delay(5);
  }
  return HTTPC_ERROR_READ_TIMEOUT;
}

bool readResponseBodyWithPoll(HTTPClient& http, String& payload) {
  pollNetwork();
  payload = http.getString();
  pollNetwork();
  return payload.length() > 0;
}

bool readJsonFloat(const JsonObject& obj, const char* key, float* out) {
  if (obj[key].is<float>() || obj[key].is<double>() ||
      obj[key].is<int>()) {
    *out = obj[key].as<float>();
    return true;
  }
  return false;
}

float pickNoseHeading(const JsonObject& plane) {
  float v = 0;
  for (const char* key :
       {"true_heading", "mag_heading", "track", "dir"}) {
    if (readJsonFloat(plane, key, &v)) return v;
  }
  return 0;
}

float pickTrackHeading(const JsonObject& plane) {
  float v = 0;
  for (const char* key :
       {"track", "true_heading", "mag_heading", "dir"}) {
    if (readJsonFloat(plane, key, &v)) return v;
  }
  return 0;
}

float pickGroundSpeed(const JsonObject& plane) {
  float v = 0;
  for (const char* key : {"gs", "tas", "ias"}) {
    if (readJsonFloat(plane, key, &v)) return v;
  }
  return 0;
}

bool isOnGround(const JsonObject& plane) {
  return plane["alt_baro"].is<const char*>() &&
         strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0;
}

void copyJsonStringTrimmed(const JsonObject& obj,
                           const char* key,
                           char* out, size_t out_len) {
  if (out_len == 0) return;
  out[0] = '\0';

  if (!obj[key].is<const char*>()) return;

  const char* s = obj[key].as<const char*>();
  size_t n = strnlen(s, out_len - 1);

  while (n > 0 && s[n - 1] == ' ') --n;

  memcpy(out, s, n);
  out[n] = '\0';
}

void formatAltitudeTag(const JsonObject& plane,
                       char* out, size_t out_len) {
  if (out_len == 0) return;
  out[0] = '\0';

  if (isOnGround(plane)) {
    snprintf(out, out_len, "GND");
    return;
  }

  float alt = 0;
  if (readJsonFloat(plane, "alt_baro", &alt) ||
      readJsonFloat(plane, "alt_geom", &alt)) {
    snprintf(out, out_len, "%d ft",
             static_cast<int>(lroundf(alt)));
  }
}

void fillTagFields(Aircraft* ac, const JsonObject& plane) {
  ac->destination[0] = '\0';

  copyJsonStringTrimmed(
      plane, "flight", ac->callsign, sizeof(ac->callsign));

  // Only actual flight callsigns can be used for route lookups.
  if (ac->callsign[0] != '\0') {
    routes::getCachedDestination(
        ac->callsign, ac->destination, sizeof(ac->destination));
  } else {
    copyJsonStringTrimmed(
        plane, "hex", ac->callsign, sizeof(ac->callsign));
  }

  copyJsonStringTrimmed(
      plane, "t", ac->type, sizeof(ac->type));

  formatAltitudeTag(plane, ac->alt, sizeof(ac->alt));
}

}  // namespace

void setPollFn(PollFn fn) { s_poll_fn = fn; }

size_t aircraftCount() { return s_aircraft_count; }

const Aircraft* aircraftList() { return s_aircraft; }

bool fetchUpdate(double center_lat, double center_lon,
                 float fetch_radius_km) {
  const float dist_nm = fetch_radius_km / kKmPerNm;

  String url = kApiBase;
  url += String(center_lat, 6);
  url += "/lon/";
  url += String(center_lon, 6);
  url += "/dist/";
  url += String(dist_nm, 1);

  {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("adsb: http.begin failed");
    return false;
  }

  http.setTimeout(kRequestTimeoutMs);

  const int code = performGetWithPoll(http);
  if (code != HTTP_CODE_OK) {
    Serial.printf("adsb: HTTP %d\n", code);
    http.end();
    return false;
  }

  String payload;
  if (!readResponseBodyWithPoll(http, payload)) {
    Serial.println("adsb: empty response");
    http.end();
    return false;
  }
  http.end();
{


  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload);

  if (err) {
    Serial.printf("adsb: JSON parse error: %s\n", err.c_str());
    Serial.printf("adsb: payload length: %u\n",
                  static_cast<unsigned>(payload.length()));
    Serial.println("----- ADSB RESPONSE START -----");
    Serial.println(payload);
    Serial.println("----- ADSB RESPONSE END -----");
    return false;
  }

  JsonArray ac = doc["ac"].as<JsonArray>();
  if (ac.isNull()) {
    s_aircraft_count = 0;
    return true;
  }

  size_t n = 0;

  for (JsonObject plane : ac) {
    if (n >= kMaxAircraft) break;

    if (!plane["lat"].is<float>() ||
        !plane["lon"].is<float>()) continue;

    if (isOnGround(plane) &&
        !config::kAdsbShowGroundAircraft) continue;

    Aircraft& aircraft = s_aircraft[n];

    aircraft.lat = plane["lat"].as<float>();
    aircraft.lon = plane["lon"].as<float>();
    aircraft.nose_deg = pickNoseHeading(plane);
    aircraft.track_deg = pickTrackHeading(plane);
    aircraft.gs_knots = pickGroundSpeed(plane);

    fillTagFields(&aircraft, plane);
    ++n;
  }

      s_aircraft_count = n;
  }  // Release the parsed ADS-B JSON document.
  }  // Release the ADS-B HTTP and TLS objects.

  // ADS-B response, JSON, HTTP, and TLS objects are now
  // out of scope before starting the destination lookup.
  routes::updateOne(s_aircraft, s_aircraft_count);

    Serial.printf("adsb: %u aircraft\n",
                static_cast<unsigned>(s_aircraft_count));

  return true;
}

}  // namespace services::adsb
