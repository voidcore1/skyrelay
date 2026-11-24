#include "ws_server.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cctype>
#include <chrono>
#include <deque>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "fanout.hpp"
#include "skyrelay/common/version.hpp"
#include "static_files.hpp"

namespace skyrelay::gateway {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using beast::error_code;

// An idle keep-alive HTTP connection is closed after this.
constexpr auto kHttpIdleTimeout = std::chrono::seconds(30);
// GET and HEAD carry no body; anything bigger is not a request for this server.
constexpr std::size_t kMaxRequestBody = 8 * 1024;
constexpr std::size_t kMaxClientMessage = 64 * 1024;
// Kept small on purpose. With the default autotuned buffer the kernel would
// queue megabytes for a browser that stopped reading, which it would then be
// fed seconds late; with a small one its writes stall early and it falls back
// to latest-value like any other slow client.
constexpr int kWsSendBuffer = 64 * 1024;
// pongs waiting behind a write; more pings than this while stalled are dropped.
constexpr std::size_t kMaxQueuedReplies = 32;
// How often held poses are checked; see Fanout::poll().
constexpr auto kPollInterval = std::chrono::milliseconds(25);
constexpr auto kAcceptRetry = std::chrono::milliseconds(100);

class Session {
 public:
  virtual ~Session() = default;
  virtual void close() = 0;
};

// Server state shared by the sessions. Only used on the io_context thread,
// apart from the atomic counters.
struct Core {
  Core(asio::io_context& io_, ServerConfig cfg_, const Clock& clock_)
      : io(io_),
        cfg(std::move(cfg_)),
        clock(clock_),
        roots{cfg.web_root, cfg.data_dir},
        fanout(clock_, cfg.latched_topics),
        acceptor(io_),
        poll_timer(io_),
        accept_retry(io_) {
    if (!is_loopback(cfg.listen.address())) {
      throw std::invalid_argument(
          "refusing non-loopback listen address without TLS (arrives in v0.3)");
    }
    acceptor.open(cfg.listen.protocol());
    acceptor.set_option(tcp::acceptor::reuse_address(true));
    acceptor.bind(cfg.listen);
    acceptor.listen(asio::socket_base::max_listen_connections);
    bound = acceptor.local_endpoint();

    // The gateway's own origins, as a browser that loaded the station from
    // it sends them. A browser leaves out the default port.
    const std::string p = ":" + std::to_string(bound.port());
    for (const char* host : {"127.0.0.1", "localhost", "[::1]"}) {
      origins.insert(std::string("http://") + host + p);
      if (bound.port() == 80) origins.insert(std::string("http://") + host);
    }
    for (const auto& o : cfg.allowed_origins) {
      const auto parsed = parse_origin(o);
      if (!parsed) throw std::invalid_argument("not an origin: '" + o + "'");
      origins.insert(*parsed);
    }
  }

  // Exactly one Host header, naming loopback with our port.
  bool host_allowed(const Request& req) const {
    return req.count(http::field::host) == 1 &&
           host_is_local(req[http::field::host], bound.port());
  }

  // No Origin at all is not a browser (tools, tests); a browser's must be
  // listed. Origin is lower-cased; it has no path, so that only touches the
  // scheme and host.
  bool origin_allowed(const Request& req) const {
    const auto n = req.count(http::field::origin);
    if (n == 0) return true;
    if (n > 1) return false;
    std::string o(req[http::field::origin]);
    for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return origins.contains(o);
  }

  uint64_t track(std::weak_ptr<Session> s) {
    const uint64_t id = next_session++;
    sessions.emplace(id, std::move(s));
    return id;
  }
  void forget(uint64_t id) { sessions.erase(id); }

  void do_accept();
  void on_accept(error_code ec, tcp::socket socket);
  void arm_poll();
  void do_stop();

