#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <thread>
#include <vector>

#include "histogram/histogram.h"
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
constexpr size_t kGrain{1 * 1024};
constexpr int kCqLen{16};
constexpr int kServerPort{13333};
constexpr int kForwarderPort{14444};
constexpr int kQueueLen = 1024;
constexpr uint64_t kSendPacks{40000000};
constexpr size_t kBufferSize = kGrain * kQueueLen;

struct LOGGER {
  template <typename... Args> void operator()(Args &&...args) const {
    ((std::cout << args << ' '), ...);
    std::cout << std::endl;
  }
} LOG;

struct CData {
  uint32_t rkey;
  void *data;
};