#include "net_defs.h"
#include "cmd.h"

void cli_cmd_httpget(char *args) {
    if (!args || !*args) {
        cmd_write("Usage: httpget <hostname>\n");
        return;
    }
    
    cmd_write("Resolving host...\n");
    ipv4_address_t ip = dns_resolve(args);
    
    if (ip.bytes[0] == 0 && ip.bytes[1] == 0) {
        cmd_write("DNS Resolution failed.\n");
        return;
    }
    
    cmd_write("Connecting to ");
    cmd_write_int(ip.bytes[0]); cmd_write(".");
    cmd_write_int(ip.bytes[1]); cmd_write(".");
    cmd_write_int(ip.bytes[2]); cmd_write(".");
    cmd_write_int(ip.bytes[3]); cmd_write("...\n");
    
    tcp_socket_t *sock = tcp_connect(ip, 80);
    if (!sock) {
        cmd_write("Connection failed.\n");
        return;
    }
    
    cmd_write("Sending Request...\n");
    
    // Construct request
    tcp_send(sock, "GET / HTTP/1.1\r\nHost: ", 0);
    tcp_send(sock, args, 0);
    tcp_send(sock, "\r\nConnection: close\r\n\r\n", 0);
    
    cmd_write("Waiting for response...\n");
    // Wait for data (simple delay loop for demo)
    for(volatile int i=0; i<200000000; i++) {
        extern void network_process_frames(void);
        network_process_frames();
    }
    
    char buf[1024];
    int len = tcp_read(sock, buf, 1023);
    if (len > 0) {
        buf[len] = 0;
        cmd_write("\n--- Response ---\n");
        cmd_write(buf);
        cmd_write("\n----------------\n");
    } else {
        cmd_write("No data received.\n");
    }
    
    tcp_close(sock);
}