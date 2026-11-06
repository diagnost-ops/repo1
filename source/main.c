// Joy-Con Sound Test - HD Rumble tone / melody tester for Nintendo Switch (libnx)
//
// Plays tones through the Joy-Con linear resonant actuators (HD Rumble).
// Works with: handheld mode, attached Joy-Con, detached Joy-Con pair (dual),
//             single Joy-Con (sideways), Pro Controller (P1..P4).

#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define MAX_DEV 16

// HD Rumble hardware limits (Hz)
#define LOW_MIN   40.875885f
#define LOW_MAX  626.286133f
#define HIGH_MIN  81.75177f
#define HIGH_MAX 1252.572266f

#define MIDI_MIN 36   // C2  ~65 Hz
#define MIDI_MAX 87   // D#6 ~1245 Hz

typedef struct {
    HidVibrationDeviceHandle h;
    int  pos;           // 1 = left, 2 = right, 0 = unknown
    char label[32];
} Dev;

static Dev  devs[MAX_DEV];
static int  ndev = 0;

static const HidNpadIdType ids[5] = {
    HidNpadIdType_Handheld, HidNpadIdType_No1, HidNpadIdType_No2,
    HidNpadIdType_No3, HidNpadIdType_No4
};
static u32 lastStyle[5];

typedef enum { BAND_AUTO = 0, BAND_LOW, BAND_HIGH, BAND_BOTH, BAND_COUNT } Band;
static const char *bandNames[BAND_COUNT] = { "AUTO (by freq)", "LOW only", "HIGH only", "BOTH" };
static const char *targetNames[3] = { "BOTH sides", "LEFT only", "RIGHT only" };
static const char *noteNames[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

typedef enum { SEQ_NONE = 0, SEQ_MELODY, SEQ_SWEEP, SEQ_PULSE } SeqMode;

// "Ode to Joy" (public domain): midi note, duration in beats
typedef struct { int midi; float beats; } MNote;
static const MNote melody[] = {
    {64,1},{64,1},{65,1},{67,1},{67,1},{65,1},{64,1},{62,1},
    {60,1},{60,1},{62,1},{64,1},{64,1.5f},{62,0.5f},{62,2},
    {64,1},{64,1},{65,1},{67,1},{67,1},{65,1},{64,1},{62,1},
    {60,1},{60,1},{62,1},{64,1},{62,1.5f},{60,0.5f},{60,2},
};
#define MELODY_LEN ((int)(sizeof(melody) / sizeof(melody[0])))
#define BEAT_MS 320.0f

static float midiToFreq(int m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }

static void addDevs(HidNpadIdType id, HidNpadStyleTag style, int n, const char *name)
{
    HidVibrationDeviceHandle hs[2];
    if (ndev + n > MAX_DEV) return;
    if (R_FAILED(hidInitializeVibrationDevices(hs, n, id, style))) return;
    for (int i = 0; i < n; i++) {
        HidVibrationDeviceInfo info;
        int pos = 0;
        if (R_SUCCEEDED(hidGetVibrationDeviceInfo(hs[i], &info)))
            pos = (int)info.position;
        Dev *d = &devs[ndev++];
        d->h = hs[i];
        d->pos = pos;
        snprintf(d->label, sizeof(d->label), "%s %s", name,
                 pos == 1 ? "L" : pos == 2 ? "R" : "?");
    }
}

static void rescan(void)
{
    ndev = 0;
    for (int i = 0; i < 5; i++) {
        u32 st = hidGetNpadStyleSet(ids[i]);
        lastStyle[i] = st;
        char name[24];
        if (i == 0) snprintf(name, sizeof(name), "Handheld");
        else        snprintf(name, sizeof(name), "P%d", i);

        if      (st & HidNpadStyleTag_NpadHandheld) addDevs(ids[i], HidNpadStyleTag_NpadHandheld, 2, name);
        else if (st & HidNpadStyleTag_NpadJoyDual)  addDevs(ids[i], HidNpadStyleTag_NpadJoyDual,  2, name);
        else if (st & HidNpadStyleTag_NpadFullKey)  addDevs(ids[i], HidNpadStyleTag_NpadFullKey,  2, name);
        else if (st & HidNpadStyleTag_NpadJoyLeft)  addDevs(ids[i], HidNpadStyleTag_NpadJoyLeft,  1, name);
        else if (st & HidNpadStyleTag_NpadJoyRight) addDevs(ids[i], HidNpadStyleTag_NpadJoyRight, 1, name);
    }
}

static bool styleChanged(void)
{
    for (int i = 0; i < 5; i++)
        if (hidGetNpadStyleSet(ids[i]) != lastStyle[i]) return true;
    return false;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static HidVibrationValue makeValue(float f, float amp, Band band)
{
    HidVibrationValue v = { 0.0f, 160.0f, 0.0f, 320.0f };
    bool useLow, useHigh;
    switch (band) {
        case BAND_LOW:  useLow = true;  useHigh = false; break;
        case BAND_HIGH: useLow = false; useHigh = true;  break;
        case BAND_BOTH: useLow = true;  useHigh = true;  break;
        default:        useLow = (f <= LOW_MAX); useHigh = !useLow; break;
    }
    if (useLow)  { v.amp_low  = amp; v.freq_low  = clampf(f, LOW_MIN,  LOW_MAX);  }
    if (useHigh) { v.amp_high = amp; v.freq_high = clampf(f, HIGH_MIN, HIGH_MAX); }
    return v;
}

static u64 nowMs(void) { return armTicksToNs(armGetSystemTick()) / 1000000ULL; }

int main(int argc, char **argv)
{
    consoleInit(NULL);
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeAny(&pad);
    hidPermitVibration(true);
    rescan();

    int   midi   = 69;      // A4 = 440 Hz
    float amp    = 0.5f;
    int   target = 0;
    Band  band   = BAND_AUTO;

    SeqMode seq = SEQ_NONE;
    u64     seqStart = 0;
    int     melIdx = 0;
    u64     melNoteStart = 0;

    bool dirty = true;
    const HidVibrationValue zero = { 0.0f, 160.0f, 0.0f, 320.0f };

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kd = padGetButtonsDown(&pad);
        u64 kh = padGetButtons(&pad);

        if (kd & HidNpadButton_Plus) break;

        if (styleChanged()) { rescan(); dirty = true; }

        // ---- settings ----
        if (kd & HidNpadButton_Up)    { if (midi < MIDI_MAX) midi++; dirty = true; }
        if (kd & HidNpadButton_Down)  { if (midi > MIDI_MIN) midi--; dirty = true; }
        if (kd & HidNpadButton_Right) { target = (target + 1) % 3;       dirty = true; }
        if (kd & HidNpadButton_Left)  { target = (target + 2) % 3;       dirty = true; }
        if (kd & HidNpadButton_R)     { amp = clampf(amp + 0.1f, 0.1f, 1.0f); dirty = true; }
        if (kd & HidNpadButton_L)     { amp = clampf(amp - 0.1f, 0.1f, 1.0f); dirty = true; }
        if (kd & HidNpadButton_ZL)    { band = (Band)((band + 1) % BAND_COUNT); dirty = true; }

        // ---- sequences ----
        u64 t = nowMs();
        if (kd & HidNpadButton_Minus) { seq = SEQ_NONE; dirty = true; }
        if (kd & HidNpadButton_X)     { seq = SEQ_MELODY; melIdx = 0; melNoteStart = t; dirty = true; }
        if (kd & HidNpadButton_Y)     { seq = SEQ_SWEEP;  seqStart = t; dirty = true; }
        if (kd & HidNpadButton_B)     { seq = SEQ_PULSE;  seqStart = t; dirty = true; }

        bool  active = false;
        float freq   = midiToFreq(midi);

        if (seq == SEQ_MELODY) {
            float dur = melody[melIdx].beats * BEAT_MS;
            u64 el = t - melNoteStart;
            if (el >= (u64)dur) {
                melIdx++;
                melNoteStart = t;
                el = 0;
                if (melIdx >= MELODY_LEN) { seq = SEQ_NONE; dirty = true; }
                else dur = melody[melIdx].beats * BEAT_MS;
            }
            if (seq == SEQ_MELODY) {
                freq = midiToFreq(melody[melIdx].midi);
                active = (float)el < dur * 0.85f;   // short gap between notes
            }
        } else if (seq == SEQ_SWEEP) {
            float p = (float)(t - seqStart) / 4000.0f;   // 4 s, 2 siren cycles
            if (p >= 1.0f) { seq = SEQ_NONE; dirty = true; }
            else {
                float ph = p * 2.0f;
                ph -= floorf(ph);
                float tri = ph < 0.5f ? ph * 2.0f : (1.0f - ph) * 2.0f;
                freq = 70.0f * powf(1200.0f / 70.0f, tri);
                active = true;
            }
        } else if (seq == SEQ_PULSE) {
            u64 el = t - seqStart;                       // 5 pulses, 120 ms on / 120 ms off
            if (el >= 1200) { seq = SEQ_NONE; dirty = true; }
            else active = (el % 240) < 120;
        }

        if (seq == SEQ_NONE && (kh & HidNpadButton_A)) active = true;   // hold A = sustained tone

        // ---- output ----
        HidVibrationValue val = makeValue(freq, amp, band);
        for (int i = 0; i < ndev; i++) {
            bool match = (target == 0) || (target == 1 && devs[i].pos == 1) || (target == 2 && devs[i].pos == 2);
            hidSendVibrationValue(devs[i].h, (active && match) ? &val : &zero);
        }

        // ---- UI ----
        static u64 lastDraw = 0;
        if (dirty || t - lastDraw > 250) {
            lastDraw = t;
            dirty = false;
            consoleClear();
            printf("== Joy-Con Sound Test (HD Rumble) ==\n\n");
            printf("Devices found: %d\n", ndev);
            for (int i = 0; i < ndev; i++) printf("  - %s\n", devs[i].label);
            if (ndev == 0) printf("  (none - connect Joy-Con / use handheld)\n");
            printf("\nNote     : %s%d  (%.1f Hz)\n", noteNames[midi % 12], midi / 12 - 1, midiToFreq(midi));
            printf("Amplitude: %.1f\n", amp);
            printf("Target   : %s\n", targetNames[target]);
            printf("Band     : %s\n", bandNames[band]);
            printf("Playing  : %s\n",
                   seq == SEQ_MELODY ? "melody" : seq == SEQ_SWEEP ? "siren sweep" :
                   seq == SEQ_PULSE ? "pulses" : "-");
            printf("\n--- Controls ---\n");
            printf("A (hold)  sustained tone\n");
            printf("B         5 pulses\n");
            printf("X         melody (Ode to Joy)\n");
            printf("Y         siren sweep 70..1200 Hz\n");
            printf("Up/Down   note +/- semitone\n");
            printf("Left/Right  target: both / left / right\n");
            printf("L / R     amplitude - / +\n");
            printf("ZL        cycle band mode\n");
            printf("-         stop     +  exit\n");
        }
        consoleUpdate(NULL);
    }

    for (int i = 0; i < ndev; i++) hidSendVibrationValue(devs[i].h, &zero);
    consoleExit(NULL);
    return 0;
}
