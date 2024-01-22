#include "forwarder.h"

int main() {
  RDMAForwarder forwarder;
  forwarder.transfer("0.0.0.0", "192.168.200.53", kForwarderPort);
  return 0;
}