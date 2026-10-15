# ZeroEffort Drum
## Multi-Instance Architecture – Time Alignment & Phase Coherence

### Objective

Add communication between ZeroEffort Drum instances to allow global drum-kit analysis and automatic correction of phase and timing issues.

The goal is not to process each track independently, but to optimize the entire drum recording as a coherent acoustic system.

---

# Scope

Version focus:

- Multi-instance communication
- Polarity analysis
- Time alignment
- Drum kit coherence metrics

Not included:

- Auto EQ
- Auto Compression
- Style recommendations
- AI model integration

Those features can build upon this architecture later.

---

# General Architecture

Each plugin instance publishes analysis data to a shared manager.

```text
Kick
  ↓
Track Analysis
  ↓

Snare
  ↓
Track Analysis
  ↓

OH L
  ↓
Track Analysis
  ↓

OH R
  ↓
Track Analysis
  ↓

Room
  ↓
Track Analysis
  ↓

Shared Drum Manager
  ↓
Global Analysis
  ↓
Recommendations / Corrections
```

---

# Shared Information

Each instance provides:

```cpp
struct TrackInfo
{
    UUID trackId;

    DrumElement element;

    float sampleRate;

    AudioPreview preview;

    SpectrumAnalysis spectrum;

    TransientMap transientMap;

    float rms;

    float peak;
};
```

DrumElement:

```cpp
enum DrumElement
{
    Kick,
    SnareTop,
    SnareBottom,
    Tom,
    FloorTom,
    OverheadL,
    OverheadR,
    Room,
    HiHat,
    Other
};
```

---

# Reference Strategy

Reference sources:

## Kick

Used for:

- Low-end phase analysis
- Low-frequency timing

## Snare

Used for:

- Midrange phase analysis
- Overhead alignment

The user can also manually select the reference.

---

# Phase Analysis

## Polarity Detection

Check:

```cpp
Correlation(A, B)
Correlation(A, -B)
```

Example:

```text
Snare Top ↔ Snare Bottom

Normal polarity:
-0.82

Inverted polarity:
+0.75
```

Result:

```text
✓ Invert Snare Bottom
```

---

# Time Alignment

## Cross-Correlation

For every relevant pair:

```cpp
delay = argmax(
    CrossCorrelation(
        reference,
        target
    )
);
```

Example:

```text
Kick ↔ OH L

Peak correlation:
+2.31 ms
```

Recommendation:

```text
Move OH L earlier by 2.31 ms
```

---

# Recommended Analysis Pairs

## Kick

```text
Kick ↔ OH L
Kick ↔ OH R
Kick ↔ Room
Kick ↔ Floor Tom
```

## Snare

```text
Snare ↔ OH L
Snare ↔ OH R
Snare ↔ Room
Snare ↔ Snare Bottom
```

## Toms

```text
Tom ↔ OH L
Tom ↔ OH R
Tom ↔ Room
```

---

# Search Window Limits

Avoid false alignment on room reflections.

Recommended limits:

```text
Kick ↔ OH      ±5 ms

Snare ↔ OH     ±5 ms

Kick ↔ Room   ±20 ms

Snare ↔ Room  ±20 ms
```

Never search across the full file.

---

# Alignment Modes

## Off

Display analysis only.

---

## Suggest

Show recommendations.

Example:

```text
OH L : +2.31 ms

OH R : +2.45 ms

Room : +7.80 ms
```

---

## Auto

Apply correction automatically.

---

## Strength

```text
0%    No correction

50%   Partial correction

100%  Full correction
```

Useful for preserving room realism.

---

# Phase Coherence Metric

Global score:

```text
0 - 100
```

Computed from:

- Correlation values
- Timing consistency
- Low-frequency phase coherence

Example:

```text
Phase Coherence

Before:
67 / 100

After:
89 / 100
```

---

# Stereo Consistency Metric

Overhead analysis:

```text
OH L ↔ OH R
```

Metrics:

- Correlation
- Delay difference
- Spectral balance

Output:

```text
Stereo Consistency

92 / 100
```

---

# Room Coherence Metric

Measures room compatibility with close microphones.

Checks:

- Delay
- Polarity
- Correlation

Output:

```text
Room Compatibility

84 / 100
```

---

# User Report Example

```text
Analysis Complete

✓ Snare Bottom polarity inverted

✓ Overhead L aligned
  +2.3 ms

✓ Overhead R aligned
  +2.5 ms

✓ Room advanced
  +4.1 ms

Phase Coherence:
91 / 100

Stereo Consistency:
88 / 100

Room Compatibility:
85 / 100
```

---

# Future Extensions

Once multi-instance communication exists, the same infrastructure can support:

- Frequency masking analysis
- Kick/Tom conflict detection
- Kit balance analysis
- Global drum bus recommendations
- Cross-track compression optimization
- AI-powered text feedback

Examples:

```text
Kick lacks attack

Snare slightly bright

Overheads dominate the mix

Room microphones underutilized
```

---

# Development Priority

## V1

- Shared instance manager
- Track registration
- Polarity detection
- Cross-correlation alignment
- Delay compensation
- User report

## V2

- Global coherence score
- Stereo consistency score
- Room compatibility score

## V3

- Frequency masking analysis
- Textual diagnostics
- Global drum-kit optimization