#include "net.hpp"

RecvPacket::~RecvPacket(){
    buf.clear();
}

/*CanMsgBytes::CanMsgBytes(const uint8_t id, const std::vector<uint8_t> all_bytes):id(id),all_bytes(all_bytes){

}

CanMsgBytes::~CanMsgBytes(){
    all_bytes.clear();
}*/

Net::Net():initialized(false),need_reset(false),expect_dbc_version(0){

}

Net::~Net(){
    shutdown();
}

uint32_t Net::init(const uint16_t dbc_version, const uint16_t port, const uint8_t type){
    expect_dbc_version = dbc_version;
    node_type = type;

    socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(socket_fd < 0){
        return NET_E_SOCK_FAIL_ASSIGN;
    }
    char broadcastEnable = 1;
    if(setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0){
        return NET_E_SOCK_FAIL_ASSIGN;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(socket_fd, (const sockaddr *)&addr, sizeof(sockaddr_in)) < 0){
        shutdown();
        return NET_E_SOCK_FAIL_BIND;
    }

    #ifdef _WIN32

    u_long nonblock = 1;
    if(ioctlsocket(socket_fd, FIONBIO, &nonblock) != 0){
        shutdown();
        return NET_E_SOCK_FAIL_FCNTL;
    }

    #else

    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1){
        shutdown();
        return NET_E_SOCK_FAIL_FCNTL;
    }
    flags = flags | O_NONBLOCK;
    if(fcntl(socket_fd, F_SETFL, flags) != 0){
        shutdown();
        return NET_E_SOCK_FAIL_FCNTL;
    }

    #endif

    initialized = true;

    return NET_E_SUCCESS;
}

uint32_t Net::shutdown(){
    Header head;
    head.dbc_version = expect_dbc_version;
    head.req_0 = true;
    head.req_1 = true;
    send(head);

    #ifdef _WIN32
    if(closesocket(socket_fd) != 0) return NET_E_CANT_CLOSE;
    WSACleanup();

    #else
    if(close(socket_fd) != 0) return NET_E_CANT_CLOSE;
    #endif

    buf.clear();
    outbuf.clear();
    //in_messages.clear();

    // DONT CLEAR connections

    initialized = false;
    return NET_E_SUCCESS;
}

uint32_t Net::reset(const uint16_t dbc_version, const uint16_t port, const uint8_t node_type){
    shutdown();
    uint32_t res = init(dbc_version, port, node_type);
    need_reset = false;
    return res;
}

uint32_t Net::recv(){
    uint32_t res = readBuf();
    if(res != NET_E_SUCCESS){
        return res;
    }
    return readMsg();
}

uint32_t Net::getPackets(std::vector<RecvPacket>& packets){
    std::lock_guard lk(inbuf_mtx);
    if(buf.empty()){
        return NET_E_NO_UDPATE;
    }
    packets = buf;
    buf.clear();
    return NET_E_SUCCESS;
}

uint32_t Net::push(const std::vector<uint8_t>& message){
    std::lock_guard lk(outbuf_mtx);
    std::copy(message.cbegin(), message.cend(), std::back_inserter(outbuf));
    return NET_E_SUCCESS;
}

uint32_t Net::flush(){
    std::vector<std::vector<uint8_t>> frag;

    std::vector<uint8_t> obuf;
    {
        std::lock_guard lk(outbuf_mtx);
        obuf = outbuf;
        outbuf.clear();
    }

    prepareFrag(obuf, frag);    
    
    std::vector<std::pair<sockaddr_in, uint8_t>> subscribers;
    {
        std::lock_guard lk(conn_mtx);
        subscribers = subs;
    }

    for(const std::pair<sockaddr_in, uint8_t> addr : subscribers){
        for(const std::vector<uint8_t>& f : frag){
            uint64_t sent;

            {
                std::lock_guard lk(sock_mtx);

                #ifdef _WIN32
                sent = sendto(socket_fd, (const char*)f.data(), f.size(), 0, (const sockaddr *)&addr.first, sizeof(addr.first));
                #else
                sent = sendto(socket_fd, f.data(), f.size(), 0, (const sockaddr *)&addr.first, sizeof(addr.first));
                #endif
            }

            if(sent == f.size()){
                continue;
            }
            else if(sent < 0){
                need_reset = true;  // TODO maybe just mark connection to be removed
                return NET_E_SOCK_FAIL_SEND_ERRNO;
            }
            else{
                return NET_E_PARTIAL_MSG;
            }
        }
    }
    frag.clear();

    return NET_E_SUCCESS;
}

