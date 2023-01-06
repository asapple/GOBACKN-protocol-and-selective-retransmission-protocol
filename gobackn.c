#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

#define DATA_TIMER  1850
#define MAX_SEQ 7
#define ACK_TIMER 770

#define inc(k) if(k < MAX_SEQ) k = k + 1; else k = 0

typedef unsigned char seq_nr;
typedef unsigned char frame_kind;
typedef unsigned char packet[PKT_LEN];

static unsigned char between(seq_nr a, seq_nr b, seq_nr c)
{
    return (((a <= b) && (b < c)) || ((a <= b) && (c < a)) || ((b < c) && (c < a)));
}

struct FRAME { 
    frame_kind kind; /* FRAME_DATA */
    seq_nr ack;
    seq_nr seq;
    packet info; 
    unsigned int  crc32;
};

static int phl_ready = 0;

static void put_frame(seq_nr *frame, int len)
{
    *(unsigned int *)(frame + len) = crc32(frame, len);
    send_frame(frame, len + 4);
    phl_ready = 0;
}

static void send_data_frame(frame_kind fk, seq_nr frame_nr, seq_nr frame_expected, packet buffer[])
{
    struct FRAME s;
    s.kind = fk;
    s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
    stop_ack_timer();
    if (fk == FRAME_DATA) {
        s.seq = frame_nr;
        memcpy(s.info, buffer[frame_nr], PKT_LEN);
        dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short *)s.info);
        put_frame((seq_nr *)&s, 3 + PKT_LEN); //�������ֽ�crcУ��
        start_timer(frame_nr, DATA_TIMER);
    }
    else if (fk == FRAME_ACK) {
        dbg_frame("Send ACK  %d\n", s.ack);
        put_frame((unsigned char*)&s, 2); //��s.ack�������crcУ��
    }
}

int main(int argc, char **argv)
{
    seq_nr next_frame_to_send = 0;
    seq_nr nbuffered = 0;
    seq_nr ack_expected = 0;
    seq_nr frame_expected = 0;
    packet buffer[MAX_SEQ + 1];


    int event, arg;
    struct FRAME f;
    int len = 0;

    protocol_init(argc, argv); 
    lprintf("Designed by Jiang Yanjun, build: " __DATE__"  "__TIME__"\n");
    lprintf("Process by CWJ, build: " __DATE__"  "__TIME__"\n");
    disable_network_layer();

    while(1) {
        event = wait_for_event(&arg);

        switch (event) {
        case NETWORK_LAYER_READY:
            get_packet(buffer[next_frame_to_send]);
            nbuffered++;
            send_data_frame(FRAME_DATA,next_frame_to_send,frame_expected,buffer);
            inc(next_frame_to_send);
            break;

        case PHYSICAL_LAYER_READY:
            phl_ready = 1;
            break;

        case FRAME_RECEIVED: 
            len = recv_frame((seq_nr *)&f, sizeof f);
            if (len < 6 || crc32((seq_nr *)&f, len) != 0) {
                dbg_event("**** Receiver Error, Bad CRC Checksum\n");
                break;
            }
            if (f.kind == FRAME_ACK) 
                dbg_frame("Recv ACK  %d\n", f.ack);
            if (f.kind == FRAME_DATA) {
                dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short *)f.info);
                if (f.seq == frame_expected) {
                    put_packet(f.info, len - 7);
                    inc(frame_expected);
                    start_ack_timer(ACK_TIMER);
                }
            } 
            while (between(ack_expected,f.ack,next_frame_to_send)) {
                stop_timer(ack_expected);
                nbuffered--;
                inc(ack_expected);
            }
            break; 

        case DATA_TIMEOUT:
            dbg_event("---- DATA %d timeout\n", arg); 
            next_frame_to_send = ack_expected;
            for (int i = 1; i <= nbuffered; i++)
            {
                send_data_frame(FRAME_DATA, next_frame_to_send, frame_expected, buffer);
                inc(next_frame_to_send);
            }
            break;

        case ACK_TIMEOUT:
            send_data_frame(FRAME_ACK, 0, frame_expected, buffer);
            break;
        }

        if (nbuffered < MAX_SEQ && phl_ready)
            enable_network_layer();
        else
            disable_network_layer();
    }
}