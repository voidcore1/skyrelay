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
    feed_.emplace(*dp_, dc_,
                  [this](std::string_view topic, uint32_t id, std::string j,
                         uint64_t t) { server_->post_sample(topic, id, std::move(j), t); });
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

struct HttpReply {
  unsigned status = 0;
  http::fields headers;
  std::string body;

  std::string header(http::field f) const { return std::string(headers[f]); }
};

// One request on a fresh connection, bounded by a 5 s timeout.
HttpReply http_request(const tcp::endpoint& ep, http::verb verb,
                       const std::string& target, const std::string& range = {}) {
  asio::io_context io;
  beast::tcp_stream stream(io);
  stream.expires_after(5s);
  http::request<http::empty_body> req{verb, target, 11};
  req.set(http::field::host, "127.0.0.1");
  if (!range.empty()) req.set(http::field::range, range);
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

  // receive_buffer > 0 shrinks the socket's receive buffer before connecting.
  explicit WsClient(const tcp::endpoint& ep, int receive_buffer = 0) : ws_(io_) {
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
      ws_.async_handshake("127.0.0.1", "/ws", [&](beast::error_code e2) { ec = e2; });
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

bool is_pose_of(const json& s, uint32_t vehicle_id) {
  return s.value("topic", "") == "pose" && s.contains("data") &&
         s["data"].value("vehicle_id", 0u) == vehicle_id;
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
  EXPECT_EQ(hello->body["contract"], "1.0.0-preview.1");
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

TEST(Gateway, SlowClientDoesNotStallOthers) {
  // Review Focus 4. Client A completes the handshake and never reads again;
  // its receive buffer is tiny, so the gateway's writes to it back up. 1999
  // poses for distinct vehicles go out first (about 1 MB of JSON for each
  // client, far more than A's socket buffers hold), then the 2000th write.
  // Client B must get that last pose within 200 ms of the write.
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
  const fs::path dir = fs::temp_directory_path();
  for (const std::string listen : {"0.0.0.0:0", "[::]:0", "192.0.2.1:8080"}) {
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 2);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    posix_spawn_file_actions_addclose(&fa, fds[1]);
    std::string exe = SKYRELAY_GATEWAY_EXE;
    std::string a1 = "--listen", a2 = listen, a3 = "--web-root", a4 = dir.string(),
                a5 = "--data-dir", a6 = dir.string();
    char* argv[] = {exe.data(), a1.data(), a2.data(), a3.data(), a4.data(),
                    a5.data(), a6.data(), nullptr};
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, exe.c_str(), &fa, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    ::close(fds[1]);
    ASSERT_EQ(rc, 0);

    // Read until the child closes its end; give up after 10 s.
    std::string out;
    const auto end = SteadyClock::now() + 10s;
    char buf[512];
    while (SteadyClock::now() < end) {
      pollfd p{fds[0], POLLIN, 0};
      if (::poll(&p, 1, 100) <= 0) continue;
      const ssize_t n = ::read(fds[0], buf, sizeof buf);
      if (n <= 0) break;
      out.append(buf, static_cast<size_t>(n));
    }
    ::close(fds[0]);
    int status = 0;
    pid_t reaped = 0;
    while ((reaped = ::waitpid(pid, &status, WNOHANG)) == 0 && SteadyClock::now() < end) {
      std::this_thread::sleep_for(10ms);
    }
    if (reaped == 0) {
      ::kill(pid, SIGKILL);
      ::waitpid(pid, &status, 0);
      ADD_FAILURE() << listen << ": gateway did not exit";
      continue;
    }
    ASSERT_TRUE(WIFEXITED(status)) << listen;
    EXPECT_EQ(WEXITSTATUS(status), 2) << listen << "\n" << out;
    EXPECT_NE(out.find("refusing non-loopback listen address without TLS (arrives in v0.3)"),
              std::string::npos)
        << out;
  }
}
