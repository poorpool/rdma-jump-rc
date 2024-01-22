#include "log.h"
#include <infiniband/verbs.h>
#include <sys/types.h>

class RDMAClient {
public:
  RDMAClient() = default;

  void connect(const char *ip, int port) {
    chan_ = rdma_create_event_channel();
    if (!chan_)
      LOG(__LINE__, "failed to create rdma chan");

    if (rdma_create_id(chan_, &cm_id_, nullptr, RDMA_PS_TCP))
      LOG(__LINE__, "failed to create cmid");

    if (!cm_id_) {
      abort();
    }

    addrinfo *res;
    if (getaddrinfo(ip, std::to_string(port).c_str(), nullptr, &res) < 0)
      LOG(__LINE__, "failed to resolve addr");

    addrinfo *t{res};
    for (; t; t = t->ai_next)
      if (!rdma_resolve_addr(cm_id_, nullptr, t->ai_addr, 500))
        break;
    if (!t)
      LOG(__LINE__, "failed to resolve addr");

    rdma_cm_event *event{};
    if (rdma_get_cm_event(chan_, &event))
      LOG(__LINE__, "failed to get cm event");
    if (event->event != RDMA_CM_EVENT_ADDR_RESOLVED)
      LOG(__LINE__, "failed to resolve addr info");
    rdma_ack_cm_event(event);

    if (rdma_resolve_route(cm_id_, 1000))
      LOG(__LINE__, "failed to resolve route");
    if (rdma_get_cm_event(chan_, &event))
      LOG(__LINE__, "failed to get cm event");
    if (event->event != RDMA_CM_EVENT_ROUTE_RESOLVED)
      LOG(__LINE__, "failed to resolve route");
    rdma_ack_cm_event(event);

    pd_ = ibv_alloc_pd(cm_id_->verbs);
    if (!pd_)
      LOG(__LINE__, "failed to alloc pd");

    cq_ = ibv_create_cq(cm_id_->verbs, kQueueLen * 2, nullptr, nullptr, 0);
    if (!cq_)
      LOG(__LINE__, "failed to create cq");

    ibv_qp_init_attr qp_init_attr{.send_cq = cq_,
                                  .recv_cq = cq_,
                                  .cap{.max_send_wr = kQueueLen,
                                       .max_recv_wr = kQueueLen,
                                       .max_send_sge = 1,
                                       .max_recv_sge = 1},
                                  .qp_type = IBV_QPT_RC};
    if (rdma_create_qp(cm_id_, pd_, &qp_init_attr))
      LOG(__LINE__, "failed to create qp");
    if (rdma_connect(cm_id_, nullptr))
      LOG(__LINE__, "failed to connect");
    if (rdma_get_cm_event(chan_, &event))
      LOG(__LINE__, "failed to get cm event");

    if (event->event != RDMA_CM_EVENT_ESTABLISHED)
      LOG(__LINE__, "failed to establish connect");
    rdma_ack_cm_event(event);
  }

  ibv_mr *reg_mr(void *ptr, uint64_t len) {
    return outer_mr_map_[ptr] =
               ibv_reg_mr(pd_, ptr, len,
                          IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                              IBV_ACCESS_REMOTE_WRITE);
  }

  void dereg_mr(void *ptr) {
    ibv_dereg_mr(outer_mr_map_[ptr]);
    outer_mr_map_.erase(ptr);
  }

  void post_send(void *msg, uint64_t offset, uint64_t len) {
    while (wc_wait_ >= kCqLen) { // 在途不得超过 cq_len
      int tmp = ibv_poll_cq(cq_, kCqLen, wc_);
      for (int i = 0; i < tmp; i++) {
        if (wc_[i].opcode == IBV_WC_RECV) {
          ;
        } else if (wc_[i].opcode == IBV_WC_SEND) {
          wc_wait_--;
        } else {
          LOG("err wc_[i] opcode", wc_[i].opcode);
        }
      }
    }
    ibv_sge sge{.addr = reinterpret_cast<uint64_t>(msg) + offset,
                .length = static_cast<uint32_t>(len),
                .lkey = outer_mr_map_[msg]->lkey};
    ibv_send_wr wr{.next = nullptr,
                   .sg_list = &sge,
                   .num_sge = 1,
                   .opcode = IBV_WR_SEND,
                   .send_flags = IBV_SEND_SIGNALED};
    ibv_send_wr *bad_wr;

    if (ibv_post_send(cm_id_->qp, &wr, &bad_wr))
      LOG(__LINE__, "failed to post client-send", bad_wr->send_flags);

    wc_wait_++;
  }

  void post_recv(void *dest, uint64_t offset, uint64_t len) {
    ibv_sge sge{.addr = reinterpret_cast<uint64_t>(dest) + offset,
                .length = static_cast<uint32_t>(len),
                .lkey = outer_mr_map_[dest]->lkey};
    ibv_recv_wr wr{
        .wr_id = offset, .next = nullptr, .sg_list = &sge, .num_sge = 1};
    ibv_recv_wr *bad_wr{};

    if (ibv_post_recv(cm_id_->qp, &wr, &bad_wr))
      LOG(__LINE__, "client failed to post recv");
  }
  void close() {
    while (wc_wait_)
      wc_wait_ -= ibv_poll_cq(cq_, kCqLen, wc_);
    rdma_disconnect(cm_id_);
    for (auto &[ptr, mr] : outer_mr_map_)
      ibv_dereg_mr(mr);
    outer_mr_map_.clear();
    LOG(__LINE__, "client closed");
  }
  ~RDMAClient() {
    ibv_dealloc_pd(pd_);
    ibv_destroy_cq(cq_);
    ibv_destroy_qp(cm_id_->qp);
    rdma_destroy_id(cm_id_);
    rdma_destroy_event_channel(chan_);
  }

  uint64_t wc_wait_{};
  ibv_cq *cq_{};
  ibv_wc wc_[kCqLen]{};

private:
  rdma_event_channel *chan_{};
  rdma_cm_id *cm_id_{};
  ibv_pd *pd_{};
  std::map<void *, ibv_mr *> outer_mr_map_{};
};