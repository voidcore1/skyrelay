#include "static_files.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/beast/core/file.hpp>
#include <boost/optional.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <system_error>
#include <tuple>
#include <utility>

#include "skyrelay/common/version.hpp"

namespace skyrelay::gateway {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace fs = std::filesystem;

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

bool iequals(std::string_view a, std::string_view b) {
  return std::ranges::equal(a, b, [](char x, char y) {
    return std::tolower(static_cast<unsigned char>(x)) ==
           std::tolower(static_cast<unsigned char>(y));
  });
}

// Digits only. A value too large for 64 bits saturates, which is still right
// for a range: it is past the end of any file.
std::optional<uint64_t> parse_u64(std::string_view s) {
  if (s.empty()) return std::nullopt;
  for (char c : s) {
    if (c < '0' || c > '9') return std::nullopt;
  }
  uint64_t v = 0;
  const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
  if (r.ec == std::errc::result_out_of_range) return std::numeric_limits<uint64_t>::max();
  return v;
}

int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// The bytes [offset, offset + length) of an open file, read in chunks while
// the response is written, so a large range never sits in memory whole.
struct FileRangeBody {
  struct value_type {
    value_type(beast::file f, uint64_t off, uint64_t len)
        : file(std::move(f)), offset(off), length(len) {}
    beast::file file;
    uint64_t offset = 0;
    uint64_t length = 0;
  };

  static uint64_t size(const value_type& v) { return v.length; }

  class writer {
   public:
    using const_buffers_type = asio::const_buffer;

    template <bool isRequest, class Fields>
    writer(http::header<isRequest, Fields>&, value_type& body)
        : body_(body), remain_(body.length) {}

    void init(beast::error_code& ec) { body_.file.seek(body_.offset, ec); }

    boost::optional<std::pair<const_buffers_type, bool>> get(beast::error_code& ec) {
      const auto want = static_cast<std::size_t>(
          std::min<uint64_t>(remain_, sizeof(buf_)));
      if (want == 0) {
        ec = {};
        return boost::none;
      }
      const std::size_t got = body_.file.read(buf_, want, ec);
      if (ec) return boost::none;
      if (got == 0) {
        // The file shrank under us.
        ec = http::error::short_read;
        return boost::none;
      }
      remain_ -= got;
      return std::make_pair(const_buffers_type(buf_, got), remain_ > 0);
    }

