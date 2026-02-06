#include "net_defs.h"
#include "cmd.h"
#include "memory_manager.h"

static ipv4_address_t dns_result_ip;
static bool dns_resolved = false;

void dns_handle_response(void *data, uint16_t len) {
    dns_header_t *dns = (dns_header_t*)data;
    if ((ntohs(dns->flags) & 0x8000) == 0) return; // Not a response
    
    // Skip queries
    uint8_t *p = (uint8_t*)(dns + 1);
    for(int i=0; i<ntohs(dns->q_count); i++) {
        while(*p != 0) p += (*p) + 1; // Skip name
        p += 5; // Skip null + type + class
    }
    
    // Parse Answers
    for(int i=0; i<ntohs(dns->ans_count); i++) {
        // Name (pointer or label)
        if ((*p & 0xC0) == 0xC0) p += 2;
        else while(*p != 0) p += (*p) + 1;
        
        uint16_t type = ntohs(*(uint16_t*)p); p += 2;
        uint16_t class = ntohs(*(uint16_t*)p); p += 2;
        uint32_t ttl = ntohl(*(uint32_t*)p); p += 4;
        uint16_t dlen = ntohs(*(uint16_t*)p); p += 2;
        
        if (type == 1 && dlen == 4) { // A Record
            dns_result_ip.bytes[0] = p[0];
            dns_result_ip.bytes[1] = p[1];
            dns_result_ip.bytes[2] = p[2];
            dns_result_ip.bytes[3] = p[3];
            dns_resolved = true;
            return;
        }
        p += dlen;
    }
}

// Callback wrapper for the network stack
static void dns_udp_callback(const ipv4_address_t* src_ip, uint16_t src_port, const mac_address_t* src_mac, const void* data, size_t length) {
    (void)src_ip; (void)src_port; (void)src_mac;
    dns_handle_response((void*)data, (uint16_t)length);
}

ipv4_address_t dns_resolve(const char *hostname) {
    dns_resolved = false;
    dns_result_ip.bytes[0] = 0;
    
    if (!network_is_initialized()) {
        cmd_write("Error: Network not initialized. Run 'netinit' first.\n");
        return dns_result_ip;
    }
    
    // Register callback
    extern int udp_register_callback(uint16_t port, void (*callback)(const ipv4_address_t*, uint16_t, const mac_address_t*, const void*, size_t));
    udp_register_callback(5353, dns_udp_callback);
    
    // Construct Query
    uint8_t buf[512];
    dns_header_t *dns = (dns_header_t*)buf;
    dns->id = htons(0x1234);
    dns->flags = htons(0x0100); // Standard query
    dns->q_count = htons(1);
    dns->ans_count = 0;
    dns->auth_count = 0;
    dns->add_count = 0;
    
    uint8_t *p = buf + sizeof(dns_header_t);
    const char *h = hostname;
    while (*h) {
        const char *next = h;
        while (*next && *next != '.') next++;
        *p++ = (uint8_t)(next - h);
        for(int i=0; i<(next-h); i++) *p++ = h[i];
        h = next;
        if (*h == '.') h++;
    }
    *p++ = 0; // End of name
    *(uint16_t*)p = htons(1); p += 2; // Type A
    *(uint16_t*)p = htons(1); p += 2; // Class IN
    
    // Use DHCP provided DNS if available, otherwise fallback to Google
    ipv4_address_t dns_server = get_dns_server_ip();
    if (dns_server.bytes[0] == 0) {
        dns_server.bytes[0] = 8; dns_server.bytes[1] = 8; 
        dns_server.bytes[2] = 8; dns_server.bytes[3] = 8;
    }

    extern int udp_send_packet(const ipv4_address_t *dest, uint16_t dest_port, uint16_t src_port, const void *data, size_t len);
    
    // Retry loop to handle ARP resolution delay
    for (int i = 0; i < 3 && !dns_resolved; i++) {
        udp_send_packet(&dns_server, 53, 5353, buf, p - buf);
        
        // Wait loop
        int timeout = 20000000; 
        while (!dns_resolved && timeout-- > 0) {
            extern void network_process_frames(void);
            network_process_frames();
        }
    }
    
    return dns_result_ip;
}

void cli_cmd_dns(char *args) {
    if (!args || !*args) {
        cmd_write("Usage: dns <hostname>\n");
        return;
    }
    
    cmd_write("Resolving ");
    cmd_write(args);
    cmd_write("...\n");
    
    ipv4_address_t ip = dns_resolve(args);
    
    if (ip.bytes[0] == 0 && ip.bytes[1] == 0) {
        cmd_write("Resolution failed.\n");
    } else {
        cmd_write("IP: ");
        cmd_write_int(ip.bytes[0]); cmd_write(".");
        cmd_write_int(ip.bytes[1]); cmd_write(".");
        cmd_write_int(ip.bytes[2]); cmd_write(".");
        cmd_write_int(ip.bytes[3]); cmd_write("\n");
    }
}