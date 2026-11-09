# Test sounds

Made for Rolo's tests (MIT, like the rest of Rolo): a quarter of a second
of a 440 Hz tone, mono, 44.1 kHz, at half volume.

- `tone.wav` — written by a short Python script (NumPy and `wave`);
- `tone.ogg` — `ffmpeg -i tone.wav -c:a libvorbis -q:a 2 tone.ogg`;
- `tone.mp3` — `ffmpeg -i tone.wav -c:a libmp3lame -b:a 64k tone.mp3`;
- `broken.wav` — text, not a sound: for the "cannot be read" message.
