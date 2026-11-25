#include <gtest/gtest.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "dds_feed.hpp"
#include "skyrelay/common/clock.hpp"
#include "skyrelay/dds/domain.hpp"
#include "skyrelay/dds/idl.hpp"
#include "skyrelay/dds/qos.hpp"
#include "skyrelay/dds/topics.hpp"
#include "ws_server.hpp"

extern char** environ;

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace fs = std::filesystem;
namespace gw = skyrelay::gateway;
namespace sdds = skyrelay::dds;
namespace v1 = skyrelay::v1;
using tcp = asio::ip::tcp;
using nlohmann::json;
using SteadyClock = std::chrono::steady_clock;
using namespace std::chrono_literals;

uint64_t realtime_ns() {
  timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

void write_file(const fs::path& p, const std::string& content) {
  std::ofstream f(p, std::ios::binary);
  f << content;
}

// The 1000-byte tile used by the range tests: byte i is i % 251.
std::string tile_bytes() {
  std::string s(1000, '\0');
  for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<char>(i % 251);
  return s;
}

// A gateway in this process, put together the way main() does it: the server
// on its own io_context thread, and a DDS feed that posts into it. Each one
// gets its own temp directory and DDS partition.
class TestGateway {
 public:
  TestGateway() {
    static std::atomic<int> n{0};
    const std::string tag = std::to_string(::getpid()) + "-" + std::to_string(n++);
    root_ = fs::temp_directory_path() / ("skyrelay-gateway-test-" + tag);
    fs::create_directories(root_ / "web");
    fs::create_directories(root_ / "data" / "tiles");
    fs::create_directories(root_ / "data" / "assets");
    write_file(root_ / "web" / "index.html", "<html>station</html>");
    write_file(root_ / "data" / "tiles" / "t.bin", tile_bytes());
    write_file(root_ / "secret", "not for the browser");

    dc_.partition = "gateway-test-" + tag;
    gw::ServerConfig cfg;
    cfg.listen = tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0);
    cfg.web_root = root_ / "web";
    cfg.data_dir = root_ / "data";
    cfg.latched_topics = gw::DdsFeed::latched_topics();
    server_.emplace(io_, cfg, clock_);
    server_->start();
    dp_.emplace(sdds::open_participant(dc_));
    feed_.emplace(
        *dp_, dc_,
        [this](std::string_view topic, uint32_t id, std::string j, uint64_t t) {
          server_->post_sample(topic, id, std::move(j), t);
        },
        [this](std::string_view topic, uint32_t id) { server_->post_gone(topic, id); });
    thread_ = std::thread([this] { io_.run(); });
  }

  ~TestGateway() {
    feed_.reset();
    server_->stop();
    thread_.join();
    server_.reset();
    dp_.reset();
    std::error_code ec;
    fs::remove_all(root_, ec);
  }

  tcp::endpoint endpoint() const { return server_->local_endpoint(); }
  const fs::path& root() const { return root_; }
  const sdds::DomainConfig& dds() const { return dc_; }
  const gw::WsServer& server() const { return *server_; }

 private:
  fs::path root_;
  sdds::DomainConfig dc_;
  asio::io_context io_;
  skyrelay::SystemClock clock_;
  std::optional<gw::WsServer> server_;
  std::optional<::dds::domain::DomainParticipant> dp_;
  std::optional<gw::DdsFeed> feed_;
  std::thread thread_;
};

struct PoseWriter {
  ::dds::domain::DomainParticipant dp;
  ::dds::pub::Publisher pub;
  ::dds::topic::Topic<v1::Pose> topic;
  ::dds::pub::DataWriter<v1::Pose> writer;

  explicit PoseWriter(const sdds::DomainConfig& c)
      : dp(sdds::open_participant(c)),
        pub(sdds::make_publisher(dp, c)),
        topic(dp, std::string(sdds::kTopicPose),
              sdds::topic_qos(sdds::Profile::TelemetryFast)),
        writer(pub, topic, sdds::writer_qos(sdds::Profile::TelemetryFast)) {}

  // A reader only gets what is written after it matched.
  bool wait_matched(std::chrono::milliseconds timeout) {
    const auto end = SteadyClock::now() + timeout;
    while (SteadyClock::now() < end) {
      if (!::dds::pub::matched_subscriptions(writer).empty()) return true;
      std::this_thread::sleep_for(10ms);
    }
    return false;
  }

  void write(uint32_t vehicle_id, double lat) {
    v1::Pose p;
    p.vehicle_id(vehicle_id);
    p.lat_deg(lat);
    p.header().t_bridge_ns(realtime_ns());
    writer.write(p);
  }
};