  asio::io_context& io;
  ServerConfig cfg;
  const Clock& clock;
  StaticRoots roots;
  Fanout fanout;
  tcp::acceptor acceptor;
  tcp::endpoint bound;  // with the real port when 0 was asked for
  std::set<std::string> origins;  // allowed on /ws, lower-case
  asio::steady_timer poll_timer;
  asio::steady_timer accept_retry;
  std::unordered_map<uint64_t, std::weak_ptr<Session>> sessions;
  uint64_t next_session = 1;
  bool stopping = false;
  WsServer::Counters counters;
};

std::string describe(const tcp::endpoint& ep) {
  const auto a = ep.address();
  return (a.is_v6() ? "[" + a.to_string() + "]" : a.to_string()) + ":" +
         std::to_string(ep.port());
}

// A JSON number from the client is echoed as it came; anything else as null.
nlohmann::ordered_json number_or_null(const nlohmann::ordered_json& j, const char* key) {
  const auto it = j.find(key);
  if (it != j.end() && it->is_number()) return *it;
  return nullptr;
}

class WsSession : public Session, public std::enable_shared_from_this<WsSession> {
 public:
  WsSession(tcp::socket&& socket, Core& core) : ws_(std::move(socket)), core_(core) {}

  void run(Request req) {
    req_ = std::move(req);
    id_ = core_.track(weak_from_this());
    auto& sock = beast::get_lowest_layer(ws_).socket();
    error_code ignored;
    sock.set_option(tcp::no_delay(true), ignored);
    sock.set_option(asio::socket_base::send_buffer_size(kWsSendBuffer), ignored);
    const auto peer = sock.remote_endpoint(ignored);
    remote_ = ignored ? "?" : describe(peer);
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res) { res.set(http::field::server, server_name()); }));
    ws_.read_message_max(kMaxClientMessage);
    ws_.async_accept(req_, beast::bind_front_handler(&WsSession::on_accept,
                                                     shared_from_this()));
  }

  void close() override { finish(); }

 private:
  void on_accept(error_code ec) {
    if (finished_) return;
    if (ec) {
      spdlog::debug("websocket handshake from {} failed: {}", remote_, ec.message());
      finish();
      return;
    }
    accepted_ = true;
    ++core_.counters.ws_clients;
    std::weak_ptr<WsSession> weak = weak_from_this();
    client_ = core_.fanout.add_client(
        [weak](std::string frame) {
          if (auto s = weak.lock()) s->write(std::move(frame));
        },
        [weak] {
          auto s = weak.lock();
          return !s || s->writing_;
        });
    registered_ = true;
    spdlog::info("client {} connected from {}", client_, remote_);
    write(hello());
    do_read();
  }

  std::string hello() const {
    nlohmann::ordered_json j;
    j["type"] = "hello";
    j["version"] = std::string(kVersion);
    j["contract"] = std::string(kContractVersion);
    j["insecure"] = true;
    j["server_time_ns"] = core_.clock.realtime_ns();
    return j.dump();
  }

  void do_read() {
    ws_.async_read(rbuf_, beast::bind_front_handler(&WsSession::on_read,
                                                    shared_from_this()));
  }

  void on_read(error_code ec, std::size_t) {
    if (finished_) return;
    if (ec) {
      finish();
      return;
    }
    if (ws_.got_text()) {
      handle(beast::buffers_to_string(rbuf_.data()));
    } else {
      ignore();
    }
    rbuf_.consume(rbuf_.size());
    do_read();
  }

  // ping is the only client message in M1; commands arrive in M2.
  void handle(const std::string& text) {
    const auto j = nlohmann::ordered_json::parse(text, nullptr, false);
    if (j.is_object()) {
      const auto type = j.find("type");
      if (type != j.end() && type->is_string() &&
          type->get_ref<const std::string&>() == "ping") {
        nlohmann::ordered_json pong;
        pong["type"] = "pong";
        pong["id"] = number_or_null(j, "id");
        pong["t_client_ms"] = number_or_null(j, "t_client_ms");
        pong["server_time_ns"] = core_.clock.realtime_ns();
        reply(pong.dump());
        return;
      }
    }
    ignore();
  }

  void ignore() {
    ++ignored_;
    ++core_.counters.ignored_client_messages;
  }

  // A reply shares the one write slot with the fan-out and goes first when
  // the current write completes.
  void reply(std::string msg) {
    if (!writing_) {
      write(std::move(msg));
    } else if (replies_.size() < kMaxQueuedReplies) {
      replies_.push_back(std::move(msg));
    } else {
      ignore();
    }
  }

  void write(std::string frame) {
    writing_ = true;
    ++core_.counters.ws_writes_in_flight;
    out_ = std::move(frame);
    ws_.text(true);
    ws_.async_write(asio::buffer(out_), beast::bind_front_handler(&WsSession::on_write,
                                                                  shared_from_this()));
  }

  void on_write(error_code ec, std::size_t) {
    --core_.counters.ws_writes_in_flight;
    writing_ = false;
    if (finished_) return;
    if (ec) {
      finish();
      return;
    }
    if (!replies_.empty()) {
      std::string next = std::move(replies_.front());
      replies_.pop_front();
      write(std::move(next));
      return;
    }
    core_.fanout.on_write_complete(client_, core_.clock.mono());
  }

  // Idempotent. Leaves the fan-out first, so nothing is sent after this, then
  // closes the socket; pending reads and writes complete with an error.
  void finish() {
    if (finished_) return;
    finished_ = true;
    if (registered_) {
      const auto st = core_.fanout.stats(client_);
      core_.fanout.remove_client(client_);
      registered_ = false;
      spdlog::info("client {} closed ({} superseded, {} ignored)", client_,
                   st.dropped_superseded, ignored_);
    }
    if (accepted_) --core_.counters.ws_clients;
    core_.forget(id_);
    beast::get_lowest_layer(ws_).close();
  }

  websocket::stream<beast::tcp_stream> ws_;
  Core& core_;
  Request req_;
  beast::flat_buffer rbuf_;
  std::string out_;
  std::deque<std::string> replies_;
  std::string remote_;
  uint64_t id_ = 0;
  Fanout::ClientId client_ = 0;
  uint64_t ignored_ = 0;
  bool writing_ = false;
  bool accepted_ = false;
  bool registered_ = false;
  bool finished_ = false;
};

