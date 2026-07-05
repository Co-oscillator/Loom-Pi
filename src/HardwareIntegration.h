#ifndef HARDWARE_INTEGRATION_H
#define HARDWARE_INTEGRATION_H

#include <iostream>
#include <string>

// Global references for Launchkey
inline int gLaunchkeyDawClient = -1;
inline int gLaunchkeyDawPort = -1;
inline int gLoomPiLaunchkeyPage = 0; // 0 = steps 1-16, 1 = steps 17-32, etc.
inline int gLoomPiLaunchkeyKnobBank = 0; // 0 = knobs 0-7, 1 = 8-15, 2 = 16-23

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
    
    // MK4 DAW mode enable: Note On Ch 16, Note 12, Velocity 127 (9F 0C 7F)
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, gOutPort);
    snd_seq_ev_set_dest(&ev, client, port);
    snd_seq_ev_set_direct(&ev);
    
    // Note On (Channel 15 is 0-indexed Channel 16)
    snd_seq_ev_set_noteon(&ev, 15, 12, 127);
    snd_seq_event_output_direct(gSeqOut, &ev);
    
    std::cout << "HardwareIntegration: Sent DAW Mode Init to Launchkey " << client << ":" << port << std::endl;
}
#endif // __APPLE__

#endif // HARDWARE_INTEGRATION_H