// A link_status writer with the contract's state_latched QoS, the way the
// bridge publishes it. Deleting it is what a bridge that exits does. With
// autodispose off, deleting it leaves its instances without a writer instead
// of disposed, which is what a bridge that crashes looks like once its
// liveliness runs out.
struct LinkWriter {
  ::dds::domain::DomainParticipant dp;
  ::dds::pub::Publisher pub;
  ::dds::topic::Topic<v1::LinkStatus> topic;
  ::dds::pub::DataWriter<v1::LinkStatus> writer;

  LinkWriter(const sdds::DomainConfig& c, bool autodispose)
      : dp(sdds::open_participant(c)),
        pub(sdds::make_publisher(dp, c)),
        topic(dp, std::string(sdds::kTopicLinkStatus),
              sdds::topic_qos(sdds::Profile::StateLatched)),
        writer(pub, topic,
               sdds::writer_qos(sdds::Profile::StateLatched)
                   << ::dds::core::policy::WriterDataLifecycle(autodispose)) {}

  bool wait_matched(std::chrono::milliseconds timeout) {
    const auto end = SteadyClock::now() + timeout;
    while (SteadyClock::now() < end) {
      if (!::dds::pub::matched_subscriptions(writer).empty()) return true;
      std::this_thread::sleep_for(10ms);
    }
    return false;
  }

  void write(uint32_t vehicle_id) {
    v1::LinkStatus l;
    l.vehicle_id(vehicle_id);
    l.state(v1::LinkState::LINK_CONNECTED);
    l.header().t_bridge_ns(realtime_ns());
    writer.write(l);
  }
};

struct HttpReply {
  unsigned status = 0;
  http::fields headers;
  std::string body;

  std::string header(http::field f) const { return std::string(headers[f]); }
};

using Headers = std::vector<std::pair<http::field, std::string>>;

// What a browser that loaded the station from the gateway sends as Host.
std::string host_of(const tcp::endpoint& ep) {
  return "127.0.0.1:" + std::to_string(ep.port());
}

// The headers of a WebSocket upgrade request, for checking how the gateway
// answers one without completing the handshake.
Headers upgrade_headers() {
  return {{http::field::upgrade, "websocket"},
          {http::field::connection, "Upgrade"},
          {http::field::sec_websocket_key, "dGhlIHNhbXBsZSBub25jZQ=="},
          {http::field::sec_websocket_version, "13"}};
}

// One request on a fresh connection, bounded by a 5 s timeout. Host is the
// gateway's own unless `extra` sets it; an empty value in `extra` removes
// that header.
HttpReply http_request(const tcp::endpoint& ep, http::verb verb,
                       const std::string& target, const std::string& range = {},
                       const Headers& extra = {}) {
  asio::io_context io;
  beast::tcp_stream stream(io);
  stream.expires_after(5s);
  http::request<http::empty_body> req{verb, target, 11};
  req.set(http::field::host, host_of(ep));
  if (!range.empty()) req.set(http::field::range, range);
  for (const auto& [field, value] : extra) {
    if (value.empty()) {
      req.erase(field);
    } else {
      req.set(field, value);
    }
  }
  beast::flat_buffer buf;
  http::response_parser<http::string_body> parser;
  parser.body_limit(16 * 1024 * 1024);
  if (verb == http::verb::head) parser.skip(true);
  beast::error_code ec;
  stream.async_connect(ep, [&](beast::error_code e) {
    if (e) {
      ec = e;
      return;
    }
    http::async_write(stream, req, [&](beast::error_code e2, size_t) {
      if (e2) {
        ec = e2;
        return;
      }
      http::async_read(stream, buf, parser,
                       [&](beast::error_code e3, size_t) { ec = e3; });
    });
  });
  io.run();
  if (ec) throw std::runtime_error(target + ": " + ec.message());
  auto res = parser.release();
  HttpReply r;
  r.status = res.result_int();
  r.headers = res.base();
  r.body = std::move(res.body());
  return r;
}

// WebSocket client. Messages are read on a background thread and stamped on
// arrival; parsing happens on the test thread, so it never delays a stamp.
class WsClient {
 public:
  struct Msg {
    json body;
    SteadyClock::time_point at;
  };