class HttpSession : public Session, public std::enable_shared_from_this<HttpSession> {
 public:
  HttpSession(tcp::socket&& socket, Core& core) : stream_(std::move(socket)), core_(core) {}

  void run() {
    id_ = core_.track(weak_from_this());
    do_read();
  }

  void close() override { finish(); }

 private:
  void do_read() {
    parser_.emplace();
    parser_->body_limit(kMaxRequestBody);
    stream_.expires_after(kHttpIdleTimeout);
    http::async_read(stream_, buffer_, *parser_,
                     beast::bind_front_handler(&HttpSession::on_read, shared_from_this()));
  }

  void on_read(error_code ec, std::size_t) {
    if (finished_) return;
    if (ec) {  // closed by the client, timed out, or not HTTP
      finish();
      return;
    }
    Request req = parser_->release();
    if (!core_.host_allowed(req)) {
      spdlog::warn("refused {}: Host '{}' is not loopback with port {}",
                   header_text(req.target()), header_text(req[http::field::host]),
                   core_.bound.port());
      send(text_response(req, http::status::forbidden,
                         "forbidden: Host must be this gateway's loopback address and port\n"));
      return;
    }
    const auto segments = decode_target(req.target());
    const bool is_ws = segments && segments->size() == 1 && (*segments)[0] == "ws";
    if (is_ws && websocket::is_upgrade(req)) {
      if (!core_.origin_allowed(req)) {
        spdlog::warn("refused WebSocket from origin '{}' (see --allow-origin)",
                     header_text(req[http::field::origin]));
        send(text_response(req, http::status::forbidden,
                           "forbidden: origin not allowed\n"));
        return;
      }
      stream_.expires_never();
      std::make_shared<WsSession>(stream_.release_socket(), core_)->run(std::move(req));
      finish();
      return;
    }
    send(respond(req, segments));
  }

