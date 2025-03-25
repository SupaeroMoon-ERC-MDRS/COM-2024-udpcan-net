#pragma once
#include <vector>
#include <algorithm>
#include <mutex>
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <arpa/inet.h>
#endif

#include "header.hpp"

#define NET_E_SUCCESS (uint32_t)0

#define NET_E_CANT_CLOSE (uint32_t)1
#define NET_E_NO_UDPATE (uint32_t)2

#define NET_E_SOCK_FAIL_ASSIGN (uint32_t)1024
#define NET_E_SOCK_FAIL_BIND (uint32_t)1025
#define NET_E_SOCK_FAIL_FCNTL (uint32_t)1026

#define NET_E_SOCK_FAIL_RECV_ERRNO (uint32_t)1027
#define NET_E_PARTIAL_MSG (uint32_t)1028
#define NET_E_SOCK_FAIL_SEND_ERRNO (uint32_t)1029

#define NET_E_UNINITIALIZED (uint32_t)1030

#define BROADCAST "255.255.255.255"

struct RecvPacket{
    sockaddr_in addr;
    Header head;
    std::vector<uint8_t> buf;

    RecvPacket(){};
    ~RecvPacket();
};

/*struct CanMsgBytes{
    uint8_t id;
    std::vector<uint8_t> all_bytes;

    CanMsgBytes(const uint8_t id, const std::vector<uint8_t> all_bytes);
    ~CanMsgBytes();
};*/

class Net{
    private:
        int32_t socket_fd;
        bool initialized;
        bool need_reset;
        uint16_t expect_dbc_version;
        std::vector<std::pair<uint8_t, uint32_t>> expect_can_ids;
        std::vector<std::pair<sockaddr_in, uint8_t>> subs;
        std::vector<std::pair<sockaddr_in, uint8_t>> pubs;
        
        std::vector<std::pair<std::vector<RecvPacket>, uint64_t>> frag_buf;
        std::vector<RecvPacket> buf;
        std::vector<uint8_t> outbuf;
        //std::vector<CanMsgBytes> in_messages;

        std::mutex sock_mtx;
        std::mutex inbuf_mtx;
        std::mutex outbuf_mtx;
        std::mutex conn_mtx;
        std::mutex flag_mtx;

        uint32_t readBuf();
        uint32_t readMsg();

        uint32_t processFrag(const std::vector<uint8_t>& bytes);
        uint32_t prepareFrag(const std::vector<uint8_t>& bytes, const std::vector<std::vector<uint8_t>>& frag);

        uint32_t send(const std::vector<uint8_t>& bytes); // thrsafety
        uint32_t send(const Header& head); // thrsafety
    
    public:
        Net();
        ~Net();

        uint32_t init(const uint16_t dbc_version, const uint16_t port);
        uint32_t reset(const uint16_t dbc_version, const uint16_t port);
        uint32_t shutdown();

        bool isInitialized(){ // TODO figure out why this cant be const
            std::lock_guard lk(flag_mtx);

            return initialized;
        }
        bool needReset(){ // TODO figure out why this cant be const
            std::lock_guard lk(flag_mtx);
            return need_reset;
        }

        uint32_t recv(); // thrsafety
        uint32_t getPackets(std::vector<RecvPacket>& packets); // thrsafety
        uint32_t push(const std::vector<uint8_t>& message); // thrsafety
        uint32_t flush(); // thrsafety

        bool hasSubscribers(){
            std::lock_guard lk(conn_mtx);
            return !subs.empty();
        } // thrsafety

        uint32_t sendTo(const std::vector<uint8_t>& bytes, const sockaddr_in addr); // thrsafety
};