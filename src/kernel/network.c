#include <stdint.h>
#include <stddef.h>
#include "network.h"
#include "e1000.h"
#include "pci.h"

static int network_initialized = 0;
static mac_address_t our_mac;
static ipv4_address_t ip_address = {{0,0,0,0}};
static uint16_t ipv4_id_counter = 0;

typedef struct { ipv4_address_t ip; mac_address_t mac; uint32_t timestamp; int valid; } arp_cache_entry_t;
#define ARP_CACHE_SIZE 16
static arp_cache_entry_t arp_cache[ARP_CACHE_SIZE];
static int arp_cache_initialized = 0;

#define UDP_MAX_CALLBACKS 8
typedef struct { uint16_t port; udp_callback_t callback; int valid; } udp_callback_entry_t;
static udp_callback_entry_t udp_callbacks[UDP_MAX_CALLBACKS];

static int frames_received_count = 0;
static int udp_packets_received_count = 0;
static int udp_callbacks_called_count = 0;
static int e1000_receive_calls = 0;
static int e1000_receive_empty = 0;
static int network_process_calls = 0;

static void* kmemcpy(void* d, const void* s, size_t n){uint8_t*D=d;const uint8_t*S=s;for(size_t i=0;i<n;i++)D[i]=S[i];return d;}
static void* kmemset(void* d,int v,size_t n){uint8_t*D=d;for(size_t i=0;i<n;i++)D[i]=(uint8_t)v;return d;}
static int kmemcmp(const void* a,const void* b,size_t n){const uint8_t*A=a;const uint8_t*B=b;for(size_t i=0;i<n;i++){if(A[i]!=B[i])return (int)A[i]-(int)B[i];}return 0;}

static uint16_t htons(uint16_t x){return (uint16_t)(((x&0xFF)<<8)|((x>>8)&0xFF));}
static uint16_t ntohs(uint16_t x){return htons(x);}
static uint32_t htonl(uint32_t v){return ((v&0xFF)<<24)|((v&0xFF00)<<8)|((v>>8)&0xFF00)|((v>>24)&0xFF);}

static uint16_t ipv4_checksum(const ipv4_header_t* h){
    uint32_t sum=0;const uint16_t* w=(const uint16_t*)h;
    for(int i=0;i<10;i++) sum+=w[i];
    while(sum>>16) sum=(sum&0xFFFF)+(sum>>16);
    return (uint16_t)(~sum);
}

static void arp_cache_init(void){ if(arp_cache_initialized) return; kmemset(arp_cache,0,sizeof(arp_cache)); arp_cache_initialized=1; }
static arp_cache_entry_t* arp_cache_find(const ipv4_address_t* ip){ for(int i=0;i<ARP_CACHE_SIZE;i++){ if(arp_cache[i].valid && kmemcmp(&arp_cache[i].ip, ip, sizeof(ipv4_address_t))==0) return &arp_cache[i]; } return NULL; }
static void arp_cache_add(const ipv4_address_t* ip,const mac_address_t* mac){ arp_cache_entry_t* e=arp_cache_find(ip); if(e){ kmemcpy(&e->mac,mac,sizeof(mac_address_t)); e->timestamp=0; return;} for(int i=0;i<ARP_CACHE_SIZE;i++){ if(!arp_cache[i].valid){ kmemcpy(&arp_cache[i].ip,ip,sizeof(ipv4_address_t)); kmemcpy(&arp_cache[i].mac,mac,sizeof(mac_address_t)); arp_cache[i].timestamp=0; arp_cache[i].valid=1; return; } } kmemcpy(&arp_cache[0].ip,ip,sizeof(ipv4_address_t)); kmemcpy(&arp_cache[0].mac,mac,sizeof(mac_address_t)); arp_cache[0].timestamp=0; arp_cache[0].valid=1; }

int network_init(void){
    if(network_initialized) return 0;
    pci_device_t device;
    if(!pci_find_device(E1000_VENDOR_ID,E1000_DEVICE_ID_82540EM,&device)) return -1;
    if(e1000_init(&device)!=0) return -1;
    if(network_get_mac_address(&our_mac)!=0) return -1;
    arp_cache_init();
    kmemset(udp_callbacks,0,sizeof(udp_callbacks));
    network_initialized=1;
    return 0;
}

