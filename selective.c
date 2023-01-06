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
    s.kind = fk;                                                //����֡����
    s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);         //ack ���ڽ��մ������޼�һ
    stop_ack_timer();                                           //����֡���ر�ack��ʱʱ��
    if (fk == FRAME_DATA) {
        s.seq = frame_nr;                                       //֡���
        memcpy(s.info, buffer[frame_nr % NR_BUFS], PKT_LEN);    //�����������ݴӷ��ʹ��ڻ������֡��������
        dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short*)s.info);//������Ϣ
        put_frame((seq_nr*)&s, 3 + PKT_LEN);                    //��s+3+PKT_LENβ�������ֽ�crcУ�飬������s->crc32��Ȼ��������֡
        start_timer(frame_nr, DATA_TIMER);                      //��ʼ����֡��ʱ��ʱ��
    }
    else if (fk == FRAME_ACK) {
        dbg_frame("Send ACK  %d\n", s.ack);                     //������Ϣ
        put_frame((unsigned char*)&s, 2);                       //��s.ack�������crcУ�飬Ȼ���ͳ���Ϊ6�ֽڵ�ACK֡
    }
    else if (fk == FRAME_NAK) {
        dbg_frame("Send NAK  %d\n", (s.ack + 1) % (MAX_SEQ + 1));
        no_nak = 0;                                             //�Ѿ�����NAK
        put_frame((unsigned char*)&s, 2);                       //��s.ack�������crcУ�飬Ȼ���ͳ���Ϊ6�ֽڵ�NAK֡
    }
}
int main(int argc, char** argv)
{
    seq_nr ack_expected = 0;            //���ʹ�������
    seq_nr next_frame_to_send = 0;      //���ʹ�������
    seq_nr frame_expected = 0;          //���մ�������
    seq_nr too_far = NR_BUFS;           //���մ�������
    struct FRAME f;                     //����֡

    packet out_buf[NR_BUFS];            //���ʹ��ڣ����ͻ��棩
    packet in_buf[NR_BUFS];             //���մ��ڣ����ջ��棩

    int arrived[NR_BUFS];               //���մ��ڽ������
    for (int i = 0; i < NR_BUFS; i++) arrived[i] = 0;
    seq_nr nbuffered = 0;               //�ѷ��͵ȴ�ȷ�ϵ�֡��

    int event, arg;
    int len = 0;                        //���յ�����֡�ĳ���
    protocol_init(argc, argv);
    lprintf("Designed by Jiang Yanjun, build: " __DATE__"  "__TIME__"\n");
    lprintf("Process by CWJ, build: " __DATE__"  "__TIME__"\n");
    disable_network_layer();            //��ʼʱ��disable����㣬�ȴ������׼���ò�enable�����

    while (1) {
        event = wait_for_event(&arg);   //�ȴ��¼���DATA_TIMEOUT�¼�����������arg��¼�˳�ʱ֡��֡���

        switch (event) {
        case NETWORK_LAYER_READY:       //�����׼���ã� ����㷢�ͻ���δ�� && ���ʹ���δ�� ��
            nbuffered++;                //�ѷ���֡����һ
            get_packet(out_buf[next_frame_to_send % NR_BUFS]);//��������ȡһ�����ģ����뷢�ʹ�������
            send_data_frame(FRAME_DATA, next_frame_to_send, frame_expected, out_buf);//��������֡
            inc(next_frame_to_send);    //���ʹ������޼�һ
            break;

        case PHYSICAL_LAYER_READY:
            phl_ready = 1;
            break;

        case FRAME_RECEIVED:
            len = recv_frame((seq_nr*)&f, sizeof(f));
            if (len < 6 || crc32((seq_nr*)&f, len) != 0) {                      //�յ������֡
                dbg_event("**** Receiver Error, Bad CRC Checksum\n");
                if (no_nak)                                                     //���NAK��û������
                    send_data_frame(FRAME_NAK, 0, frame_expected, out_buf);     //����NAK���������Ϊ��������frame_expected������֡
                break;
            }
            if (f.kind == FRAME_ACK)
                dbg_frame("Recv ACK  %d\n", f.ack);
            if (f.kind == FRAME_DATA) {
                dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short*)f.info);
                if ((f.seq != frame_expected) && no_nak) {                      //����յ���֡���ǽ��մ������޵�����֡��ô���������Ϊ��������frame_expected������֡
                    dbg_event(" Recv frame is not lower bound, NAK sent back\n");
                    send_data_frame(FRAME_NAK, 0, frame_expected, out_buf);
                }
                
                if (between(frame_expected, f.seq, too_far) && arrived[f.seq % NR_BUFS] == 0)//��������֡��������ڽ��մ����ڣ����Ҹ�������ݵ�һ�ε���
                {
                    arrived[f.seq % NR_BUFS] = 1;                               //��ע�ѵ���
                    memcpy(in_buf[f.seq % NR_BUFS], f.info, PKT_LEN);           //������մ���
                    while (arrived[frame_expected % NR_BUFS])                   //�ƶ����մ���
                    {
                        put_packet(in_buf[frame_expected % NR_BUFS], PKT_LEN);  //�ϴ��������
                        no_nak = 1;                                             //����no_nak
                        arrived[frame_expected % NR_BUFS] = 0;                  //�����ϴ�������������֡
                        inc(frame_expected);                                    //�������մ���
                        inc(too_far);                                           //�������մ���
                        start_ack_timer(ACK_TIMER);
                    }
                }
            }
            if (f.kind == FRAME_NAK && between(ack_expected, (f.ack + 1) % (MAX_SEQ + 1), next_frame_to_send))  //�˼�Ҫ����ack+1Ҳ���ǶԷ����մ������޵�����
            {
                send_data_frame(FRAME_DATA, (f.ack + 1) % (MAX_SEQ + 1), frame_expected, out_buf);
            }
            while (between(ack_expected, f.ack, next_frame_to_send)) {           //���ܽ��յ���֡������֡��ACK֡����NAK֡��f.ack������Ч��
                nbuffered--;
                stop_timer(ack_expected);
                inc(ack_expected);
            }
            break;
        case DATA_TIMEOUT:
            dbg_event("---- DATA %d timeout\n", arg);
            send_data_frame(FRAME_DATA, arg, frame_expected, out_buf);          //�ط���ʱ����֡
            break;

        case ACK_TIMEOUT:
            send_data_frame(FRAME_ACK, 0, frame_expected, out_buf);             //ֱ�ӷ���ACK
            break;
        }

        if (nbuffered < NR_BUFS && phl_ready)
            enable_network_layer();
        else
            disable_network_layer();
    }
}