  // receive_buffer > 0 shrinks the socket's receive buffer before connecting;
  // a non-empty origin is sent as the Origin header, as a browser would.
  explicit WsClient(const tcp::endpoint& ep, int receive_buffer = 0,
                    const std::string& origin = {})
      : ws_(io_) {
    if (!origin.empty()) {
      ws_.set_option(websocket::stream_base::decorator(
          [origin](websocket::request_type& req) { req.set(http::field::origin, origin); }));
    }
    auto& stream = beast::get_lowest_layer(ws_);
    stream.socket().open(ep.protocol());
    if (receive_buffer > 0) {
      stream.socket().set_option(asio::socket_base::receive_buffer_size(receive_buffer));
    }
    beast::error_code ec;
    stream.expires_after(5s);
    stream.async_connect(ep, [&](beast::error_code e) {
      if (e) {
        ec = e;
        return;
      }
      ws_.async_handshake(host_of(ep), "/ws", [&](beast::error_code e2) { ec = e2; });
    });
    io_.run();
    io_.restart();
    if (ec) throw std::runtime_error("websocket connect: " + ec.message());
    stream.expires_never();
    ws_.text(true);
  }

  ~WsClient() {
    if (thread_.joinable()) {
      asio::post(io_, [this] {
        beast::error_code ignored;
        beast::get_lowest_layer(ws_).socket().close(ignored);
      });
      thread_.join();
    } else {
      beast::error_code ignored;
      beast::get_lowest_layer(ws_).socket().close(ignored);
    }
  }

  void start_reading() {
    do_read();
    thread_ = std::thread([this] { io_.run(); });
  }

  // The next message, or nullopt after the timeout.
  std::optional<Msg> next(std::chrono::milliseconds timeout) {
    std::unique_lock lk(mu_);
    if (!cv_.wait_for(lk, timeout, [this] { return !raw_.empty() || closed_; }) ||
        raw_.empty()) {
      return std::nullopt;
    }
    auto [text, at] = std::move(raw_.front());
    raw_.pop_front();
    lk.unlock();
    return Msg{json::parse(text), at};
  }

  // Skips messages until one satisfies pred.
  std::optional<Msg> wait_for(const std::function<bool(const json&)>& pred,
                              std::chrono::milliseconds timeout) {
    const auto end = SteadyClock::now() + timeout;
    while (SteadyClock::now() < end) {
      auto m = next(std::chrono::duration_cast<std::chrono::milliseconds>(
          end - SteadyClock::now()));
      if (!m) return std::nullopt;
      if (pred(m->body)) return m;
    }
    return std::nullopt;
  }

  // Waits for a frame carrying a sample that satisfies pred. Samples always
  // arrive as an array; the returned body is the matching sample.
  std::optional<Msg> wait_for_sample(const std::function<bool(const json&)>& pred,
                                     std::chrono::milliseconds timeout) {
    std::optional<Msg> hit;
    auto frame = wait_for(
        [&](const json& j) {
          if (!j.is_array()) return false;
          for (const auto& s : j) {
            if (pred(s)) {
              hit = Msg{s, {}};
              return true;
            }
          }
          return false;
        },
        timeout);
    if (!frame) return std::nullopt;
    hit->at = frame->at;
    return hit;
  }

  bool send(const std::string& text) {
    std::promise<beast::error_code> done;
    auto result = done.get_future();
    asio::post(io_, [&] {
      ws_.async_write(asio::buffer(text), [&](beast::error_code ec, size_t) {
        done.set_value(ec);
      });
    });
    return result.wait_for(5s) == std::future_status::ready && !result.get();
  }

 private:
  void do_read() {
    ws_.async_read(buf_, [this](beast::error_code ec, size_t) {
      if (ec) {
        std::lock_guard lk(mu_);
        closed_ = true;
        cv_.notify_all();
        return;
      }
      const auto at = SteadyClock::now();
      std::string text = beast::buffers_to_string(buf_.data());
      buf_.consume(buf_.size());
      {
        std::lock_guard lk(mu_);
        raw_.emplace_back(std::move(text), at);
      }
      cv_.notify_all();
      do_read();
    });
  }

  asio::io_context io_;
  websocket::stream<beast::tcp_stream> ws_;
  beast::flat_buffer buf_;
  std::thread thread_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::pair<std::string, SteadyClock::time_point>> raw_;
  bool closed_ = false;
};