uint32_t Net::readBuf(){
    bool stop = false;
    while(!stop){
        sockaddr_in addr;

        #ifdef _WIN32
        int addr_len = sizeof(sockaddr_in);
        #else
        socklen_t addr_len = sizeof(sockaddr_in);
        #endif

        std::vector<uint8_t> buffer(1024u, 0);
        int64_t nread;

        {
            std::lock_guard lk(sock_mtx);

            #ifdef _WIN32
            nread = recvfrom(socket_fd, (char*)buffer.data(), 1024, 0, (sockaddr *)&addr, &addr_len);
            #else
            nread = recvfrom(socket_fd, buffer.data(), 1024, 0, (sockaddr *)&addr, &addr_len);
            #endif
        }

        if(nread > 0){
            Header head = *(Header*)buffer.data();
            if(head.frag_ref == 0){
                std::lock_guard lk(inbuf_mtx);
                buf.emplace_back().addr = addr;
                buf.back().head = head;
                buf.back().buf.resize(nread - sizeof(Header));
                std::copy(buffer.cbegin() + sizeof(Header), buffer.cend(), buf.back().buf.begin());
            }
            else{
                processFrag(buffer, addr);
            }
        }
        #ifdef _WIN32
        else if(WSAGetLastError() == WSAEWOULDBLOCK){
            stop = true;
        }
        #else
        else if(errno == EAGAIN){
            stop = true;
        }
        #endif
        else{
            need_reset = true;
            return NET_E_SOCK_FAIL_RECV_ERRNO;
        }
    }
    
    return NET_E_SUCCESS;
}

uint32_t Net::send(const std::vector<uint8_t>& bytes){
    std::vector<std::vector<uint8_t>> frag;
    prepareFrag(bytes, frag);
    std::vector<std::pair<sockaddr_in, uint8_t>> subscribers;
    {
        std::lock_guard lk(conn_mtx);
        subscribers = subs;
    }

    for(const std::pair<sockaddr_in, uint8_t> addr : subscribers){
        for(const std::vector<uint8_t>& f : frag){
            uint64_t sent;

            {
                std::lock_guard lk(sock_mtx);

                #ifdef _WIN32
                sent = sendto(socket_fd, (const char*)f.data(), f.size(), 0, (const sockaddr *)&addr.first, sizeof(addr.first));
                #else
                sent = sendto(socket_fd, f.data(), f.size(), 0, (const sockaddr *)&addr.first, sizeof(addr.first));
                #endif
            }

            if(sent == f.size()){
                continue;
            }
            else if(sent < 0){
                need_reset = true;  // TODO maybe just mark connection to be removed
                return NET_E_SOCK_FAIL_SEND_ERRNO;
            }
            else{
                return NET_E_PARTIAL_MSG;
            }
        }
    }
    frag.clear();

    return NET_E_SUCCESS;
}

uint32_t Net::send(const Header& head){
    std::vector<uint8_t> buf(sizeof(Header));
    memcpy(buf.data(), this, sizeof(Header));
    return send(buf);
}

uint32_t Net::sendTo(const std::vector<uint8_t>& bytes, const sockaddr_in addr){
    std::vector<std::vector<uint8_t>> frag;
    prepareFrag(bytes, frag);

    for(const std::vector<uint8_t>& f : frag){
        uint64_t sent;

        {
            std::lock_guard lk(sock_mtx);

            #ifdef _WIN32
            sent = sendto(socket_fd, (const char*)f.data(), f.size(), 0, (const sockaddr *)&addr, sizeof(addr));
            #else
            sent = sendto(socket_fd, f.data(), f.size(), 0, (const sockaddr *)&addr, sizeof(addr));
            #endif
        }

        if(sent == f.size()){
            continue;
        }
        else if(sent < 0){
            need_reset = true;  // TODO maybe just mark connection to be removed
            return NET_E_SOCK_FAIL_SEND_ERRNO;
        }
        else{
            return NET_E_PARTIAL_MSG;
        }
    }

    frag.clear();
    return NET_E_SUCCESS;
}

uint32_t Net::readMsg(){
    std::vector<uint32_t> remove_indexes;
    uint32_t i = 0;

    for(const RecvPacket& pack : buf){
        if(pack.head.payload_length == 0){
            remove_indexes.insert(remove_indexes.begin(), i);
        }

        uint8_t req = pack.head.getReq();

        if(req == 1){
            subs.push_back({pack.addr, pack.head.getType()});
        }

        else if(req == 2){
            std::vector<uint8_t> search(sizeof(sockaddr_in));  // TODO this is horrible
            memcpy(search.data(), &pack.addr, sizeof(sockaddr_in));
            auto it = std::find_if(pubs_unconfirmed.begin(), pubs_unconfirmed.end(), [&search](sockaddr_in& conn){
                std::vector<uint8_t> arr(sizeof(sockaddr_in));
                memcpy(arr.data(), &conn, sizeof(sockaddr_in));
                return search == arr;
            });
            pubs_unconfirmed.erase(it);
            pubs.push_back({pack.addr, pack.head.getType()});
        }

        else if(req == 3){            
            std::vector<uint8_t> search(sizeof(sockaddr_in));  // TODO this is horrible
            memcpy(search.data(), &pack.addr, sizeof(sockaddr_in));

            auto it = std::find_if(pubs.begin(), pubs.end(), [&search](std::pair<sockaddr_in, uint8_t>& conn){
                std::vector<uint8_t> arr(sizeof(sockaddr_in));
                memcpy(arr.data(), &conn.first, sizeof(sockaddr_in));
                return search == arr;
            });
            pubs.erase(it);

            it = std::find_if(subs.begin(), subs.end(), [&search](std::pair<sockaddr_in, uint8_t>& conn){
                std::vector<uint8_t> arr(sizeof(sockaddr_in));
                memcpy(arr.data(), &conn.first, sizeof(sockaddr_in));
                return search == arr;
            });
            subs.erase(it);
        }
        
        i++;
    }

    for(const uint32_t pos : remove_indexes){
        buf.erase(buf.begin() + pos);
    }
    
    remove_indexes.clear();
    uint64_t now = std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    for(uint32_t i = 0; i < frag_buf.size(); i++){
        if(now - frag_buf[i].second > FRAG_STALE_MS){
            remove_indexes.insert(remove_indexes.begin(), i);
        }
    }

    for(const uint32_t pos : remove_indexes){
        frag_buf.erase(frag_buf.begin() + pos);
    }

    return NET_E_SUCCESS;
}