int network_get_mac_address(mac_address_t* mac){
    e1000_device_t* dev=e1000_get_device();
    if(!dev) return -1;
    *mac=*(mac_address_t*)&dev->mac_address;
    return 0;
}

int network_get_ipv4_address(ipv4_address_t* ip){ if(!network_initialized) return -1; *ip=ip_address; return 0; }
int network_set_ipv4_address(const ipv4_address_t* ip){ if(!network_initialized) return -1; ip_address=*ip; return 0; }

int network_send_frame(const void* data,size_t length){ if(!network_initialized) return -1; if(length>ETH_FRAME_MAX_SIZE) return -1; return e1000_send_packet(data,length); }

int network_receive_frame(void* buffer,size_t buffer_size){
    if(!network_initialized) return 0;
    e1000_receive_calls++;
    int result=e1000_receive_packet(buffer,buffer_size);
    if(result==0) e1000_receive_empty++;
    return result;
}

void network_process_frames(void){
    network_process_calls++;
    if(!network_initialized) return;
    uint8_t frame_buffer[ETH_FRAME_MAX_SIZE];
    int frame_length;
    while((frame_length=network_receive_frame(frame_buffer,sizeof(frame_buffer)))>0){
        frames_received_count++;
        if(frame_length<(int)sizeof(eth_header_t)) continue;
        eth_header_t* eth=(eth_header_t*)frame_buffer;
        uint16_t ethertype=ntohs(eth->ethertype);
        int is_broadcast=1; int is_for_us=1;
        for(int i=0;i<6;i++){ if(eth->dest_mac[i]!=0xFF) is_broadcast=0; if(eth->dest_mac[i]!=our_mac.bytes[i]) is_for_us=0; }
        if(!is_broadcast && !is_for_us) continue;
        void* payload=frame_buffer+sizeof(eth_header_t);
        size_t payload_length=frame_length-sizeof(eth_header_t);
        if(ethertype==ETH_ETHERTYPE_ARP){
            if(payload_length>=sizeof(arp_header_t)) arp_process_packet((arp_header_t*)payload,payload_length);
        } else if(ethertype==ETH_ETHERTYPE_IPV4){
            if(payload_length>=sizeof(ipv4_header_t)){
                ipv4_header_t* ip=(ipv4_header_t*)payload;
                uint16_t checksum=ip->checksum;
                ip->checksum=0;
                uint16_t calc=ipv4_checksum(ip);
                ip->checksum=checksum;
                if(checksum==calc){
                    int for_our_ip=1;
                    for(int i=0;i<4;i++){ if(ip->dest_ip[i]!=ip_address.bytes[i]) { for_our_ip=0; break; } }
                    if(for_our_ip || ip->dest_ip[0]==255){
                        mac_address_t src_mac;
                        kmemcpy(src_mac.bytes,eth->src_mac,6);
                        ipv4_process_packet(ip,&src_mac,payload_length);
                    }
                }
            }
        }
    }
}

int arp_send_request(const ipv4_address_t* target_ip){
    if(!network_initialized) return -1;
    uint8_t frame[ETH_FRAME_MAX_SIZE];
    eth_header_t* eth=(eth_header_t*)frame;
    arp_header_t* arp=(arp_header_t*)(frame+sizeof(eth_header_t));
    for(int i=0;i<6;i++) eth->dest_mac[i]=0xFF;
    kmemcpy(eth->src_mac,our_mac.bytes,6);
    eth->ethertype=htons(ETH_ETHERTYPE_ARP);
    arp->hw_type=htons(1);
    arp->proto_type=htons(ETH_ETHERTYPE_IPV4);
    arp->hw_len=6;
    arp->proto_len=4;
    arp->opcode=htons(ARP_OP_REQUEST);
    kmemcpy(arp->sender_mac,our_mac.bytes,6);
    kmemcpy(arp->sender_ip,ip_address.bytes,4);
    for(int i=0;i<6;i++) arp->target_mac[i]=0;
    kmemcpy(arp->target_ip,target_ip->bytes,4);
    size_t frame_length=sizeof(eth_header_t)+sizeof(arp_header_t);
    return network_send_frame(frame,frame_length);
}

int arp_lookup(const ipv4_address_t* ip,mac_address_t* mac){
    if(!network_initialized) return -1;
    arp_cache_entry_t* e=arp_cache_find(ip);
    if(e && e->valid){ *mac=e->mac; return 0; }
    arp_send_request(ip);
    return -1;
}

