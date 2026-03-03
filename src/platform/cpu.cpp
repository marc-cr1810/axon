// cpu.cpp — implements find_efficiency_cores() and pin_thread_to_cores()
//
// IMPORTANT: Do NOT include axon headers here that open namespace axon.
// Including them causes GCC's ADL in -std=c++23 mode to resolve std:: to
// axon::std, breaking all standard library lookups.
// Instead we forward-declare the functions and include only system headers.

#include <string>
#include <string_view>
#include <vector>
#include <cstdio>
#include <pthread.h>
#include <sched.h>

namespace axon
{
std::vector<int> find_efficiency_cores();
void pin_thread_to_cores(const std::vector<int> &cores);
} // namespace axon

std::vector<int> axon::find_efficiency_cores()
{
  std::vector<int> cores;
#ifdef __linux__
  for (int cpu = 0; cpu < 128; ++cpu)
  {
    std::string path = "/sys/devices/system/cpu/cpu";
    path += std::to_string(cpu);
    path += "/topology/core_type";

    FILE *f = ::fopen(path.c_str(), "r");
    if (!f)
      break;
    char buf[32]{};
    if (::fgets(buf, static_cast<int>(sizeof(buf)), f) != nullptr)
    {
      std::string_view sv{buf};
      if (sv.find("efficiency") != sv.npos || sv.find("atom") != sv.npos)
        cores.push_back(cpu);
    }
    ::fclose(f);
  }
#endif
  return cores;
}

void axon::pin_thread_to_cores(const std::vector<int> &cores)
{
  if (cores.empty())
    return;
#ifdef __linux__
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  for (int c : cores)
    CPU_SET(c, &cpuset);
  ::pthread_setaffinity_np(::pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}
