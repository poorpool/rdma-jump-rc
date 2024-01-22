#include "server.h"

int main(){
    RDMAServer server;
    server.listen("0.0.0.0", server_port);
    server.stop();
    return 0;
}