void arp_process_packet(const arp_header_t* arp,size_t length){
    if(length<sizeof(arp_header_t)) return;
    if(ntohs(arp->hw_type)!=1 || ntohs(arp->proto_type)!=ETH_ETHERTYPE_IPV4) return;
    uint16_t opcode=ntohs(arp->opcode);
    ipv4_address_t sender_ip; mac_address_t sender_mac;
    kmemcpy(sender_ip.bytes,arp->sender_ip,4);
    kmemcpy(sender_mac.bytes,arp->sender_mac,6);
    arp_cache_add(&sender_ip,&sender_mac);
    if(opcode==ARP_OP_REQUEST){
        int is_for_us=1;
        for(int i=0;i<4;i++){ if(arp->target_ip[i]!=ip_address.bytes[i]) { is_for_us=0; break; } }
        if(is_for_us){
            uint8_t frame[ETH_FRAME_MAX_SIZE];
            eth_header_t* eth=(eth_header_t*)frame;
            arp_header_t* r=(arp_header_t*)(frame+sizeof(eth_header_t));
            kmemcpy(eth->dest_mac,arp->sender_mac,6);
            kmemcpy(eth->src_mac,our_mac.bytes,6);
            eth->ethertype=htons(ETH_ETHERTYPE_ARP);
            r->hw_type=htons(1);
            r->proto_type=htons(ETH_ETHERTYPE_IPV4);
            r->hw_len=6;
            r->proto_len=4;
            r->opcode=htons(ARP_OP_REPLY);
            kmemcpy(r->sender_mac,our_mac.bytes,6);
            kmemcpy(r->sender_ip,ip_address.bytes,4);
            kmemcpy(r->target_mac,arp->sender_mac,6);
            kmemcpy(r->target_ip,arp->sender_ip,4);
            size_t frame_length=sizeof(eth_header_t)+sizeof(arp_header_t);
            network_send_frame(frame,frame_length);
        }
    }
}

int ipv4_send_packet(const ipv4_address_t* dest_ip,uint8_t protocol,const void* data,size_t data_length){
    if(!network_initialized) return -1;
    mac_address_t dest_mac;
    int is_bcast=(dest_ip->bytes[0]==255 && dest_ip->bytes[1]==255 && dest_ip->bytes[2]==255 && dest_ip->bytes[3]==255);
    if(is_bcast){ for(int i=0;i<6;i++) dest_mac.bytes[i]=0xFF; }
    else { int ok=arp_lookup(dest_ip,&dest_mac); if(ok!=0){ for(int i=0;i<6;i++) dest_mac.bytes[i]=0xFF; } }
    uint8_t frame[ETH_FRAME_MAX_SIZE];
    eth_header_t* eth=(eth_header_t*)frame;
    ipv4_header_t* ip=(ipv4_header_t*)(frame+sizeof(eth_header_t));
    void* ip_payload=frame+sizeof(eth_header_t)+sizeof(ipv4_header_t);
    kmemcpy(eth->dest_mac,dest_mac.bytes,6);
    kmemcpy(eth->src_mac,our_mac.bytes,6);
    eth->ethertype=htons(ETH_ETHERTYPE_IPV4);
    ip->version_ihl=(4<<4)|5;
    ip->tos=0;
    ip->total_length=htons(sizeof(ipv4_header_t)+data_length);
    ip->id=htons(ipv4_id_counter++);
    ip->flags_frag=0;
    ip->ttl=64;
    ip->protocol=protocol;
    ip->checksum=0;
    kmemcpy(ip->src_ip,ip_address.bytes,4);
    kmemcpy(ip->dest_ip,dest_ip->bytes,4);
    ip->checksum=ipv4_checksum(ip);
    kmemcpy(ip_payload,data,data_length);
    size_t frame_length=sizeof(eth_header_t)+sizeof(ipv4_header_t)+data_length;
    return network_send_frame(frame,frame_length);
}

