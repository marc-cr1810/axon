#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <axon/sync/atomic_queue.hpp>
#include <thread>
#include <vector>
#include <atomic>

using namespace axon;

TEST_CASE("SpscQueue basic push/pop")
{
  SpscQueue<int, 16> q;
  CHECK(q.empty());
  CHECK(q.push(42));
  CHECK(!q.empty());
  auto v = q.pop();
  REQUIRE(v.has_value());
  CHECK(*v == 42);
  CHECK(q.empty());
}

TEST_CASE("SpscQueue fills and rejects when full")
{
  SpscQueue<int, 4> q;
  for (int i = 0; i < 4; ++i)
    CHECK(q.push(i));
  CHECK(!q.push(99)); // full
  for (int i = 0; i < 4; ++i)
  {
    auto v = q.pop();
    REQUIRE(v.has_value());
    CHECK(*v == i);
  }
  CHECK(!q.pop().has_value()); // empty
}

TEST_CASE("SpscQueue producer/consumer threads")
{
  SpscQueue<int, 256> q;
  constexpr int kItems = 100'000;
  std::atomic<bool> done{false};

  std::thread producer(
      [&]
      {
        for (int i = 0; i < kItems; ++i)
        {
          while (!q.push(i))
          {
          } // spin until space
        }
      });

  std::vector<int> received;
  received.reserve(kItems);
  std::thread consumer(
      [&]
      {
        while ((int)received.size() < kItems)
        {
          if (auto v = q.pop())
            received.push_back(*v);
        }
      });

  producer.join();
  consumer.join();

  REQUIRE(received.size() == kItems);
  for (int i = 0; i < kItems; ++i)
    CHECK(received[i] == i);
}

TEST_CASE("MpmcQueue basic push/pop")
{
  MpmcQueue<int, 16> q;
  CHECK(q.empty());
  CHECK(q.push(7));
  auto v = q.pop();
  REQUIRE(v.has_value());
  CHECK(*v == 7);
}

TEST_CASE("MpmcQueue multi-producer multi-consumer")
{
  MpmcQueue<int, 512> q;
  constexpr int kProducers = 4, kConsumers = 4, kPerProducer = 10'000;
  std::atomic<int> total{0};
  std::vector<std::thread> threads;

  for (int p = 0; p < kProducers; ++p)
    threads.emplace_back(
        [&]
        {
          for (int i = 0; i < kPerProducer; ++i)
            while (!q.push(1))
            {
            }
        });

  for (int c = 0; c < kConsumers; ++c)
    threads.emplace_back(
        [&]
        {
          while (total.load(std::memory_order_relaxed) < kProducers * kPerProducer)
          {
            if (auto v = q.pop())
              total.fetch_add(1, std::memory_order_relaxed);
          }
        });

  for (auto &t : threads)
    t.join();
  CHECK(total.load() == kProducers * kPerProducer);
}
