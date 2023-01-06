#include <stdio.h>
#include <string.h>
#include "protocol.h"
#include "datalink.h"

#define MAX_SEQ 63
#define DATA_TIMER 8420
#define ACK_TIMER 7350
#define NR_BUFS ((MAX_SEQ+1)/2)

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
int no_nak = 1;
static int phl_ready = 0;

static void put_frame(seq_nr* frame, int len)
{
    *(unsigned int*)(frame + len) = crc32(frame, len);
    send_frame(frame, len + 4);
    phl_ready = 0;
}

static void send_data_frame(frame_kind fk, seq_nr frame_nr, seq_nr frame_expected, packet buffer[])
{
    struct FRAME s;
    s.kind = fk;                                                //数据帧类型
    s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);         //ack 等于接收窗口下限减一
    stop_ack_timer();                                           //发送帧，关闭ack超时时钟
    if (fk == FRAME_DATA) {
        s.seq = frame_nr;                                       //帧序号
        memcpy(s.info, buffer[frame_nr % NR_BUFS], PKT_LEN);    //将待发送数据从发送窗口缓存存入帧的数据域
        dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short*)s.info);//调试信息
        put_frame((seq_nr*)&s, 3 + PKT_LEN);                    //在s+3+PKT_LEN尾加入四字节crc校验，即存入s->crc32，然后发送数据帧
        start_timer(frame_nr, DATA_TIMER);                      //开始数据帧超时计时器
    }
    else if (fk == FRAME_ACK) {
        dbg_frame("Send ACK  %d\n", s.ack);                     //调试信息
        put_frame((unsigned char*)&s, 2);                       //在s.ack后面插入crc校验，然后发送长度为6字节的ACK帧
    }
    else if (fk == FRAME_NAK) {
        dbg_frame("Send NAK  %d\n", (s.ack + 1) % (MAX_SEQ + 1));
        no_nak = 0;                                             //已经发送NAK
        put_frame((unsigned char*)&s, 2);                       //在s.ack后面插入crc校验，然后发送长度为6字节的NAK帧
    }
}
int main(int argc, char** argv)
{
    seq_nr ack_expected = 0;            //发送窗口下限
    seq_nr next_frame_to_send = 0;      //发送窗口上限
    seq_nr frame_expected = 0;          //接收窗口下限
    seq_nr too_far = NR_BUFS;           //接收窗口上限
    struct FRAME f;                     //数据帧

    packet out_buf[NR_BUFS];            //发送窗口（发送缓存）
    packet in_buf[NR_BUFS];             //接收窗口（接收缓存）

    int arrived[NR_BUFS];               //接收窗口接收情况
    for (int i = 0; i < NR_BUFS; i++) arrived[i] = 0;
    seq_nr nbuffered = 0;               //已发送等待确认的帧数

    int event, arg;
    int len = 0;                        //接收的数据帧的长度
    protocol_init(argc, argv);
    lprintf("Designed by Jiang Yanjun, build: " __DATE__"  "__TIME__"\n");
    lprintf("Process by CWJ, build: " __DATE__"  "__TIME__"\n");
    disable_network_layer();            //初始时将disable网络层，等待物理层准备好才enable网络层

    while (1) {
        event = wait_for_event(&arg);   //等待事件，DATA_TIMEOUT事件，传出参数arg记录了超时帧的帧序号

        switch (event) {
        case NETWORK_LAYER_READY:       //网络层准备好（ 物理层发送缓冲未满 && 发送窗口未满 ）
            nbuffered++;                //已发送帧数加一
            get_packet(out_buf[next_frame_to_send % NR_BUFS]);//从网络层获取一个报文，存入发送窗口上限
            send_data_frame(FRAME_DATA, next_frame_to_send, frame_expected, out_buf);//发送数据帧
            inc(next_frame_to_send);    //发送窗口上限加一
            break;

        case PHYSICAL_LAYER_READY:
            phl_ready = 1;
            break;

        case FRAME_RECEIVED:
            len = recv_frame((seq_nr*)&f, sizeof(f));
            if (len < 6 || crc32((seq_nr*)&f, len) != 0) {                      //收到错误的帧
                dbg_event("**** Receiver Error, Bad CRC Checksum\n");
                if (no_nak)                                                     //如果NAK还没被发送
                    send_data_frame(FRAME_NAK, 0, frame_expected, out_buf);     //发送NAK，请求序号为接收下限frame_expected的数据帧
                break;
            }
            if (f.kind == FRAME_ACK)
                dbg_frame("Recv ACK  %d\n", f.ack);
            if (f.kind == FRAME_DATA) {
                dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short*)f.info);
                if ((f.seq != frame_expected) && no_nak) {                      //如果收到的帧不是接收窗口下限的数据帧那么，请求序号为接收下限frame_expected的数据帧
                    dbg_event(" Recv frame is not lower bound, NAK sent back\n");
                    send_data_frame(FRAME_NAK, 0, frame_expected, out_buf);
                }
                
                if (between(frame_expected, f.seq, too_far) && arrived[f.seq % NR_BUFS] == 0)//接收数据帧的序号落在接收窗口内，并且该序号数据第一次到达
                {
                    arrived[f.seq % NR_BUFS] = 1;                               //标注已到达
                    memcpy(in_buf[f.seq % NR_BUFS], f.info, PKT_LEN);           //存入接收窗口
                    while (arrived[frame_expected % NR_BUFS])                   //移动接收窗口
                    {
                        put_packet(in_buf[frame_expected % NR_BUFS], PKT_LEN);  //上传到网络层
                        no_nak = 1;                                             //重置no_nak
                        arrived[frame_expected % NR_BUFS] = 0;                  //重置上传到网络层的数据帧
                        inc(frame_expected);                                    //滑动接收窗口
                        inc(too_far);                                           //滑动接收窗口
                        start_ack_timer(ACK_TIMER);
                    }
                }
            }
            if (f.kind == FRAME_NAK && between(ack_expected, (f.ack + 1) % (MAX_SEQ + 1), next_frame_to_send))  //人家要的是ack+1也就是对方接收窗口下限的数据
            {
                send_data_frame(FRAME_DATA, (f.ack + 1) % (MAX_SEQ + 1), frame_expected, out_buf);
            }
            while (between(ack_expected, f.ack, next_frame_to_send)) {           //不管接收到的帧是数据帧、ACK帧还是NAK帧，f.ack都是有效的
                nbuffered--;
                stop_timer(ack_expected);
                inc(ack_expected);
            }
            break;
        case DATA_TIMEOUT:
            dbg_event("---- DATA %d timeout\n", arg);
            send_data_frame(FRAME_DATA, arg, frame_expected, out_buf);          //重发超时数据帧
            break;

        case ACK_TIMEOUT:
            send_data_frame(FRAME_ACK, 0, frame_expected, out_buf);             //直接返回ACK
            break;
        }

        if (nbuffered < NR_BUFS && phl_ready)
            enable_network_layer();
        else
            disable_network_layer();
    }
}