int ipv4_send_packet_to_mac(const ipv4_address_t* dest_ip,const mac_address_t* dest_mac,uint8_t protocol,const void* data,size_t data_length){
    if(!network_initialized) return -1;
    uint8_t frame[ETH_FRAME_MAX_SIZE];
    eth_header_t* eth=(eth_header_t*)frame;
    ipv4_header_t* ip=(ipv4_header_t*)(frame+sizeof(eth_header_t));
    void* ip_payload=frame+sizeof(eth_header_t)+sizeof(ipv4_header_t);
    kmemcpy(eth->dest_mac,dest_mac->bytes,6);
    kmemcpy(eth->src_mac,our_mac.bytes,6);
    eth->ethertype=htons(ETH_ETHERTYPE_IPV4);
    ip->version_ihl=(4<<4)|5;
    ip->tos=0;
    ip->total_length=htons(sizeof(ipv4_header_t)+data_length);
    ip->id=htons(ipv4_id_counter++);
    ip->flags_frag=0;
    ip->ttl=64;
    ip->protocol=protocol;
    ip->checksum=0;
    kmemcpy(ip->src_ip,ip_address.bytes,4);
    kmemcpy(ip->dest_ip,dest_ip->bytes,4);
    ip->checksum=ipv4_checksum(ip);
    kmemcpy(ip_payload,data,data_length);
    size_t frame_length=sizeof(eth_header_t)+sizeof(ipv4_header_t)+data_length;
    return network_send_frame(frame,frame_length);
}

void ipv4_process_packet(const ipv4_header_t* ip,const mac_address_t* src_mac,size_t length){
    if(length<sizeof(ipv4_header_t)) return;
    uint8_t ihl=(ip->version_ihl & 0x0F)*4;
    if(ihl<20 || length<ihl) return;
    uint16_t total_length=ntohs(ip->total_length);
    if(total_length>length) return;
    void* payload=(void*)ip+ihl;
    size_t payload_length=total_length-ihl;
    if(ip->protocol==IP_PROTO_UDP){
        if(payload_length>=sizeof(udp_header_t)){
            udp_packets_received_count++;
            ipv4_address_t src_ip;
            kmemcpy(src_ip.bytes,ip->src_ip,4);
            udp_header_t* udp=(udp_header_t*)payload;
            uint16_t dest_port=ntohs(udp->dest_port);
            uint16_t src_port=ntohs(udp->src_port);
            uint16_t udp_length=ntohs(udp->length);
            if(udp_length>payload_length) return;
            void* udp_payload=(void*)udp+sizeof(udp_header_t);
            size_t udp_payload_length=udp_length-sizeof(udp_header_t);
            for(int i=0;i<UDP_MAX_CALLBACKS;i++){
                if(udp_callbacks[i].valid && udp_callbacks[i].port==dest_port){
                    udp_callbacks_called_count++;
                    udp_callbacks[i].callback(&src_ip,src_port,src_mac,udp_payload,udp_payload_length);
                    return;
                }
            }
        }
    }
}

int udp_send_packet(const ipv4_address_t* dest_ip,uint16_t dest_port,uint16_t src_port,const void* data,size_t data_length){
    if(!network_initialized) return -1;
    uint8_t udp_packet[ETH_FRAME_MAX_SIZE];
    udp_header_t* udp=(udp_header_t*)udp_packet;
    void* udp_payload=udp_packet+sizeof(udp_header_t);
    udp->src_port=htons(src_port);
    udp->dest_port=htons(dest_port);
    udp->length=htons(sizeof(udp_header_t)+data_length);
    udp->checksum=0;
    kmemcpy(udp_payload,data,data_length);
    size_t udp_packet_length=sizeof(udp_header_t)+data_length;
    return ipv4_send_packet(dest_ip,IP_PROTO_UDP,udp_packet,udp_packet_length);
}

int udp_send_packet_to_mac(const ipv4_address_t* dest_ip,const mac_address_t* dest_mac,uint16_t dest_port,uint16_t src_port,const void* data,size_t data_length){
    if(!network_initialized) return -1;
    uint8_t udp_packet[ETH_FRAME_MAX_SIZE];
    udp_header_t* udp=(udp_header_t*)udp_packet;
    void* udp_payload=udp_packet+sizeof(udp_header_t);
    udp->src_port=htons(src_port);
    udp->dest_port=htons(dest_port);
    udp->length=htons(sizeof(udp_header_t)+data_length);
    udp->checksum=0;
    kmemcpy(udp_payload,data,data_length);
    size_t udp_packet_length=sizeof(udp_header_t)+data_length;
    return ipv4_send_packet_to_mac(dest_ip,dest_mac,IP_PROTO_UDP,udp_packet,udp_packet_length);
}