// The real skyrelay-gateway executable, with stdout and stderr captured.
class GatewayProcess {
 public:
  explicit GatewayProcess(const std::vector<std::string>& args) {
    int fds[2];
    if (::pipe(fds) != 0) throw std::runtime_error("pipe failed");
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 2);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    posix_spawn_file_actions_addclose(&fa, fds[1]);
    std::vector<std::string> owned{SKYRELAY_GATEWAY_EXE};
    owned.insert(owned.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& a : owned) argv.push_back(a.data());
    argv.push_back(nullptr);
    const int rc = posix_spawn(&pid_, owned[0].c_str(), &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    ::close(fds[1]);
    fd_ = fds[0];
    if (rc != 0) {
      ::close(fd_);
      throw std::runtime_error("posix_spawn failed");
    }
  }

  ~GatewayProcess() {
    if (!reaped_) {
      ::kill(pid_, SIGKILL);
      int status = 0;
      ::waitpid(pid_, &status, 0);
    }
    if (fd_ >= 0) ::close(fd_);
  }

  GatewayProcess(const GatewayProcess&) = delete;
  GatewayProcess& operator=(const GatewayProcess&) = delete;

  pid_t pid() const { return pid_; }
  const std::string& output() const { return out_; }

  // Reads output until it contains `needle`; false on timeout or EOF first.
  bool read_until(const std::string& needle, std::chrono::milliseconds timeout) {
    const auto end = SteadyClock::now() + timeout;
    while (out_.find(needle) == std::string::npos) {
      if (SteadyClock::now() >= end || !read_some(100)) return false;
    }
    return true;
  }

  // Waits for the process to exit, reading its output meanwhile so it never
  // blocks on a full pipe. The wait status, or nullopt on timeout.
  std::optional<int> wait_exit(std::chrono::milliseconds timeout) {
    const auto end = SteadyClock::now() + timeout;
    while (SteadyClock::now() < end) {
      read_some(10);
      int status = 0;
      if (::waitpid(pid_, &status, WNOHANG) == pid_) {
        reaped_ = true;
        while (read_some(0)) {
        }
        return status;
      }
    }
    return std::nullopt;
  }

 private:
  // False at EOF or on error; true otherwise, also when nothing came.
  bool read_some(int timeout_ms) {
    if (fd_ < 0) return false;
    pollfd p{fd_, POLLIN, 0};
    if (::poll(&p, 1, timeout_ms) <= 0) return true;
    char buf[4096];
    const ssize_t n = ::read(fd_, buf, sizeof buf);
    if (n <= 0) {
      ::close(fd_);
      fd_ = -1;
      return false;
    }
    out_.append(buf, static_cast<size_t>(n));
    return true;
  }

  pid_t pid_ = -1;
  int fd_ = -1;
  std::string out_;
  bool reaped_ = false;
};

bool is_pose_of(const json& s, uint32_t vehicle_id) {
  return s.value("topic", "") == "pose" && s.contains("data") &&
         s["data"].value("vehicle_id", 0u) == vehicle_id;
}

bool is_link_of(const json& s, uint32_t vehicle_id) {
  return s.value("type", "") == "sample" && s.value("topic", "") == "link_status" &&
         s.contains("data") && s["data"].value("vehicle_id", 0u) == vehicle_id;
}

bool is_gone_of(const json& s, const std::string& topic, uint32_t vehicle_id) {
  return s.value("type", "") == "gone" && s.value("topic", "") == topic &&
         s.value("vehicle_id", 0u) == vehicle_id;
}

}  // namespace

TEST(Gateway, ServesRangeWith206) {
  TestGateway gw;
  const auto ep = gw.endpoint();
  const std::string tile = tile_bytes();

  auto r = http_request(ep, http::verb::get, "/tiles/t.bin", "bytes=10-19");
  EXPECT_EQ(r.status, 206u);
  EXPECT_EQ(r.header(http::field::content_range), "bytes 10-19/1000");
  EXPECT_EQ(r.header(http::field::content_length), "10");
  EXPECT_EQ(r.header(http::field::accept_ranges), "bytes");
  EXPECT_EQ(r.body, tile.substr(10, 10));

  auto h = http_request(ep, http::verb::head, "/tiles/t.bin");
  EXPECT_EQ(h.status, 200u);
  EXPECT_EQ(h.header(http::field::accept_ranges), "bytes");
  EXPECT_EQ(h.header(http::field::content_length), "1000");
  EXPECT_TRUE(h.body.empty());

  auto u = http_request(ep, http::verb::get, "/tiles/t.bin", "bytes=2000-");
  EXPECT_EQ(u.status, 416u);
  EXPECT_EQ(u.header(http::field::content_range), "bytes */1000");

  // The rest of what the PMTiles reader relies on.
  auto full = http_request(ep, http::verb::get, "/tiles/t.bin");
  EXPECT_EQ(full.status, 200u);
  EXPECT_EQ(full.body, tile);
  auto suffix = http_request(ep, http::verb::get, "/tiles/t.bin", "bytes=-100");
  EXPECT_EQ(suffix.status, 206u);
  EXPECT_EQ(suffix.header(http::field::content_range), "bytes 900-999/1000");
  EXPECT_EQ(suffix.body, tile.substr(900));
  auto multi = http_request(ep, http::verb::get, "/tiles/t.bin", "bytes=0-1,5-6");
  EXPECT_EQ(multi.status, 200u);
  EXPECT_EQ(multi.body.size(), 1000u);
  // HEAD ignores Range: range handling is only defined for GET.
  auto head_range = http_request(ep, http::verb::head, "/tiles/t.bin", "bytes=0-9");
  EXPECT_EQ(head_range.status, 200u);
  EXPECT_EQ(head_range.header(http::field::content_length), "1000");
}

