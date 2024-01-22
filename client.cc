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
  for (int i = 0; i < kQueueLen; i++) {
    client.post_recv(recv_data, i * kGrain, kGrain);
  }

  LOG("client start!");

  for (size_t siz = 0; siz < kGrain * kSendPacks; siz += kGrain) {
    while (client.wc_wait_ >= kCqLen) { // 在途不得超过 cq_len
      int tmp = ibv_poll_cq(client.cq_, kCqLen, client.wc_);
      for (int i = 0; i < tmp; i++) {
        if (client.wc_[i].opcode == IBV_WC_RECV) {
          client.wc_wait_--;
          client.post_recv(recv_data, client.wc_[i].wr_id, kGrain);
        } else if (client.wc_[i].opcode == IBV_WC_SEND) {
          ;
        } else {
          LOG("err wc_[i] opcode", client.wc_[i].opcode);
        }
      }
    }
    client.post_send(data, siz % kQueueLen, kGrain);
  }
  LOG("client end!");
  client.close();

  free(data);
  free(recv_data);
  return 0;
}