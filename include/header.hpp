#include <stdint.h>
#include <cstring>

#define HEADER_ROVER uint8_t(0)
#define HEADER_DRONE uint8_t(1)
#define HEADER_REMOTE uint8_t(2)
#define HEADER_GS uint8_t(3)

#define HEADER_PUSH uint8_t(0)
#define HEADER_CONN uint8_t(1)
#define HEADER_ACK uint8_t(2)
#define HEADER_DECONN uint8_t(3)

#pragma pack(push,1)
struct Header{
    bool type_0 : 1;  // type 0: rover, 1: drone, 2: remote, 3: gs
    bool type_1 : 1;

    bool req_0 : 1; // req 0: push data, 1: conn req, 2: ack conn, 3: deconn req
    bool req_1 : 1;

    bool more_frag : 1; // more fragments coming

    bool padding_0 : 1;
    bool padding_1 : 1;
    bool padding_2 : 1;

    uint16_t payload_length : 16;
    uint8_t frag_index : 8; // message pos in frag
    uint8_t frag_ref : 8; // 0 not a frag

    uint16_t dbc_version : 16; // can id

    Header(){
        type_0 = false;
        type_1 = false;

        req_0 = false;
        req_1 = false;

        more_frag = false;

        padding_0 = true;
        padding_1 = false;
        padding_2 = true;

        payload_length = 0;
        frag_index = 0;
        frag_ref = 0;

        dbc_version = 0;
    }

    inline uint8_t getType() const {
        return (*(uint8_t * )this) & 3;
    }

    inline uint8_t getReq() const {
        return ((*(uint8_t * )this) & 12) >> 2;
    }

    inline bool lastFrag() const {
        return !more_frag && frag_ref != 0;
    }

    inline bool validity() const {
        bool push_valid = getReq() != 0 || payload_length != 0;
        bool frag_valid = !more_frag || frag_ref != 0;
        bool preamle_valid = padding_0 && !padding_1 && padding_2;
        return push_valid && frag_valid && preamle_valid;
    }

};
#pragma pack(pop)