   private:
    value_type& body_;
    uint64_t remain_;
    char buf_[64 * 1024];
  };
};

template <class Body>
void common_headers(http::response<Body>& res, const Request& req) {
  res.set(http::field::server, server_name());
  res.keep_alive(req.keep_alive());
}

enum class Lookup { Found, NotFound, Outside };

struct Located {
  Lookup status;
  fs::path path;
};

// Resolves segments [from, end) under root and checks that the real path,
// after every symlink, is still inside the real root.
Located locate(const fs::path& root, const std::vector<std::string>& segments,
               size_t from) {
  std::error_code ec;
  const fs::path real_root = fs::canonical(root, ec);
  if (ec) return {Lookup::NotFound, {}};
  fs::path p = real_root;
  for (size_t i = from; i < segments.size(); ++i) p /= segments[i];
  const fs::path real = fs::canonical(p, ec);
  if (ec) return {Lookup::NotFound, {}};
  const auto [r, unused] =
      std::mismatch(real_root.begin(), real_root.end(), real.begin(), real.end());
  if (r != real_root.end()) return {Lookup::Outside, {}};
  if (!fs::is_regular_file(real, ec)) return {Lookup::NotFound, {}};
  return {Lookup::Found, real};
}

http::message_generator send_file(const Request& req, const fs::path& path) {
  beast::error_code ec;
  beast::file file;
  file.open(path.c_str(), beast::file_mode::scan, ec);
  if (ec) return text_response(req, http::status::not_found, "not found\n");
  const uint64_t size = file.size(ec);
  if (ec) return text_response(req, http::status::internal_server_error, "read error\n");

  const std::string type(mime_type(path.filename().string()));
  // Range is only defined for GET. With If-Range the client asks for the range
  // only if the file is unchanged; there are no validators here, so it gets the
  // whole file, which is always a correct answer.
  RangeResult rr{RangeKind::Full, {0, 0}};
  if (req.method() == http::verb::get && req.count(http::field::range) > 0 &&
      req.count(http::field::if_range) == 0) {
    rr = parse_range(req[http::field::range], size);
  }

  if (rr.kind == RangeKind::Unsatisfiable) {
    http::response<http::empty_body> res{http::status::range_not_satisfiable,
                                         req.version()};
    common_headers(res, req);
    res.set(http::field::accept_ranges, "bytes");
    res.set(http::field::content_range, "bytes */" + std::to_string(size));
    res.content_length(0);
    return res;
  }

  if (rr.kind == RangeKind::Partial) {
    const uint64_t len = rr.range.last - rr.range.first + 1;
    http::response<FileRangeBody> res{
        std::piecewise_construct,
        std::forward_as_tuple(std::move(file), rr.range.first, len),
        std::forward_as_tuple(http::status::partial_content, req.version())};
    common_headers(res, req);
    res.set(http::field::content_type, type);
    res.set(http::field::accept_ranges, "bytes");
    res.set(http::field::content_range, "bytes " + std::to_string(rr.range.first) +
                                             "-" + std::to_string(rr.range.last) +
                                             "/" + std::to_string(size));
    res.content_length(len);
    return res;
  }

  // Full, or a Range header that is ignored.
  if (req.method() == http::verb::head) {
    http::response<http::empty_body> res{http::status::ok, req.version()};
    common_headers(res, req);
    res.set(http::field::content_type, type);
    res.set(http::field::accept_ranges, "bytes");
    res.content_length(size);
    return res;
  }
  http::file_body::value_type body;
  body.reset(std::move(file), ec);
  if (ec) return text_response(req, http::status::internal_server_error, "read error\n");
  http::response<http::file_body> res{std::piecewise_construct,
                                      std::forward_as_tuple(std::move(body)),
                                      std::forward_as_tuple(http::status::ok, req.version())};
  common_headers(res, req);
  res.set(http::field::content_type, type);
  res.set(http::field::accept_ranges, "bytes");
  res.content_length(size);
  return res;
}

}  // namespace

RangeResult parse_range(std::string_view header, uint64_t size) {
  const ByteRange whole{0, size == 0 ? 0 : size - 1};
  header = trim(header);
  if (header.empty()) return {RangeKind::Full, whole};
  const auto eq = header.find('=');
  if (eq == std::string_view::npos || !iequals(trim(header.substr(0, eq)), "bytes")) {
    return {RangeKind::Invalid, whole};
  }
  const std::string_view spec = trim(header.substr(eq + 1));
  // Several ranges would need a multipart/byteranges answer. Nothing here asks
  // for one, and the whole file is a valid reply.
  if (spec.find(',') != std::string_view::npos) return {RangeKind::Full, whole};

  const auto dash = spec.find('-');
  if (dash == std::string_view::npos) return {RangeKind::Invalid, whole};
  const std::string_view first_s = trim(spec.substr(0, dash));
  const std::string_view last_s = trim(spec.substr(dash + 1));

  if (first_s.empty()) {
    // bytes=-N: the last N bytes.
    const auto n = parse_u64(last_s);
    if (!n) return {RangeKind::Invalid, whole};
    if (*n == 0 || size == 0) return {RangeKind::Unsatisfiable, whole};
    return {RangeKind::Partial, {size - std::min(*n, size), size - 1}};
  }

  const auto first = parse_u64(first_s);
  if (!first) return {RangeKind::Invalid, whole};
  std::optional<uint64_t> last;
  if (!last_s.empty()) {
    last = parse_u64(last_s);
    if (!last || *last < *first) return {RangeKind::Invalid, whole};
  }
  if (*first >= size) return {RangeKind::Unsatisfiable, whole};
  return {RangeKind::Partial, {*first, std::min(last.value_or(size - 1), size - 1)}};
}

