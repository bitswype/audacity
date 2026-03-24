#!/usr/bin/env python3
"""
Automated multi-channel audio test script for Audacity.

Tests the hardware loopback path:
1. Generates an N-channel test WAV with unique frequency per channel
2. Plays it through the specified ALSA device
3. Captures on the loopback input
4. Analyzes via FFT to verify per-channel routing
5. Reports pass/fail per channel with crosstalk measurement

Usage:
    python3 multichannel_test.py [--channels N] [--device hw:1,0] [--duration 3]

Requires: numpy, soundfile, subprocess (aplay, arecord)
"""

import argparse
import subprocess
import sys
import time
import os
import tempfile

import numpy as np
import soundfile as sf


def generate_test_signal(num_channels, sample_rate, duration, output_path):
    """Generate a WAV with unique sine wave per channel (FFT-bin-aligned)."""
    t = np.arange(int(sample_rate * duration)) / sample_rate
    freqs = [468.75 + i * 175.78 for i in range(num_channels)]

    data = np.zeros((len(t), num_channels), dtype=np.float64)
    for ch, freq in enumerate(freqs):
        data[:, ch] = 0.5 * np.sin(2 * np.pi * freq * t)

    sf.write(output_path, data, sample_rate, subtype='PCM_32')
    return freqs


def play_and_capture(device, play_file, capture_file, num_channels,
                     sample_rate, capture_duration):
    """Play a WAV and simultaneously capture on the same device."""
    # Start capture in background
    capture_cmd = [
        'arecord', '-D', device,
        '-f', 'S32_LE', '-r', str(sample_rate),
        '-c', str(num_channels),
        '-d', str(capture_duration),
        capture_file
    ]
    capture_proc = subprocess.Popen(
        capture_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Brief delay to ensure capture is running
    time.sleep(0.5)

    # Play the test file
    play_cmd = [
        'aplay', '-D', device,
        play_file
    ]
    subprocess.run(play_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # Wait for capture to finish
    capture_proc.wait()


def analyze_capture(capture_file, expected_freqs, sample_rate,
                    isolation_threshold_db=-60.0):
    """FFT analysis of captured audio. Returns per-channel results."""
    data, rate = sf.read(capture_file, dtype='float64')

    if data.ndim == 1:
        data = data.reshape(-1, 1)

    num_channels = min(data.shape[1], len(expected_freqs))

    # Use the middle of the capture to avoid start/end transients
    start = max(0, data.shape[0] // 2 - 4096)

    if start + 8192 > data.shape[0]:
        print("ERROR: Not enough audio data captured")
        return []

    NFFT = 8192
    results = []

    for ch in range(num_channels):
        chunk = data[start:start + NFFT, ch]
        spectrum = np.abs(np.fft.rfft(chunk))
        freq_bins = np.fft.rfftfreq(NFFT, 1.0 / rate)

        # Skip DC bin (index 0) -- find peak in actual signal range
        peak_bin = np.argmax(spectrum[1:]) + 1
        peak_freq = freq_bins[peak_bin]
        peak_mag = spectrum[peak_bin]

        expected = expected_freqs[ch]
        match = abs(peak_freq - expected) < 10 and peak_mag > 1.0

        # Measure worst-case crosstalk
        max_crosstalk_db = -999.0
        for other_ch in range(len(expected_freqs)):
            if other_ch == ch:
                continue
            other_bin = int(round(expected_freqs[other_ch] / (rate / NFFT)))
            if other_bin < len(spectrum):
                other_mag = spectrum[other_bin]
                if other_mag > 0 and peak_mag > 0:
                    xt = 20 * np.log10(other_mag / peak_mag)
                    max_crosstalk_db = max(max_crosstalk_db, xt)

        status = "PASS" if match else "FAIL"
        isolation_ok = max_crosstalk_db < isolation_threshold_db

        results.append({
            'channel': ch,
            'expected_freq': expected,
            'measured_freq': peak_freq,
            'magnitude': peak_mag,
            'crosstalk_db': max_crosstalk_db,
            'freq_match': match,
            'isolation_ok': isolation_ok,
            'status': status if isolation_ok else "FAIL (crosstalk)",
        })

    return results


def print_results(results, test_name=""):
    """Print results in a clear table format."""
    if test_name:
        print(f"\n=== {test_name} ===")

    passed = 0
    failed = 0

    for r in results:
        status = r['status']
        if 'PASS' in status:
            passed += 1
        else:
            failed += 1

        print("  ch%2d: expected %8.2f Hz, got %8.2f Hz, "
              "crosstalk %6.1f dB  [%s]" % (
                  r['channel'], r['expected_freq'], r['measured_freq'],
                  r['crosstalk_db'], status))

    total = len(results)
    print(f"\nResult: {passed}/{total} PASS, {failed} FAIL")

    if passed == total:
        print("\n*** ALL CHANNELS VERIFIED ***")

    return failed == 0


def main():
    parser = argparse.ArgumentParser(
        description='Automated multi-channel audio loopback test')
    parser.add_argument('--channels', type=int, default=16,
                        help='Number of channels to test (default: 16)')
    parser.add_argument('--device', type=str, default='hw:1,0',
                        help='ALSA device for play/capture (default: hw:1,0)')
    parser.add_argument('--rate', type=int, default=48000,
                        help='Sample rate (default: 48000)')
    parser.add_argument('--duration', type=float, default=2.0,
                        help='Test signal duration in seconds (default: 2.0)')
    parser.add_argument('--capture-duration', type=int, default=4,
                        help='Capture duration in seconds (default: 4)')
    parser.add_argument('--isolation', type=float, default=-60.0,
                        help='Min channel isolation in dB (default: -60)')
    parser.add_argument('--keep-files', action='store_true',
                        help='Keep generated WAV files')

    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmpdir:
        test_wav = os.path.join(tmpdir, 'test_signal.wav')
        capture_wav = os.path.join(tmpdir, 'captured.wav')

        if args.keep_files:
            test_wav = '/tmp/mctest_signal.wav'
            capture_wav = '/tmp/mctest_captured.wav'

        print(f"Multi-Channel Loopback Test")
        print(f"  Channels: {args.channels}")
        print(f"  Device: {args.device}")
        print(f"  Rate: {args.rate} Hz")
        print(f"  Duration: {args.duration}s play, {args.capture_duration}s capture")
        print(f"  Isolation threshold: {args.isolation} dB")
        print()

        # Step 1: Generate test signal
        print("Generating test signal...", end=' ', flush=True)
        freqs = generate_test_signal(
            args.channels, args.rate, args.duration, test_wav)
        print("done")

        # Step 2: Play and capture
        print("Playing and capturing...", end=' ', flush=True)
        play_and_capture(
            args.device, test_wav, capture_wav,
            args.channels, args.rate, args.capture_duration)
        print("done")

        # Step 3: Analyze
        print("Analyzing capture...")
        results = analyze_capture(
            capture_wav, freqs, args.rate, args.isolation)

        if not results:
            print("FAIL: No results from analysis")
            sys.exit(1)

        # Step 4: Report
        all_pass = print_results(results, "Direct aplay/arecord loopback")

        sys.exit(0 if all_pass else 1)


if __name__ == '__main__':
    main()
