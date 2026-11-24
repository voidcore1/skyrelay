#pragma once

// Static file serving: the web station from its web root, and the offline map
// data (PMTiles archive, glyphs, sprites) from the data directory. The map
// library reads the PMTiles archive with HTTP Range requests, so a wrong range
// answer leaves the map blank without any error in the browser.

#include <boost/beast/http.hpp>

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace skyrelay::gateway {

struct ByteRange {
  uint64_t first, last;  // inclusive
};

enum class RangeKind { Full, Partial, Unsatisfiable, Invalid };

struct RangeResult {
  RangeKind kind;
  ByteRange range;  // meaningful for Partial only
};

// Parses a Range header value for a representation of `size` bytes. Only a
// single byte range is supported: several ranges give Full, and so does an
// empty header. Invalid covers a unit other than bytes and a malformed spec
// (including last < first); the server ignores such a header and sends the
// whole file, as RFC 9110 allows. Unsatisfiable means the range starts at or
// past the end (or is an empty suffix) and is answered with 416.
RangeResult parse_range(std::string_view header, uint64_t size);

// Splits an origin-form request target into decoded path segments. The query
// is dropped, %xx escapes are decoded, and empty and "." segments are skipped.
// Returns nullopt for a target that does not start with '/', a bad escape, a
// NUL byte, or a ".." segment (also when it was escaped).
std::optional<std::vector<std::string>> decode_target(std::string_view target);

// Content-Type by file extension, case-insensitive. Unknown types are
// application/octet-stream.
std::string_view mime_type(std::string_view filename);

// Value of the Server header.
std::string server_name();

using Request = boost::beast::http::request<boost::beast::http::string_body>;

struct StaticRoots {
  std::filesystem::path web_root;  // the built web station
  std::filesystem::path data_dir;  // tiles/ and assets/
};

// Answers a GET or HEAD for already decoded segments:
//   /tiles/...   from data_dir/tiles, 404 if missing
//   /assets/...  from data_dir/assets, 404 if missing
//   anything else from web_root; a missing path whose last segment has no dot
//   (a client-side route) gets index.html, a missing asset gets 404.
// A path that resolves outside its root, also through a symlink, gets 400.
// Other methods get 405.
boost::beast::http::message_generator serve_static(
    const Request& req, const std::vector<std::string>& segments,
    const StaticRoots& roots);

// A small text/plain response, with optional extra headers; HEAD gets the
// headers only.
using ExtraHeaders =
    std::initializer_list<std::pair<boost::beast::http::field, std::string_view>>;
boost::beast::http::message_generator text_response(
    const Request& req, boost::beast::http::status status, std::string_view body,
    ExtraHeaders extra = {});

}  // namespace skyrelay::gateway
