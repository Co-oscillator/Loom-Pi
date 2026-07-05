#ifndef MIDI_DEVICE_SETTINGS_H
#define MIDI_DEVICE_SETTINGS_H

#include <string>

struct MidiDeviceSettings {
    std::string name;
    int client = -1;  // ALSA client ID or CoreMIDI destination index
    int port = -1;    // ALSA port ID
    int sendChannel = -1;     // 1-16, -1 = All, 0 = Off/Muted
    int receiveChannel = -1;  // 1-16, -1 = All, 0 = Off/Ignored
    bool muteOutgoing = false;
    bool ignoreIncoming = false;
    bool isInput = false;
    bool isOutput = false;
};

#endif // MIDI_DEVICE_SETTINGS_H