TEST(Gateway, RejectsTraversal) {
  TestGateway gw;
  const auto ep = gw.endpoint();
  EXPECT_EQ(http_request(ep, http::verb::get, "/tiles/../secret").status, 400u);
  EXPECT_EQ(http_request(ep, http::verb::get, "/../secret").status, 400u);
  EXPECT_EQ(http_request(ep, http::verb::get, "/tiles/%2e%2e/secret").status, 400u);
  EXPECT_EQ(http_request(ep, http::verb::get, "/assets/..%2f..%2fsecret").status, 400u);
  EXPECT_EQ(http_request(ep, http::verb::get, "/tiles/t.bin%00.png").status, 400u);
  // A symlink that leads out of the served directory is refused as well.
  fs::create_symlink(gw.root() / "secret", gw.root() / "data" / "tiles" / "out.bin");
  EXPECT_EQ(http_request(ep, http::verb::get, "/tiles/out.bin").status, 400u);
  fs::create_directory_symlink(gw.root(), gw.root() / "web" / "up");
  EXPECT_EQ(http_request(ep, http::verb::get, "/up/secret").status, 400u);
}

TEST(Gateway, ServesSpaAndHealth) {
  TestGateway gw;
  const auto ep = gw.endpoint();
  auto root = http_request(ep, http::verb::get, "/");
  EXPECT_EQ(root.status, 200u);
  EXPECT_EQ(root.body, "<html>station</html>");
  EXPECT_EQ(root.header(http::field::content_type), "text/html; charset=utf-8");
  // A client-side route falls back to index.html ...
  auto route = http_request(ep, http::verb::get, "/fleet/3?x=1");
  EXPECT_EQ(route.status, 200u);
  EXPECT_EQ(route.body, "<html>station</html>");
  // ... a missing asset does not, and map data never does.
  EXPECT_EQ(http_request(ep, http::verb::get, "/missing.js").status, 404u);
  EXPECT_EQ(http_request(ep, http::verb::get, "/tiles/missing").status, 404u);
  EXPECT_EQ(http_request(ep, http::verb::get, "/assets/fonts/x").status, 404u);
  EXPECT_EQ(http_request(ep, http::verb::get, "/tiles").status, 404u);

  auto health = http_request(ep, http::verb::get, "/healthz");
  EXPECT_EQ(health.status, 200u);
  EXPECT_EQ(health.body, "ok");
  EXPECT_EQ(health.header(http::field::content_type), "text/plain; charset=utf-8");
  auto head = http_request(ep, http::verb::head, "/healthz");
  EXPECT_EQ(head.status, 200u);
  EXPECT_TRUE(head.body.empty());

  auto post = http_request(ep, http::verb::post, "/");
  EXPECT_EQ(post.status, 405u);
  EXPECT_EQ(post.header(http::field::allow), "GET, HEAD");
  EXPECT_EQ(http_request(ep, http::verb::get, "/ws").status, 426u);
}

