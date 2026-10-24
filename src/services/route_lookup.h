
#pragma once

#include <cstddef>

namespace services::adsb {
struct Aircraft;
}

namespace services::routes {

// Read a known destination without making a network request.
void getCachedDestination(const char* callsign,
                          char* out, size_t out_len);

// Look up at most one uncached callsign per refresh.
void updateOne(adsb::Aircraft* aircraft, size_t count);

}  // namespace services::routes
