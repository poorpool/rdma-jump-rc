#include "log.h"

class RDMAServer {
public:
  RDMAServer() = default;

  void listen(const char *ip, int port) {
    chan_ = rdma_create_event_channel();
    if (!chan_)
      LOG(__LINE__, "failed to create event channel");

    rdma_create_id(chan_, &listen_id_, nullptr, RDMA_PS_TCP);
    if (!listen_id_)
      LOG(__LINE__, "failed to create listen cm_id");

    sockaddr_in sin{.sin_family = AF_INET,
                    .sin_port = htons(port),
                    .sin_addr = {.s_addr = inet_addr(ip)}};
    if (rdma_bind_addr(listen_id_, (sockaddr *)&sin))
      LOG(__LINE__, "failed to bind addr");
    if (rdma_listen(listen_id_, 1))
      LOG(__LINE__, "failed to begin rdma listen");
    int num_devices{};
    pd_ = ibv_alloc_pd(rdma_get_devices(&num_devices)[0]);
    if (!pd_)
      LOG(__LINE__, "failed to alloc pd");
    LOG(__LINE__, "ready to listen");

    rdma_cm_event *event{};
    for (;;) {
      if (stop_)
        break;
      if (rdma_get_cm_event(chan_, &event))
        LOG(__LINE__, "failed to get cm event");
      switch (event->event) {
      case RDMA_CM_EVENT_CONNECT_REQUEST: {
        rdma_cm_id *cm_id{event->id};
        rdma_ack_cm_event(event);
        create_connection(cm_id);
        break;
      }
      case RDMA_CM_EVENT_DISCONNECTED: {
        worker_map_[event->id]->stop();
        worker_map_[event->id]->t_->join();
        delete worker_map_[event->id];
        worker_map_.erase(event->id);
        LOG("client", event->id, "disconnected");
        rdma_ack_cm_event(event);
        break;
      }
      default:
        rdma_ack_cm_event(event);
        break;
      }
    }
  }

  void stop() {
    stop_ = true;
    for (auto &[id, worker] : worker_map_)
      worker->stop(), worker->t_->join();
  }

  ~RDMAServer() {
    ibv_dealloc_pd(pd_);
    rdma_destroy_id(listen_id_);
    rdma_destroy_event_channel(chan_);
    for (auto &[id, worker] : worker_map_)
      worker->stop(), worker->t_->join();
  }

  class Worker {
  public:
    Worker() { lat_hist_.Clear(); };

    void run() {
      uint64_t timer{};
      uint64_t start{};
      uint64_t recv_bytes{};
      uint64_t recv_cnt{};
      for (int i = 0; i < kQueueLen; i++) {
        post_recv(kGrain);
      }
      for (;;) {
        if (stop_) {
          LOG(lat_hist_.ToString());
          LOG("throughput(MB/s):",
              recv_bytes / 1024.0 / 1024 / (timer - start) * 1e9);
          LOG("Total received package num:", recv_cnt);
          LOG("Total received MiB:", recv_bytes / 1024.0 / 1024);
          return;
        }

        ibv_poll_cq_attr cq_attr{};
        int ret = ibv_start_poll(cq_ex_, &cq_attr);
        if (ret == ENOENT)
          continue;

        post_recv(kGrain);
        cnt_++;
        if (!start)
          start = timer = ibv_wc_read_completion_wallclock_ns(cq_ex_);
        else {
          uint64_t last_timer{timer};
          timer = ibv_wc_read_completion_wallclock_ns(cq_ex_);
          lat_hist_.Add((timer - last_timer) / 1e3);
        }
        if (ibv_wc_read_opcode(cq_ex_) == IBV_WC_RECV) {
          recv_bytes += kGrain, recv_cnt++;
        }
        ibv_end_poll(cq_ex_);
        if (cnt_ == kSendPacks) {
          stop_ = true;
        }
      }
    }
    void stop() {
      stop_ = true;
      ibv_dereg_mr(resp_mr_);
      ibv_dereg_mr(msg_mr_);
      ibv_destroy_cq(cq_);
      ibv_destroy_qp(cm_id_->qp);
      // rdma_destroy_id(cm_id_);
      // ibv_destroy_comp_channel(comp_chan_);
    }
    ~Worker() {
      // ibv_dereg_mr(resp_mr_);
      // ibv_dereg_mr(msg_mr_);
      // ibv_destroy_cq(cq_);
      // ibv_destroy_comp_channel(comp_chan_);
      LOG(cm_id_, "worker destroyed");
    }
    rdma_cm_id *cm_id_{};
    ibv_mr *resp_mr_{}, *msg_mr_{};
    char resp_buf_[kGrain * kQueueLen + 1]{},
        msg_buf_[kGrain * kQueueLen + 1]{};
    // ibv_comp_channel *comp_chan_{};
    ibv_cq_ex *cq_ex_{};
    ibv_cq *cq_{};

