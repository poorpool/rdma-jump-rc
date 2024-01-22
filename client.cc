#include "client.h"
#include <cstdlib>
#include <infiniband/verbs.h>

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf("Usage: %s <server_ip>\n", argv[0]);
    return 0;
  }

  RDMAClient client;
  client.connect(argv[1], kForwarderPort);

  char *data = static_cast<char *>(malloc(kBufferSize));
  char *recv_data = static_cast<char *>(malloc(kBufferSize));
  client.reg_mr(data, kBufferSize);
  client.reg_mr(recv_data, kBufferSize);

  for (size_t i = 0; i < kBufferSize; i++)
    data[i] = 'a' + i % 26;

  LOG("client registered mr");
  // 不必使用 ACK 控制在途包,使用post_send()里面的控制就好了
  // for (int i = 0; i < kQueueLen; i++) {
  //   client.post_recv(recv_data, i * kGrain, kGrain);
  // }

  LOG("client start, please waiting...");

  for (size_t siz = 0; siz < kGrain * kSendPacks; siz += kGrain) {
    client.post_send(data, siz % kQueueLen, kGrain);
  }
  LOG("client end!");
  client.close();

  free(data);
  free(recv_data);
  return 0;
}