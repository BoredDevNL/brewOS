#include "cli_utils.h"
#include "io.h"

void play_note(int freq, int duration_ms) {
    if (freq == 0) { 
        outb(0x61, inb(0x61) & 0xFC);
    } else {
        int div = 1193180 / freq;
        outb(0x43, 0xB6);
        outb(0x42, div & 0xFF);
        outb(0x42, (div >> 8) & 0xFF);
        outb(0x61, inb(0x61) | 0x03);
    }
    

    cli_sleep(duration_ms); 
    
    outb(0x61, inb(0x61) & 0xFC);
    cli_sleep(20); 
}

void cli_cmd_minecraft(char *args) {
    (void)args;
    cli_write("Playing: Sweden - C418 (What a masterpiece)\n");

    int melody[] = {
        196, 330, 294, 0,           // G3, E4, D4, rest
        196, 262, 247, 220, 196, 0, // G3, C4, B3, A3, G3, rest
        
        196, 330, 294, 392, 330, 0, // G3, E4, D4, G4, E4, rest
        
        440, 330, 294, 0,           // A4, E4, D4, rest
        262, 247, 220, 196, 147, 0, // C4, B3, A3, G3, D3, rest
        
        196, 330, 294, 0,           // Return to G3, E4, D4
        196, 262, 247, 220, 196     // Final resolution
    };

    int rhythm[] = {
        1000, 1000, 2000, 500,
        1000, 1000, 1000, 1000, 2000, 500,
        
        1000, 1000, 1000, 1000, 2000, 500,
        
        1000, 1000, 2000, 500,
        1000, 1000, 1000, 1000, 2000, 500,
        
        1000, 1000, 2000, 500,
        1000, 1000, 1000, 1000, 3000
    };

    int song_length = sizeof(melody) / sizeof(melody[0]);

    for (int i = 0; i < song_length; i++) {
        play_note(melody[i], rhythm[i]);
    }

    outb(0x61, inb(0x61) & 0xFC);
    cli_write("Composition finished.\n");
}