  void send(http::message_generator msg) {
    keep_alive_ = msg.keep_alive();
    beast::async_write(stream_, std::move(msg),
                       beast::bind_front_handler(&HttpSession::on_write, shared_from_this()));
  }

  // A header value for the log, cut short.
  static std::string header_text(boost::core::string_view v) {
    return std::string(v.substr(0, 120));
  }

  http::message_generator respond(const Request& req,
                                  const std::optional<std::vector<std::string>>& segments) {
    if (!segments) return text_response(req, http::status::bad_request, "bad request\n");
    if (segments->size() == 1 && (*segments)[0] == "healthz") {
      if (req.method() != http::verb::get && req.method() != http::verb::head) {
        return text_response(req, http::status::method_not_allowed,
                             "method not allowed\n", {{http::field::allow, "GET, HEAD"}});
      }
      return text_response(req, http::status::ok, "ok");
    }
    if (segments->size() == 1 && (*segments)[0] == "ws") {
      return text_response(req, http::status::upgrade_required,
                           "websocket upgrade required\n",
                           {{http::field::upgrade, "websocket"}});
    }
    return serve_static(req, *segments, core_.roots);
  }

  void on_write(error_code ec, std::size_t) {
    if (finished_) return;
    if (ec || !keep_alive_) {
      error_code ignored;
      stream_.socket().shutdown(tcp::socket::shutdown_send, ignored);
      finish();
      return;
    }
    do_read();
  }

  void finish() {
    if (finished_) return;
    finished_ = true;
    core_.forget(id_);
    stream_.close();
  }

  beast::tcp_stream stream_;
  Core& core_;
  beast::flat_buffer buffer_;
  std::optional<http::request_parser<http::string_body>> parser_;
  uint64_t id_ = 0;
  bool keep_alive_ = false;
  bool finished_ = false;
};

void Core::do_accept() {
  acceptor.async_accept(io, [this](error_code ec, tcp::socket socket) {
    on_accept(ec, std::move(socket));
  });
}

void Core::on_accept(error_code ec, tcp::socket socket) {
  if (stopping) return;
  if (ec) {
    if (ec == asio::error::operation_aborted) return;
    // Out of file descriptors and the like: try again shortly rather than spin.
    spdlog::warn("accept failed: {}", ec.message());
    accept_retry.expires_after(kAcceptRetry);
    accept_retry.async_wait([this](error_code e) {
      if (!e && !stopping) do_accept();
    });
    return;
  }
  std::make_shared<HttpSession>(std::move(socket), *this)->run();
  do_accept();
}

void Core::arm_poll() {
  poll_timer.expires_after(kPollInterval);
  poll_timer.async_wait([this](error_code ec) {
    if (ec || stopping) return;
    fanout.poll(clock.mono());
    arm_poll();
  });
}

void Core::do_stop() {
  if (stopping) return;
  stopping = true;
  error_code ignored;
  acceptor.close(ignored);
  poll_timer.cancel();
  accept_retry.cancel();
  // close() calls forget(), so work on a copy.
  auto open = std::move(sessions);
  sessions.clear();
  for (auto& [id, weak] : open) {
    if (auto s = weak.lock()) s->close();
  }
}

}  // namespace

std::optional<tcp::endpoint> parse_listen(std::string_view text) {
  std::string_view host;
  std::string_view port;
  bool bracketed = false;
  if (!text.empty() && text.front() == '[') {
    const auto close = text.find(']');
    if (close == std::string_view::npos || close + 1 >= text.size() ||
        text[close + 1] != ':') {
      return std::nullopt;
    }
    host = text.substr(1, close - 1);
    port = text.substr(close + 2);
    bracketed = true;
  } else {
    const auto colon = text.rfind(':');
    if (colon == std::string_view::npos) return std::nullopt;
    host = text.substr(0, colon);
    port = text.substr(colon + 1);
  }
  if (port.empty() || port.size() > 5) return std::nullopt;
  unsigned value = 0;
  for (char c : port) {
    if (c < '0' || c > '9') return std::nullopt;
    value = value * 10 + static_cast<unsigned>(c - '0');
  }
  if (value > 65535) return std::nullopt;
  error_code ec;
  const auto addr = asio::ip::make_address(std::string(host), ec);
  if (ec || addr.is_v6() != bracketed) return std::nullopt;
  return tcp::endpoint(addr, static_cast<unsigned short>(value));
}

