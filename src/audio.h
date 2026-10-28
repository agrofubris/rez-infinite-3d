#pragma once

// Mutes (true) or unmutes (false) this process's audio session once the game
// starts playing sound. Runs in the background; returns immediately.
void StartGameAudioMute(bool mute);
