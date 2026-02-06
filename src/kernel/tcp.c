#include "net_defs.h"
#include "cmd.h"
#include "memory_manager.h"

// Simplified TCP State
typedef enum {
    TCP_CLOSED,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT
} tcp_state_enum;

struct tcp_socket_t {
    ipv4_address_t remote_ip;
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t seq_num;
    uint32_t ack_num;
    tcp_state_enum state;
    
    // Receive Buffer
    uint8_t *rx_buffer;
    int rx_size;
    int rx_pos;
    bool connected;
};

static tcp_socket_t *active_socket = NULL; // Single socket support for simplicity

// Pseudo Header for Checksum
typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t reserved;
    uint8_t protocol;
    uint16_t tcp_len;
} __attribute__((packed)) tcp_pseudo_header_t;

static uint16_t tcp_checksum(tcp_socket_t *sock, tcp_header_t *tcp, const void *data, uint16_t len) {
    uint32_t sum = 0;
    
    // Pseudo Header
    ipv4_address_t local = get_local_ip();
    tcp_pseudo_header_t ph;
    ph.src_ip = *(uint32_t*)local.bytes;
    ph.dst_ip = *(uint32_t*)sock->remote_ip.bytes;
    ph.reserved = 0;
    ph.protocol = IP_PROTO_TCP;
    ph.tcp_len = htons(sizeof(tcp_header_t) + len);
    
    uint16_t *p = (uint16_t*)&ph;
    for(int i=0; i<sizeof(tcp_pseudo_header_t)/2; i++) sum += p[i];
    
    // TCP Header + Data
    p = (uint16_t*)tcp;
    for(int i=0; i<sizeof(tcp_header_t)/2; i++) sum += p[i];
    
    p = (uint16_t*)data;
    int dlen = len;
    while(dlen > 1) {
        sum += *p++;
        dlen -= 2;
    }
    if(dlen) sum += *(uint8_t*)p;
    
    while(sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

void tcp_send_packet(tcp_socket_t *sock, uint8_t flags, const void *data, uint16_t len) {
    uint16_t total_len = sizeof(tcp_header_t) + len;
    uint8_t *packet = kmalloc(total_len);
    
    tcp_header_t *tcp = (tcp_header_t*)packet;
    tcp->src_port = htons(sock->local_port);
    tcp->dst_port = htons(sock->remote_port);
    tcp->seq_num = htonl(sock->seq_num);
    tcp->ack_num = htonl(sock->ack_num);
    tcp->data_offset = (sizeof(tcp_header_t) / 4) << 4;
    tcp->flags = flags;
    tcp->window_size = htons(8192);
    tcp->urgent_ptr = 0;
    tcp->checksum = 0;
    
    if (data) {
        // Copy data
        uint8_t *payload = packet + sizeof(tcp_header_t);
        const uint8_t *d = (const uint8_t*)data;
        for(int i=0; i<len; i++) payload[i] = d[i];
    }
    
    tcp->checksum = tcp_checksum(sock, tcp, data, len);
    
    ip_send_packet(sock->remote_ip, IP_PROTO_TCP, packet, total_len);
    kfree(packet);
    
    // Advance sequence for SYN/FIN or data
    if (len > 0 || (flags & (TCP_SYN|TCP_FIN))) {
        sock->seq_num += (len > 0 ? len : 1);
    }
}

void tcp_handle_packet(ipv4_address_t src, void *data, uint16_t len) {
    if (!active_socket) return;
    
    tcp_header_t *tcp = (tcp_header_t*)data;
    uint16_t data_len = len - ((tcp->data_offset >> 4) * 4);
    uint8_t *payload = (uint8_t*)data + ((tcp->data_offset >> 4) * 4);
    
    // Check ports
    if (ntohs(tcp->dst_port) != active_socket->local_port) return;
    
    // State Machine
    if (active_socket->state == TCP_SYN_SENT) {
        if ((tcp->flags & TCP_SYN) && (tcp->flags & TCP_ACK)) {
            active_socket->ack_num = ntohl(tcp->seq_num) + 1;
            active_socket->state = TCP_ESTABLISHED;
            active_socket->connected = true;
            // Send ACK
            tcp_send_packet(active_socket, TCP_ACK, NULL, 0);
        }
    } else if (active_socket->state == TCP_ESTABLISHED) {
        if (tcp->flags & TCP_FIN) {
            active_socket->ack_num = ntohl(tcp->seq_num) + 1;
            tcp_send_packet(active_socket, TCP_ACK | TCP_FIN, NULL, 0);
            active_socket->state = TCP_CLOSED;
            active_socket->connected = false;
        } else if (data_len > 0) {
            // Accept data
            if (active_socket->rx_pos < active_socket->rx_size) {
                for(int i=0; i<data_len && active_socket->rx_pos < active_socket->rx_size - 1; i++) {
                    active_socket->rx_buffer[active_socket->rx_pos++] = payload[i];
                }
                active_socket->rx_buffer[active_socket->rx_pos] = 0; // Null terminate for text
            }
            active_socket->ack_num = ntohl(tcp->seq_num) + data_len;
            tcp_send_packet(active_socket, TCP_ACK, NULL, 0);
        }
    }
}

tcp_socket_t* tcp_connect(ipv4_address_t ip, uint16_t port) {
    if (active_socket) kfree(active_socket);
    
    active_socket = kmalloc(sizeof(tcp_socket_t));
    active_socket->remote_ip = ip;
    active_socket->remote_port = port;
    active_socket->local_port = 49152 + (port % 1000); // Random-ish ephemeral
    active_socket->seq_num = 1000;
    active_socket->ack_num = 0;
    active_socket->state = TCP_SYN_SENT;
    active_socket->connected = false;
    active_socket->rx_buffer = kmalloc(65536); // 64KB buffer
    active_socket->rx_size = 65536;
    active_socket->rx_pos = 0;
    
    // Send SYN
    tcp_send_packet(active_socket, TCP_SYN, NULL, 0);
    
    // Wait for connection (Blocking)
    int timeout = 100000000;
    while (!active_socket->connected && timeout-- > 0);
    
    if (!active_socket->connected) {
        kfree(active_socket->rx_buffer);
        kfree(active_socket);
        active_socket = NULL;
        return NULL;
    }
    
    return active_socket;
}

void tcp_send(tcp_socket_t *sock, const char *data, int len) {
    if (!sock || !sock->connected) return;
    if (len == 0) {
        // Calculate strlen
        const char *p = data;
        while(*p++) len++;
    }
    tcp_send_packet(sock, TCP_PSH | TCP_ACK, data, len);
}

void tcp_close(tcp_socket_t *sock) {
    if (!sock) return;
    tcp_send_packet(sock, TCP_FIN | TCP_ACK, NULL, 0);
    sock->state = TCP_CLOSED;
    sock->connected = false;
    // Give time for packet to go out
    for(volatile int i=0; i<1000000; i++);
    kfree(sock->rx_buffer);
    kfree(sock);
    active_socket = NULL;
}

int tcp_read(tcp_socket_t *sock, char *buffer, int max_len) {
    if (!sock) return 0;
    // Simple copy of what we have
    int count = 0;
    for (int i = 0; i < sock->rx_pos && i < max_len; i++) {
        buffer[i] = sock->rx_buffer[i];
        count++;
    }
    return count;
}