    bool stop_{};
    std::thread *t_{};

    uint64_t cnt_{};

  private:
    leveldb::Histogram lat_hist_;
    void post_recv(int len) {

      ibv_sge sge{.addr = (uint64_t)msg_buf_,
                  .length = static_cast<uint32_t>(len),
                  .lkey = msg_mr_->lkey};
      ibv_recv_wr wr{.next = nullptr, .sg_list = &sge, .num_sge = 1};
      ibv_recv_wr *bad_wr{};
      if (ibv_post_recv(cm_id_->qp, &wr, &bad_wr))
        LOG(__LINE__, "failed to post recv");
    }
    void post_send(int len) {
      ibv_sge sge{.addr = (uint64_t)resp_buf_,
                  .length = static_cast<uint32_t>(len),
                  .lkey = resp_mr_->lkey};
      ibv_send_wr wr{.next = nullptr,
                     .sg_list = &sge,
                     .num_sge = 1,
                     .opcode = IBV_WR_SEND,
                     .send_flags = IBV_SEND_SIGNALED};
      ibv_send_wr *bad_wr;
      if (ibv_post_send(cm_id_->qp, &wr, &bad_wr))
        LOG(__LINE__, "failed to post send");
    }
  };
  std::map<rdma_cm_id *, Worker *> worker_map_;

private:
  void create_connection(rdma_cm_id *cm_id) {
    int num_devices{};

    auto *worker = new Worker;
    ibv_cq_init_attr_ex cq_attr_ex{};
    cq_attr_ex.cqe = kQueueLen * 2;
    cq_attr_ex.cq_context = nullptr;
    cq_attr_ex.channel = nullptr;
    cq_attr_ex.comp_vector = 0;
    cq_attr_ex.wc_flags = IBV_WC_EX_WITH_COMPLETION_TIMESTAMP_WALLCLOCK;
    worker->cq_ex_ =
        ibv_create_cq_ex(rdma_get_devices(&num_devices)[0], &cq_attr_ex);
    worker->cq_ = ibv_cq_ex_to_cq(worker->cq_ex_);

    if (!worker->cq_)
      LOG(__LINE__, "failed to create cq");

    ibv_qp_init_attr qp_init_attr{.send_cq = worker->cq_,
                                  .recv_cq = worker->cq_,
                                  .cap{.max_send_wr = kQueueLen,
                                       .max_recv_wr = kQueueLen,
                                       .max_send_sge = 1,
                                       .max_recv_sge = 1},
                                  .qp_type = IBV_QPT_RC};
    if (rdma_create_qp(cm_id, pd_, &qp_init_attr))
      LOG(__LINE__, "failed to create qp");
    worker->cm_id_ = cm_id;
    worker->msg_mr_ =
        ibv_reg_mr(pd_, worker->msg_buf_, sizeof(worker->msg_buf_),
                   IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                       IBV_ACCESS_REMOTE_WRITE);
    worker->resp_mr_ =
        ibv_reg_mr(pd_, worker->resp_buf_, sizeof(worker->resp_buf_),
                   IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                       IBV_ACCESS_REMOTE_WRITE);
    worker->t_ = new std::thread(&Worker::run, worker);
    worker_map_[cm_id] = worker;
    if (rdma_accept(cm_id, nullptr))
      LOG(__LINE__, "failed to accept connection");
  }
  bool stop_{};
  rdma_event_channel *chan_{};
  rdma_cm_id *listen_id_{};
  ibv_pd *pd_{};
};