TEST(Gateway, StreamsPoseJson) {
  TestGateway gw;
  WsClient c(gw.endpoint());
  c.start_reading();
  auto hello = c.next(5s);
  ASSERT_TRUE(hello);
  EXPECT_EQ(hello->body["type"], "hello");
  EXPECT_EQ(hello->body["version"], "0.1.0-preview");
  EXPECT_EQ(hello->body["contract"], "1.0.0-preview.2");
  EXPECT_EQ(hello->body["insecure"], true);
  EXPECT_GT(hello->body["server_time_ns"].get<uint64_t>(), 0u);

  PoseWriter w(gw.dds());
  ASSERT_TRUE(w.wait_matched(5s));
  w.write(2, 47.397742);
  auto s = c.wait_for_sample([](const json& j) { return is_pose_of(j, 2); }, 5s);
  ASSERT_TRUE(s);
  EXPECT_EQ(s->body["type"], "sample");
  EXPECT_EQ(s->body["topic"], "pose");
  EXPECT_EQ(s->body["data"]["vehicle_id"], 2);
  EXPECT_DOUBLE_EQ(s->body["data"]["lat_deg"].get<double>(), 47.397742);
  ASSERT_TRUE(s->body["age_ms"].is_number());
  EXPECT_GE(s->body["age_ms"].get<double>(), 0.0);
  EXPECT_LT(s->body["age_ms"].get<double>(), 5000.0);
}

// A bridge that goes away must not be replayed as if it were still there.
// Vehicle 5's writer is deleted, which disposes its instance; vehicle 6's
// writer has autodispose off, so its instance is left without a writer. A
// client that was connected gets a gone notice for each, and a client that
// connects afterwards gets no link_status for either.
TEST(Gateway, ForgetsLinkStatusOfAWriterThatWentAway) {
  TestGateway gw;
  WsClient early(gw.endpoint());
  early.start_reading();
  ASSERT_TRUE(early.next(5s));  // hello

  auto w5 = std::make_unique<LinkWriter>(gw.dds(), true);
  auto w6 = std::make_unique<LinkWriter>(gw.dds(), false);
  ASSERT_TRUE(w5->wait_matched(5s));
  ASSERT_TRUE(w6->wait_matched(5s));
  w5->write(5);
  w6->write(6);
  ASSERT_TRUE(early.wait_for_sample([](const json& s) { return is_link_of(s, 5); }, 5s));
  ASSERT_TRUE(early.wait_for_sample([](const json& s) { return is_link_of(s, 6); }, 5s));

  // While the writers are there, a new client gets the latched values.
  {
    WsClient replayed(gw.endpoint());
    replayed.start_reading();
    ASSERT_TRUE(replayed.next(5s));  // hello
    EXPECT_TRUE(replayed.wait_for_sample([](const json& s) { return is_link_of(s, 5); }, 5s));
  }

  w5.reset();
  w6.reset();
  EXPECT_TRUE(early.wait_for_sample(
      [](const json& s) { return is_gone_of(s, "link_status", 5); }, 5s))
      << "no gone notice for the disposed instance";
  EXPECT_TRUE(early.wait_for_sample(
      [](const json& s) { return is_gone_of(s, "link_status", 6); }, 5s))
      << "no gone notice for the instance without a writer";

  WsClient late(gw.endpoint());
  late.start_reading();
  auto hello = late.next(5s);
  ASSERT_TRUE(hello);
  EXPECT_EQ(hello->body["type"], "hello");
  EXPECT_FALSE(late.wait_for([](const json& j) { return j.is_array(); }, 1s))
      << "a client that joined after the writers left got a replay";
}

TEST(Gateway, SlowClientDoesNotStallOthers) {
  // A slow browser must never stall the others. Client A completes the
  // handshake and never reads again; its receive buffer is tiny, so the
  // gateway's writes to it back up. 1999 poses for distinct vehicles go out
  // first (about 1 MB of JSON for each client, far more than A's socket
  // buffers hold), then the 2000th write. Client B must get that last pose
  // within 200 ms of the write.
  TestGateway gw;
  WsClient a(gw.endpoint(), 4096);
  WsClient b(gw.endpoint());
  b.start_reading();
  ASSERT_TRUE(b.next(5s));  // hello

  PoseWriter w(gw.dds());
  ASSERT_TRUE(w.wait_matched(5s));
  for (uint32_t v = 1; v <= 1999; ++v) w.write(v, 47.0);
  ASSERT_TRUE(b.wait_for_sample([](const json& s) { return is_pose_of(s, 1999); }, 20s))
      << "client B did not get the first 1999 poses";
  // B is idle now, so an outstanding write can only be A's: A is stalled.
  EXPECT_GE(gw.server().counters().ws_writes_in_flight.load(), 1u);
  EXPECT_EQ(gw.server().counters().ws_clients.load(), 2u);

  const auto t_last_write = SteadyClock::now();
  w.write(2000, 47.5);
  auto last = b.wait_for_sample([](const json& s) { return is_pose_of(s, 2000); }, 5s);
  ASSERT_TRUE(last) << "client B never got the last pose";
  const auto delay =
      std::chrono::duration_cast<std::chrono::milliseconds>(last->at - t_last_write);
  ::testing::Test::RecordProperty("last_pose_delay_ms", static_cast<int>(delay.count()));
  EXPECT_LT(delay, 200ms) << "client B got the last pose " << delay.count()
                          << " ms after it was written";
}

