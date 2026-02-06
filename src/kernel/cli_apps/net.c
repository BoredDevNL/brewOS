#include "cli_utils.h"
#include "network.h"
#include "fat32.h"
#include "cmd.h"
#include "memory_manager.h"

static void print_mac(const mac_address_t* mac){
    char buf[64];
    int p=0;
    for(int i=0;i<6;i++){
        int v=mac->bytes[i];
        int hi=(v>>4)&0xF, lo=v&0xF;
        buf[p++]= (hi<10)?('0'+hi):('A'+(hi-10));
        buf[p++]= (lo<10)?('0'+lo):('A'+(lo-10));
        if(i<5) buf[p++]=':';
    }
    buf[p]=0;
    cli_write(buf);
}

void cli_cmd_netinit(char *args){
    (void)args;
    int r=network_init();
    if(r==0){
        cli_write("Network initialized\n");
        int d=network_dhcp_acquire();
        if(d==0){
            cli_write("DHCP acquired\n");
        } else {
            cli_write("DHCP failed\n");
        }
    } else {
        cli_write("Network init failed\n");
    }
}

void cli_cmd_netinfo(char *args){
    (void)args;
    mac_address_t mac;
    ipv4_address_t ip;
    if(network_get_mac_address(&mac)==0){
        cli_write("MAC: "); print_mac(&mac); cli_write("\n");
    }
    if(network_get_ipv4_address(&ip)==0){
        cli_write("IP: ");
        for(int i=0;i<4;i++){ cli_write_int(ip.bytes[i]); if(i<3) cli_write("."); }
        cli_write("\n");
    }
    cli_write("Frames: "); cli_write_int(network_get_frames_received()); cli_write("\n");
    cli_write("UDP packets: "); cli_write_int(network_get_udp_packets_received()); cli_write("\n");
    cli_write("UDP callbacks: "); cli_write_int(network_get_udp_callbacks_called()); cli_write("\n");
    cli_write("E1000 receive calls: "); cli_write_int(network_get_e1000_receive_calls()); cli_write("\n");
    cli_write("E1000 receive empty: "); cli_write_int(network_get_e1000_receive_empty()); cli_write("\n");
    cli_write("Process calls: "); cli_write_int(network_get_process_calls()); cli_write("\n");
}

void cli_cmd_ipset(char *args){
    if(!args||!*args){ cli_write("Usage: IPSET a.b.c.d\n"); return; }
    ipv4_address_t ip={{0,0,0,0}};
    int part=0; int val=0; int i=0;
    while(args[i]){
        char ch=args[i++];
        if(ch>='0'&&ch<='9'){ val=val*10+(ch-'0'); if(val>255){ cli_write("Invalid IP\n"); return; } }
        else if(ch=='.'){ if(part>3){ cli_write("Invalid IP\n"); return; } ip.bytes[part++]=(uint8_t)val; val=0; }
        else { cli_write("Invalid IP\n"); return; }
    }
    if(part!=3){ cli_write("Invalid IP\n"); return; }
    ip.bytes[3]=(uint8_t)val;
    if(network_set_ipv4_address(&ip)==0){ cli_write("IP set\n"); } else { cli_write("IP set failed\n"); }
}

void cli_cmd_udpsend(char *args){
    if(!args||!*args){ cli_write("Usage: UDPSEND ip port data\n"); return; }
    char ipstr[32]; int pos=0;
    while(args[pos] && args[pos]!=' '){ ipstr[pos]=args[pos]; pos++; }
    ipstr[pos]=0;
    while(args[pos]==' ') pos++;
    char portstr[16]; int p=0;
    while(args[pos] && args[pos]!=' '){ portstr[p++]=args[pos++]; }
    portstr[p]=0;
    while(args[pos]==' ') pos++;
    char* datastr = args+pos;
    ipv4_address_t ip={{0,0,0,0}};
    int idx=0; int val=0; int j=0;
    while(ipstr[j]){
        char ch=ipstr[j++];
        if(ch>='0'&&ch<='9'){ val=val*10+(ch-'0'); if(val>255){ cli_write("Invalid IP\n"); return; } }
        else if(ch=='.'){ if(idx>3){ cli_write("Invalid IP\n"); return; } ip.bytes[idx++]=(uint8_t)val; val=0; }
        else { cli_write("Invalid IP\n"); return; }
    }
    if(idx!=3){ cli_write("Invalid IP\n"); return; }
    ip.bytes[3]=(uint8_t)val;
    int port=0; int k=0; while(portstr[k]){ char ch=portstr[k++]; if(ch<'0'||ch>'9'){ cli_write("Invalid port\n"); return; } port=port*10+(ch-'0'); }
    if(port<=0||port>65535){ cli_write("Invalid port\n"); return; }
    int len=(int)cli_strlen(datastr);
    if(len<=0){ cli_write("No data\n"); return; }
    int r=udp_send_packet(&ip,(uint16_t)port,12345,datastr,(size_t)len);
    if(r==0) cli_write("Sent\n"); else cli_write("Send failed\n");
}

static void udp_print_callback(const ipv4_address_t* src_ip,uint16_t src_port,const mac_address_t* src_mac,const void* data,size_t length){
    (void)src_mac;
    
    FAT32_FileHandle *fh = fat32_open("messages", "a");
    if (fh) {
        char buf[32];
        fat32_write(fh, "UDP from ", 9);
        
        // Write IP
        for(int i=0;i<4;i++){ 
            cli_itoa(src_ip->bytes[i], buf);
            fat32_write(fh, buf, cli_strlen(buf));
            if(i<3) fat32_write(fh, ".", 1);
        }
        
        fat32_write(fh, ":", 1);
        
        // Write Port
        cli_itoa(src_port, buf);
        fat32_write(fh, buf, cli_strlen(buf));
        
        fat32_write(fh, " ", 1);
        
        // Write Message
        fat32_write(fh, data, length);
        fat32_write(fh, "\n", 1);
        
        fat32_close(fh);
        
        cmd_increment_msg_count();
    }
}

void cli_cmd_udptest(char *args){
    if(!args||!*args){ cli_write("Usage: UDPTEST port\n"); return; }
    int port=cli_atoi(args);
    if(port<=0||port>65535){ cli_write("Invalid port\n"); return; }
    if(udp_register_callback((uint16_t)port,udp_print_callback)==0) cli_write("UDP callback registered\n"); else cli_write("Register failed\n");
}

void cli_cmd_msgrc(char *args) {
    (void)args;
    // Reset message count since we are viewing them
    cmd_reset_msg_count();

    FAT32_FileHandle *fh = fat32_open("messages", "r");
    if (!fh) {
        cli_write("No messages.\n");
        return;
    }

    uint32_t size = fh->size;
    if (size == 0) {
        fat32_close(fh);
        cli_write("No messages.\n");
        return;
    }

    char *buffer = (char*)kmalloc(size + 1);
    if (!buffer) {
        fat32_close(fh);
        cli_write("Error: Out of memory\n");
        return;
    }

    fat32_read(fh, buffer, size);
    buffer[size] = 0;
    fat32_close(fh);

    int count = 0;
    int pos = size - 1;

    while (count < 10 && pos >= 0) {
        // Skip trailing newlines/whitespace
        while (pos >= 0 && (buffer[pos] == '\n' || buffer[pos] == '\r')) {
            buffer[pos] = 0; 
            pos--;
        }
        if (pos < 0) break;

        // Find start of line
        while (pos >= 0 && buffer[pos] != '\n' && buffer[pos] != '\r') pos--;
        
        cli_write(&buffer[pos + 1]);
        cli_write("\n");
        count++;
    }

    kfree(buffer);
}