bool is_loopback(const asio::ip::address& a) {
  return a.is_v4() ? a.to_v4().is_loopback() : a.to_v6().is_loopback();
}

bool host_is_local(std::string_view host, unsigned short port) {
  std::string h(host);
  for (char& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  std::string_view name;
  std::string_view port_text;
  bool has_port = false;
  bool v6 = false;
  const std::string_view hv(h);
  if (!hv.empty() && hv.front() == '[') {
    const auto close = hv.find(']');
    if (close == std::string_view::npos) return false;
    name = hv.substr(1, close - 1);
    const std::string_view rest = hv.substr(close + 1);
    if (!rest.empty()) {
      if (rest.front() != ':') return false;
      port_text = rest.substr(1);
      has_port = true;
    }
    v6 = true;
  } else {
    const auto colon = hv.rfind(':');
    name = hv.substr(0, colon);
    if (colon != std::string_view::npos) {
      port_text = hv.substr(colon + 1);
      has_port = true;
    }
  }

  unsigned value = 80;  // no port in Host means the default one
  if (has_port) {
    if (port_text.empty() || port_text.size() > 5) return false;
    value = 0;
    for (char c : port_text) {
      if (c < '0' || c > '9') return false;
      value = value * 10 + static_cast<unsigned>(c - '0');
    }
  }
  if (value != port) return false;

  if (!v6 && name == "localhost") return true;
  error_code ec;
  if (v6) {
    const auto a = asio::ip::make_address_v6(std::string(name), ec);
    return !ec && a.is_loopback();
  }
  const auto a = asio::ip::make_address_v4(std::string(name), ec);
  return !ec && a.is_loopback();
}

std::optional<std::string> parse_origin(std::string_view text) {
  std::string o(text);
  for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  std::string_view rest(o);
  if (rest.starts_with("http://")) {
    rest.remove_prefix(7);
  } else if (rest.starts_with("https://")) {
    rest.remove_prefix(8);
  } else {
    return std::nullopt;
  }
  // The authority only: no path (not even "/"), query, fragment, user info
  // or whitespace, any of which would make it never equal a browser's Origin.
  if (rest.empty() || rest.find_first_of("/?#@ \t") != std::string_view::npos) {
    return std::nullopt;
  }
  return o;
}

struct WsServer::Impl : Core {
  using Core::Core;
};

WsServer::WsServer(asio::io_context& io, ServerConfig config, const Clock& clock)
    : impl_(std::make_unique<Impl>(io, std::move(config), clock)) {}

WsServer::~WsServer() = default;

tcp::endpoint WsServer::local_endpoint() const { return impl_->bound; }

void WsServer::start() {
  asio::post(impl_->io, [core = impl_.get()] {
    if (core->stopping) return;
    core->do_accept();
    core->arm_poll();
  });
}

void WsServer::post_sample(std::string_view topic, uint32_t vehicle_id, std::string json,
                           uint64_t t_bridge_ns) {
  asio::post(impl_->io, [core = impl_.get(), topic = std::string(topic), vehicle_id,
                         json = std::move(json), t_bridge_ns]() mutable {
    if (core->stopping) return;
    ++core->counters.samples;
    core->fanout.publish(topic, vehicle_id, std::move(json), core->clock.mono(),
                         t_bridge_ns);
  });
}

void WsServer::stop() {
  asio::post(impl_->io, [core = impl_.get()] { core->do_stop(); });
}

const WsServer::Counters& WsServer::counters() const { return impl_->counters; }

}  // namespace skyrelay::gateway
