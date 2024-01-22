#include "client.h"
#include <cstdlib>
#include <infiniband/verbs.h>
using ll = long long;

int main(int argc, char **argv) {
  RDMAClient client;
  client.connect(argv[1], forwarder_port);
  auto data_size = queue_len * grain;
  auto data = (char *)malloc(data_size);
  void *tmp;
  posix_memalign(&tmp, 4096, queue_len * grain);
  client.recv_data = (char *)tmp;
  LOG(__LINE__, "connected");
  client.reg_mr(data, data_size);
  client.reg_mr(client.recv_data, queue_len * grain);
  for (ll i{}; i < data_size; ++i)
    data[i] = 'a' + i % 26;
  LOG(__LINE__, "connected");
  for (int i = 0; i < queue_len; i++) {
    client.post_recv(client.recv_data, i * grain, grain);
  }
  LOG(__LINE__, "start!");
  for (ll i{}; i < grain * sendPacks; i += grain) {
    while (client.wc_wait_ >= cq_len) {
      // LOG("waiting", client.wc_wait_, cq_len);
      int tmp = ibv_poll_cq(client.cq_, cq_len, client.wc_);
      for (int i = 0; i < tmp; i++) {
        if (client.wc_[i].opcode == IBV_WC_RECV) {
          client.wc_wait_--;
          client.post_recv(client.recv_data, client.wc_[i].wr_id, grain);
          //   LOG("get send response", client.send_response_num++);
          client.send_response_num++;
        } else if (client.wc_[i].opcode == IBV_WC_SEND) {

        } else {
          LOG("err wci opcode", client.wc_[i].opcode);
        }
      }
    }
    // LOG("send", i / grain);
    client.post_send(data, i % queue_len, grain);
  }
  LOG(__LINE__, "end!");
  client.close();
  free(data);
  free(client.recv_data);
  return 0;
}