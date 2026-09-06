#!/usr/bin/env python3
"""Validate the PCM input used for HOME banner audio."""
import sys
import wave


def validate(path):
    with wave.open(str(path), 'rb') as wav:
        if wav.getnchannels() != 2 or wav.getsampwidth() != 2 or wav.getcomptype() != 'NONE':
            raise ValueError('HOME banner audio requires stereo PCM16.')
        frames, rate = wav.getnframes(), wav.getframerate()
        if not 0 < frames <= rate * 3:
            raise ValueError('HOME banner audio must be at most 3 seconds.')
        if len(wav.readframes(frames)) != frames * 4:
            raise ValueError('Truncated HOME banner audio.')
        return frames / rate


if __name__ == '__main__':
    try:
        print(f'Banner audio: {validate(sys.argv[1]):.3f}s, stereo PCM16 verified.')
    except (ValueError, wave.Error, OSError) as error:
        sys.exit(str(error))
