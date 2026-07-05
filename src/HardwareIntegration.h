#ifndef HARDWARE_INTEGRATION_H
#define HARDWARE_INTEGRATION_H

#include <iostream>
#include <string>

// Global references for Launchkey
inline int gLaunchkeyDawClient = -1;
inline int gLaunchkeyDawPort = -1;
inline int gLoomPiLaunchkeyPage = 0; // 0 = steps 1-16, 1 = steps 17-32, etc.

// Forward declare to avoid circular dependency
class AudioEngine;
class UIManager;
extern void pushLaunchkeyLedUpdate(AudioEngine* engine, UIManager* ui);

#ifndef __APPLE__
#include <alsa/asoundlib.h>

// External references
extern snd_seq_t* gSeqOut;
extern int gOutPort;

inline void sendLaunchkeyDawModeInit(int client, int port) {
    if (gSeqOut == nullptr || gOutPort < 0) return;
    
    // MK4 DAW mode SysEx: F0 00 20 29 02 13 0C 7F F7
    unsigned char mk4_sysex[] = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x13, 0x0C, 0x7F, 0xF7};
    
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, gOutPort);
    snd_seq_ev_set_dest(&ev, client, port);
    snd_seq_ev_set_direct(&ev);
    
    snd_seq_ev_set_sysex(&ev, sizeof(mk4_sysex), mk4_sysex);
    snd_seq_event_output_direct(gSeqOut, &ev);
    
    std::cout << "HardwareIntegration: Sent DAW Mode Init to Launchkey " << client << ":" << port << std::endl;
}
#endif // __APPLE__

#endif // HARDWARE_INTEGRATION_H