int udp_register_callback(uint16_t port,udp_callback_t callback){
    if(!network_initialized) return -1;
    for(int i=0;i<UDP_MAX_CALLBACKS;i++){
        if(!udp_callbacks[i].valid || udp_callbacks[i].port==port){
            udp_callbacks[i].port=port;
            udp_callbacks[i].callback=callback;
            udp_callbacks[i].valid=1;
            return 0;
        }
    }
    return -1;
}

int network_is_initialized(void){ return network_initialized; }
int network_get_frames_received(void){ return frames_received_count; }
int network_get_udp_packets_received(void){ return udp_packets_received_count; }
int network_get_udp_callbacks_called(void){ return udp_callbacks_called_count; }
int network_get_e1000_receive_calls(void){ return e1000_receive_calls; }
int network_get_e1000_receive_empty(void){ return e1000_receive_empty; }
int network_get_process_calls(void){ return network_process_calls; }

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_MAGIC_COOKIE 0x63825363U
#define DHCP_OP_BOOTREQUEST 1
#define DHCP_OP_BOOTREPLY   2
#define DHCP_HTYPE_ETHERNET 1
#define DHCP_HLEN_ETHERNET  6
#define DHCP_MSG_DISCOVER 1
#define DHCP_MSG_OFFER    2
#define DHCP_MSG_REQUEST  3
#define DHCP_MSG_ACK      5
#define DHCP_MSG_NAK      6
#define DHCP_OPT_MSG_TYPE 53
#define DHCP_OPT_SERVER_ID 54
#define DHCP_OPT_REQ_IP 50
#define DHCP_OPT_PARAM_REQ_LIST 55
#define DHCP_OPT_END 255

typedef struct {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic_cookie;
    uint8_t  options[312];
} __attribute__((packed)) dhcp_packet_t;

static volatile int dhcp_state = 0;
static uint32_t dhcp_xid = 0;
static ipv4_address_t dhcp_offered_ip;
static uint32_t dhcp_server_id = 0;

static uint32_t htonl32(uint32_t v){return ((v&0xFF)<<24)|((v&0xFF00)<<8)|((v>>8)&0xFF00)|((v>>24)&0xFF);}
static uint32_t ntohl32(uint32_t v){return htonl32(v);}
static uint16_t htons16(uint16_t v){return (uint16_t)(((v&0xFF)<<8)|((v>>8)&0xFF));}

static void dhcp_build_discover(dhcp_packet_t* pkt){
    kmemset(pkt,0,sizeof(dhcp_packet_t));
    pkt->op=DHCP_OP_BOOTREQUEST; pkt->htype=DHCP_HTYPE_ETHERNET; pkt->hlen=DHCP_HLEN_ETHERNET; pkt->xid=htonl32(dhcp_xid); pkt->flags=htons16(0x8000);
    kmemcpy(pkt->chaddr,our_mac.bytes,6);
    pkt->magic_cookie=htonl32(DHCP_MAGIC_COOKIE);
    uint8_t* opt=pkt->options;
    *opt++=DHCP_OPT_MSG_TYPE; *opt++=1; *opt++=DHCP_MSG_DISCOVER;
    *opt++=DHCP_OPT_PARAM_REQ_LIST; *opt++=3; *opt++=1; *opt++=3; *opt++=6;
    *opt++=DHCP_OPT_END;
}

static void dhcp_build_request(dhcp_packet_t* pkt){
    kmemset(pkt,0,sizeof(dhcp_packet_t));
    pkt->op=DHCP_OP_BOOTREQUEST; pkt->htype=DHCP_HTYPE_ETHERNET; pkt->hlen=DHCP_HLEN_ETHERNET; pkt->xid=htonl32(dhcp_xid); pkt->flags=htons16(0x8000);
    kmemcpy(pkt->chaddr,our_mac.bytes,6);
    pkt->magic_cookie=htonl32(DHCP_MAGIC_COOKIE);
    uint8_t* opt=pkt->options;
    *opt++=DHCP_OPT_MSG_TYPE; *opt++=1; *opt++=DHCP_MSG_REQUEST;
    *opt++=DHCP_OPT_REQ_IP; *opt++=4; *opt++=dhcp_offered_ip.bytes[0]; *opt++=dhcp_offered_ip.bytes[1]; *opt++=dhcp_offered_ip.bytes[2]; *opt++=dhcp_offered_ip.bytes[3];
    *opt++=DHCP_OPT_SERVER_ID; *opt++=4;
    *opt++=(uint8_t)((dhcp_server_id>>24)&0xFF); *opt++=(uint8_t)((dhcp_server_id>>16)&0xFF); *opt++=(uint8_t)((dhcp_server_id>>8)&0xFF); *opt++=(uint8_t)(dhcp_server_id&0xFF);
    *opt++=DHCP_OPT_END;
}

