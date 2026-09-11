# Nemu Audio Subsystem Architecture

## 1. Scope & Standards

The audio subsystem emulates the Nintendo Switch audio pipeline, delivering low-latency, glitch-free audio output synchronized with emulation timing.

Key Standards:
* **Sampling Rate:** 48,000 Hz (48 kHz native).
* **Sample Format:** 16-bit Signed Linear PCM (or 32-bit Float intermediate).
* **Channel Configurations:** Stereo (2.0) and Surround (5.1).
* **Target Latency:** 20 ms to 40 ms buffer window.

---

## 2. Audio Pipeline Architecture

```
Guest Software (Game / Homebrew)
               │
               ▼
   Horizon Audio Service (audren:u / audout:u)
               │
               ▼
      Audio Command Processor
   (Voices, Biquad Filters, Mix Buffers, Effect Submixes)
               │
               ▼
      Sample Rate & Resampler
   (Polyphase resampler if source != 48 kHz)
               │
               ▼
      Circular Ring Buffer
   (Thread-safe lock-free SPSC queue)
               │
               ▼
      Host Audio Backend
 ┌─────────────┴─────────────┐
 ▼                           ▼
XAudio2 2.9 (Xbox)    WASAPI / PulseAudio (Host)
```

---

## 3. Host Backends

### 3.1 Xbox Series S/X Native Backend: XAudio2 2.9
* Creates `IXAudio2` engine with low-latency mastering voice.
* Implements `IXAudio2VoiceCallback` to track buffer consumption.
* Submits dynamic audio buffers using `SourceVoice->SubmitSourceBuffer()`.
* Fully compatible with Xbox hardware spatial sound and Dolby Atmos for Headphones.

### 3.2 Linux Host Test Backend: PulseAudio / OpenAL / Null
* Implements standard callback or blocking audio stream for developer machine testing and regression verification.

---

## 4. Clock Drift & Frame Pacing Synchronization

If emulation runs faster or slower than exact 60 FPS:
* An unadjusted audio stream will either underrun (causing audible clicks and popping) or overflow (causing latency spikes).
* Nemu employs dynamic time-stretching / dynamic sample rate adjustment (adjusting output sample rate by up to ±0.5%) based on ring buffer fullness, keeping audio smooth without audible pitch distortion.