TEST(Gateway, PingPong) {
  TestGateway gw;
  WsClient c(gw.endpoint());
  c.start_reading();
  ASSERT_TRUE(c.next(5s));  // hello
  // Anything that is not a ping is ignored and counted, and the connection
  // stays up.
  ASSERT_TRUE(c.send(R"({"type":"command_request","vehicle_id":1})"));
  ASSERT_TRUE(c.send("not json"));
  ASSERT_TRUE(c.send(R"({"type":"ping","id":7,"t_client_ms":1234.5})"));
  auto pong = c.wait_for([](const json& j) { return j.is_object() && j.value("type", "") == "pong"; },
                         5s);
  ASSERT_TRUE(pong);
  EXPECT_EQ(pong->body["id"], 7);
  EXPECT_DOUBLE_EQ(pong->body["t_client_ms"].get<double>(), 1234.5);
  EXPECT_GT(pong->body["server_time_ns"].get<uint64_t>(), 0u);
  EXPECT_EQ(gw.server().counters().ignored_client_messages.load(), 2u);
}

TEST(Gateway, RefusesNonLoopbackListen) {
  const std::string dir = fs::temp_directory_path().string();
  for (const std::string listen : {"0.0.0.0:0", "[::]:0", "192.0.2.1:8080"}) {
    GatewayProcess g({"--listen", listen, "--web-root", dir, "--data-dir", dir});
    const auto status = g.wait_exit(10s);
    ASSERT_TRUE(status) << listen << ": gateway did not exit\n" << g.output();
    ASSERT_TRUE(WIFEXITED(*status)) << listen;
    EXPECT_EQ(WEXITSTATUS(*status), 2) << listen << "\n" << g.output();
    EXPECT_NE(g.output().find(
                  "refusing non-loopback listen address without TLS (arrives in M3, v0.3)"),
              std::string::npos)
        << g.output();
  }
}

// DNS rebinding: a page on evil.example resolves its own name to 127.0.0.1,
// and the browser then sends its requests here with Host: evil.example.
TEST(Gateway, RejectsForeignHost) {
  TestGateway gw;
  const auto ep = gw.endpoint();
  const std::string port = std::to_string(ep.port());
  for (const std::string& host : std::vector<std::string>
       {"evil.example:" + port, "evil.example", "localhost.evil.example:" + port,
        "127.0.0.1",  // no port means 80
        "127.0.0.1:1", "127.0.0.1:" + port + "0", "0.0.0.0:" + port, "[::2]:" + port,
        "[::1]" + port}) {
    auto r = http_request(ep, http::verb::get, "/healthz", {}, {{http::field::host, host}});
    EXPECT_EQ(r.status, 403u) << "Host: " << host;
    EXPECT_EQ(r.header(http::field::content_type), "text/plain; charset=utf-8");
  }
  // No Host at all.
  EXPECT_EQ(http_request(ep, http::verb::get, "/healthz", {}, {{http::field::host, ""}}).status,
            403u);
  // The same for static files and for the WebSocket, whatever the Origin.
  EXPECT_EQ(http_request(ep, http::verb::get, "/tiles/t.bin", {},
                         {{http::field::host, "evil.example:" + port}})
                .status,
            403u);
  auto up = upgrade_headers();
  up.emplace_back(http::field::host, "evil.example:" + port);
  up.emplace_back(http::field::origin, "http://127.0.0.1:" + port);
  EXPECT_EQ(http_request(ep, http::verb::get, "/ws", {}, up).status, 403u);
}

TEST(Gateway, AcceptsLocalhostHost) {
  TestGateway gw;
  const auto ep = gw.endpoint();
  const std::string port = std::to_string(ep.port());
  for (const std::string& host : std::vector<std::string> {"localhost:" + port, "LocalHost:" + port, "127.0.0.1:" + port,
                                 "127.0.0.2:" + port, "[::1]:" + port}) {
    auto r = http_request(ep, http::verb::get, "/healthz", {}, {{http::field::host, host}});
    EXPECT_EQ(r.status, 200u) << "Host: " << host;
    EXPECT_EQ(r.body, "ok") << "Host: " << host;
  }
}

