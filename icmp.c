#include "net_defs.h"
#include "cmd.h"
#include "memory_manager.h"

static volatile bool ping_reply_received = false;
static uint16_t ping_id_counter = 0;

void icmp_handle_packet(ipv4_address_t src, void *data, uint16_t len) {
    icmp_header_t *icmp = (icmp_header_t *)data;
    
    if (icmp->type == 0) { // Echo Reply
        ping_reply_received = true;
        char buf[64];
        // Simple output
        cmd_write("Reply from ");
        cmd_write_int(src.bytes[0]); cmd_write(".");
        cmd_write_int(src.bytes[1]); cmd_write(".");
        cmd_write_int(src.bytes[2]); cmd_write(".");
        cmd_write_int(src.bytes[3]); 
        cmd_write(": bytes=");
        cmd_write_int(len - sizeof(icmp_header_t));
        cmd_write(" seq=");
        cmd_write_int(ntohs(icmp->sequence));
        cmd_write("\n");
    }
}

void cli_cmd_ping(char *args) {
    if (!args || !*args) {
        cmd_write("Usage: ping <ip>\n");
        return;
    }

    // Parse IP (Simplified)
    ipv4_address_t dest;
    int ip_parts[4];
    const char *p = args;
    for(int i=0; i<4; i++) {
        ip_parts[i] = 0;
        while(*p >= '0' && *p <= '9') {
            ip_parts[i] = ip_parts[i]*10 + (*p - '0');
            p++;
        }
        if(*p == '.') p++;
        dest.bytes[i] = (uint8_t)ip_parts[i];
    }

    cmd_write("Pinging...\n");

    for (int i = 0; i < 4; i++) {
        icmp_header_t icmp;
        icmp.type = 8; // Echo Request
        icmp.code = 0;
        icmp.id = htons(++ping_id_counter);
        icmp.sequence = htons(i + 1);
        icmp.checksum = 0;
        icmp.checksum = net_checksum(&icmp, sizeof(icmp_header_t));

        ping_reply_received = false;
        ip_send_packet(dest, IP_PROTO_ICMP, &icmp, sizeof(icmp_header_t));

        // Simple busy wait for ~1 second (assuming loop speed)
        for(volatile int w=0; w<100000000 && !ping_reply_received; w++);
    }
}