std::optional<std::vector<std::string>> decode_target(std::string_view target) {
  if (target.empty() || target.front() != '/') return std::nullopt;
  const auto q = target.find_first_of("?#");
  if (q != std::string_view::npos) target = target.substr(0, q);

  std::string path;
  path.reserve(target.size());
  for (size_t i = 0; i < target.size(); ++i) {
    char c = target[i];
    if (c == '%') {
      if (target.size() - i < 3) return std::nullopt;
      const int hi = hex_digit(target[i + 1]);
      const int lo = hex_digit(target[i + 2]);
      if (hi < 0 || lo < 0) return std::nullopt;
      c = static_cast<char>(hi * 16 + lo);
      i += 2;
    }
    if (c == '\0') return std::nullopt;
    path += c;
  }

  // Split after decoding, so an escaped slash cannot hide a ".." segment.
  std::vector<std::string> segments;
  size_t start = 0;
  while (start <= path.size()) {
    const size_t end = std::min(path.find('/', start), path.size());
    const std::string_view seg(path.data() + start, end - start);
    if (seg == "..") return std::nullopt;
    if (!seg.empty() && seg != ".") segments.emplace_back(seg);
    start = end + 1;
  }
  return segments;
}

std::string_view mime_type(std::string_view filename) {
  const auto dot = filename.rfind('.');
  if (dot == std::string_view::npos) return "application/octet-stream";
  std::string ext(filename.substr(dot + 1));
  for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  static constexpr std::pair<std::string_view, std::string_view> kTypes[] = {
      {"html", "text/html; charset=utf-8"},
      {"js", "text/javascript; charset=utf-8"},
      {"mjs", "text/javascript; charset=utf-8"},
      {"css", "text/css; charset=utf-8"},
      {"json", "application/json"},
      {"map", "application/json"},
      {"png", "image/png"},
      {"svg", "image/svg+xml"},
      {"ico", "image/x-icon"},
      {"woff2", "font/woff2"},
      {"txt", "text/plain; charset=utf-8"},
      {"pbf", "application/x-protobuf"},
      {"pmtiles", "application/octet-stream"},
  };
  for (const auto& [e, type] : kTypes) {
    if (ext == e) return type;
  }
  return "application/octet-stream";
}

std::string server_name() { return "skyrelay-gateway/" + std::string(kVersion); }

http::message_generator text_response(const Request& req, http::status status,
                                      std::string_view body, ExtraHeaders extra) {
  if (req.method() == http::verb::head) {
    http::response<http::empty_body> res{status, req.version()};
    common_headers(res, req);
    res.set(http::field::content_type, "text/plain; charset=utf-8");
    for (const auto& [f, v] : extra) res.set(f, v);
    res.content_length(body.size());
    return res;
  }
  http::response<http::string_body> res{status, req.version()};
  common_headers(res, req);
  res.set(http::field::content_type, "text/plain; charset=utf-8");
  for (const auto& [f, v] : extra) res.set(f, v);
  res.body() = std::string(body);
  res.prepare_payload();
  return res;
}

http::message_generator serve_static(const Request& req,
                                     const std::vector<std::string>& segments,
                                     const StaticRoots& roots) {
  if (req.method() != http::verb::get && req.method() != http::verb::head) {
    return text_response(req, http::status::method_not_allowed, "method not allowed\n",
                         {{http::field::allow, "GET, HEAD"}});
  }

  fs::path root = roots.web_root;
  size_t from = 0;
  bool spa = true;
  if (!segments.empty() && (segments[0] == "tiles" || segments[0] == "assets")) {
    root = roots.data_dir / segments[0];
    from = 1;
    spa = false;
  }

  Located f{Lookup::NotFound, {}};
  if (segments.size() > from) f = locate(root, segments, from);
  if (f.status == Lookup::Outside) {
    return text_response(req, http::status::bad_request, "bad request\n");
  }
  // Client-side routes have no dot in their last segment; assets do.
  if (f.status == Lookup::NotFound && spa &&
      (segments.empty() || segments.back().find('.') == std::string::npos)) {
    f = locate(roots.web_root, {"index.html"}, 0);
  }
  if (f.status != Lookup::Found) {
    return text_response(req, http::status::not_found, "not found\n");
  }
  return send_file(req, f.path);
}

}  // namespace skyrelay::gateway
