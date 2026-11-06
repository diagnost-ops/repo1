// Joy-Con Sound Test - HD Rumble tone / melody tester for Nintendo Switch (libnx)
//
// Plays tones through the Joy-Con linear resonant actuators (HD Rumble).
// Works with: handheld mode, attached Joy-Con, detached Joy-Con pair (dual),
//             single Joy-Con (sideways), Pro Controller (P1..P4).
// Has a simple touch UI (sliders + buttons) as well as button controls.

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

// Frequency range exposed in the UI
#define FREQ_MIN  41.0f
#define FREQ_MAX 1250.0f

// Console cell size in pixels (80x45 grid on 1280x720)
#define CW 16

// ---- touch layout (pixels) / console layout (1-based rows/cols) ----
#define ROW_FREQ_LABEL   9
#define ROW_FREQ_BAR    11
#define ROW_AMP_LABEL   14
#define ROW_AMP_BAR     16
#define BAR_COL          6
#define BAR_W           68
#define BAR_X0          ((BAR_COL - 1) * CW)
#define BAR_PX          (BAR_W * CW)
#define FREQ_Y0         128
#define FREQ_Y1         208
#define AMP_Y0          224
#define AMP_Y1          288

#define BTN_W           18
#define BTN_H           3
static const int BTN_COL[4] = { 1, 21, 41, 61 };
static const int BTN_ROW[3] = { 21, 25, 29 };
#define NBTN_USED 11


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
static const char *bandShort[BAND_COUNT]   = { "AUTO", "LOW", "HIGH", "BOTH" };
static const char *targetShort[3]          = { "BOTH", "LEFT", "RIGHT" };
static const char *noteNames[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

typedef enum { SEQ_NONE = 0, SEQ_MELODY, SEQ_SWEEP, SEQ_PULSE } SeqMode;

// Songs (all public domain): midi note (0 = rest), duration in beats
typedef struct { int midi; float beats; } MNote;
typedef struct { const char *name; const MNote *notes; int len; float beatMs; int repeats; } Song;

static const MNote ode[] = {   // Ode to Joy
    {64,1},{64,1},{65,1},{67,1},{67,1},{65,1},{64,1},{62,1},
    {60,1},{60,1},{62,1},{64,1},{64,1.5f},{62,0.5f},{62,2},
    {64,1},{64,1},{65,1},{67,1},{67,1},{65,1},{64,1},{62,1},
    {60,1},{60,1},{62,1},{64,1},{62,1.5f},{60,0.5f},{60,2},
};
static const MNote bday[] = {  // Happy Birthday
    {67,0.75f},{67,0.25f},{69,1},{67,1},{72,1},{71,2},
    {67,0.75f},{67,0.25f},{69,1},{67,1},{74,1},{72,2},
    {67,0.75f},{67,0.25f},{79,1},{76,1},{72,1},{71,1},{69,1},
    {77,0.75f},{77,0.25f},{76,1},{72,1},{74,1},{72,2},
};
static const MNote jingle[] = { // Jingle Bells (chorus)
    {64,1},{64,1},{64,2},  {64,1},{64,1},{64,2},
    {64,1},{67,1},{60,1.5f},{62,0.5f},  {64,4},
    {65,1},{65,1},{65,1.5f},{65,0.5f},  {65,1},{64,1},{64,1},{64,0.5f},{64,0.5f},
    {64,1},{62,1},{62,1},{64,1},  {62,2},{67,2},
    {64,1},{64,1},{64,2},  {64,1},{64,1},{64,2},
    {64,1},{67,1},{60,1.5f},{62,0.5f},  {64,4},
    {65,1},{65,1},{65,1.5f},{65,0.5f},  {65,1},{64,1},{64,1},{64,0.5f},{64,0.5f},
    {67,1},{67,1},{65,1},{62,1},  {60,4},
};
#define NOTES(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define NSONGS 4
#define SPEC_MAX 160
static MNote spec[SPEC_MAX];
static Song songs[NSONGS] = {
    { "Ode to Joy",     ode,    NOTES(ode),    320.0f, 2 },
    { "Happy Birthday", bday,   NOTES(bday),   420.0f, 1 },
    { "Jingle Bells",   jingle, NOTES(jingle), 270.0f, 1 },
    { "Full Spectrum",  spec,   0,             240.0f, 1 },
};

// Full-range test: chromatic scale up and down over the whole HD Rumble
// range (~41 Hz .. ~1245 Hz), then octave leaps across the range.
static void buildSpectrumSong(void)
{
    static const int leaps[] = { 28, 40, 52, 64, 76, 87, 76, 64, 52, 40, 28 };
    int n = 0;
    for (int m = 28; m <= 87; m++) { spec[n].midi = m; spec[n].beats = 0.5f; n++; }
    spec[n].midi = 0; spec[n].beats = 1.0f; n++;
    for (int m = 87; m >= 28; m--) { spec[n].midi = m; spec[n].beats = 0.5f; n++; }
    spec[n].midi = 0; spec[n].beats = 1.0f; n++;
    for (int i = 0; i < 11; i++) { spec[n].midi = leaps[i]; spec[n].beats = 1.0f; n++; }
    songs[3].len = n;
}

static float midiToFreq(int m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

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

// ---------- UI helpers ----------

static float freqToT(float f) { return logf(f / FREQ_MIN) / logf(FREQ_MAX / FREQ_MIN); }
static float tToFreq(float t) { return FREQ_MIN * powf(FREQ_MAX / FREQ_MIN, clampf(t, 0.0f, 1.0f)); }

// ---------- Equalizer (shows which frequencies are being played) ----------
#define EQ_BINS     26
#define EQ_H         6
#define EQ_ROW_NOW  37
#define EQ_ROW_TOP  38
#define EQ_ROW_AXIS (EQ_ROW_TOP + EQ_H)
#define EQ_COL0      2
#define EQ_STEP      3          // 2 chars bar + 1 gap

static float eqLvl[EQ_BINS];
static float eqPeak[EQ_BINS];

static void eqUpdate(bool active, float f, float a)
{
    for (int i = 0; i < EQ_BINS; i++) {
        eqLvl[i] *= 0.86f;
        if (eqLvl[i] < 0.01f) eqLvl[i] = 0.0f;
        eqPeak[i] -= 0.012f;
        if (eqPeak[i] < 0.0f) eqPeak[i] = 0.0f;
    }
    if (active) {
        int b = (int)(freqToT(f) * EQ_BINS);
        if (b < 0) b = 0;
        if (b >= EQ_BINS) b = EQ_BINS - 1;
        if (a > eqLvl[b]) eqLvl[b] = a;
        if (b > 0           && a * 0.4f > eqLvl[b - 1]) eqLvl[b - 1] = a * 0.4f;
        if (b < EQ_BINS - 1 && a * 0.4f > eqLvl[b + 1]) eqLvl[b + 1] = a * 0.4f;
    }
    for (int i = 0; i < EQ_BINS; i++)
        if (eqLvl[i] > eqPeak[i]) eqPeak[i] = eqLvl[i];
}

static bool eqVisible(void)
{
    for (int i = 0; i < EQ_BINS; i++)
        if (eqLvl[i] > 0.0f || eqPeak[i] > 0.0f) return true;
    return false;
}

static void eqDrawAxis(void)
{
    static const int marks[6] = { 41, 100, 200, 400, 800, 1250 };
    printf("\x1b[%d;1H\x1b[K", EQ_ROW_AXIS);
    for (int i = 0; i < 6; i++) {
        char buf[8];
        int len = snprintf(buf, sizeof(buf), "%d", marks[i]);
        int col = EQ_COL0 + (int)(EQ_STEP * EQ_BINS * freqToT((float)marks[i]));
        if (col + len > 80) col = 81 - len;
        printf("\x1b[%d;%dH%s", EQ_ROW_AXIS, col, buf);
    }
}

static void eqDraw(bool active, float hz)
{
    printf("\x1b[%d;1HEQUALIZER (Hz, log scale)   Now: ", EQ_ROW_NOW);
    if (active) printf("%7.1f Hz", hz); else printf("    --- Hz");
    printf("\x1b[K");
    for (int r = 0; r < EQ_H; r++) {
        int need = EQ_H - r;
        printf("\x1b[%d;1H ", EQ_ROW_TOP + r);
        for (int b = 0; b < EQ_BINS; b++) {
            int h  = (int)ceilf(sqrtf(eqLvl[b])  * EQ_H);
            int ph = (int)ceilf(sqrtf(eqPeak[b]) * EQ_H);
            if (h >= need)       printf("## ");
            else if (ph == need) printf("-- ");
            else                 printf("   ");
        }
        printf("\x1b[K");
    }
}

static void drawBar(int row, float t)
{
    int pos = (int)(clampf(t, 0.0f, 1.0f) * (BAR_W - 1) + 0.5f);
    printf("\x1b[%d;%dH[", row, BAR_COL - 1);
    for (int i = 0; i < BAR_W; i++)
        putchar(i < pos ? '=' : (i == pos ? '#' : '-'));
    printf("]");
}

static void drawButtonRow(int rowIdx, const char *labels[4], const bool pressed[4])
{
    int row = BTN_ROW[rowIdx];
    for (int line = 0; line < BTN_H; line++) {
        printf("\x1b[%d;1H", row + line);
        for (int c = 0; c < 4; c++) {
            if (c) printf("  ");
            if (!labels[c]) { printf("%*s", BTN_W, ""); continue; }
            char fill = pressed[c] ? '#' : '-';
            char side = pressed[c] ? '#' : '|';
            char corner = pressed[c] ? '#' : '+';
            if (line == 1) {
                int len = (int)strlen(labels[c]);
                int inner = BTN_W - 2;
                int left = (inner - len) / 2;
                int right = inner - len - left;
                printf("%c%*s%s%*s%c", side, left, "", labels[c], right, "", side);
            } else {
                putchar(corner);
                for (int i = 0; i < BTN_W - 2; i++) putchar(fill);
                putchar(corner);
            }
        }
        printf("\x1b[K");
    }
}

static bool inBtn(int b, u32 x, u32 y)
{
    if (b >= NBTN_USED) return false;
    int rowIdx = b / 4, c = b % 4;
    u32 x0 = (u32)((BTN_COL[c] - 1) * CW);
    u32 x1 = x0 + BTN_W * CW;
    u32 y0 = (u32)((BTN_ROW[rowIdx] - 1) * CW);
    u32 y1 = y0 + BTN_H * CW;
    return x >= x0 && x < x1 && y >= y0 && y < y1;
}

int main(int argc, char **argv)
{
    consoleInit(NULL);
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeAny(&pad);
    hidInitializeTouchScreen();
    hidPermitVibration(true);
    buildSpectrumSong();
    rescan();

    float freq   = 440.0f;
    float amp    = 0.5f;
    int   target = 0;
    Band  band   = BAND_AUTO;

    SeqMode seq = SEQ_NONE;
    u64     seqStart = 0;
    int     melIdx = 0, melPass = 0, song = 0;
    u64     melNoteStart = 0;

    bool btnNow[12]  = { false };
    bool btnPrev[12] = { false };
    bool quit = false;

    bool dirty = true;
    bool needClear = true;
    const HidVibrationValue zero = { 0.0f, 160.0f, 0.0f, 320.0f };

    while (appletMainLoop() && !quit) {
        padUpdate(&pad);
        u64 kd = padGetButtonsDown(&pad);
        u64 kh = padGetButtons(&pad);

        if (kd & HidNpadButton_Plus) break;

        if (styleChanged()) { rescan(); dirty = true; needClear = true; }

        // ---- touch ----
        HidTouchScreenState ts = { 0 };
        hidGetTouchScreenStates(&ts, 1);
        memcpy(btnPrev, btnNow, sizeof(btnNow));
        memset(btnNow, 0, sizeof(btnNow));
        for (int i = 0; i < ts.count && i < 16; i++) {
            u32 x = ts.touches[i].x, y = ts.touches[i].y;
            if (y >= FREQ_Y0 && y < FREQ_Y1 && x + 32 >= BAR_X0 && x <= BAR_X0 + BAR_PX + 32) {
                float t = ((float)x - BAR_X0) / (float)BAR_PX;
                freq = tToFreq(t);
                dirty = true;
            } else if (y >= AMP_Y0 && y < AMP_Y1 && x + 32 >= BAR_X0 && x <= BAR_X0 + BAR_PX + 32) {
                amp = clampf(((float)x - BAR_X0) / (float)BAR_PX, 0.0f, 1.0f);
                dirty = true;
            } else {
                for (int b = 0; b < NBTN_USED; b++)
                    if (inBtn(b, x, y)) btnNow[b] = true;
            }
        }
        for (int b = 0; b < 12; b++)
            if (btnNow[b] != btnPrev[b]) dirty = true;
        u16 pressedEdge = 0;
        for (int b = 0; b < 12; b++)
            if (btnNow[b] && !btnPrev[b]) pressedEdge |= (1 << b);

        // ---- button settings ----
        float semi = 1.0594631f;
        if (kd & HidNpadButton_Up)    { freq = clampf(freq * semi, FREQ_MIN, FREQ_MAX); dirty = true; }
        if (kd & HidNpadButton_Down)  { freq = clampf(freq / semi, FREQ_MIN, FREQ_MAX); dirty = true; }
        if (kd & HidNpadButton_Right) { target = (target + 1) % 3; dirty = true; }
        if (kd & HidNpadButton_Left)  { target = (target + 2) % 3; dirty = true; }
        if (kd & HidNpadButton_R)     { amp = clampf(amp + 0.1f, 0.0f, 1.0f); dirty = true; }
        if (kd & HidNpadButton_L)     { amp = clampf(amp - 0.1f, 0.0f, 1.0f); dirty = true; }
        if (kd & HidNpadButton_ZL)    { band = (Band)((band + 1) % BAND_COUNT); dirty = true; }

        // ---- sequences ----
        u64 t = nowMs();
        int startSong = -1;
        bool doStop   = (kd & HidNpadButton_Minus) || (pressedEdge & (1 << 3));
        bool doSweep  = (kd & HidNpadButton_Y)     || (pressedEdge & (1 << 2));
        bool doPulse  = (kd & HidNpadButton_B)     || (pressedEdge & (1 << 1));
        if (kd & HidNpadButton_ZR) { song = (song + 1) % NSONGS; dirty = true; }
        if (kd & HidNpadButton_X)  startSong = song;
        for (int i = 0; i < NSONGS; i++)
            if (pressedEdge & (1 << (4 + i))) startSong = i;

        if (pressedEdge & (1 << 10)) quit = true;
        if (pressedEdge & (1 << 8)) { target = (target + 1) % 3; dirty = true; }
        if (pressedEdge & (1 << 9)) { band = (Band)((band + 1) % BAND_COUNT); dirty = true; }

        if (doStop)   { seq = SEQ_NONE; dirty = true; }
        if (startSong >= 0) { song = startSong; seq = SEQ_MELODY; melIdx = 0; melPass = 0; melNoteStart = t; dirty = true; }
        if (doSweep)  { seq = SEQ_SWEEP;  seqStart = t; dirty = true; }
        if (doPulse)  { seq = SEQ_PULSE;  seqStart = t; dirty = true; }

        bool  active   = false;
        float playFreq = freq;

        if (seq == SEQ_MELODY) {
            const Song *sg = &songs[song];
            float dur = sg->notes[melIdx].beats * sg->beatMs;
            u64 el = t - melNoteStart;
            if (el >= (u64)dur) {
                melIdx++;
                melNoteStart = t;
                el = 0;
                if (melIdx >= sg->len) {
                    melPass++;
                    melIdx = 0;
                    if (melPass >= sg->repeats) { seq = SEQ_NONE; dirty = true; }
                }
                if (seq == SEQ_MELODY) dur = sg->notes[melIdx].beats * sg->beatMs;
            }
            if (seq == SEQ_MELODY) {
                int n = sg->notes[melIdx].midi;
                if (n > 0) playFreq = midiToFreq(n);
                active = n > 0 && (float)el < dur * 0.85f;   // short gap between notes
            }
        } else if (seq == SEQ_SWEEP) {
            float p = (float)(t - seqStart) / 4000.0f;   // 4 s, 2 siren cycles
            if (p >= 1.0f) { seq = SEQ_NONE; dirty = true; }
            else {
                float ph = p * 2.0f;
                ph -= floorf(ph);
                float tri = ph < 0.5f ? ph * 2.0f : (1.0f - ph) * 2.0f;
                playFreq = 70.0f * powf(1200.0f / 70.0f, tri);
                active = true;
            }
        } else if (seq == SEQ_PULSE) {
            u64 el = t - seqStart;                       // 5 pulses, 120 ms on / 120 ms off
            if (el >= 1200) { seq = SEQ_NONE; dirty = true; }
            else active = (el % 240) < 120;
        }

        // hold A (or hold the TONE button on screen) = sustained tone
        if (seq == SEQ_NONE && ((kh & HidNpadButton_A) || btnNow[0])) active = true;

        // ---- output ----
        HidVibrationValue val = makeValue(playFreq, amp, band);
        for (int i = 0; i < ndev; i++) {
            bool match = (target == 0) || (target == 1 && devs[i].pos == 1) || (target == 2 && devs[i].pos == 2);
            hidSendVibrationValue(devs[i].h, (active && match) ? &val : &zero);
        }

        // ---- equalizer ----
        static bool eqWasOn = false;
        eqUpdate(active, playFreq, amp);
        bool eqOn = eqVisible();
        if (eqOn || eqWasOn) eqDraw(active, playFreq);
        eqWasOn = eqOn;

        // ---- UI ----
        static u64 lastDraw = 0;
        if (dirty || t - lastDraw > 250) {
            lastDraw = t;
            dirty = false;
            if (needClear) { consoleClear(); needClear = false; }

            int midi = (int)lroundf(69.0f + 12.0f * log2f(freq / 440.0f));
            if (midi < 0) midi = 0;

            printf("\x1b[1;1H== Joy-Con Sound Test (HD Rumble) ==\x1b[K");

            printf("\x1b[3;1HDevices (%d):", ndev);
            for (int i = 0; i < ndev; i++) printf(" [%s]", devs[i].label);
            if (ndev == 0) printf(" none - connect Joy-Con / use handheld");
            printf("\x1b[K");

            printf("\x1b[5;1HPlaying: %s\x1b[K",
                   seq == SEQ_MELODY ? songs[song].name : seq == SEQ_SWEEP ? "siren sweep" :
                   seq == SEQ_PULSE ? "pulses" : "-");

            printf("\x1b[6;1HSong: %s  (ZR = next)\x1b[K", songs[song].name);

            printf("\x1b[%d;1HFrequency: %7.1f Hz   (%s%d)\x1b[K",
                   ROW_FREQ_LABEL, freq, noteNames[midi % 12], midi / 12 - 1);
            drawBar(ROW_FREQ_BAR, freqToT(freq));
            printf("\x1b[%d;1H%d Hz", ROW_FREQ_BAR + 1, (int)FREQ_MIN);
            printf("\x1b[%d;%dH%d Hz\x1b[K", ROW_FREQ_BAR + 1, BAR_COL + BAR_W - 8, (int)FREQ_MAX);

            printf("\x1b[%d;1HVolume: %3d%%\x1b[K", ROW_AMP_LABEL, (int)(amp * 100.0f + 0.5f));
            drawBar(ROW_AMP_BAR, amp);

            char tgt[24], bnd[24];
            snprintf(tgt, sizeof(tgt), "SIDE: %s", targetShort[target]);
            snprintf(bnd, sizeof(bnd), "BAND: %s", bandShort[band]);
            const char *row0[4] = { "HOLD TONE", "PULSES", "SIREN", "STOP" };
            const char *row1[4] = { "ODE TO JOY", "HAPPY BIRTHDAY", "JINGLE BELLS", "FULL SPECTRUM" };
            const char *row2[4] = { tgt, bnd, "EXIT", NULL };
            bool p0[4] = { btnNow[0], btnNow[1], btnNow[2], btnNow[3] };
            bool p1[4] = { btnNow[4], btnNow[5], btnNow[6], btnNow[7] };
            bool p2[4] = { btnNow[8], btnNow[9], btnNow[10], false };
            drawButtonRow(0, row0, p0);
            drawButtonRow(1, row1, p1);
            drawButtonRow(2, row2, p2);

            printf("\x1b[33;1HButtons: A hold tone | B pulses | X play song | ZR next song | Y siren\x1b[K");
            printf("\x1b[34;1H Up/Down note | Left/Right side | L/R volume | ZL band\x1b[K");
            printf("\x1b[35;1H -  stop | +  exit\x1b[K");

            eqDrawAxis();
            eqDraw(active, playFreq);
        }
        consoleUpdate(NULL);
    }

    for (int i = 0; i < ndev; i++) hidSendVibrationValue(devs[i].h, &zero);
    consoleExit(NULL);
    return 0;
}
