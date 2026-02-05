#include "cli_utils.h"
#include "pci.h"

static void print_hex16(uint16_t v){
    char buf[7]; buf[0]='0'; buf[1]='x';
    for(int i=0;i<4;i++){
        int nyb=(v >> ((3-i)*4)) & 0xF;
        buf[2+i]= nyb<10?('0'+nyb):('A'+(nyb-10));
    }
    buf[6]=0;
    cli_write(buf);
}

static void print_hex8(uint8_t v){
    char buf[5]; buf[0]='0'; buf[1]='x';
    int hi=(v>>4)&0xF, lo=v&0xF;
    buf[2]= hi<10?('0'+hi):('A'+(hi-10));
    buf[3]= lo<10?('0'+lo):('A'+(lo-10));
    buf[4]=0;
    cli_write(buf);
}

void cli_cmd_pcilist(char *args){
    (void)args;
    pci_device_t devs[64];
    int n=pci_enumerate_devices(devs,64);
    cli_write("PCI devices:\n");
    for(int i=0;i<n;i++){
        cli_write(" ");
        cli_write_int(devs[i].bus); cli_write(":");
        cli_write_int(devs[i].device); cli_write(".");
        cli_write_int(devs[i].function); cli_write("  ");
        cli_write("vendor="); print_hex16(devs[i].vendor_id); cli_write(" ");
        cli_write("device="); print_hex16(devs[i].device_id); cli_write(" ");
        cli_write("class="); print_hex8(devs[i].class_code); cli_write(" ");
        cli_write("subclass="); print_hex8(devs[i].subclass); cli_write(" ");
        cli_write("prog_if="); print_hex8(devs[i].prog_if);
        if(devs[i].vendor_id==0x8086 && devs[i].device_id==0x100E){
            cli_write("  [e1000]");
        }
        cli_write("\n");
    }
    cli_write_int(n); cli_write(" device(s)\n");
}
