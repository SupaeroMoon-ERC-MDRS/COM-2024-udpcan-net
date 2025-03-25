#include "net.hpp"

int main(){
    Header h;
    h.req_0 = true;
    h.req_1 = false;

    uint8_t status_s = *(uint8_t*)&h >> 2;
    uint8_t status = *(uint8_t*)&h;

    sizeof(Header);

    return 0;
}