static uint8_t dhcp_get_option(const uint8_t* opts,uint8_t code){
    const uint8_t* p=opts;
    while(*p!=DHCP_OPT_END){ uint8_t c=*p++; uint8_t l=*p++; if(c==code) return p[0]; p+=l; }
    return 0;
}

static void dhcp_udp_callback(const ipv4_address_t* src_ip,uint16_t src_port,const mac_address_t* src_mac,const void* payload,size_t payload_length){
    (void)src_ip; (void)src_mac;
    if(src_port!=DHCP_SERVER_PORT || payload_length<sizeof(dhcp_packet_t)-312) return;
    dhcp_packet_t* pkt=(dhcp_packet_t*)payload;
    if(pkt->op!=DHCP_OP_BOOTREPLY) return;
    if(ntohl32(pkt->xid)!=dhcp_xid) return;
    if(ntohl32(pkt->magic_cookie)!=DHCP_MAGIC_COOKIE) return;
    uint8_t mtype=dhcp_get_option(pkt->options,DHCP_OPT_MSG_TYPE);
    if(mtype==DHCP_MSG_OFFER){
        uint32_t yi_host=ntohl32(pkt->yiaddr);
        dhcp_offered_ip.bytes[0]=(uint8_t)((yi_host>>24)&0xFF);
        dhcp_offered_ip.bytes[1]=(uint8_t)((yi_host>>16)&0xFF);
        dhcp_offered_ip.bytes[2]=(uint8_t)((yi_host>>8)&0xFF);
        dhcp_offered_ip.bytes[3]=(uint8_t)(yi_host&0xFF);
        const uint8_t* p=pkt->options; dhcp_server_id=0;
        while(*p!=DHCP_OPT_END){ uint8_t c=*p++; uint8_t l=*p++; if(c==DHCP_OPT_SERVER_ID && l==4){ dhcp_server_id=((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|(uint32_t)p[3]; break; } p+=l; }
        if(dhcp_server_id!=0) dhcp_state=1;
    } else if(mtype==DHCP_MSG_ACK){
        uint32_t yi_host=ntohl32(pkt->yiaddr);
        ip_address.bytes[0]=(uint8_t)((yi_host>>24)&0xFF);
        ip_address.bytes[1]=(uint8_t)((yi_host>>16)&0xFF);
        ip_address.bytes[2]=(uint8_t)((yi_host>>8)&0xFF);
        ip_address.bytes[3]=(uint8_t)(yi_host&0xFF);
        dhcp_state=2;
    } else if(mtype==DHCP_MSG_NAK){
        dhcp_state=-1;
    }
}

int network_dhcp_acquire(void){
    if(!network_initialized) return -1;
    if(udp_register_callback(DHCP_CLIENT_PORT,dhcp_udp_callback)!=0) return -1;
    dhcp_xid += 0x12345u + (uint32_t)ipv4_id_counter;
    dhcp_state=0; dhcp_server_id=0;
    dhcp_packet_t pkt;
    dhcp_build_discover(&pkt);
    ipv4_address_t bcast={{255,255,255,255}};
    udp_send_packet(&bcast,DHCP_SERVER_PORT,DHCP_CLIENT_PORT,&pkt,sizeof(dhcp_packet_t));
    for(int i=0;i<500000 && dhcp_state==0;i++){ network_process_frames(); if(i%1000==0){ for(volatile int d=0; d<100000; d++){} } }
    if(dhcp_state!=1) return -1;
    dhcp_build_request(&pkt);
    udp_send_packet(&bcast,DHCP_SERVER_PORT,DHCP_CLIENT_PORT,&pkt,sizeof(dhcp_packet_t));
    for(int i=0;i<500000 && dhcp_state==1;i++){ network_process_frames(); if(i%1000==0){ for(volatile int d=0; d<100000; d++){} } }
    return (dhcp_state==2)?0:-1;
}
