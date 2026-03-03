/**
 * ping_pong.cpp — Round-trip latency benchmark.
 *
 * Run in two terminals:
 *   ./ping_pong publisher
 *   ./ping_pong subscriber
 */

// ── Standard + system headers BEFORE any axon header ─────────────────────────
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// ── Axon IPC library ──────────────────────────────────────────────────────────
#include <axon/axon.hpp>

using namespace std::chrono_literals;
using namespace axon;

static constexpr std::string_view kSegment = "ping_pong";
static constexpr std::size_t kRounds = 10'000;

struct Ping
{
  int64_t send_ns{};
};

static volatile sig_atomic_t g_running = 1;

int run_publisher()
{
  auto node = Node::create(kSegment);
  if (!node)
  {
    std::fprintf(stderr, "[pub] Node::create failed: %s\n", describe(node.error()).data());
    return 1;
  }

  Publisher<Ping> pub{*node, "ping"};
  std::fprintf(stdout, "[pub] Waiting 2 seconds for subscriber to join...\n");
  std::this_thread::sleep_for(2s);
  std::fprintf(stdout, "[pub] Ready — sending %zu pings...\n", kRounds);

  std::signal(SIGINT, [](int) { g_running = 0; });

  for (std::size_t i = 0; i < kRounds && g_running; ++i)
  {
    Ping *p = pub.loan();
    if (!p)
    {
      std::this_thread::sleep_for(1ms);
      continue;
    }
    p->send_ns = now_ns();
    pub.publish(p);
    std::this_thread::sleep_for(std::chrono::microseconds{100});
  }

  std::fprintf(stdout, "[pub] Done. Published %llu pings, dropped %llu.\n", static_cast<unsigned long long>(pub.published()), static_cast<unsigned long long>(pub.dropped()));
  return 0;
}

int run_subscriber()
{
  auto node = Node::join(kSegment);
  if (!node)
  {
    std::fprintf(stderr, "[sub] Node::join failed: %s\n", describe(node.error()).data());
    return 1;
  }

  Subscriber<Ping> sub{*node, "ping"};
  std::fprintf(stdout, "[sub] Listening...\n");

  std::vector<int64_t> latencies;
  latencies.reserve(kRounds);

  std::signal(SIGINT, [](int) { g_running = 0; });

  while (g_running && latencies.size() < kRounds)
  {
    const Ping *p = sub.take(500ms);
    if (!p)
      continue;
    const int64_t lat = now_ns() - p->send_ns;
    latencies.push_back(lat);
    sub.release(p);
  }

  if (latencies.empty())
  {
    std::fprintf(stdout, "[sub] No data received.\n");
    return 0;
  }

  std::sort(latencies.begin(), latencies.end());
  const double mean = static_cast<double>(std::accumulate(latencies.begin(), latencies.end(), int64_t{0})) / static_cast<double>(latencies.size());

  auto pct = [&](double p) -> long { return static_cast<long>(latencies[static_cast<std::size_t>(p / 100.0 * latencies.size())]); };

  std::fprintf(stdout, "\n─── One-way Latency (%zu samples) ───\n", latencies.size());
  std::fprintf(stdout, "  min  : %5ldns\n", static_cast<long>(latencies.front()));
  std::fprintf(stdout, "  mean : %5.0fns\n", mean);
  std::fprintf(stdout, "  p50  : %5ldns\n", pct(50));
  std::fprintf(stdout, "  p95  : %5ldns\n", pct(95));
  std::fprintf(stdout, "  p99  : %5ldns\n", pct(99));
  std::fprintf(stdout, "  max  : %5ldns\n", static_cast<long>(latencies.back()));
  return 0;
}

int main(int argc, char *argv[])
{
  if (argc < 2)
  {
    std::fprintf(stderr, "Usage: %s <publisher|subscriber>\n", argv[0]);
    return 1;
  }
  const std::string_view role{argv[1]};
  if (role == "publisher")
    return run_publisher();
  if (role == "subscriber")
    return run_subscriber();
  std::fprintf(stderr, "Unknown role '%s'\n", argv[1]);
  return 1;
}
