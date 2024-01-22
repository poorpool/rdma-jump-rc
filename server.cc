#include "server.h"

int main() {
  RDMAServer server;
  server.listen("0.0.0.0", kServerPort);
  server.stop();
  return 0;
}