// A WebSocket is not covered by the same-origin policy: without this check
// any page the operator visits could read live positions.
TEST(Gateway, RejectsForeignOrigin) {
  TestGateway gw;
  const auto ep = gw.endpoint();
  const std::string port = std::to_string(ep.port());
  for (const std::string& origin : std::vector<std::string>
       {"http://evil.example", "null", "http://127.0.0.1:1", "https://127.0.0.1:" + port,
        "http://127.0.0.1:" + port + "/", "http://localhost.evil.example:" + port}) {
    auto up = upgrade_headers();
    up.emplace_back(http::field::origin, origin);
    auto r = http_request(ep, http::verb::get, "/ws", {}, up);
    EXPECT_EQ(r.status, 403u) << "Origin: " << origin;
  }
  EXPECT_THROW(WsClient(ep, 0, "http://evil.example"), std::runtime_error);
  EXPECT_EQ(gw.server().counters().ws_clients.load(), 0u);
}

TEST(Gateway, AcceptsOwnOrigin) {
  TestGateway gw;
  const auto ep = gw.endpoint();
  const std::string port = std::to_string(ep.port());
  // Scheme and host compare case-insensitively.
  for (const std::string& origin : std::vector<std::string>
       {"http://127.0.0.1:" + port, "http://localhost:" + port, "HTTP://LocalHost:" + port,
        "http://[::1]:" + port}) {
    WsClient c(ep, 0, origin);
    c.start_reading();
    auto hello = c.next(5s);
    ASSERT_TRUE(hello) << "Origin: " << origin;
    EXPECT_EQ(hello->body["type"], "hello");
  }
  // No Origin: not a browser, so there is no page to guard against.
  WsClient tool(ep);
  tool.start_reading();
  ASSERT_TRUE(tool.next(5s));
}

TEST(Gateway, AcceptsAllowedOrigin) {
  // Through the real executable, so the --allow-origin flag is covered too.
  const std::string dir = fs::temp_directory_path().string();
  {
    GatewayProcess bad({"--listen", "127.0.0.1:0", "--web-root", dir, "--data-dir", dir,
                        "--allow-origin", "http://localhost:5173/"});
    const auto status = bad.wait_exit(10s);
    ASSERT_TRUE(status) << bad.output();
    EXPECT_TRUE(WIFEXITED(*status) && WEXITSTATUS(*status) == 2) << bad.output();
  }

  GatewayProcess g({"--listen", "127.0.0.1:0", "--web-root", dir, "--data-dir", dir,
                    "--partition", "gateway-test-origin-" + std::to_string(::getpid()),
                    "--allow-origin", "http://LOCALHOST:5173", "--allow-origin",
                    "https://station.example"});
  // The line ends with " (web root ...", so the port is complete once that
  // has arrived.
  ASSERT_TRUE(g.read_until("(web root", 10s)) << g.output();
  const std::string marker = "listening on http://127.0.0.1:";
  const auto at = g.output().find(marker);
  ASSERT_NE(at, std::string::npos) << g.output();
  const auto port = static_cast<unsigned short>(std::stoul(g.output().substr(at + marker.size())));
  const tcp::endpoint ep(asio::ip::make_address("127.0.0.1"), port);

  WsClient dev_server(ep, 0, "http://localhost:5173");
  dev_server.start_reading();
  auto hello = dev_server.next(5s);
  ASSERT_TRUE(hello);
  EXPECT_EQ(hello->body["type"], "hello");
  WsClient other(ep, 0, "https://station.example");
  other.start_reading();
  ASSERT_TRUE(other.next(5s));

  // Only what is listed: another port of the same host is still foreign.
  auto up = upgrade_headers();
  up.emplace_back(http::field::origin, "http://localhost:5174");
  EXPECT_EQ(http_request(ep, http::verb::get, "/ws", {}, up).status, 403u);

  // And it shuts down promptly with clients still connected. The bound is on
  // the gateway's own measurement, from the signal being handled to the
  // participant being gone; the wall time around it also covers process exit,
  // which a sanitizer can stretch well past a second.
  ASSERT_EQ(::kill(g.pid(), SIGTERM), 0);
  const auto status = g.wait_exit(10s);
  ASSERT_TRUE(status) << g.output();
  EXPECT_TRUE(WIFEXITED(*status) && WEXITSTATUS(*status) == 0) << g.output();
  const std::string stopped = "stopped in ";
  const auto at_stopped = g.output().rfind(stopped);
  ASSERT_NE(at_stopped, std::string::npos) << g.output();
  const auto took_ms = std::stol(g.output().substr(at_stopped + stopped.size()));
  EXPECT_LT(took_ms, 1000) << g.output();
}
