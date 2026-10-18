// Space Channel 5 Part 2: the console's audio output, for the rhythm offset (sc5p2_rhythm_offset.cpp).
#pragma once

namespace sc5p2::rhythm {

// Starts watching the audio output (the Switch: audctl's active output target, read once now and then every second
// by a small background thread). Idempotent; elsewhere a no-op.
void StartAudioOutputWatch();

// True while the console's audio goes to Bluetooth headphones (always false off the Switch or when unreadable).
bool BluetoothActive();

}  // namespace sc5p2::rhythm
