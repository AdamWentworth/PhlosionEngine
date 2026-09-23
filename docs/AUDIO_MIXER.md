# Runtime audio mixer

`Phlosion::Audio` is a renderer-free, owner-thread CPU mixer. Callers provide
decoded 48 kHz mono or stereo float PCM through `AudioClip::create`; the clip
copies and owns the samples. `AudioMixer::render` advances the timeline and
writes interleaved stereo float PCM in blocks of 1–4096 frames. No decoder or
streaming source is part of this CPU layer.

`AudioPlayOptions` chooses one of Music, Sfx, Ambient, or Voice, plus voice gain,
looping, and optional spatial placement. Master, bus, and voice gains are
bounded to `[0, 1]`. Bus and voice fades advance per sample frame, even while
muted. Spatial placement applies linear distance attenuation and constant-power
left/right panning to mono clips; stereo clips retain their authored channels.
The output is clamped to `[-1, 1]` after mixing.

Call `stop` for one voice, `stopAll` for scene changes, or `reset` to clear voices,
fades, gains, mute, and listener state. Voice IDs remain unique for the mixer
lifetime, including across reset. Invalid requests leave the previous state and
caller output intact. All mixer operations, including destruction, must run on
the thread that created the mixer. `AudioClip::create` may run independently.

`Phlosion::AudioSdl` provides an opt-in SDL queued-device adapter. After `start`,
call `pump` with elapsed sample frames on the same thread as the mixer. It queues
at most 100 ms of audio; when the queue is full or no device is available, the
mixer timeline still advances through a null sink. A failed `start` can be
retried. `stopAll` clears both live voices and queued audio, and `shutdown`
releases the adapter's SDL subsystem reference. The adapter requires exact
48 kHz stereo float device support; it does not resample. Destroy it before the
mixer and the host's final `SDL_Quit`.

`PhlosionAudioMixerContracts` checks numeric mixing, fades, looping, clip
lifetime, spatial movement, cleanup, invalid input, and thread ownership.
`PhlosionSdlAudioContracts` uses SDL's dummy driver to check startup/retry,
unavailable-device progress, queue limits, cleanup, and subsystem ownership.
Physical playback and game-specific audio cues require later qualification.
