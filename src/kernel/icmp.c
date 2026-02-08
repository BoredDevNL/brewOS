#include "net_defs.h"
#include "cmd.h"
#include "memory_manager.h"
#include "wm.h"

static volatile bool ping_reply_received = false;
static uint16_t ping_id_counter = 0;
static uint16_t current_ping_id = 0;
static bool is_pinging = false;

void icmp_handle_packet(ipv4_address_t src, void *data, uint16_t len) {
    icmp_header_t *icmp = (icmp_header_t *)data;
    
    if (icmp->type == 0 && is_pinging && ntohs(icmp->id) == current_ping_id) { // Echo Reply
        ping_reply_received = true;
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

    is_pinging = true;
    const int payload_size = 8;
    uint8_t packet[sizeof(icmp_header_t) + payload_size];
    icmp_header_t *icmp = (icmp_header_t *)packet;

    for (int i = 0; i < 4; i++) {
        current_ping_id = ++ping_id_counter;
        icmp->type = 8; // Echo Request
        icmp->code = 0;
        icmp->id = htons(current_ping_id);
        icmp->sequence = htons(i + 1);
        icmp->checksum = 0;
        
        // Fill payload
        for (int j = 0; j < payload_size; j++) {
            packet[sizeof(icmp_header_t) + j] = (uint8_t)('a' + (j % 26));
        }

        icmp->checksum = net_checksum(packet, sizeof(packet));

        ping_reply_received = false;
        ip_send_packet(dest, IP_PROTO_ICMP, packet, sizeof(packet));


        uint32_t start_ticks = wm_get_ticks();
        while (!ping_reply_received && (wm_get_ticks() - start_ticks) < 180) { // 3 seconds timeout
            network_process_frames();
        }
        
        if (!ping_reply_received) {
            cmd_write("Request timed out. (Did you run 'netinit'?)\n");
        } else if (i < 3) {
            // Wait a bit before next ping
            uint32_t wait_start = wm_get_ticks();
            while ((wm_get_ticks() - wait_start) < 60) {
                 network_process_frames();
            }
        }
    }
    is_pinging = false;
}