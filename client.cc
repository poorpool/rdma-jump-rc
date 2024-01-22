#include "client.h"
#include <cstdlib>
#include <infiniband/verbs.h>

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf("Usage: %s <server_ip>\n", argv[0]);
    return 0;
  }

  RDMAClient client;
  client.connect(argv[1], forwarder_port);

  size_t data_size = static_cast<size_t>(queue_len) * grain; // 循环内存区域大小

  char *data = static_cast<char *>(malloc(data_size));
  char *recv_data = static_cast<char *>(malloc(data_size));
  client.reg_mr(data, data_size);
  client.reg_mr(recv_data, data_size);

  for (size_t i = 0; i < data_size; i++)
    data[i] = 'a' + i % 26;

  LOG("client registered mr");
  for (int i = 0; i < queue_len; i++) {
    client.post_recv(recv_data, i * grain, grain);
  }

  LOG("client start!");

  for (size_t siz = 0; siz < grain * sendPacks; siz += grain) {
    while (client.wc_wait_ >= cq_len) { // 在途不得超过 cq_len
      int tmp = ibv_poll_cq(client.cq_, cq_len, client.wc_);
      for (int i = 0; i < tmp; i++) {
        if (client.wc_[i].opcode == IBV_WC_RECV) {
          client.wc_wait_--;
          client.post_recv(recv_data, client.wc_[i].wr_id, grain);
        } else if (client.wc_[i].opcode == IBV_WC_SEND) {
          ;
        } else {
          LOG("err wc_[i] opcode", client.wc_[i].opcode);
        }
      }
    }
    client.post_send(data, siz % queue_len, grain);
  }
  LOG("client end!");
  client.close();

  free(data);
  free(recv_data);
  return 0;
}