uint32_t Net::processFrag(const std::vector<uint8_t>& bytes, const sockaddr_in& addr){
    Header head = *(Header*)bytes.data();

    uint32_t pos = 0;
    for(const std::pair<std::vector<RecvPacket>, uint64_t>& frag : frag_buf){
        if(frag.first.front().head.frag_ref == head.frag_ref){
            break;
        }
        pos++;
    }

    if(pos >= frag_buf.size()){
        frag_buf.push_back({{}, 0});
    }
    
    frag_buf[pos].first.emplace_back().addr = addr;
    frag_buf[pos].first.back().head = head;
    frag_buf[pos].first.back().buf.resize(bytes.size() - sizeof(Header));
    std::copy(bytes.cbegin() + sizeof(Header), bytes.cend(), frag_buf[pos].first.back().buf.begin());

    frag_buf[pos].second = std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);

    bool end_found = false;
    uint8_t end_pos = 0;
    uint32_t total_len = 0;
    for(const RecvPacket& pack : frag_buf[pos].first){
        if(!pack.head.more_frag){
            end_found = true;
            end_pos = pack.head.frag_index;
        }
        total_len += pack.buf.size();
    }

    if(end_found && (end_pos + 1) == frag_buf[pos].first.size()){
        // insertion sort is best here since this can be considered 'almost' sorted
        auto first = frag_buf[pos].first.begin();
        auto last = frag_buf[pos].first.end();
        for (auto it = first; it != last; ++it)
            std::rotate(std::upper_bound(first, it, *it, [](const RecvPacket& a, const RecvPacket& b){
                return a.head.frag_index < b.head.frag_index;
            }), it, std::next(it));
            
        {
            std::lock_guard lk(inbuf_mtx);
            
            buf.emplace_back().addr = addr;
            buf.back().head = frag_buf[pos].first.back().head;
            buf.back().buf.reserve(total_len);

            for(const RecvPacket& pack : frag_buf[pos].first){
                std::copy(pack.buf.cbegin(), pack.buf.cend(), std::back_inserter(buf.back().buf));
            }
        }

        frag_buf.erase(frag_buf.begin() + pos);
    }
    return NET_E_SUCCESS;
}

uint32_t Net::prepareFrag(const std::vector<uint8_t>& bytes, std::vector<std::vector<uint8_t>>& frag){
    uint32_t frag_num = std::ceil((float)bytes.size() / (float)FRAG_SIZE);
    frag.resize(frag_num);

    for(uint32_t i = 0; i < frag_num - 1; i++){
        frag[i].resize(FRAG_SIZE + sizeof(Header));
        std::copy(bytes.cbegin() + i * FRAG_SIZE, bytes.cbegin() + (i + 1) * FRAG_SIZE, frag[i].begin() + sizeof(Header));
    }

    frag[frag_num - 1].resize(bytes.size() - ((frag_num - 1) * FRAG_SIZE) + sizeof(Header));
    std::copy(bytes.cbegin() + (frag_num - 1) * FRAG_SIZE, bytes.cend(), frag[frag_num - 1].begin() + sizeof(Header));

    uint8_t frag_ref = ++frag_ref_cycle;
    frag_ref_cycle += (!frag_ref_cycle); // cant have this at 0

    for(uint32_t i = 0; i < frag_num; i++){
        Header head;
        head.dbc_version = expect_dbc_version;
        head.type_0 = node_type & 1;
        head.type_1 = node_type & 2;
        head.req_0 = false;
        head.req_1 = false;
        head.more_frag = i != (frag_num - 1);
        head.frag_ref = frag_ref;
        head.frag_index = i;
        head.payload_length = frag[i].size() - sizeof(Header);

        *(Header *)frag[i].data() = head;
    }

    return NET_E_SUCCESS;
}