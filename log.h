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
const size_t grain{47 * 1024};
const int cq_len{16};
const int server_port{13333};
const int forwarder_port{14444};
const int queue_len = 1024;
uint64_t sendPacks{2500000};

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