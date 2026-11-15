// IRON ROAD - a Death Track style 3D combat racer. C + raylib. Windows & Linux.
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#define NSEG 800
#define MAXCARS 6
static int LAPS = 3;
static float HALF = 15.0f;
#define WALL (HALF - 1.2f)
#define NRACES 10
#define MAXV 56.0f
#define CAR_R 2.1f

enum { W_GUN, W_MISSILE, W_LASER, W_MINE, W_CALT, NW };
static const char *wname[NW] = {"GUN", "MISSILE", "LASER", "MINE", "CALTROPS"};
static const char *wkey[NW] = {"F", "G", "H", "B", "V"};
static const int ammoMax[NW] = {300, 12, 20, 12, 12};
static const int ammoStart[NW] = {150, 4, 6, 3, 3};
static const float wcd[NW] = {0.07f, 0.9f, 0.7f, 0.6f, 0.6f};

typedef struct { Vector2 p, t, r; } TP;  // centre, tangent, right-hand normal (x,z plane)
static TP trk[NSEG];
static float trkLen;
static float trkY[NSEG];      // road elevation (flyovers)
static float trkK[NSEG];      // road curvature (1/radius) at each point
static Vector2 crossP;        // centre of the flyover crossing
static bool hasBridge;
static int bridgeLo = -1, bridgeHi = -1;   // track indices of the lower and upper pass at the flyover
static float baseY;           // elevation added to particles spawned this frame
static bool classic;          // game mode: false = arcade, true = classic ("death track")
static int mode;
typedef struct { Vector2 p; float h, y; int kind, dir; } Sign;   // kind 0 warning, 1 hairpin warning, 2 chevron
static Sign signs[400];
static int nsigns;

typedef struct { float thr, steer; bool hb, fire[NW]; } Ctl;

typedef struct { int armor, fspike, rspike, gun, missile, laser, ammo, engine, trans, tires; } Upg;
typedef struct { float x, y, z, dx, dz, r, depth; } Dent;
typedef struct { const char *name, *desc; float spd, acc, grip, hp, mass, sx, sy, sz; } CarType;
static const CarType ctypes[3] = {
    {"BRAWLER", "Balanced all-rounder. No weaknesses, no tricks.", 1.00f, 1.00f, 1.00f, 100, 1.00f, 1.00f, 1.00f, 1.00f},
    {"HORNET", "Light and nimble: quicker, grippier, but fragile and easily shoved around.", 1.06f, 1.12f, 1.10f, 80, 0.80f, 0.94f, 0.88f, 1.04f},
    {"CRUSHER", "Heavy armored bruiser: very tough and wins collisions, but slower.", 0.94f, 0.88f, 0.94f, 135, 1.45f, 1.12f, 1.18f, 0.97f},
};
static const Color carPalette[8] = {{40,120,255,255},{230,50,50,255},{235,200,40,255},{40,200,120,255},{255,130,20,255},{160,70,210,255},{235,235,235,255},{70,70,80,255}};
static int carType, carColor;   // the player's choice (saved with the campaign)
#define NDENT 24
// deformable body lattice: top 9x17, sides 17x4 (x2), ends 9x4 (x2)
#define NV 361

typedef struct {
    Vector2 p, v;
    float h, steer, hp, maxv, lane;
    int ammo[NW];
    float cd[NW];
    int lap, idx, kills, lastAttacker, place;
    float mass; int type; float finT, aiBurst, aiRest; bool gone;
    float prog, respawn, invuln, flat, stuck, finishTime, aiT, maxhp, y, accel, gripN, steerK, accFall, battery, evadeT, prevSteerIn, prevThr;
    bool ai, alive, finished, meshDirty;
    Upg u;
    Dent dents[NDENT]; int ndent;
    Vector3 bv[NV]; float bd[NV];
    Color col;
    const char *name;
} Car;
static Car cars[MAXCARS];

typedef struct { bool on; int type; Vector2 p; float y; Vector2 v; int owner; float life; int target; float dmg; float lockT; bool aiRolled; } Proj;
static Proj proj[256];
typedef struct { bool on; Vector2 p; int owner; float arm, life, y; } Mine;
static Mine mines[64];
typedef struct { bool on; Vector2 p; int owner; float life, y; } Calt;
static Calt calts[64];
typedef struct { Vector2 p; int type; float respawn, y; } Pickup;
static Pickup picks[14];
static int npicks = 14;
typedef struct { Vector3 a, b; float life; Color c; } Beam;
static Beam beams[32];
typedef struct { Vector3 p, v; float life, max, size; Color c; } Part;
static Part parts[4000];
static int partHead;
typedef struct { Vector3 p; float h, w, d; Color c; int type; } Deco;
static Deco deco[220];

typedef enum { ST_MENU, ST_STORE, ST_COUNT, ST_RACE, ST_DONE, ST_END, ST_CARSEL } State;
static State state = ST_MENU;
static float stateT, raceT, shake;
static char msgs[5][64];
static float msgT[5];

// ---------------------------------------------------------------- audio
static Sound sGun, sMis, sLas, sExp, sMine, sPick, sEng, sBeep, sBeepMine, sBeepCalt;
static bool audioOK;
static Sound synth(float f0, float f1, float dur, float noise, float decay, int saw) {
    int n = (int)(22050 * dur);
    short *d = malloc(n * sizeof(short));
    float ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / n, f = f0 + (f1 - f0) * t;
        ph += f / 22050.0f;
        float w = saw ? (2 * (ph - floorf(ph)) - 1) : sinf(ph * 6.2831853f);
        float nz = (float)GetRandomValue(-1000, 1000) / 1000.0f;
        float env = expf(-decay * t);
        d[i] = (short)((w * (1 - noise) + nz * noise) * env * 14000);
    }
    Wave w = {n, 22050, 16, 1, d};
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}
static Sound engineLoop(void) {
    int n = 22050;
    short *d = malloc(n * sizeof(short));
    for (int i = 0; i < n; i++) {
        float t = (float)i / 22050.0f, s = 0;
        for (int h = 1; h <= 6; h++) s += sinf(t * 6.2831853f * 50 * h) / h;
        d[i] = (short)(s * 4500);
    }
    Wave w = {n, 22050, 16, 1, d};
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}
static void snd(Sound s, float vol, float pitch) {
    if (!audioOK) return;
    SetSoundVolume(s, vol);
    SetSoundPitch(s, pitch);
    PlaySound(s);
}


// ---------------------------------------------------------------- title music (synthesized at startup, no assets)
// 8 bars of A minor at 112 BPM: pad, bass, arpeggio with echo, lead melody and drums, rendered to a 22 kHz loop.
#define MUS_SR 22050
static Music titleMusic;
static bool musicOK;
static float musicVol;
static unsigned char *musicWav;
static int musicWavSize;
static float mNoise(void) { static unsigned st = 12345; st = st * 1664525u + 1013904223u; return ((st >> 8) & 0xFFFF) / 32768.0f - 1.0f; }
static float midiHz(float n) { return 440.0f * powf(2.0f, (n - 69.0f) / 12.0f); }
static void mNote(float *mix, int N, float t0, float dur, float hz, float vol, int wave, float decay) {   // wave: 0 square, 1 saw, 2 sine
    int s0 = (int)(t0 * MUS_SR), n = (int)(dur * MUS_SR);
    for (int i = 0; i < n; i++) {
        float t = (float)i / MUS_SR, ph = hz * t; ph -= floorf(ph);
        float w = wave == 0 ? (ph < 0.4f ? 1.0f : -1.0f) : wave == 1 ? 2 * ph - 1 : sinf(6.2831853f * ph);
        float env = expf(-decay * t) * fminf(1, t / 0.005f) * (i > n - 300 ? (float)(n - i) / 300 : 1);
        mix[(s0 + i) % N] += w * env * vol;   // modulo: the tail wraps around so the loop is seamless
    }
}
static void mKick(float *mix, int N, float t0, float vol) {
    int s0 = (int)(t0 * MUS_SR), n = (int)(0.28f * MUS_SR); float ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / MUS_SR, f = 45 + 95 * expf(-22 * t);
        ph += f / MUS_SR;
        mix[(s0 + i) % N] += sinf(6.2831853f * ph) * expf(-9 * t) * vol;
    }
}
static void mSnare(float *mix, int N, float t0, float vol) {
    int s0 = (int)(t0 * MUS_SR), n = (int)(0.22f * MUS_SR);
    for (int i = 0; i < n; i++) {
        float t = (float)i / MUS_SR;
        mix[(s0 + i) % N] += (mNoise() * expf(-17 * t) * 0.6f + sinf(6.2831853f * 190 * t) * expf(-26 * t) * 0.4f) * vol;
    }
}
static void mHat(float *mix, int N, float t0, float vol, float decay) {
    int s0 = (int)(t0 * MUS_SR), n = (int)(0.09f * MUS_SR); float prev = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / MUS_SR, x = mNoise();
        mix[(s0 + i) % N] += (x - prev) * 0.5f * expf(-decay * t) * vol;   // differencing = crude high-pass
        prev = x;
    }
}
static void buildTitleMusic(void) {
    const float beat = 60.0f / 112.0f, step = beat / 4;
    const int bars = 8, steps = bars * 16, N = (int)(steps * step * MUS_SR);
    float *mix = calloc(N, sizeof(float));
    static const int bassRoot[8] = {33, 33, 29, 31, 33, 33, 29, 28};                       // A A F G A A F E
    static const int tri[8][3] = {{69,72,76},{69,72,76},{65,69,72},{67,71,74},{69,72,76},{69,72,76},{65,69,72},{64,68,71}};
    static const int arpPat[16] = {0,1,2,1, 0,1,2,1, 0,1,2,1, 2,1,2,1};
    static const int lead[4][8] = {{69,0,72,0,76,0,74,72},{69,0,72,76,81,0,79,76},{69,0,72,0,77,0,76,72},{68,71,76,0,74,71,68,0}};
    for (int bar = 0; bar < bars; bar++) {
        float tb = bar * 16 * step;
        for (int k = 0; k < 3; k++) mNote(mix, N, tb, 16 * step, midiHz(tri[bar][k] - 12), 0.045f, 2, 0.25f);   // pad
        for (int st = 0; st < 16; st++) {
            float t = tb + st * step;
            if (st % 2 == 0) {   // bass on eighths
                int e = st / 2, n = bassRoot[bar] + ((e == 2 || e == 5) ? 12 : (e == 7 ? 7 : 0));
                mNote(mix, N, t, 2 * step * 0.9f, midiHz((float)n), 0.30f, 1, 3.5f);
            }
            int an = tri[bar][arpPat[st] % 3] + (st >= 12 ? 12 : 0);      // arpeggio + echo
            mNote(mix, N, t, step * 1.5f, midiHz((float)an), 0.075f, 0, 7);
            mNote(mix, N, t + 3 * step, step * 1.5f, midiHz((float)an), 0.03f, 0, 9);
            if (bar >= 4 && st % 2 == 0 && lead[bar - 4][st / 2])            // melody in the second half
                mNote(mix, N, t, 2 * step * 1.8f, midiHz((float)lead[bar - 4][st / 2]), 0.13f, 1, 2.2f);
            if (st == 0 || st == 8 || (st == 10 && bar % 2)) mKick(mix, N, t, 0.9f);   // drums
            if (st == 4 || st == 12) mSnare(mix, N, t, 0.5f);
            if (st % 2 == 0) mHat(mix, N, t, st == 14 ? 0.22f : 0.13f, st == 14 ? 28.0f : 70.0f);
        }
    }
    int dataBytes = N * 2;
    musicWavSize = 44 + dataBytes;
    musicWav = malloc(musicWavSize);
    unsigned char *h = musicWav;
    #define W32(o, v) do { unsigned u_ = (unsigned)(v); h[o] = u_ & 255; h[(o)+1] = (u_ >> 8) & 255; h[(o)+2] = (u_ >> 16) & 255; h[(o)+3] = (u_ >> 24) & 255; } while (0)
    memcpy(h, "RIFF", 4); W32(4, musicWavSize - 8); memcpy(h + 8, "WAVEfmt ", 8); W32(16, 16);
    h[20] = 1; h[21] = 0; h[22] = 1; h[23] = 0; W32(24, MUS_SR); W32(28, MUS_SR * 2); h[32] = 2; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4); W32(40, dataBytes);
    short *pcm = (short *)(h + 44);   // 44-byte header keeps this 2-byte aligned
    for (int i = 0; i < N; i++) pcm[i] = (short)(tanhf(mix[i] * 1.1f) * 0.85f * 32767);
    free(mix);
    if (getenv("RACE_DUMPMUSIC")) { FILE *f = fopen(getenv("RACE_DUMPMUSIC"), "wb"); if (f) { fwrite(musicWav, 1, musicWavSize, f); fclose(f); } }
    titleMusic = LoadMusicStreamFromMemory(".wav", musicWav, musicWavSize);
    musicOK = IsMusicValid(titleMusic);
    if (musicOK) titleMusic.looping = true;
    else fprintf(stderr, "title music failed to load\n");
}

// ---------------------------------------------------------------- helpers
static float rf(float a, float b) { return a + (b - a) * (GetRandomValue(0, 10000) / 10000.0f); }
static float angDiff(float a, float b) {
    float d = fmodf(b - a, 2 * PI);
    if (d > PI) d -= 2 * PI;
    if (d < -PI) d += 2 * PI;
    return d;
}
static Vector2 fwdOf(float h) { return (Vector2){sinf(h), cosf(h)}; }
static void addMsg(const char *fmt, ...) {
    for (int i = 4; i > 0; i--) { memcpy(msgs[i], msgs[i - 1], 64); msgT[i] = msgT[i - 1]; }
    va_list a; va_start(a, fmt); vsnprintf(msgs[0], 64, fmt, a); va_end(a);
    msgT[0] = 5;
}
static void part(Vector3 p, Vector3 v, float life, float size, Color c) {
    Part *q = &parts[partHead++ % 4000];
    q->p = (Vector3){p.x, p.y + baseY, p.z}; q->v = v; q->life = q->max = life; q->size = size; q->c = c;
}
static int nearestIdx(Vector2 p, int hint) {
    int best = hint; float bd = 1e18f;
    for (int d = -30; d <= 30; d++) {
        int i = (hint + d + NSEG) % NSEG;
        float dd = Vector2DistanceSqr(p, trk[i].p);
        if (dd < bd) { bd = dd; best = i; }
    }
    return best;
}

// ---------------------------------------------------------------- track
#include "tracks_data.h"
typedef struct {
    const char *name, *place;
    const float (*pts)[2]; int n;
    float half;
    Color skyTop, skyBot, ground, road1, road2, wallA, wallB, curb, decoA, decoB;
    int decoType;  // 0 buildings, 1 trees, 2 rocks, 3 spires, 4 ice
} TrackDef;
#define TD(arr) arr, (int)(sizeof(arr) / sizeof(arr[0]))
static const TrackDef tracksDef[NRACES] = {
 {"RUST VALLEY","Arizona badlands",TD(TP0),15,{70,120,200,255},{200,190,170,255},{150,110,70,255},{70,66,64,255},{62,58,56,255},{200,60,40,255},{235,230,220,255},{110,100,90,255},{170,100,60,255},{130,80,50,255},2},
 {"NEON CITY","Tokyo-9 night circuit",TD(TP1),15,{8,6,30,255},{70,20,90,255},{18,18,30,255},{40,40,52,255},{34,34,46,255},{0,220,255,255},{255,40,200,255},{70,70,90,255},{0,180,255,255},{255,50,180,255},0},
 {"FROZEN PASS","Siberian ice road",TD(TP2),16,{120,160,210,255},{225,235,245,255},{235,240,248,255},{90,100,115,255},{80,90,105,255},{40,90,200,255},{240,240,255,255},{190,205,220,255},{200,225,245,255},{150,190,225,255},4},
 {"TOXIC SWAMP","Louisiana bayou",TD(TP3),15,{60,90,50,255},{150,170,100,255},{50,70,40,255},{70,64,55,255},{60,55,48,255},{150,200,40,255},{60,60,40,255},{90,80,50,255},{40,100,40,255},{70,120,50,255},1},
 {"CANYON RUN","Utah red rock",TD(TP4),16,{230,150,90,255},{250,210,150,255},{185,95,55,255},{75,68,66,255},{66,60,58,255},{230,140,30,255},{255,240,200,255},{140,70,40,255},{200,100,50,255},{160,70,40,255},2},
 {"VOLCANO RING","Hawaii lava fields",TD(TP5),16,{40,10,10,255},{200,70,30,255},{40,28,28,255},{50,46,46,255},{44,40,40,255},{255,90,0,255},{40,30,30,255},{60,45,45,255},{255,100,20,255},{90,50,40,255},3},
 {"HARBOR DOCKS","Rotterdam shipyards",TD(TP6),17,{110,150,190,255},{200,215,225,255},{75,80,88,255},{66,68,72,255},{58,60,64,255},{255,200,0,255},{30,30,30,255},{120,125,135,255},{200,80,40,255},{40,90,160,255},0},
 {"MOON BASE","Mare Tranquillitatis",TD(TP7),16,{2,2,8,255},{30,30,50,255},{120,120,125,255},{90,90,96,255},{80,80,86,255},{100,180,255,255},{220,220,230,255},{150,150,160,255},{170,170,180,255},{100,100,115,255},2},
 {"JUNGLE RUINS","Yucatan temple road",TD(TP8),15,{100,170,120,255},{190,225,160,255},{35,85,35,255},{85,70,50,255},{75,62,44,255},{120,200,60,255},{90,70,40,255},{100,80,50,255},{25,100,30,255},{40,130,45,255},1},
 {"DEATH TRACK","The final arena",TD(TP9),17,{20,0,0,255},{120,10,10,255},{25,22,24,255},{36,34,36,255},{30,28,30,255},{255,0,0,255},{20,20,20,255},{80,0,0,255},{200,20,20,255},{60,60,60,255},3},
};
static int curTrack;
#define TDEF (&tracksDef[curTrack])

static void buildTrack(void) {
    const float (*cp)[2] = TDEF->pts;
    HALF = TDEF->half;
    const int M = TDEF->n, D = 12000;
    static Vector2 dense[12000]; static float cum[12001];
    for (int i = 0; i < D; i++) {
        float u = (float)i / D * M; int s = (int)u; float f = u - s;
        Vector2 P[4];
        for (int k = 0; k < 4; k++) { int j = (s + k - 1 + M) % M; P[k] = (Vector2){cp[j][0], cp[j][1]}; }
        float f2 = f * f, f3 = f2 * f;
        dense[i].x = 0.5f * ((2*P[1].x) + (-P[0].x+P[2].x)*f + (2*P[0].x-5*P[1].x+4*P[2].x-P[3].x)*f2 + (-P[0].x+3*P[1].x-3*P[2].x+P[3].x)*f3);
        dense[i].y = 0.5f * ((2*P[1].y) + (-P[0].y+P[2].y)*f + (2*P[0].y-5*P[1].y+4*P[2].y-P[3].y)*f2 + (-P[0].y+3*P[1].y-3*P[2].y+P[3].y)*f3);
    }
    cum[0] = 0;
    for (int i = 0; i < D; i++) cum[i + 1] = cum[i] + Vector2Distance(dense[i], dense[(i + 1) % D]);
    trkLen = cum[D];
    int j = 0;
    for (int i = 0; i < NSEG; i++) {
        float target = trkLen * i / NSEG;
        while (j < D - 1 && cum[j + 1] < target) j++;
        trk[i].p = dense[j];
    }
    for (int i = 0; i < NSEG; i++) {
        Vector2 t = Vector2Normalize(Vector2Subtract(trk[(i + 1) % NSEG].p, trk[(i + NSEG - 1) % NSEG].p));
        trk[i].t = t; trk[i].r = (Vector2){-t.y, t.x};
    }
}

// ---------------------------------------------------------------- elevation & warning signs
static void buildCurvature(void) {
    float seg = trkLen / NSEG;
    for (int i = 0; i < NSEG; i++) {
        Vector2 a = trk[(i - 4 + NSEG) % NSEG].t, b = trk[(i + 4) % NSEG].t;
        trkK[i] = fabsf(atan2f(a.x * b.y - a.y * b.x, Vector2DotProduct(a, b))) / (8 * seg);
    }
}
static float yAt(Vector2 p, int idx) {
    TP *a = &trk[idx];
    float seg = trkLen / NSEG, t = Vector2DotProduct(Vector2Subtract(p, a->p), a->t) / seg;
    int j = t >= 0 ? (idx + 1) % NSEG : (idx + NSEG - 1) % NSEG;
    return trkY[idx] + (trkY[j] - trkY[idx]) * fminf(fabsf(t), 1);
}
static float gy(Vector2 p) {  // elevation at an arbitrary point (global nearest-centre search)
    int best = 0; float bd = 1e18f;
    for (int i = 0; i < NSEG; i++) { float d = Vector2DistanceSqr(p, trk[i].p); if (d < bd) { bd = d; best = i; } }
    return yAt(p, best);
}
static void buildBridge(void) {
    memset(trkY, 0, sizeof trkY); hasBridge = false;
    float seg = trkLen / NSEG, bd = 1e18f; int bi = -1, bj = -1;
    for (int i = 0; i < NSEG; i += 2) for (int j = i + 1; j < NSEG; j += 2) {
        int cd = j - i; if (NSEG - cd < cd) cd = NSEG - cd;
        if (cd * seg < 150) continue;
        float d = Vector2DistanceSqr(trk[i].p, trk[j].p);
        if (d < bd) { bd = d; bi = i; bj = j; }
    }
    if (bi < 0 || sqrtf(bd) > HALF * 1.6f) return;
    hasBridge = true; bridgeLo = bi; bridgeHi = bj;
    crossP = Vector2Scale(Vector2Add(trk[bi].p, trk[bj].p), 0.5f);
    if (getenv("RACE_DEBUG")) fprintf(stderr, "bridge: crossing idx %d/%d\n", bi, bj);
    float W = 230.0f / seg;
    for (int i = 0; i < NSEG; i++) {
        int d = abs(i - bj); if (NSEG - d < d) d = NSEG - d;
        float u = d / W, h;
        if (u <= 0.3f) h = 1; else if (u >= 1) h = 0; else { float t = (u - 0.3f) / 0.7f; h = 1 - t * t * (3 - 2 * t); }
        trkY[i] = 8.0f * h;
    }
}
static void addSign(Vector2 p, Vector2 face, int kind, int dir, float y) {
    if (nsigns >= 400) return;
    signs[nsigns++] = (Sign){p, atan2f(face.x, face.y), y, kind, dir};
}
static void buildSigns(void) {
    nsigns = 0;
    float seg = trkLen / NSEG, kv[NSEG];
    for (int i = 0; i < NSEG; i++) {
        Vector2 a = trk[(i - 5 + NSEG) % NSEG].t, b = trk[(i + 5) % NSEG].t;
        kv[i] = atan2f(a.x * b.y - a.y * b.x, Vector2DotProduct(a, b)) / (10 * seg);  // >0 = turning right
    }
    const float thr = 1.0f / 85;
    int z = 0;
    while (z < NSEG && fabsf(kv[z]) > thr) z++;
    if (z == NSEG) return;
    int s0 = -1;
    for (int n = 1; n <= NSEG + 1; n++) {
        int i = (z + n) % NSEG;
        bool in = fabsf(kv[i]) > thr && n <= NSEG;
        if (in && s0 < 0) s0 = i;
        if (!in && s0 >= 0) {
            int e = (i + NSEG - 1) % NSEG, len = (e - s0 + NSEG) % NSEG + 1;
            float sum = 0, mk = 0;
            for (int q = 0, j = s0; q < len; q++, j = (j + 1) % NSEG) { sum += kv[j] * seg; mk = fmaxf(mk, fabsf(kv[j])); }
            int dir = sum > 0 ? 1 : -1;
            float total = fabsf(sum);
            if (total >= 0.55f) {
                bool hairpin = total > 2.2f || mk > 1.0f / 38;
                if (getenv("RACE_DEBUG")) fprintf(stderr, "corner at idx %d len %d turn %.0f deg %s%s\n", s0, len, total * 57.3f, dir > 0 ? "R" : "L", hairpin ? " HAIRPIN" : "");
                int pre[3] = {(int)(105 / seg), (int)(190 / seg), (int)(55 / seg)};
                for (int w = 0; w < (hairpin ? 3 : 1); w++) {
                    int wi = ((s0 - pre[w]) % NSEG + NSEG) % NSEG;
                    TP *t = &trk[wi];
                    for (int side = -1; side <= 1; side += 2)
                        addSign(Vector2Add(t->p, Vector2Scale(t->r, side * (HALF + 3.2f))), Vector2Scale(t->t, -1), hairpin ? 1 : 0, dir, trkY[wi]);
                }
                int step = len / 6 > 3 ? len / 6 : 3;
                for (int q = 0; q < len; q += step) {
                    int ci = (s0 + q) % NSEG; TP *t = &trk[ci];
                    Vector2 pos = Vector2Add(t->p, Vector2Scale(t->r, -dir * (HALF + 3.0f)));
                    Vector2 nrm = Vector2Subtract(Vector2Scale(t->r, dir * 1.0f), Vector2Scale(t->t, 0.5f));
                    addSign(pos, nrm, 2, dir, trkY[ci]);
                }
            }
            s0 = -1;
        }
    }
}

// ---------------------------------------------------------------- game objects
static int raceIdx, money, score;
static float deadT;
static Upg pu;  // player's purchased upgrades
static float pHp = -1;            // classic: carried-over armor (<0 = full)
static int pAmmo[NW];             // classic: carried-over consumables
static int champ[MAXCARS];        // championship points of the AI drivers (the player's are in `score`)
static bool warnMissile, warnMine, warnCalt;   // proximity / lock warnings for the player this frame
static float warnMissileDist, warnMineDist, warnCaltDist;
static int gridSlot(int i);
static void dent(Car *c, Vector2 src, float strength);
static void explode(Vector2 p, float radius, float dmg, int owner, bool big);
void damageCar(Car *c, float d, int owner);

static int ammoCap(Car *c, int w) { return (int)(ammoMax[w] * (1 + 0.5f * c->u.ammo)); }
static float gunDmg(Car *c) { return 3.5f + 1.3f * c->u.gun; }
static float misDmg(Car *c) { return 45 + 15 * c->u.missile; }
static float lasDmg(Car *c) { return 22 + 8 * c->u.laser; }
static float cdOf(Car *c, int w) {
    float m = 1;
    if (w == W_GUN) m = 1 - 0.09f * c->u.gun;
    if (w == W_LASER) m = 1 - 0.1f * c->u.laser;
    if (w == W_MISSILE) m = 1 - 0.1f * c->u.missile;
    return wcd[w] * m;
}

static float battCap(Car *c) { return (classic && c->ai) ? 4.0f : 10.0f * (1 + 0.5f * c->u.ammo); }
static int gridSlot(int i) {   // classic: starting grid follows the championship standings
    if (!classic) return i;
    #define GKEY(x) ((x) == 0 ? score : champ[x]) * 10 + ((x) == 0 ? 0 : 6 - (x))
    int rank = 0, ki = GKEY(i);
    for (int j = 0; j < MAXCARS; j++) if (j != i && GKEY(j) > ki) rank++;
    return rank;
}
static void respawnCar(Car *c) {
    TP *t = &trk[c->idx];
    c->p = Vector2Add(t->p, Vector2Scale(t->r, c->lane * 0.5f));
    c->h = atan2f(t->t.x, t->t.y);
    c->v = (Vector2){0, 0};
    c->steer = 0; c->alive = true; c->invuln = 2.0f; c->flat = 0; c->stuck = 0; c->y = trkY[c->idx];
    if (c->hp <= 0) { c->hp = c->maxhp; c->ndent = 0; c->meshDirty = true; }
}
static void initCar(int i) {
    Car *c = &cars[i];
    memset(c, 0, sizeof(*c));
    static const char *names[MAXCARS] = {"YOU", "VIPER", "REAPER", "SKULL", "BLAZE", "NOVA"};
    static const Color cols[MAXCARS] = {{40,120,255,255},{230,50,50,255},{230,200,40,255},{150,60,200,255},{255,130,20,255},{40,200,120,255}};
    c->name = names[i]; c->col = cols[i]; c->ai = i > 0;
    c->type = i == 0 ? carType : (i + raceIdx) % 3;
    if (i == 0) c->col = carPalette[carColor];
    else {   // don't let an AI share the player's colour
        Color pc = carPalette[carColor];
        if (c->col.r == pc.r && c->col.g == pc.g && c->col.b == pc.b) c->col = carColor == 0 ? (Color){120, 120, 130, 255} : carPalette[0];
    }
    int r = raceIdx;
    c->accel = 42; c->gripN = 7; c->steerK = 1.9f; c->accFall = 0.6f;
    if (i == 0) {
        c->maxv = MAXV; c->u = pu;
        if (classic) {  // classic: a stock car is slow; engine / transmission / tires make it competitive
            c->maxv = 34 + 3.0f * pu.engine + 2.5f * pu.trans;
            c->accel = 24 + 4.0f * pu.engine + 1.5f * pu.trans;
            c->accFall = 0.75f - 0.1f * pu.trans;
            c->gripN = 4.5f + 0.5f * pu.tires;
            c->steerK = 1.5f + 0.1f * pu.tires;
        }
    } else {
        {
            // classic: two leaders (VIPER, REAPER) get away; the other three form a pack near a stock car's pace
            static const float tierOff[MAXCARS] = {0, 13.0f, 10.0f, 3.0f, 1.5f, 0.0f};
            c->maxv = classic ? 31.0f + r * 1.9f + tierOff[i] + rf(0, 2.0f) : 40.0f + r * 0.9f + rf(0, 7);
            if (getenv("RACE_DEBUG")) fprintf(stderr, "AI %s maxv=%.1f\n", c->name, c->maxv);
        }
        c->u = (Upg){r / 2 + (r > 0 ? GetRandomValue(0, 1) : 0), (r + GetRandomValue(0, 3)) / 4, (r + GetRandomValue(0, 3)) / 4,
                     (r + GetRandomValue(0, 2)) / 3, r / 4, r / 4, r / 3};
        if (c->u.armor > 5) c->u.armor = 5;
        if (c->u.gun > 4) c->u.gun = 4;
    }
    {   // car type modifiers
        const CarType *ct = &ctypes[c->type];
        c->maxv *= ct->spd; c->accel *= ct->acc; c->gripN *= ct->grip; c->mass = ct->mass;
        c->maxhp = ct->hp + 30 * c->u.armor;
    }
    // grid slot: in classic mode you start at the very back until you have won on this track
    int slot = gridSlot(i);
    c->lane = (slot % 2 ? 1 : -1) * 5.0f;
    c->idx = NSEG - 6 - (slot / 2) * 6;
    c->lap = 0; c->hp = c->maxhp;
    c->meshDirty = true;
    for (int w = 0; w < NW; w++) c->ammo[w] = (int)(ammoStart[w] * (1 + 0.5f * c->u.ammo));
    if (classic && i > 0) {   // classic AI cars are short on ammo too
        c->ammo[W_GUN] = 50; c->ammo[W_MISSILE] = 1; c->ammo[W_MINE] = 1; c->ammo[W_CALT] = 1;
    }
    if (classic && i == 0) {  // consumables and damage carry over between races
        for (int w = 0; w < NW; w++) c->ammo[w] = pAmmo[w];
        if (pHp >= 0) c->hp = fminf(pHp, c->maxhp);
    }
    c->lastAttacker = -1;
    c->battery = battCap(c);   // classic: lasers run off a rechargeable battery
    respawnCar(c); c->invuln = 0;
    if (classic && i == 0 && c->hp < c->maxhp) {  // show the accumulated damage as dents
        int nd = (int)((1 - c->hp / c->maxhp) * 14);
        for (int d = 0; d < nd; d++) {
            float a = rf(0, 6.28f);
            dent(c, Vector2Add(c->p, (Vector2){cosf(a) * 4, sinf(a) * 4}), rf(8, 28));
        }
    }
}
static void newRace(void) {
    memset(proj, 0, sizeof(proj)); memset(mines, 0, sizeof(mines));
    memset(calts, 0, sizeof(calts)); memset(beams, 0, sizeof(beams)); memset(parts, 0, sizeof(parts));
    for (int i = 0; i < MAXCARS; i++) initCar(i);
    npicks = classic ? 3 : 14;   // classic: power-ups are rare, single use
    for (int i = 0; i < npicks; i++) {
        int idx = classic ? (i * NSEG / 3 + 120 + GetRandomValue(0, 60)) % NSEG : (i * NSEG / 14 + 40) % NSEG;
        picks[i].p = Vector2Add(trk[idx].p, Vector2Scale(trk[idx].r, rf(-8, 8)));
        picks[i].type = classic ? (i == 2 ? -1 : GetRandomValue(1, NW - 1)) : ((i % 3 == 0) ? -1 : GetRandomValue(0, NW - 1));  // -1 = repair
        picks[i].respawn = 0; picks[i].y = trkY[idx];
    }
    deadT = 0;
    raceT = 0; memset(msgT, 0, sizeof(msgT));
}
static void loadTrack(int i) {
    curTrack = i;
    buildTrack();
    buildBridge();
    buildSigns();
    buildCurvature();
    const TrackDef *T = TDEF;
    float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
    for (int k = 0; k < NSEG; k++) {
        minx = fminf(minx, trk[k].p.x); maxx = fmaxf(maxx, trk[k].p.x);
        minz = fminf(minz, trk[k].p.y); maxz = fmaxf(maxz, trk[k].p.y);
    }
    SetRandomSeed(1000 + i * 77);
    for (int n = 0; n < 220; n++) {
        for (;;) {
            Vector2 p = {rf(minx - 250, maxx + 250), rf(minz - 250, maxz + 250)};
            float best = 1e9f;
            for (int k = 0; k < NSEG; k += 4) best = fminf(best, Vector2Distance(p, trk[k].p));
            if (best < 38) continue;
            float h = rf(3, 22), w = rf(4, 14);
            Color c = GetRandomValue(0, 1) ? T->decoA : T->decoB;
            if (T->decoType == 1) { h = rf(6, 16); w = rf(4, 8); }
            if (T->decoType == 2) { h = rf(2, 9); w = rf(5, 16); }
            if (T->decoType == 3) { h = rf(10, 34); w = rf(3, 8); }
            if (T->decoType == 4) { h = rf(3, 14); w = rf(5, 12); }
            deco[n] = (Deco){{p.x, 0, p.y}, h, w, T->decoType == 0 ? rf(4, 14) : w, c, T->decoType};
            break;
        }
    }
    SetRandomSeed((unsigned)(GetTime() * 100000) + 13);
}

// ---- crinkle damage: dents deform the body lattice
static Vector2 toLocal(Car *c, Vector2 w) {
    Vector2 d = Vector2Subtract(w, c->p);
    return (Vector2){d.x * cosf(c->h) - d.y * sinf(c->h), d.x * sinf(c->h) + d.y * cosf(c->h)};
}
static void dent(Car *c, Vector2 src, float strength) {
    Vector2 l = toLocal(c, src);
    float len = Vector2Length(l);
    if (len < 0.01f) { l = (Vector2){0, 1}; len = 1; }
    Vector2 dir = {l.x / len, l.y / len};
    float t = fminf(fabsf(dir.x) > 1e-4f ? 1.0f / fabsf(dir.x) : 1e9f, fabsf(dir.y) > 1e-4f ? 2.2f / fabsf(dir.y) : 1e9f);
    float depth = fminf(0.8f, strength * 0.045f), r = Clamp(0.5f + strength * 0.05f, 0.5f, 2.2f);
    Dent nd = {dir.x * t, 0.7f + rf(-0.15f, 0.25f), dir.y * t, -dir.x, -dir.y, r, depth};
    c->meshDirty = true;
    for (int i = 0; i < c->ndent; i++) {
        Dent *d = &c->dents[i];
        float dd = sqrtf((d->x - nd.x) * (d->x - nd.x) + (d->z - nd.z) * (d->z - nd.z));
        if (dd < 0.6f) {
            d->depth = fminf(0.85f, d->depth + depth * 0.8f);
            d->r = fmaxf(d->r, r * 0.9f);
            return;
        }
    }
    if (c->ndent < NDENT) c->dents[c->ndent++] = nd;
    else c->dents[GetRandomValue(0, NDENT - 1)] = nd;
}
static void hitCar(Car *c, float dmg, int owner, Vector2 src) {
    if (!c->alive || c->invuln > 0) return;
    if (classic && c == &cars[0] && owner > 0) dmg *= 0.6f;   // classic: AI fire is less lethal
    dent(c, src, dmg);
    damageCar(c, dmg, owner);
}

static void explode(Vector2 p, float radius, float dmg, int owner, bool big) {
    int n = big ? 60 : 25;
    for (int i = 0; i < n; i++) {
        Vector3 v = {rf(-1, 1), rf(0.2f, 1.2f), rf(-1, 1)};
        v = Vector3Scale(Vector3Normalize(v), rf(4, big ? 22 : 14));
        part((Vector3){p.x, 0.8f, p.y}, v, rf(0.4f, 1.1f), rf(0.4f, 1.3f), i % 3 ? ORANGE : (i % 3 == 0 ? YELLOW : RED));
    }
    for (int i = 0; i < n / 2; i++)
        part((Vector3){p.x, 1, p.y}, (Vector3){rf(-3, 3), rf(2, 6), rf(-3, 3)}, rf(0.8f, 1.8f), rf(0.8f, 2), (Color){60, 60, 60, 200});
    snd(sExp, 0.9f, rf(0.8f, 1.1f));
    for (int i = 0; i < MAXCARS; i++) {
        Car *c = &cars[i];
        if (!c->alive || fabsf(c->y - baseY) > 4) continue;
        float d = Vector2Distance(c->p, p);
        if (d < radius) {
            float dm = dmg * (1 - d / radius * 0.6f);
            hitCar(c, dm, owner, p);
            Vector2 push = Vector2Normalize(Vector2Subtract(c->p, p));
            c->v = Vector2Add(c->v, Vector2Scale(push, 10));
        }
    }
    float dc = Vector2Distance(p, cars[0].p);
    if (dc < 60) shake = fmaxf(shake, big ? 0.8f : 0.4f);
}
void damageCar(Car *c, float d, int owner) {
    if (!c->alive || c->invuln > 0) return;
    c->hp -= d;
    if (owner >= 0 && &cars[owner] != c) c->lastAttacker = owner;
    if (c->hp <= 0) {
        c->hp = 0; c->alive = false; c->respawn = 3.5f;
        explode(c->p, 0, 0, -1, true);
        if (classic) for (int d = 0; d < 9; d++) dent(c, Vector2Add(c->p, (Vector2){rf(-3, 3), rf(-3, 3)}), rf(25, 50));   // mangled hulk
        for (int i = 0; i < 30; i++)
            part((Vector3){c->p.x, 1, c->p.y}, (Vector3){rf(-8, 8), rf(3, 14), rf(-8, 8)}, rf(0.6f, 1.6f), 0.5f, c->col);
        int self = (int)(c - cars);
        if (c->lastAttacker >= 0 && c->lastAttacker != self) {
            cars[c->lastAttacker].kills++;
            addMsg("%s destroyed %s", cars[c->lastAttacker].name, c->name);
        } else addMsg("%s wrecked", c->name);
        c->lastAttacker = -1;
    }
}

static void fireWeapon(Car *c, int ci, int w) {
    if (!c->alive || c->cd[w] > 0 || c->ammo[w] <= 0) return;
    c->cd[w] = cdOf(c, w); c->ammo[w]--;
    if (classic && w == W_LASER) c->battery -= 1;
    baseY = c->y;
    Vector2 f = fwdOf(c->h);
    Vector2 r = {-f.y, f.x};
    Vector2 nose = Vector2Add(c->p, Vector2Scale(f, 2.8f));
    float near = Vector2Distance(c->p, cars[0].p);
    float vol = near < 150 ? (1 - near / 150) : 0.0f;
    if (w == W_GUN || w == W_MISSILE) {
        int nb = (w == W_GUN && c->u.gun >= 3) ? 2 : 1;
        for (int b = 0; b < nb; b++) for (int i = 0; i < 256; i++) if (!proj[i].on) {
            Proj *p = &proj[i];
            p->on = true; p->owner = ci; p->y = c->y + 0.9f; p->target = -1;
            float side = 0;
            if (w == W_GUN) {
                if (nb == 2) side = b ? 0.5f : -0.5f;
                else if (c->u.gun == 2) side = (c->ammo[W_GUN] & 1) ? 0.5f : -0.5f;
            }
            p->p = Vector2Add(nose, Vector2Scale(r, side));
            if (w == W_GUN) {
                float sp = rf(-0.025f, 0.025f);
                Vector2 d = fwdOf(c->h + sp);
                p->type = 0; p->life = 1.2f; p->dmg = gunDmg(c);
                p->v = Vector2Add(Vector2Scale(d, 170), c->v);
                if (b == 0) snd(sGun, 0.35f * vol, rf(0.9f, 1.2f) - 0.05f * c->u.gun);
            } else {
                p->type = 1; p->life = 5; p->v = Vector2Add(Vector2Scale(f, 40), c->v);
                p->dmg = misDmg(c);
                float best = 1e9f;
                for (int k = 0; k < MAXCARS; k++) {
                    if (k == ci || !cars[k].alive || (classic && c->ai && k != 0)) continue;   // classic: AI missiles lock only onto you
                    Vector2 rel = Vector2Subtract(cars[k].p, c->p);
                    float d = Vector2Length(rel);
                    if (d < 200 && Vector2DotProduct(Vector2Normalize(rel), f) > 0.8f && d < best) { best = d; p->target = k; }
                }
                snd(sMis, 0.8f * (0.3f + vol), 1);
            }
            break;
        }
    } else if (w == W_LASER) {
        float best = 1e9f; int hit = -1;   // unlimited range
        for (int k = 0; k < MAXCARS; k++) {
            if (k == ci || !cars[k].alive || fabsf(cars[k].y - c->y) > 3.5f) continue;
            Vector2 rel = Vector2Subtract(cars[k].p, c->p);
            float along = Vector2DotProduct(rel, f), lat = fabsf(Vector2DotProduct(rel, r));
            if (along > 0 && along < best && lat < 2.4f) { best = along; hit = k; }
        }
        Vector2 e = Vector2Add(c->p, Vector2Scale(f, hit >= 0 ? best : 1500.0f));   // a miss draws a long beam
        for (int i = 0; i < 32; i++) if (beams[i].life <= 0) {
            beams[i] = (Beam){{nose.x, c->y + 1.0f, nose.y}, {e.x, c->y + 1.0f, e.y}, 0.12f, (Color){255, 40, 40, 255}};
            break;
        }
        if (hit >= 0) {
            hitCar(&cars[hit], lasDmg(c), ci, c->p);
            for (int i = 0; i < 10; i++) part((Vector3){e.x, 1, e.y}, (Vector3){rf(-6, 6), rf(0, 6), rf(-6, 6)}, 0.4f, 0.3f, RED);
        }
        snd(sLas, 0.6f * (0.3f + vol), rf(0.9f, 1.1f));
    } else if (w == W_MINE) {
        for (int i = 0; i < 64; i++) if (!mines[i].on) {
            mines[i] = (Mine){true, Vector2Subtract(c->p, Vector2Scale(f, 3.5f)), ci, 0.8f, 40, c->y};
            break;
        }
        snd(sMine, 0.4f * (0.3f + vol), 1);
    } else if (w == W_CALT) {
        for (int i = 0; i < 64; i++) if (!calts[i].on) {
            calts[i] = (Calt){true, Vector2Subtract(c->p, Vector2Scale(f, 3.5f)), ci, 30, c->y};
            break;
        }
        snd(sMine, 0.4f * (0.3f + vol), 1.6f);
    }
}

// ---------------------------------------------------------------- car physics
static void updateCar(Car *c, int ci, Ctl *k, float dt) {
    if (c->gone) return;   // finished and left the track
    if (c->finished && c->alive) {
        if (c->ai) { c->finT += dt; if (c->finT > 2.0f) { c->alive = false; c->gone = true; return; } }   // cool-down, then leave
        else c->invuln = 100;   // the player's finished car can no longer be hurt
    }
    if (!c->alive) {
        if (classic) return;   // classic: wrecks stay wrecked
        c->respawn -= dt;
        if (c->respawn <= 0) respawnCar(c);
        return;
    }
    baseY = c->y;
    if (classic) {   // laser battery recharges slowly; show the charge as the laser "ammo"
        c->battery = fminf(battCap(c), c->battery + dt / (7.0f - 1.0f * c->u.laser));
        c->ammo[W_LASER] = (int)c->battery;
    }
    {   // a quick swerve or hard stab of the brakes is an evasive manoeuvre (breaks missile locks)
        bool ev = (fabsf(k->steer - c->prevSteerIn) > 0.9f && fabsf(k->steer) > 0.5f) || (k->thr < -0.5f && c->prevThr >= -0.5f);
        if (ev) c->evadeT = 0.6f; else c->evadeT -= dt;
        c->prevSteerIn = k->steer; c->prevThr = k->thr;
    }
    c->invuln -= dt; c->flat -= dt;
    for (int w = 0; w < NW; w++) c->cd[w] -= dt;
    Vector2 f = fwdOf(c->h);
    float vf = Vector2DotProduct(c->v, f);
    float maxv = c->maxv * (c->flat > 0 ? 0.6f : 1);
    // steering
    float target = k->steer;
    c->steer += (target - c->steer) * fminf(1, 9 * dt);
    float rate = c->steerK * Clamp(fabsf(vf) / 10, 0, 1) / (1 + fabsf(vf) / 55);
    if (k->hb) rate *= 1.5f;
    float omega = c->steer * rate;                       // yaw rate the driver is asking for (rad/s)
    // Tyre limit: following a turn needs lateral acceleration = speed * yaw rate. Beyond what the tyres can
    // grip the front end washes out (understeer): the car turns less than asked, scrubs speed and smokes.
    float aMax = c->gripN * 3.5f * (c->flat > 0 ? 0.43f : 1.0f) * (k->hb ? 1.5f : 1.0f);
    float aReq = fabsf(vf * omega);
    if (aReq > aMax) {
        float over = aReq / aMax;
        omega /= over;
        c->v = Vector2Scale(c->v, 1 - Clamp(over - 1, 0, 1.5f) * 0.45f * dt);
        if (over > 1.15f && GetRandomValue(0, 1) == 0)
            part((Vector3){c->p.x - f.x * 1.5f, 0.2f, c->p.y - f.y * 1.5f}, (Vector3){rf(-1, 1), rf(0.5f, 1.5f), rf(-1, 1)}, 0.7f, 0.6f, (Color){190, 190, 190, 150});
    }
    c->h -= omega * dt * (vf >= -1 ? 1 : -1);
    f = fwdOf(c->h);
    Vector2 r = {-f.y, f.x};
    vf = Vector2DotProduct(c->v, f);
    float vl = Vector2DotProduct(c->v, r);
    if (k->thr > 0) { if (vf < maxv) vf += k->thr * c->accel * (1 - c->accFall * fmaxf(vf, 0) / maxv) * dt; }
    else if (k->thr < 0) { if (vf > 1) vf -= 65 * dt; else vf = fmaxf(vf + k->thr * 25 * dt, -16); }
    vf -= vf * 0.28f * dt;
    if (vf > maxv) vf -= (vf - maxv) * 2 * dt;
    float grip = k->hb ? 1.3f : (c->flat > 0 ? c->gripN * 0.43f : c->gripN);
    vl *= expf(-grip * dt);
    if (k->hb) vf *= expf(-0.7f * dt);
    c->v = Vector2Add(Vector2Scale(f, vf), Vector2Scale(r, vl));
    c->p = Vector2Add(c->p, Vector2Scale(c->v, dt));
    // progress / lap
    int ni = nearestIdx(c->p, c->idx);
    if (c->idx > NSEG * 0.8f && ni < NSEG * 0.2f) {
        c->lap++;
        if (c->lap > LAPS && !c->finished) {
            c->finished = true; c->finishTime = raceT;
            addMsg("%s finished!", c->name);
        } else if (!c->ai) addMsg("LAP %d / %d", c->lap, LAPS);
    } else if (c->idx < NSEG * 0.2f && ni > NSEG * 0.8f) c->lap--;
    c->idx = ni;
    c->prog = c->lap * NSEG + ni;
    c->y = yAt(c->p, ni);
    // walls
    TP *t = &trk[ni];
    float off = Vector2DotProduct(Vector2Subtract(c->p, t->p), t->r);
    if (fabsf(off) > WALL) {
        float s = off > 0 ? 1 : -1;
        c->p = Vector2Subtract(c->p, Vector2Scale(t->r, off - s * WALL));
        float vn = Vector2DotProduct(c->v, t->r) * s;
        if (vn > 0) {
            c->v = Vector2Subtract(c->v, Vector2Scale(t->r, s * vn * 1.4f));
            if (getenv("RACE_DEBUG") && vn > 6) fprintf(stderr, "WALLHIT car=%d speed=%.0f km/h vn=%.1f idx=%d\n", ci, Vector2Length(c->v) * 3.6f, vn, c->idx);
            if (vn > 4) dent(c, Vector2Add(c->p, Vector2Scale(t->r, s)), vn * 0.9f);
            if (vn > 6) { damageCar(c, vn * 0.35f, -1); if (!c->ai) shake = fmaxf(shake, 0.2f); }
            for (int i = 0; i < 4; i++)
                part((Vector3){c->p.x, 0.6f, c->p.y}, (Vector3){rf(-4, 4), rf(1, 5), rf(-4, 4)}, 0.3f, 0.2f, YELLOW);
        }
        c->v = Vector2Scale(c->v, 1 - 0.8f * dt);
    }
    // weapons
    if (!c->finished) for (int w = 0; w < NW; w++) if (k->fire[w]) fireWeapon(c, ci, w);   // no shooting after the flag
    // pickups
    for (int i = 0; i < npicks; i++) {
        Pickup *pk = &picks[i];
        if (pk->respawn > 0) continue;
        if (Vector2Distance(pk->p, c->p) < 3.6f && fabsf(pk->y - c->y) < 3) {
            pk->respawn = classic ? 1e9f : 14;
            if (pk->type < 0) c->hp = fminf(c->maxhp, c->hp + 40);
            else {
                static const int amt[NW] = {80, 2, 4, 2, 2};
                c->ammo[pk->type] = (int)fminf(ammoCap(c, pk->type), c->ammo[pk->type] + amt[pk->type]);
            }
            if (!c->ai) { snd(sPick, 0.6f, 1); addMsg(pk->type < 0 ? "Repair +40" : "+%s", pk->type < 0 ? "" : wname[pk->type]); }
        }
    }
    // mines & caltrops
    for (int i = 0; i < 64; i++) {
        Mine *m = &mines[i];
        if (m->on && m->arm <= 0 && Vector2Distance(m->p, c->p) < 2.8f && fabsf(m->y - c->y) < 3) {
            m->on = false;
            explode(m->p, 9, 55, m->owner, true);
        }
        Calt *q = &calts[i];
        if (q->on && Vector2Distance(q->p, c->p) < 3.2f && c->flat < 3.0f && fabsf(q->y - c->y) < 3) {
            c->flat = 5; damageCar(c, 6, q->owner);
            for (int j = 0; j < 12; j++) part((Vector3){c->p.x, 0.3f, c->p.y}, (Vector3){rf(-3, 3), rf(1, 4), rf(-3, 3)}, 0.5f, 0.2f, LIGHTGRAY);
            if (!c->ai) addMsg("TIRES SHREDDED!");
        }
    }
    // exhaust / flat sparks
    if (GetRandomValue(0, 3) == 0)
        part((Vector3){c->p.x - f.x * 2.2f, 0.5f, c->p.y - f.y * 2.2f}, (Vector3){rf(-0.5f, 0.5f), rf(0.5f, 1.5f), rf(-0.5f, 0.5f)}, 0.6f, 0.4f, (Color){120, 120, 120, 140});
    if (c->flat > 0 && GetRandomValue(0, 1) == 0)
        part((Vector3){c->p.x, 0.2f, c->p.y}, (Vector3){rf(-2, 2), rf(1, 3), rf(-2, 2)}, 0.25f, 0.15f, ORANGE);
    if (c->hp < c->maxhp * 0.35f && GetRandomValue(0, 2) == 0)
        part((Vector3){c->p.x, 1.4f, c->p.y}, (Vector3){rf(-1, 1), rf(2, 4), rf(-1, 1)}, 0.8f, 0.7f, (Color){30, 30, 30, 200});
}

// ---------------------------------------------------------------- AI
// highest speed from which the car can still make every upcoming corner (grip-limited), braking at a sane rate
static float cornerSpeed(Car *c) {
    float seg = trkLen / NSEG, best = c->maxv;
    float aMax = c->gripN * 3.5f * (c->flat > 0 ? 0.43f : 1.0f) * 0.8f;   // keep 20% in reserve
    for (int j = 1; j <= 75; j++) {
        float kj = fmaxf(trkK[(c->idx + j) % NSEG], 1e-4f);
        float vj = sqrtf(aMax / kj), d = j * seg;
        float allowed = sqrtf(vj * vj + 2 * 32.0f * d);
        if (allowed < best) best = allowed;
    }
    return best;
}
static void aiControl(Car *c, int ci, Ctl *k, float dt) {
    memset(k, 0, sizeof(*k));
    if (!c->alive) return;
    float speed = Vector2Length(c->v);
    int look = 6 + (int)(speed * 0.18f);
    TP *t = &trk[(c->idx + look) % NSEG];
    float laneT = c->lane;
    float wreckCap = 1e9f;
    if (classic) for (int w = 0; w < MAXCARS; w++) {   // steer around wrecks on the track
        Car *wk = &cars[w];
        if (wk->alive || wk->gone || fabsf(wk->y - c->y) > 3) continue;
        int dI = (wk->idx - c->idx + NSEG) % NSEG;
        if (dI < 3 || dI > look + 28) continue;
        float offw = Vector2DotProduct(Vector2Subtract(wk->p, trk[wk->idx].p), trk[wk->idx].r);
        float cur = Vector2DotProduct(Vector2Subtract(c->p, trk[c->idx].p), trk[c->idx].r);
        float a = offw - 5.5f, b = offw + 5.5f;
        bool aok = fabsf(a) <= 11, bok = fabsf(b) <= 11;
        if (aok && bok) laneT = fabsf(a - cur) < fabsf(b - cur) ? a : b;
        else if (aok) laneT = a; else if (bok) laneT = b; else laneT = offw > 0 ? -9.0f : 9.0f;
        wreckCap = 26.0f;
    }
    Vector2 tgt = Vector2Add(t->p, Vector2Scale(t->r, laneT));
    Vector2 f = fwdOf(c->h), d = Vector2Subtract(tgt, c->p);
    float ang = atan2f(f.x * d.y - f.y * d.x, Vector2DotProduct(f, d));
    k->steer = Clamp(ang * 2.2f, -1, 1);
    float tv = fminf(cornerSpeed(c), wreckCap);
    if (speed < tv - 1) k->thr = 1;
    else if (speed > tv + 2) k->thr = -1;
    if (fabsf(ang) > 1.0f && speed > 15) k->hb = true;
    // stuck
    if (speed < 3 && state == ST_RACE) c->stuck += dt; else c->stuck = 0;
    if (c->stuck > 2.5f) { respawnCar(c); }
    // combat
    if (c->finished) { k->thr = 0; return; }   // coast over the line, no more fighting
    c->aiT -= dt;
    if (c->aiT > 0 || state != ST_RACE || raceT < 8) return;
    c->aiT = 0.05f;
    if (classic) {   // guns fire in short bursts with long rests
        if (c->aiBurst > 0) { c->aiBurst -= 0.05f; if (c->aiBurst <= 0) c->aiRest = 2.5f + rf(0, 2.5f); }   // burst over -> rest
        else c->aiRest -= 0.05f;
    }
    Vector2 r = {-f.y, f.x};
    for (int o = 0; o < MAXCARS; o++) {
        if (o == ci || !cars[o].alive || fabsf(cars[o].y - c->y) > 3 || (classic && o != 0)) continue;   // classic: AI cars fight only you
        Vector2 rel = Vector2Subtract(cars[o].p, c->p);
        float dist = Vector2Length(rel), along = Vector2DotProduct(rel, f), lat = fabsf(Vector2DotProduct(rel, r));
        if (along > 0) {
            float skill = Clamp((c->maxv - 40) / 15, 0, 1);
            if (classic) {
                if (dist < 60 && lat < 3.0f && c->aiBurst <= 0 && c->aiRest <= 0) c->aiBurst = 0.7f;   // start a burst
                if (c->aiBurst > 0 && dist < 70 && lat < 3.5f) k->fire[W_GUN] = true;
                if (dist < 100 && lat < 2.2f && GetRandomValue(0, 1000) < 15) k->fire[W_LASER] = true;
                if (dist < 140 && dist > 20 && lat < dist * 0.3f && GetRandomValue(0, 1000) < 10) k->fire[W_MISSILE] = true;
            } else {
                if (dist < 70 && lat < 3.5f && GetRandomValue(0, 100) < 40 + skill * 30) k->fire[W_GUN] = true;
                if (dist < 100 && lat < 2.2f && GetRandomValue(0, 100) < 12) k->fire[W_LASER] = true;
                if (dist < 140 && dist > 20 && lat < dist * 0.3f && GetRandomValue(0, 100) < 8) k->fire[W_MISSILE] = true;
            }
        } else if (dist < 40 && lat < 9 && GetRandomValue(0, classic ? 1000 : 100) < (classic ? 12 : 7)) {
            k->fire[GetRandomValue(0, 1) ? W_MINE : W_CALT] = true;
        }
    }
}

// ---------------------------------------------------------------- world update
static void updateWorld(float dt) {
    warnMissile = false; warnMissileDist = 1e9f;
    // car-car collisions
    for (int i = 0; i < MAXCARS; i++) for (int j = i + 1; j < MAXCARS; j++) {
        Car *a = &cars[i], *b = &cars[j];
        if (!a->alive || !b->alive || fabsf(a->y - b->y) > 3) continue;
        baseY = a->y;
        Vector2 dlt = Vector2Subtract(b->p, a->p);
        float d = Vector2Length(dlt);
        if (d < CAR_R * 2 && d > 0.001f) {
            Vector2 n = Vector2Scale(dlt, 1 / d);
            float pen = CAR_R * 2 - d;
            a->p = Vector2Subtract(a->p, Vector2Scale(n, pen / 2));
            b->p = Vector2Add(b->p, Vector2Scale(n, pen / 2));
            float rv = Vector2DotProduct(Vector2Subtract(b->v, a->v), n);
            if (rv < 0) {
                Vector2 imp = Vector2Scale(n, rv * 0.9f);
                float ma = a->mass > 0 ? a->mass : 1, mb = b->mass > 0 ? b->mass : 1;   // heavier car gets shoved less
                a->v = Vector2Add(a->v, Vector2Scale(imp, 2 * mb / (ma + mb))); b->v = Vector2Subtract(b->v, Vector2Scale(imp, 2 * ma / (ma + mb)));
                float im = -rv;
                if (im > 3) {
                    dent(a, b->p, im * 0.8f); dent(b, a->p, im * 0.8f);
                    Car *pr[2] = {a, b}; int id[2] = {i, j};
                    for (int sd = 0; sd < 2; sd++) {
                        Car *x = pr[sd], *y = pr[1 - sd];
                        Vector2 nn = sd ? Vector2Scale(n, -1) : n;
                        float fd = Vector2DotProduct(nn, fwdOf(x->h));
                        int lvl = fd > 0.5f ? x->u.fspike : (fd < -0.5f ? x->u.rspike : 0);
                        if (lvl > 0 && y->alive && y->invuln <= 0) {
                            hitCar(y, lvl * (3 + im * 0.3f), id[sd], x->p);
                            dent(y, x->p, lvl * 12.0f);
                            if (!x->ai) addMsg("SPIKED %s!", y->name);
                        }
                    }
                }
                if (im > 8) { damageCar(a, im * 0.3f * sqrtf(mb / ma), j); damageCar(b, im * 0.3f * sqrtf(ma / mb), i);
                    for (int s = 0; s < 6; s++) part((Vector3){a->p.x + n.x * CAR_R, 0.8f, a->p.y + n.y * CAR_R}, (Vector3){rf(-5, 5), rf(1, 6), rf(-5, 5)}, 0.3f, 0.2f, YELLOW); }
            }
        }
    }
    // wrecks (classic): immovable obstacles that smoulder for the whole race
    if (classic) for (int w = 0; w < MAXCARS; w++) {
        Car *wk = &cars[w];
        if (wk->alive || wk->gone) continue;
        wk->respawn += dt;   // age of the wreck
        baseY = wk->y;
        if (Vector2DistanceSqr(wk->p, cars[0].p) < 200.0f * 200.0f && GetRandomValue(0, 5) == 0)
            part((Vector3){wk->p.x + rf(-1, 1), 0.9f, wk->p.y + rf(-1, 1)}, (Vector3){rf(-0.5f, 0.5f), rf(2, 4), rf(-0.5f, 0.5f)}, rf(1.0f, 2.0f), rf(0.8f, 1.6f), (Color){45, 45, 45, 190});
        if (wk->respawn < 25 && GetRandomValue(0, 12) == 0)
            part((Vector3){wk->p.x + rf(-1, 1), 0.8f, wk->p.y + rf(-1, 1)}, (Vector3){rf(-0.5f, 0.5f), rf(1, 3), rf(-0.5f, 0.5f)}, 0.4f, 0.4f, ORANGE);
        for (int j = 0; j < MAXCARS; j++) {
            Car *c = &cars[j];
            if (!c->alive || fabsf(c->y - wk->y) > 3) continue;
            Vector2 dlt = Vector2Subtract(c->p, wk->p);
            float d = Vector2Length(dlt);
            if (d >= CAR_R * 2 || d < 0.001f) continue;
            Vector2 n = Vector2Scale(dlt, 1 / d);
            c->p = Vector2Add(c->p, Vector2Scale(n, CAR_R * 2 - d));
            float vn = Vector2DotProduct(c->v, n);
            if (vn < 0) {
                c->v = Vector2Scale(Vector2Subtract(c->v, Vector2Scale(n, vn * 1.4f)), 0.85f);
                baseY = c->y;
                if (getenv("RACE_DEBUG")) fprintf(stderr, "WRECKHIT car=%d speed=%.0f km/h\n", j, -vn * 3.6f);
                if (-vn > 3) dent(c, wk->p, -vn * 0.9f);
                if (-vn > 6) { hitCar(c, -vn * 0.35f, -1, wk->p); if (j == 0) shake = fmaxf(shake, 0.3f); }
                for (int sp = 0; sp < 4; sp++) part((Vector3){c->p.x - n.x * 1.5f, 0.6f, c->p.y - n.y * 1.5f}, (Vector3){rf(-4, 4), rf(1, 5), rf(-4, 4)}, 0.3f, 0.2f, YELLOW);
            }
        }
    }
    // projectiles
    for (int i = 0; i < 256; i++) {
        Proj *p = &proj[i];
        if (!p->on) continue;
        p->life -= dt;
        baseY = p->y - 0.9f;
        if (p->type == 1) p->lockT += dt;
        if (p->type == 1 && p->target >= 0 && cars[p->target].alive) {
            Car *tc = &cars[p->target];
            float dd = Vector2Distance(p->p, tc->p);
            if (p->target == 0) { warnMissile = true; warnMissileDist = fminf(warnMissileDist, dd); }
            if (tc->ai && !p->aiRolled && p->lockT > 0.5f) {   // AI drivers try to dodge too
                p->aiRolled = true;
                if (GetRandomValue(0, 99) < (classic ? 35 + 3 * raceIdx : 30)) {
                    tc->evadeT = 0.6f;
                    Vector2 rr = {-fwdOf(tc->h).y, fwdOf(tc->h).x};
                    tc->v = Vector2Add(tc->v, Vector2Scale(rr, GetRandomValue(0, 1) ? 7.0f : -7.0f));
                }
            }
            if (tc->evadeT > 0 && p->lockT > 0.25f && dd > 9) {   // evaded: the missile loses its lock
                if (p->target == 0) addMsg("MISSILE EVADED!");
                else if (p->owner == 0) addMsg("%s dodged your missile", tc->name);
                p->target = -1;
            }
        }
        if (p->type == 1) {  // missile homing
            if (p->target >= 0 && cars[p->target].alive) {
                Vector2 want = Vector2Normalize(Vector2Subtract(cars[p->target].p, p->p));
                float sp = Vector2Length(p->v), cur = atan2f(p->v.x, p->v.y), des = atan2f(want.x, want.y);
                cur += Clamp(angDiff(cur, des), -2.8f * dt, 2.8f * dt);
                sp = fminf(sp + 60 * dt, 105);
                p->v = (Vector2){sinf(cur) * sp, cosf(cur) * sp};
                p->y += (cars[p->target].y + 0.9f - p->y) * fminf(1, 6 * dt);
            } else {
                float sp = fminf(Vector2Length(p->v) + 60 * dt, 105);
                p->v = Vector2Scale(Vector2Normalize(p->v), sp);
            }
            part((Vector3){p->p.x, p->y, p->p.y}, (Vector3){rf(-1, 1), rf(0, 1), rf(-1, 1)}, 0.5f, 0.5f, (Color){200, 200, 200, 180});
        }
        Vector2 np = Vector2Add(p->p, Vector2Scale(p->v, dt));
        bool kill = p->life <= 0;
        for (int k = 0; k < MAXCARS && !kill; k++) {
            Car *c = &cars[k];
            if (k == p->owner || !c->alive || fabsf((p->y - 0.9f) - c->y) > 3.5f) continue;
            // segment-vs-circle so fast bullets can't tunnel
            Vector2 seg = Vector2Subtract(np, p->p);
            float L2 = Vector2DotProduct(seg, seg), tt = L2 > 0 ? Clamp(Vector2DotProduct(Vector2Subtract(c->p, p->p), seg) / L2, 0, 1) : 0;
            Vector2 cl = Vector2Add(p->p, Vector2Scale(seg, tt));
            if (Vector2Distance(cl, c->p) < (p->type ? 2.6f : CAR_R)) {
                if (p->type == 0) {
                    hitCar(c, p->dmg, p->owner, p->p);
                    for (int s = 0; s < 3; s++) part((Vector3){cl.x, 1, cl.y}, (Vector3){rf(-4, 4), rf(0, 4), rf(-4, 4)}, 0.25f, 0.15f, YELLOW);
                    p->on = false; kill = false;
                } else { p->p = cl; kill = true; }
                break;
            }
        }
        if (!p->on) continue;
        if (!kill) {
            TP *t = &trk[nearestIdx(np, nearestIdx(p->p, (int)(trkLen ? cars[p->owner].idx : 0)))];
            float off = Vector2DotProduct(Vector2Subtract(np, t->p), t->r);
            if (fabsf(off) > HALF) kill = true;
        }
        if (kill) {
            p->on = false;
            if (p->type == 1) explode(p->p, 11 + (p->dmg - 45) / 10, p->dmg, p->owner, true);
        } else p->p = np;
    }
    for (int i = 0; i < 64; i++) {
        if (mines[i].on) { mines[i].arm -= dt; mines[i].life -= dt; if (mines[i].life <= 0) mines[i].on = false; }
        if (calts[i].on) { calts[i].life -= dt; if (calts[i].life <= 0) calts[i].on = false; }
    }
    for (int i = 0; i < npicks; i++) if (picks[i].respawn > 0) picks[i].respawn -= dt;
    for (int i = 0; i < 32; i++) beams[i].life -= dt;
    for (int i = 0; i < 4000; i++) {
        Part *q = &parts[i];
        if (q->life <= 0) continue;
        q->life -= dt;
        q->p = Vector3Add(q->p, Vector3Scale(q->v, dt));
        q->v.y -= 12 * dt;
        if (q->p.y < 0.1f) { q->p.y = 0.1f; q->v.y *= -0.3f; q->v.x *= 0.7f; q->v.z *= 0.7f; }
    }
    for (int i = 0; i < 5; i++) msgT[i] -= dt;
    // beep warnings for mines / caltrops ahead of the player
    warnMine = warnCalt = false; warnMineDist = warnCaltDist = 1e9f;
    Car *pl = &cars[0];
    if (pl->alive) {
        Vector2 f = fwdOf(pl->h), r = {-f.y, f.x};
        for (int i = 0; i < 64; i++) {
            if (mines[i].on && fabsf(mines[i].y - pl->y) < 3) {
                Vector2 rel = Vector2Subtract(mines[i].p, pl->p);
                float along = Vector2DotProduct(rel, f), lat = fabsf(Vector2DotProduct(rel, r));
                if (along > 2 && along < 55 && lat < 9) { warnMine = true; warnMineDist = fminf(warnMineDist, along); }
            }
            if (calts[i].on && fabsf(calts[i].y - pl->y) < 3) {
                Vector2 rel = Vector2Subtract(calts[i].p, pl->p);
                float along = Vector2DotProduct(rel, f), lat = fabsf(Vector2DotProduct(rel, r));
                if (along > 2 && along < 55 && lat < 9) { warnCalt = true; warnCaltDist = fminf(warnCaltDist, along); }
            }
        }
    }
}

// ---------------------------------------------------------------- rendering
static void quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col) {
    DrawTriangle3D(a, b, c, col);
    DrawTriangle3D(a, c, d, col);
}
static Vector3 V3(Vector2 p, float y) { return (Vector3){p.x, y, p.y}; }

static Vector3 P3(Vector2 p, int i, float dy) { return (Vector3){p.x, trkY[i] + dy, p.y}; }
static void drawTrack(void) {
    Color deck = {55, 55, 62, 255};
    for (int i = 0; i < NSEG; i++) {
        int n = (i + 1) % NSEG;
        TP *a = &trk[i], *b = &trk[n];
        Vector2 aL = Vector2Subtract(a->p, Vector2Scale(a->r, HALF)), aR = Vector2Add(a->p, Vector2Scale(a->r, HALF));
        Vector2 bL = Vector2Subtract(b->p, Vector2Scale(b->r, HALF)), bR = Vector2Add(b->p, Vector2Scale(b->r, HALF));
        Color rc = (i / 5) % 2 ? TDEF->road1 : TDEF->road2;
        quad(P3(aL, i, 0.02f), P3(bL, n, 0.02f), P3(bR, n, 0.02f), P3(aR, i, 0.02f), rc);
        if (i % 8 < 4) {
            Vector2 al = Vector2Subtract(a->p, Vector2Scale(a->r, 0.25f)), ar = Vector2Add(a->p, Vector2Scale(a->r, 0.25f));
            Vector2 bl = Vector2Subtract(b->p, Vector2Scale(b->r, 0.25f)), br = Vector2Add(b->p, Vector2Scale(b->r, 0.25f));
            quad(P3(al, i, 0.09f), P3(bl, n, 0.09f), P3(br, n, 0.09f), P3(ar, i, 0.09f), (Color){220, 200, 60, 255});
        }
        Color wc = (i / 4) % 2 ? TDEF->wallA : TDEF->wallB;
        Vector2 aLo = Vector2Subtract(a->p, Vector2Scale(a->r, HALF + 1.2f)), bLo = Vector2Subtract(b->p, Vector2Scale(b->r, HALF + 1.2f));
        Vector2 aRo = Vector2Add(a->p, Vector2Scale(a->r, HALF + 1.2f)), bRo = Vector2Add(b->p, Vector2Scale(b->r, HALF + 1.2f));
        quad(P3(aL, i, 0), P3(bL, n, 0), P3(bL, n, 1.3f), P3(aL, i, 1.3f), wc);
        quad(P3(aR, i, 0), P3(bR, n, 0), P3(bR, n, 1.3f), P3(aR, i, 1.3f), wc);
        quad(P3(aL, i, 1.3f), P3(bL, n, 1.3f), P3(bLo, n, 1.3f), P3(aLo, i, 1.3f), TDEF->curb);
        quad(P3(aR, i, 1.3f), P3(bR, n, 1.3f), P3(bRo, n, 1.3f), P3(aRo, i, 1.3f), TDEF->curb);
        if (trkY[i] > 0.3f || trkY[n] > 0.3f) {   // flyover: deck thickness, underside and support pillars
            quad(P3(aLo, i, -1.2f), P3(bLo, n, -1.2f), P3(bLo, n, 1.3f), P3(aLo, i, 1.3f), deck);
            quad(P3(aRo, i, -1.2f), P3(bRo, n, -1.2f), P3(bRo, n, 1.3f), P3(aRo, i, 1.3f), deck);
            quad(P3(aLo, i, -1.2f), P3(bLo, n, -1.2f), P3(bRo, n, -1.2f), P3(aRo, i, -1.2f), (Color){35, 35, 40, 255});
            if (i % 6 == 0 && trkY[i] > 2 && Vector2Distance(a->p, crossP) > 55) {
                for (int sd = -1; sd <= 1; sd += 2) {
                    Vector2 pp = Vector2Add(a->p, Vector2Scale(a->r, sd * (HALF - 2.5f)));
                    DrawCylinderEx((Vector3){pp.x, 0, pp.y}, (Vector3){pp.x, trkY[i] - 1.2f, pp.y}, 1.1f, 1.1f, 8, (Color){95, 95, 100, 255});
                }
            }
        }
    }
    // start / finish line
    for (int k = 0; k < 10; k++) for (int j = 0; j < 2; j++) {
        Vector2 c0 = Vector2Add(trk[0].p, Vector2Add(Vector2Scale(trk[0].r, -HALF + 1.5f * k + 0.75f), Vector2Scale(trk[0].t, j * 1.5f)));
        DrawCube((Vector3){c0.x, trkY[0] + 0.1f, c0.y}, 1.5f, 0.06f, 1.5f, (k + j) % 2 ? WHITE : BLACK);
    }
}

// ---- warning signs: yellow diamonds before sharp corners, chevron boards around them
static void strokeQuad(float x1, float y1, float x2, float y2, float th, float z) {
    float dx = x2 - x1, dy = y2 - y1, l = sqrtf(dx * dx + dy * dy), nx = -dy / l * th / 2, ny = dx / l * th / 2;
    rlVertex3f(x1 + nx, y1 + ny, z); rlVertex3f(x1 - nx, y1 - ny, z); rlVertex3f(x2 - nx, y2 - ny, z);
    rlVertex3f(x1 + nx, y1 + ny, z); rlVertex3f(x2 - nx, y2 - ny, z); rlVertex3f(x2 + nx, y2 + ny, z);
}
static void chevron(float cx, float cy, float dir, float sc, float z) {  // '>' pointing toward +x*dir
    float tx = 0.3f * dir * sc, bx = -0.35f * dir * sc;
    strokeQuad(cx + bx, cy + 0.6f * sc, cx + tx, cy, 0.24f * sc, z);
    strokeQuad(cx + tx, cy, cx + bx, cy - 0.6f * sc, 0.24f * sc, z);
}
static void drawSign(Sign *s) {
    Color black = {15, 15, 15, 255};
    rlPushMatrix();
    rlTranslatef(s->p.x, s->y, s->p.y);
    rlRotatef(s->h * RAD2DEG, 0, 1, 0);
    float cy = s->kind == 2 ? 2.3f : 3.5f;
    DrawCylinderEx((Vector3){0, 0, 0}, (Vector3){0, cy, 0}, 0.1f, 0.1f, 6, GRAY);
    if (s->kind == 2) {
        DrawCube((Vector3){0, cy, 0}, 3.0f, 2.0f, 0.08f, black);
        DrawCube((Vector3){0, cy, 0.03f}, 2.8f, 1.8f, 0.08f, (Color){245, 200, 0, 255});
        rlBegin(RL_TRIANGLES); rlColor4ub(15, 15, 15, 255); chevron(0, cy, (float)s->dir, 1.1f, 0.1f); rlEnd();
    } else {
        rlPushMatrix();
        rlTranslatef(0, cy, 0); rlRotatef(45, 0, 0, 1);
        DrawCube((Vector3){0, 0, 0}, 2.3f, 2.3f, 0.08f, black);
        DrawCube((Vector3){0, 0, 0.03f}, 2.05f, 2.05f, 0.08f, s->kind == 1 ? (Color){255, 120, 0, 255} : (Color){245, 200, 0, 255});
        rlPopMatrix();
        rlBegin(RL_TRIANGLES); rlColor4ub(15, 15, 15, 255);
        if (s->kind == 1) { chevron(-0.4f, cy, (float)s->dir, 0.9f, 0.1f); chevron(0.4f, cy, (float)s->dir, 0.9f, 0.1f); }
        else chevron(0, cy, (float)s->dir, 1.2f, 0.1f);
        rlEnd();
    }
    rlPopMatrix();
}
static void drawSigns(Vector2 viewer) {
    for (int i = 0; i < nsigns; i++)
        if (Vector2DistanceSqr(signs[i].p, viewer) < 450.0f * 450.0f) drawSign(&signs[i]);
}

// lattice helpers: body half extents
#define BX 1.0f
#define BY 0.35f
#define BZ 2.2f
static float hash3(Vector3 p) {
    float v = sinf(roundf(p.x * 50) * 127.1f + roundf(p.y * 50) * 311.7f + roundf(p.z * 50) * 74.7f) * 43758.5453f;
    return v - floorf(v);
}
static Vector3 bodyBase(int face, int i, int j) {
    float u, v;
    switch (face) {
    case 0: u = -BX + 2 * BX * i / 8; v = -BZ + 2 * BZ * j / 16; return (Vector3){u, BY, v};
    case 1: u = -BZ + 2 * BZ * i / 16; v = -BY + 2 * BY * j / 3; return (Vector3){BX, v, u};
    case 2: u = -BZ + 2 * BZ * i / 16; v = -BY + 2 * BY * j / 3; return (Vector3){-BX, v, u};
    case 3: u = -BX + 2 * BX * i / 8; v = -BY + 2 * BY * j / 3; return (Vector3){u, v, BZ};
    default: u = -BX + 2 * BX * i / 8; v = -BY + 2 * BY * j / 3; return (Vector3){u, v, -BZ};
    }
}
static const int faceNU[5] = {8, 16, 16, 8, 8}, faceNV[5] = {16, 3, 3, 3, 3}, faceOff[5] = {0, 153, 221, 289, 325};
static void rebuildMesh(Car *c) {
    for (int f = 0; f < 5; f++)
        for (int i = 0; i <= faceNU[f]; i++) for (int j = 0; j <= faceNV[f]; j++) {
            Vector3 p = bodyBase(f, i, j), q = p;
            Vector3 D = {0, 0, 0}; float dk = 0;
            for (int n = 0; n < c->ndent; n++) {
                Dent *d = &c->dents[n];
                float dx = p.x - d->x, dy = (p.y + 0.7f) - d->y, dz = p.z - d->z;
                float dist = sqrtf(dx * dx + dy * dy + dz * dz);
                if (dist >= d->r) continue;
                float w = 1 - dist / d->r; w *= w;
                float jag = 0.55f + 0.9f * hash3(p);   // crumple: uneven push per vertex
                D.x += d->dx * d->depth * w * jag; D.z += d->dz * d->depth * w * jag;
                D.y += (hash3((Vector3){p.z, p.x, p.y}) - 0.4f) * 0.5f * d->depth * w;
                dk += w * d->depth;
            }
            float m = Vector3Length(D);
            if (m > 0.8f) D = Vector3Scale(D, 0.8f / m);
            q = Vector3Add(p, D);
            int vi = faceOff[f] + i * (faceNV[f] + 1) + j;
            c->bv[vi] = (Vector3){q.x, q.y + 0.7f, q.z};
            c->bd[vi] = dk;
        }
    c->meshDirty = false;
}
static void tri(Vector3 a, Vector3 b, Vector3 c3, Color col) {
    rlColor4ub(col.r, col.g, col.b, col.a);
    rlVertex3f(a.x, a.y, a.z); rlVertex3f(b.x, b.y, b.z); rlVertex3f(c3.x, c3.y, c3.z);
}
static void drawCarModel(Car *c, bool tiltDamage) {
    if (c->meshDirty) rebuildMesh(c);
    const CarType *ct = &ctypes[c->type];
    rlPushMatrix();
    rlScalef(ct->sx, ct->sy, ct->sz);
    float hpr = Clamp(c->hp / c->maxhp, 0, 1);
    Color dark = (Color){25, 25, 28, 255};
    static const float shade[5] = {1.0f, 0.78f, 0.78f, 0.68f, 0.6f};
    rlBegin(RL_TRIANGLES);
    for (int f = 0; f < 5; f++) {
        for (int i = 0; i < faceNU[f]; i++) for (int j = 0; j < faceNV[f]; j++) {
            int W = faceNV[f] + 1, o = faceOff[f];
            int i00 = o + i * W + j, i10 = o + (i + 1) * W + j, i01 = i00 + 1, i11 = i10 + 1;
            float dk = (c->bd[i00] + c->bd[i10] + c->bd[i01] + c->bd[i11]) * 0.25f;
            float k = shade[f] * (1 - fminf(0.55f, dk * 0.9f)) * (0.65f + 0.35f * hpr);
            Color col = {(unsigned char)(c->col.r * k), (unsigned char)(c->col.g * k), (unsigned char)(c->col.b * k), 255};
            // scorched/scraped tint where dented
            if (dk > 0.08f) { col.r = (unsigned char)(col.r * 0.85f + 12); col.g = (unsigned char)(col.g * 0.8f); col.b = (unsigned char)(col.b * 0.8f); }
            tri(c->bv[i00], c->bv[i10], c->bv[i11], col);
            tri(c->bv[i00], c->bv[i11], c->bv[i01], col);
        }
    }
    rlEnd();
    DrawCube((Vector3){0, 0.36f, 0}, 1.9f, 0.1f, 4.2f, dark);
    float ch = 0.55f * (1 - 0.3f * (1 - hpr));
    DrawCube((Vector3){0, 1.05f + ch / 2, -0.3f}, 1.6f, ch, 2.0f, (Color){30, 40, 55, 255});
    DrawCube((Vector3){0, 1.6f, -2.0f}, 2.2f, 0.12f, 0.5f, dark);  // spoiler
    DrawCube((Vector3){-0.9f, 1.4f, -2.0f}, 0.1f, 0.4f, 0.1f, dark);
    DrawCube((Vector3){0.9f, 1.4f, -2.0f}, 0.1f, 0.4f, 0.1f, dark);
    // guns by level
    DrawCube((Vector3){0, 1.15f, 1.0f}, 0.25f, 0.2f, 2.2f, dark);
    if (c->u.gun >= 2) { DrawCube((Vector3){-0.5f, 1.1f, 1.2f}, 0.2f, 0.18f, 2.0f, dark); DrawCube((Vector3){0.5f, 1.1f, 1.2f}, 0.2f, 0.18f, 2.0f, dark); }
    if (c->u.gun >= 4) DrawCube((Vector3){0, 1.2f, 2.3f}, 0.4f, 0.4f, 0.8f, (Color){80, 80, 90, 255});
    if (c->u.missile > 0) { DrawCube((Vector3){-0.95f, 1.15f, -0.6f}, 0.3f, 0.3f, 1.2f, (Color){200, 200, 200, 255}); DrawCube((Vector3){0.95f, 1.15f, -0.6f}, 0.3f, 0.3f, 1.2f, (Color){200, 200, 200, 255}); }
    if (c->u.laser > 0) DrawCube((Vector3){0, 1.45f, 0.3f}, 0.3f, 0.25f, 0.5f, (Color){255, 60, 60, 255});
    // armor plating
    for (int a = 0; a < c->u.armor; a++) {
        float y = 0.45f + 0.12f * a;
        DrawCube((Vector3){-1.08f, y + 0.12f, 0}, 0.1f, 0.1f, 3.4f - 0.2f * a, (Color){90, 92, 100, 255});
        DrawCube((Vector3){1.08f, y + 0.12f, 0}, 0.1f, 0.1f, 3.4f - 0.2f * a, (Color){90, 92, 100, 255});
    }
    for (int sx = -1; sx <= 1; sx += 2) for (int sz = -1; sz <= 1; sz += 2)
        DrawCube((Vector3){sx * 1.15f, 0.45f, sz * 1.4f}, 0.35f, 0.9f, 0.9f, dark);
    DrawCube((Vector3){-0.6f, 0.75f, BZ + 0.02f}, 0.4f, 0.15f, 0.05f, YELLOW);
    DrawCube((Vector3){0.6f, 0.75f, BZ + 0.02f}, 0.4f, 0.15f, 0.05f, YELLOW);
    // spikes
    for (int e = 0; e < 2; e++) {
        int lvl = e ? c->u.rspike : c->u.fspike;
        if (lvl <= 0) continue;
        float z = e ? -BZ - 0.1f : BZ + 0.15f, dir = e ? -1.0f : 1.0f;
        float len = 0.5f + 0.35f * lvl;
        int n = 2 + lvl * 2;
        DrawCube((Vector3){0, 0.65f, z - dir * 0.1f}, 2.1f, 0.15f, 0.2f, (Color){70, 70, 75, 255});
        for (int s = 0; s < n; s++) {
            float x = -0.9f + 1.8f * s / (n - 1);
            DrawCylinderEx((Vector3){x, 0.65f, z}, (Vector3){x, 0.65f, z + dir * len}, 0.1f, 0.0f, 5, (Color){210, 210, 220, 255});
        }
        if (lvl >= 3) for (int s = 0; s < 3; s++) {
            float x = -0.6f + 0.6f * s;
            DrawCylinderEx((Vector3){x, 0.95f, z}, (Vector3){x, 0.95f, z + dir * len * 0.8f}, 0.08f, 0.0f, 5, (Color){210, 210, 220, 255});
        }
    }
    if (c->type == 1) {   // HORNET: big rear wing, front splitter
        DrawCube((Vector3){0, 1.85f, -2.0f}, 2.7f, 0.08f, 0.7f, (Color){20, 20, 24, 255});
        DrawCube((Vector3){0, 0.4f, 2.25f}, 1.9f, 0.08f, 0.5f, (Color){20, 20, 24, 255});
    } else if (c->type == 2) {   // CRUSHER: roll cage, exhaust stacks, roof lamps
        DrawCube((Vector3){0, 1.95f, -0.3f}, 1.7f, 0.1f, 0.1f, (Color){60, 60, 66, 255});
        DrawCube((Vector3){-0.8f, 1.65f, -0.3f}, 0.1f, 0.6f, 0.1f, (Color){60, 60, 66, 255});
        DrawCube((Vector3){0.8f, 1.65f, -0.3f}, 0.1f, 0.6f, 0.1f, (Color){60, 60, 66, 255});
        DrawCylinderEx((Vector3){-0.55f, 1.0f, -0.95f}, (Vector3){-0.55f, 1.9f, -0.95f}, 0.12f, 0.12f, 8, (Color){130, 130, 140, 255});
        DrawCylinderEx((Vector3){0.55f, 1.0f, -0.95f}, (Vector3){0.55f, 1.9f, -0.95f}, 0.12f, 0.12f, 8, (Color){130, 130, 140, 255});
        for (int l = -1; l <= 1; l += 2) DrawCube((Vector3){l * 0.45f, 2.0f, -0.3f}, 0.3f, 0.2f, 0.2f, (Color){255, 240, 150, 255});
    }
    rlPopMatrix();
    (void)tiltDamage;
}
static void drawCar(Car *c, int ci) {
    if (!c->alive) return;
    if (c->invuln > 0 && ((int)(c->invuln * 12) & 1)) return;
    rlPushMatrix();
    rlTranslatef(c->p.x, c->y, c->p.y);
    rlRotatef(c->h * RAD2DEG, 0, 1, 0);
    {   // pitch with the slope of the road
        float seg = trkLen / NSEG;
        float dy = (trkY[(c->idx + 1) % NSEG] - trkY[(c->idx + NSEG - 1) % NSEG]) / (2 * seg);
        float along = Vector2DotProduct(fwdOf(c->h), trk[c->idx].t);
        rlRotatef(-atanf(dy * (along >= 0 ? 1 : -1)) * RAD2DEG, 1, 0, 0);
    }
    drawCarModel(c, true);
    rlPopMatrix();
    (void)ci;
}

static void drawWorld(Camera3D cam) {
    BeginMode3D(cam);
    DrawPlane((Vector3){0, -0.3f, 0}, (Vector2){3000, 3000}, TDEF->ground);
    for (int i = 0; i < 220; i++) {
        Deco *d = &deco[i];
        switch (d->type) {
        case 1:
            DrawCylinderEx((Vector3){d->p.x, 0, d->p.z}, (Vector3){d->p.x, d->h * 0.4f, d->p.z}, d->w * 0.1f, d->w * 0.08f, 6, (Color){80, 55, 30, 255});
            DrawCylinderEx((Vector3){d->p.x, d->h * 0.25f, d->p.z}, (Vector3){d->p.x, d->h, d->p.z}, d->w * 0.5f, 0, 7, d->c);
            break;
        case 2: DrawCube((Vector3){d->p.x, d->h / 2, d->p.z}, d->w, d->h, d->d, d->c);
                DrawCube((Vector3){d->p.x + d->w * 0.3f, d->h * 0.8f, d->p.z}, d->w * 0.5f, d->h * 0.8f, d->d * 0.6f, ColorBrightness(d->c, 0.1f)); break;
        case 3: case 4:
            DrawCylinderEx((Vector3){d->p.x, 0, d->p.z}, (Vector3){d->p.x, d->h, d->p.z}, d->w * 0.5f, 0, 5, d->c); break;
        default:
            DrawCube((Vector3){d->p.x, d->h / 2, d->p.z}, d->w, d->h, d->d, ColorBrightness(d->c, -0.35f));
            DrawCube((Vector3){d->p.x, d->h + 0.3f, d->p.z}, d->w + 0.4f, 0.6f, d->d + 0.4f, d->c);
            break;
        }
    }
    drawTrack();
    drawSigns(cars[0].p);
    for (int i = 0; i < npicks; i++) {
        Pickup *pk = &picks[i];
        if (pk->respawn > 0) continue;
        Color c = pk->type < 0 ? GREEN : (Color[]){YELLOW, ORANGE, RED, PURPLE, SKYBLUE}[pk->type];
        float bob = pk->y + 1.2f + sinf(GetTime() * 3 + i) * 0.25f;
        rlPushMatrix();
        rlTranslatef(pk->p.x, bob, pk->p.y);
        rlRotatef(GetTime() * 90, 0, 1, 0);
        DrawCube((Vector3){0, 0, 0}, 1.4f, 1.4f, 1.4f, c);
        DrawCubeWires((Vector3){0, 0, 0}, 1.6f, 1.6f, 1.6f, WHITE);
        rlPopMatrix();
    }
    for (int i = 0; i < 64; i++) {
        if (mines[i].on) {
            DrawCylinder(V3(mines[i].p, mines[i].y + 0.05f), 1.0f, 1.1f, 0.4f, 12, (Color){50, 50, 50, 255});
            bool blink = mines[i].arm <= 0 && ((int)(GetTime() * 4) & 1);
            DrawSphere(V3(mines[i].p, mines[i].y + 0.5f), 0.22f, blink ? RED : MAROON);
        }
        if (calts[i].on) {
            for (int s = 0; s < 9; s++) {
                float a = s * 2.4f, rr = 0.4f + (s % 3) * 0.9f;
                Vector3 b = {calts[i].p.x + cosf(a) * rr, calts[i].y + 0.03f, calts[i].p.y + sinf(a) * rr};
                DrawCylinderEx(b, (Vector3){b.x, b.y + 0.7f, b.z}, 0.18f, 0.0f, 4, LIGHTGRAY);
            }
        }
    }
    for (int i = 0; i < MAXCARS; i++) {
        Car *w = &cars[i];
        if (classic && !w->alive && !w->gone) {   // burnt-out wreck stays on the track
            Color keep = w->col; w->col = (Color){38, 34, 32, 255};
            rlPushMatrix();
            rlTranslatef(w->p.x, w->y - 0.05f, w->p.y);
            rlRotatef(w->h * RAD2DEG, 0, 1, 0);
            rlRotatef(8.0f + (i % 3) * 5.0f, 0, 0, 1);
            drawCarModel(w, false);
            rlPopMatrix();
            w->col = keep;
        } else drawCar(&cars[i], i);
    }
    for (int i = 0; i < 256; i++) {
        Proj *p = &proj[i];
        if (!p->on) continue;
        if (p->type == 0) {
            Vector2 d = Vector2Scale(Vector2Normalize(p->v), 2.5f);
            DrawCylinderEx((Vector3){p->p.x, p->y, p->p.y}, (Vector3){p->p.x - d.x, p->y, p->p.y - d.y}, 0.08f, 0.08f, 4, (Color){255, 240, 120, 255});
        } else {
            rlPushMatrix();
            rlTranslatef(p->p.x, p->y, p->p.y);
            rlRotatef(atan2f(p->v.x, p->v.y) * RAD2DEG, 0, 1, 0);
            DrawCube((Vector3){0, 0, 0}, 0.4f, 0.4f, 1.8f, (Color){220, 220, 220, 255});
            DrawCube((Vector3){0, 0, 1.0f}, 0.3f, 0.3f, 0.4f, RED);
            DrawCube((Vector3){0, 0, -1.0f}, 0.2f, 0.2f, 0.4f, ORANGE);
            rlPopMatrix();
        }
    }
    rlDrawRenderBatchActive();
    for (int i = 0; i < 32; i++) if (beams[i].life > 0) {
        Beam *b = &beams[i];
        float a = beams[i].life / 0.12f;
        DrawCylinderEx(b->a, b->b, 0.35f * a, 0.35f * a, 6, (Color){255, 60, 60, 120});
        DrawCylinderEx(b->a, b->b, 0.1f, 0.1f, 6, WHITE);
    }
    for (int i = 0; i < 4000; i++) {
        Part *q = &parts[i];
        if (q->life <= 0) continue;
        float a = q->life / q->max;
        Color c = q->c; c.a = (unsigned char)(c.a * a);
        DrawCube(q->p, q->size * (0.4f + a), q->size * (0.4f + a), q->size * (0.4f + a), c);
    }
    EndMode3D();
}

static void drawBar(int x, int y, int w, int h, float f, Color c) {
    DrawRectangle(x - 2, y - 2, w + 4, h + 4, (Color){0, 0, 0, 170});
    DrawRectangle(x, y, (int)(w * Clamp(f, 0, 1)), h, c);
}

static int rankOf(int ci) {
    int r = 1;
    for (int i = 0; i < MAXCARS; i++) {
        if (i == ci) continue;
        // finished cars rank by finish time (1e6 keeps float precision fine enough to tell finish times apart), then by progress; wrecks last
        float a = cars[i].finished ? 1e6f - cars[i].finishTime : (cars[i].alive || classic ? cars[i].prog : cars[i].prog - 1e5f);
        float b = cars[ci].finished ? 1e6f - cars[ci].finishTime : (cars[ci].alive || classic ? cars[ci].prog : cars[ci].prog - 1e5f);
        if (a > b) r++;
    }
    return r;
}

static void drawHUD(Camera3D cam) {
    int W = GetScreenWidth(), H = GetScreenHeight();
    Car *pl = &cars[0];
    // enemy health bars
    for (int i = 1; i < MAXCARS; i++) {
        Car *c = &cars[i];
        if (!c->alive || Vector2Distance(c->p, pl->p) > 110) continue;
        Vector3 wp = {c->p.x, c->y + 3.0f, c->p.y};
        if (Vector3DotProduct(Vector3Subtract(wp, cam.position), Vector3Subtract(cam.target, cam.position)) <= 0) continue;
        Vector2 sp = GetWorldToScreen(wp, cam);
        drawBar((int)sp.x - 25, (int)sp.y, 50, 5, c->hp / c->maxhp, c->col);
        DrawText(c->name, (int)sp.x - MeasureText(c->name, 14) / 2, (int)sp.y - 16, 14, WHITE);
    }
    // health
    DrawText("ARMOR", 20, H - 70, 16, WHITE);
    drawBar(20, H - 48, 240, 18, pl->hp / pl->maxhp, pl->hp > pl->maxhp * 0.35f ? (Color){60, 200, 60, 255} : RED);
    if (pl->flat > 0) DrawText("FLAT TIRES", 20, H - 92, 20, ORANGE);
    // weapons
    for (int w = 0; w < NW; w++) {
        int x = 290 + w * 118;
        bool ready = pl->ammo[w] > 0 && pl->cd[w] <= 0;
        DrawRectangle(x, H - 70, 110, 52, (Color){0, 0, 0, 160});
        DrawRectangleLines(x, H - 70, 110, 52, ready ? WHITE : GRAY);
        DrawText(TextFormat("[%s] %s", wkey[w], wname[w]), x + 6, H - 64, 14, ready ? WHITE : GRAY);
        DrawText(TextFormat("%d", pl->ammo[w]), x + 6, H - 44, 24, pl->ammo[w] ? YELLOW : RED);
        if (classic && w == W_LASER) {   // recharge bar
            DrawRectangle(x + 40, H - 36, 62, 8, (Color){60, 60, 60, 255});
            DrawRectangle(x + 40, H - 36, (int)(62 * Clamp(pl->battery / battCap(pl), 0, 1)), 8, SKYBLUE);
        }
    }
    // speed / lap / pos
    int kmh = (int)(Vector2Length(pl->v) * 3.6f);
    DrawText(TextFormat("%d", kmh), W - 190, H - 100, 60, WHITE);
    DrawText("km/h", W - 80, H - 70, 20, LIGHTGRAY);
    int lap = pl->lap < 1 ? 1 : (pl->lap > LAPS ? LAPS : pl->lap);
    DrawText(TextFormat("LAP %d/%d", lap, LAPS), 20, 20, 30, WHITE);
    DrawText(TextFormat("POS %d/%d", rankOf(0), MAXCARS), 20, 56, 30, YELLOW);
    DrawText(TextFormat("TIME %.1f", raceT), 20, 92, 20, LIGHTGRAY);
    DrawText(TextFormat("KILLS %d", pl->kills), 20, 116, 20, LIGHTGRAY);
    if (Vector2DotProduct(pl->v, trk[pl->idx].t) < -6)
        DrawText("WRONG WAY", W / 2 - MeasureText("WRONG WAY", 50) / 2, 120, 50, RED);
    if (!pl->alive) DrawText("WRECKED", W / 2 - MeasureText("WRECKED", 60) / 2, H / 2 - 30, 60, RED);
    {   // incoming-danger warnings
        bool flash = ((int)(GetTime() * 6)) & 1;
        int wy = 150;
        if (warnMissile) {
            const char *t1 = "!! MISSILE LOCK !!", *t2 = "SWERVE OR BRAKE!";
            if (flash) DrawText(t1, W / 2 - MeasureText(t1, 44) / 2, wy, 44, RED);
            DrawText(t2, W / 2 - MeasureText(t2, 24) / 2, wy + 48, 24, ORANGE);
            wy += 84;
        }
        if (warnMine) { const char *t = "MINE AHEAD"; DrawText(t, W / 2 - MeasureText(t, 32) / 2, wy, 32, flash ? YELLOW : ORANGE); wy += 38; }
        if (warnCalt) { const char *t = "CALTROPS AHEAD"; DrawText(t, W / 2 - MeasureText(t, 32) / 2, wy, 32, flash ? ORANGE : YELLOW); }
    }
    for (int i = 0; i < 5; i++) if (msgT[i] > 0) DrawText(msgs[i], W / 2 - MeasureText(msgs[i], 22) / 2, 20 + i * 26, 22, Fade(WHITE, fminf(1, msgT[i])));
    // minimap
    float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
    for (int i = 0; i < NSEG; i++) {
        minx = fminf(minx, trk[i].p.x); maxx = fmaxf(maxx, trk[i].p.x);
        minz = fminf(minz, trk[i].p.y); maxz = fmaxf(maxz, trk[i].p.y);
    }
    float sc = 170 / fmaxf(maxx - minx, maxz - minz);
    int mx = W - 200, my = 20;
    DrawRectangle(mx - 8, my - 8, 196, 196, (Color){0, 0, 0, 140});
    #define MM(p) (Vector2){mx + (maxx - (p).x) * sc, my + 170 - ((p).y - minz) * sc}   // +Z up; the right-hand side of the map is world -X (same as the driver's right)
    for (int i = 0; i < NSEG; i += 4) DrawLineV(MM(trk[i].p), MM(trk[(i + 4) % NSEG].p), GRAY);
    for (int i = 0; i < MAXCARS; i++) {
        if (cars[i].alive) DrawCircleV(MM(cars[i].p), i == 0 ? 5 : 3.5f, cars[i].col);
        else if (classic && !cars[i].gone) { Vector2 q = MM(cars[i].p); DrawLineEx((Vector2){q.x - 3, q.y - 3}, (Vector2){q.x + 3, q.y + 3}, 2, RED); DrawLineEx((Vector2){q.x - 3, q.y + 3}, (Vector2){q.x + 3, q.y - 3}, 2, RED); }
    }
}

static void drawCenter(const char *s, int y, int size, Color c) {
    DrawText(s, GetScreenWidth() / 2 - MeasureText(s, size) / 2, y, size, c);
}

// ---- tiny diagnostic log (ironroad.log) to help track down "it froze" reports
static void logf_(const char *fmt, ...) {
    FILE *f = fopen("ironroad.log", "a");
    if (!f) return;
    va_list a; va_start(a, fmt);
    fprintf(f, "[%7.2f] ", GetTime()); vfprintf(f, fmt, a); fputc('\n', f);
    va_end(a); fclose(f);
}
// ---------------------------------------------------------------- store / campaign
enum { K_UPG, K_REPAIR, K_AMMO };
typedef struct { const char *name, *desc; int kind, max, base, step, arg; } Item;
#define NITEMS_ARCADE 7
#define NITEMS_ALL 15
static const Item items[NITEMS_ALL] = {
    {"ARMOR PLATING", "+30 armor per level. Side plates soak the punishment.", K_UPG, 5, 500, 300, 0},
    {"FRONT SPIKES", "Spiked ram bumper. Head-on hits shred the other car.", K_UPG, 3, 400, 300, 0},
    {"REAR SPIKES", "Spiked tail. Tailgaters impale themselves.", K_UPG, 3, 400, 300, 0},
    {"MACHINE GUN", "More damage and fire rate. Lv2 twin barrels, Lv3 double shot, Lv4 heavy cannon.", K_UPG, 4, 400, 300, 0},
    {"MISSILES", "+15 damage, bigger blast, faster reload. Racks on the roof.", K_UPG, 3, 600, 300, 0},
    {"LASER", "+8 damage, faster recharge.", K_UPG, 3, 600, 300, 0},
    {"AMMO BAY", "+50% ammo capacity for every weapon.", K_UPG, 3, 300, 250, 0},
    // ---- classic mode only
    {"ENGINE", "+3 top speed and +4 acceleration per level. A stock engine cannot catch the pack.", K_UPG, 5, 600, 400, 0},
    {"TRANSMISSION", "+2.5 top speed and stronger acceleration at high speed.", K_UPG, 4, 500, 350, 0},
    {"TIRES", "More grip and sharper steering. You need these for the hairpins.", K_UPG, 5, 500, 300, 0},
    {"REPAIR", "Fix 20 armor points ($5 each). Damage carries over between races.", K_REPAIR, 0, 0, 0, 0},
    {"GUN AMMO +50", "Rounds are NOT refilled between races in classic mode.", K_AMMO, 0, 60, 0, W_GUN},
    {"MISSILE x1", "One homing missile.", K_AMMO, 0, 200, 0, W_MISSILE},
    {"MINE x1", "One mine.", K_AMMO, 0, 100, 0, W_MINE},
    {"CALTROPS x1", "One batch of tire-shredding spikes.", K_AMMO, 0, 80, 0, W_CALT},
};
static int nItems(void) { return classic ? NITEMS_ALL : NITEMS_ARCADE; }
static int *itemLvl(int i) {
    int *p[10] = {&pu.armor, &pu.fspike, &pu.rspike, &pu.gun, &pu.missile, &pu.laser, &pu.ammo, &pu.engine, &pu.trans, &pu.tires};
    return p[i];
}
static float playerMaxHp(void) { return ctypes[carType].hp + 30.0f * pu.armor; }
static float playerHp(void) { return pHp < 0 ? playerMaxHp() : fminf(pHp, playerMaxHp()); }
static int ammoCapP(int w) { return (int)(ammoMax[w] * (1 + 0.5f * pu.ammo)); }
static int repairAmt(void) { float m = playerMaxHp() - playerHp(); return (int)ceilf(fminf(20, m)); }
static int itemPrice(int i) {
    switch (items[i].kind) {
    case K_UPG: return items[i].base + items[i].step * *itemLvl(i);
    case K_REPAIR: return 5 * repairAmt();
    default: return items[i].base;
    }
}
static bool itemFull(int i) {
    switch (items[i].kind) {
    case K_UPG: return *itemLvl(i) >= items[i].max;
    case K_REPAIR: return repairAmt() <= 0;
    default: return pAmmo[items[i].arg] >= ammoCapP(items[i].arg);
    }
}
static bool buyItem(int i) {
    if (itemFull(i) || money < itemPrice(i)) return false;
    money -= itemPrice(i);
    switch (items[i].kind) {
    case K_UPG:
        (*itemLvl(i))++;
        if (i == 0 && pHp >= 0) pHp += 30;   // new plating also comes with its armor
        break;
    case K_REPAIR: { float h = playerHp() + repairAmt(); pHp = h >= playerMaxHp() ? -1 : h; break; }
    default: {
        int w = items[i].arg, add = w == W_GUN ? 50 : 1;
        pAmmo[w] = (int)fminf(ammoCapP(w), pAmmo[w] + add);
    }
    }
    return true;
}

#define SAVE_MAGIC 0x1A0AE
typedef struct { int magic, raceIdx, money, score; Upg u; float hp; int ammo[NW]; unsigned char won_unused[NRACES]; int champ[MAXCARS]; int carType, carColor; } Save;
#define SAVE_V1_SIZE offsetof(Save, champ)   // older saves end before the championship table
static const char *savePath(int m) { return m ? "ironroad_classic.sav" : "ironroad_arcade.sav"; }
static bool readSave(int m, Save *sv) {
    FILE *f = fopen(savePath(m), "rb");
    if (!f) return false;
    memset(sv, 0, sizeof *sv);
    size_t n = fread(sv, 1, sizeof *sv, f);
    bool ok = n >= SAVE_V1_SIZE && sv->magic == SAVE_MAGIC && sv->raceIdx >= 0 && sv->raceIdx < NRACES;
    fclose(f);
    return ok;
}
static void saveGame(void) {
    Save sv = {SAVE_MAGIC, raceIdx, money, score, pu, pHp, {0}, {0}, {0}, carType, carColor};
    for (int w = 0; w < NW; w++) sv.ammo[w] = pAmmo[w];
    for (int w = 1; w < MAXCARS; w++) sv.champ[w] = champ[w];
    FILE *f = fopen(savePath(mode), "wb");
    if (f) { fwrite(&sv, sizeof sv, 1, f); fclose(f); }
}
static bool loadGame(int m) {
    Save sv;
    if (!readSave(m, &sv)) return false;
    raceIdx = sv.raceIdx; money = sv.money; score = sv.score; pu = sv.u; pHp = sv.hp;
    for (int w = 0; w < NW; w++) pAmmo[w] = sv.ammo[w];
    for (int w = 1; w < MAXCARS; w++) champ[w] = sv.champ[w];
    carType = sv.carType >= 0 && sv.carType < 3 ? sv.carType : 0; carColor = sv.carColor >= 0 && sv.carColor < 8 ? sv.carColor : 0;
    return true;
}
static void resetProgress(void) {
    raceIdx = 0; score = 0; memset(&pu, 0, sizeof pu); memset(champ, 0, sizeof champ); carType = 0; carColor = 0;
    money = classic ? 1500 : 1000;
    pHp = -1;
    static const int startAmmo[NW] = {100, 2, 2, 1, 1};
    for (int w = 0; w < NW; w++) pAmmo[w] = startAmmo[w];
}
static void newGame(void) { resetProgress(); remove(savePath(mode)); }
static const int payout[MAXCARS] = {2500, 1700, 1200, 800, 500, 300};
static const int payoutC[MAXCARS] = {3500, 2400, 1700, 1100, 700, 400};
static const int placePts[MAXCARS] = {10, 7, 5, 3, 2, 1};
static int lastPay, lastRank;
static bool lostRace, bailout;

// race over: payout, persistence, results screen. dead = classic mode, player wrecked.
static void finishRace(bool dead) {
    state = ST_DONE; stateT = 0; lostRace = dead; bailout = false;
    lastRank = dead ? MAXCARS : rankOf(0);
    int bounty = classic ? 300 : 250;
    lastPay = (dead ? 0 : (classic ? payoutC : payout)[lastRank - 1]) + cars[0].kills * bounty;
    money += lastPay;
    if (classic) {
        for (int w = 0; w < NW; w++) pAmmo[w] = cars[0].ammo[w];
        pHp = dead ? 0.25f * cars[0].maxhp : cars[0].hp;
        if (pHp >= cars[0].maxhp) pHp = -1;
        if (money < 600) { money = 600; bailout = true; }   // sponsor keeps you from being stuck broke
    }
    if (!dead) {
        score += placePts[lastRank - 1];
        for (int i = 1; i < MAXCARS; i++) champ[i] += placePts[rankOf(i) - 1];
        raceIdx++;
    }
    saveGame();
    logf_("race end: dead=%d rank=%d pay=%d money=%d pHp=%.0f raceIdx=%d", dead, lastRank, lastPay, money, pHp, raceIdx);
    if (getenv("RACE_AUTOPLAY")) { int al = 0; for (int i = 1; i < MAXCARS; i++) al += cars[i].alive; fprintf(stderr, "RACEEND dead=%d rank=%d pay=%d money=%d pHp=%.0f raceIdx=%d ammoGun=%d aiAlive=%d gridP%d score=%d champ=%d,%d,%d,%d,%d\n", dead, lastRank, lastPay, money, pHp, raceIdx, pAmmo[0], al, gridSlot(0) + 1, score, champ[1], champ[2], champ[3], champ[4], champ[5]); }
}

static void enterStore(void) {
    loadTrack(raceIdx);
    newRace();
    state = ST_STORE; stateT = 0;
}
static void drawStoreScene(Camera3D cam) {
    BeginMode3D(cam);
    DrawCylinder((Vector3){0, -0.15f, 0}, 5.5f, 5.5f, 0.2f, 40, (Color){45, 48, 55, 255});
    DrawCylinder((Vector3){0, -0.04f, 0}, 5.6f, 5.6f, 0.04f, 40, ORANGE);
    rlPushMatrix();
    rlRotatef(GetTime() * 35, 0, 1, 0);
    drawCarModel(&cars[0], false);
    rlPopMatrix();
    EndMode3D();
}
static void drawStoreUI(int sel) {
    int W = GetScreenWidth(), H = GetScreenHeight();
    int n = nItems() + 1, rowH = 52;   // +1: START RACE row
    int vis = (H - 130 - 140) / rowH; if (vis > n) vis = n;
    static int top = 0;
    if (sel < top) top = sel;
    if (sel >= top + vis) top = sel - vis + 1;
    if (top > n - vis) top = n - vis;
    DrawRectangle(20, 20, 560, H - 40, (Color){0, 0, 0, 180});
    DrawText("GARAGE", 40, 34, 44, ORANGE);
    DrawText(classic ? "CLASSIC" : "ARCADE", 230, 40, 22, classic ? RED : SKYBLUE);
    DrawText(ctypes[carType].name, 230, 64, 18, LIGHTGRAY);
    DrawText(TextFormat("$%d", money), 400, 40, 36, GREEN);
    DrawText(TextFormat("Next: Race %d/%d  %s", raceIdx + 1, NRACES, tracksDef[raceIdx < NRACES ? raceIdx : NRACES - 1].name), 40, 86, 22, WHITE);
    for (int r = 0; r < vis; r++) {
        int i = top + r, y = 130 + r * rowH;
        if (i == n - 1) {   // START RACE row
            DrawRectangle(30, y - 4, 540, rowH - 4, i == sel ? (Color){60, 180, 60, 150} : (Color){30, 90, 30, 110});
            DrawText(">>  START RACE", 44, y + 6, 28, i == sel ? WHITE : (Color){170, 230, 170, 255});
            continue;
        }
        const Item *it = &items[i];
        bool full = itemFull(i), can = !full && money >= itemPrice(i);
        if (i == sel) DrawRectangle(30, y - 4, 540, rowH - 4, (Color){255, 140, 0, 70});
        DrawText(it->name, 44, y, 24, i == sel ? WHITE : LIGHTGRAY);
        if (it->kind == K_UPG) {
            int lvl = *itemLvl(i);
            for (int p = 0; p < it->max; p++)
                DrawRectangle(44 + p * 22, y + 28, 18, 12, p < lvl ? ORANGE : (Color){70, 70, 70, 255});
        } else if (it->kind == K_REPAIR) {
            DrawRectangle(44, y + 28, 200, 12, (Color){70, 70, 70, 255});
            DrawRectangle(44, y + 28, (int)(200 * playerHp() / playerMaxHp()), 12, playerHp() > playerMaxHp() * 0.35f ? (Color){60, 200, 60, 255} : RED);
            DrawText(TextFormat("%d / %d", (int)playerHp(), (int)playerMaxHp()), 256, y + 24, 18, LIGHTGRAY);
        } else {
            DrawText(TextFormat("have %d / %d", pAmmo[it->arg], ammoCapP(it->arg)), 44, y + 26, 18, pAmmo[it->arg] ? YELLOW : RED);
        }
        if (full) DrawText(it->kind == K_UPG ? "MAX" : "FULL", 480, y + 8, 24, GOLD);
        else DrawText(TextFormat("$%d", itemPrice(i)), 460, y + 8, 24, can ? GREEN : RED);
    }
    if (n > vis) {
        if (top > 0) DrawText("^ more", 480, 100, 18, LIGHTGRAY);
        if (top + vis < n) DrawText("v more", 44, 130 + vis * rowH + 30, 18, LIGHTGRAY);
    }
    int dy = 130 + vis * rowH + 6;
    DrawText(sel == n - 1 ? "Select this row and press ENTER (or press R) when you are done shopping." : items[sel].desc, 40, dy, 18, YELLOW);
    DrawText("UP/DOWN select    ENTER buy    R or the START RACE row to race    ESC menu", 40, H - 70, 18, LIGHTGRAY);
    const TrackDef *T = &tracksDef[raceIdx < NRACES ? raceIdx : NRACES - 1];
    DrawText(T->place, W - 20 - MeasureText(T->place, 22), 24, 22, LIGHTGRAY);
    DrawText(T->name, W - 20 - MeasureText(T->name, 40), 48, 40, WHITE);
    if (classic) {
        DrawText(TextFormat("Top speed %d km/h   Accel %.0f   Grip %.1f   Armor %d", (int)(cars[0].maxv * 3.6f), cars[0].accel, cars[0].gripN, (int)cars[0].maxhp),
                 W - 20 - 640, H - 40, 20, WHITE);
        int gp = gridSlot(0) + 1;
        DrawText(TextFormat("Championship points %d  -  you start P%d on the grid", score, gp), W - 20 - 620, H - 70, 18, gp == 1 ? GREEN : ORANGE);
    } else
        DrawText(TextFormat("Armor %d   Gun dmg %.1f   Missile %.0f   Laser %.0f", (int)cars[0].maxhp, gunDmg(&cars[0]), misDmg(&cars[0]), lasDmg(&cars[0])),
                 W - 20 - 560, H - 40, 20, WHITE);
}

static void drawCarSelUI(void) {
    int H = GetScreenHeight();
    DrawRectangle(20, 20, 560, H - 40, (Color){0, 0, 0, 180});
    DrawText("CHOOSE YOUR CAR", 40, 34, 40, ORANGE);
    DrawText(classic ? "CLASSIC" : "ARCADE", 40, 82, 22, classic ? RED : SKYBLUE);
    for (int t = 0; t < 3; t++) {
        int y = 130 + t * 64;
        bool on = t == carType;
        DrawRectangle(30, y - 4, 540, 58, on ? (Color){255, 140, 0, 80} : (Color){255, 255, 255, 12});
        DrawText(ctypes[t].name, 50, y + 8, 30, on ? WHITE : LIGHTGRAY);
        if (on) DrawText("^  v", 480, y + 12, 26, ORANGE);
    }
    const CarType *ct = &ctypes[carType];
    int y0 = 340;
    struct { const char *n; float v; } bars[5] = {
        {"TOP SPEED", (ct->spd - 0.85f) / 0.3f}, {"ACCELERATION", (ct->acc - 0.8f) / 0.4f}, {"GRIP", (ct->grip - 0.85f) / 0.3f},
        {"ARMOR", ct->hp / 150.0f}, {"WEIGHT", ct->mass / 1.6f}};
    for (int b = 0; b < 5; b++) {
        DrawText(bars[b].n, 50, y0 + b * 34, 18, LIGHTGRAY);
        DrawRectangle(230, y0 + b * 34 + 2, 300, 16, (Color){60, 60, 60, 255});
        DrawRectangle(230, y0 + b * 34 + 2, (int)(300 * Clamp(bars[b].v, 0.05f, 1)), 16, ORANGE);
    }
    DrawText(ct->desc, 40, y0 + 5 * 34 + 8, 16, YELLOW);
    DrawText("COLOR", 50, H - 150, 18, LIGHTGRAY);
    for (int c = 0; c < 8; c++) {
        DrawRectangle(130 + c * 52, H - 156, 42, 30, carPalette[c]);
        if (c == carColor) DrawRectangleLinesEx((Rectangle){128.0f + c * 52, H - 158.0f, 46, 34}, 3, WHITE);
    }
    DrawText("UP/DOWN car     LEFT/RIGHT color     ENTER confirm     ESC back", 40, H - 70, 17, LIGHTGRAY);
}

// ---------------------------------------------------------------- main
int main(void) {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "IRON ROAD");
    SetExitKey(KEY_NULL);
    InitAudioDevice();
    audioOK = IsAudioDeviceReady();
    if (audioOK) {
        sGun = synth(900, 300, 0.08f, 0.5f, 6, 1);
        sMis = synth(300, 80, 0.5f, 0.6f, 3, 1);
        sLas = synth(1800, 200, 0.25f, 0.05f, 4, 0);
        sExp = synth(120, 30, 0.9f, 0.85f, 4, 0);
        sMine = synth(400, 200, 0.12f, 0.2f, 8, 0);
        sPick = synth(500, 1200, 0.18f, 0, 5, 0);
        sEng = engineLoop();
        sBeep = synth(1250, 1250, 0.09f, 0, 6, 0);        // missile lock
        sBeepMine = synth(480, 480, 0.13f, 0, 5, 0);       // mine ahead (low)
        sBeepCalt = synth(1900, 1300, 0.07f, 0, 6, 0);     // caltrops ahead (chirp)
        { double t0 = GetTime(); buildTitleMusic(); if (getenv("RACE_DEBUG")) fprintf(stderr, "title music built in %.0f ms\n", (GetTime() - t0) * 1000); }
    }
    rlDisableBackfaceCulling();
    rlSetClipPlanes(1.0, 3500.0);   // a tight near/far range keeps depth precision high (prevents road/ground z-fighting)
    SetRandomSeed(7);
    remove("ironroad.log");
    if (getenv("RACE_CAR")) sscanf(getenv("RACE_CAR"), "%d,%d", &carType, &carColor);   // test hook
    mode = 0; classic = false;
    if (getenv("RACE_CLASSIC")) { mode = 1; classic = true; }
    int menuSel0 = mode;
    { int ct0 = carType, cc0 = carColor; resetProgress(); loadGame(mode); if (getenv("RACE_CAR")) { carType = ct0; carColor = cc0; } }
    const char *at = getenv("RACE_AUTOTEST");
    bool autotest = at != NULL;
    if (autotest) {
        raceIdx = atoi(at) > 0 ? atoi(at) - 1 : 0;
        if (getenv("RACE_UPG")) { pu = getenv("RACE_UPG")[0] == '2' ? (Upg){1, 1, 1, 1, 1, 1, 1} : (Upg){5, 3, 3, 4, 3, 3, 3}; money = 99999; }
    }
    loadTrack(raceIdx);
    newRace();
    Camera3D cam = {0};
    cam.up = (Vector3){0, 1, 0}; cam.fovy = 70; cam.projection = CAMERA_PERSPECTIVE;
    float camH = 0;
    bool paused = false;
    int frames = 0, sel = 0, menuSel = menuSel0;
    float camY = 0;
    bool storeShot = getenv("RACE_STORE") != NULL;
    bool autoplay = getenv("RACE_AUTOPLAY") != NULL;  // test harness: self-driving player, 1 lap
    if (autoplay) { LAPS = 1; if (getenv("RACE_AUTOPLAY")[0] != '3') { state = ST_COUNT; stateT = 0; } }
    if (autotest && storeShot) {
        enterStore();
        if (getenv("RACE_DENT")) {  // visual test: batter the preview car
            Car *c = &cars[0]; c->p = (Vector2){0, 0}; c->h = 0;
            dent(c, (Vector2){0, 5}, 40); dent(c, (Vector2){3, 3}, 30); dent(c, (Vector2){-3, 1}, 35);
            for (int i = 0; i < 6; i++) dent(c, (Vector2){3, -1 + i * 0.3f}, 10);
            dent(c, (Vector2){-1, -5}, 45); c->hp = c->maxhp * 0.4f;
        }
    }
    else if (autotest && !getenv("RACE_MENU") && !getenv("RACE_CARSEL")) { state = ST_COUNT; stateT = 0; }
    if (getenv("RACE_CARSEL")) { initCar(0); state = ST_CARSEL; stateT = 0; }
    if (getenv("RACE_BRIDGE") && hasBridge) {   // test hook: player under the flyover, another car on top of it
        cars[0].idx = (bridgeLo + NSEG - 10) % NSEG; respawnCar(&cars[0]); cars[0].invuln = 0; camH = cars[0].h;
        cars[1].idx = bridgeHi; respawnCar(&cars[1]); cars[1].invuln = 0;
    }
    if (getenv("RACE_WRECK") && classic) {   // test hook: car 1 is wrecked at the given track index
        int wi = atoi(getenv("RACE_WRECK")) % NSEG;
        cars[1].idx = wi; respawnCar(&cars[1]); cars[1].invuln = 0; cars[1].lap = 0; cars[1].prog = (float)wi;
        damageCar(&cars[1], 1e6f, 0);
        if (getenv("RACE_WRECK_OFFSET")) cars[1].p = Vector2Add(cars[1].p, Vector2Scale(trk[wi].r, (float)atof(getenv("RACE_WRECK_OFFSET"))));
    }
    if (getenv("RACE_TELEPORT")) { cars[0].idx = atoi(getenv("RACE_TELEPORT")) % NSEG; respawnCar(&cars[0]); cars[0].invuln = 0; camH = cars[0].h; }

    while (!WindowShouldClose()) {
        if (GetFrameTime() > 0.3f) logf_("HITCH: frame took %.2fs in state %d", GetFrameTime(), (int)state);
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE)) logf_("key enter/space pressed in state %d (stateT=%.2f)", (int)state, stateT);
        float dt = fminf(GetFrameTime(), 1 / 30.0f);
        { static int prevState = -1, stores = 0;
          if ((int)state != prevState) {
              logf_("state %d -> %d  raceIdx=%d money=%d", prevState, (int)state, raceIdx, money);
              if (autoplay) fprintf(stderr, "STATE %d -> %d (raceIdx=%d money=%d)\n", prevState, (int)state, raceIdx, money);
              if (state == ST_STORE) stores++;
              prevState = (int)state;
          }
          if (autoplay && stores >= 3 && state == ST_STORE && stateT > 3) { TakeScreenshot("shot.png"); fprintf(stderr, "AUTOPLAY: 3rd garage reached\n"); break; } }
        if (autoplay && getenv("RACE_AUTOPLAY")[0] != '3' && state == ST_STORE && stateT > 2) { TakeScreenshot("shot.png"); fprintf(stderr, "AUTOPLAY: reached store ok\n"); break; }
        if (autotest && (!autoplay || getenv("RACE_SHOT")) && ++frames > (getenv("RACE_SHOT") ? atoi(getenv("RACE_SHOT")) : 300)) { if (frames == (getenv("RACE_SHOT") ? atoi(getenv("RACE_SHOT")) : 300) + 1) fprintf(stderr, "FPS at shot: %d\n", GetFPS()); TakeScreenshot("shot.png"); if (frames > 305) break; }
        if (getenv("RACE_SHOTCOUNT") && state == ST_COUNT && stateT > 2.0f) { TakeScreenshot("shot.png"); break; }   // test hook: shoot the (frozen) countdown scene
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (state == ST_MENU) break;
            state = ST_MENU; loadTrack(raceIdx); newRace(); paused = false;
        }
        if (IsKeyPressed(KEY_P) && (state == ST_RACE)) paused = !paused;
        stateT += dt;
        if (state == ST_MENU) {
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) menuSel ^= 1;
            if (IsKeyPressed(KEY_N) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE)) {
                mode = menuSel; classic = mode == 1;
                if (IsKeyPressed(KEY_N) || !loadGame(mode)) {   // new campaign: pick a car first
                    newGame(); sel = 0; loadTrack(0); newRace();
                    state = ST_CARSEL; stateT = 0;
                } else enterStore();
            }
        } else if (state == ST_CARSEL) {
            bool ch = false;
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) { carType = (carType + 1) % 3; ch = true; }
            if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) { carType = (carType + 2) % 3; ch = true; }
            if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) { carColor = (carColor + 1) % 8; ch = true; }
            if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) { carColor = (carColor + 7) % 8; ch = true; }
            if (ch) { initCar(0); snd(sPick, 0.4f, 1.3f); }
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) { saveGame(); enterStore(); }
        } else if (state == ST_STORE) {
            int ni = nItems(), rows = ni + 1;   // last row is START RACE
            if (sel >= rows) sel = 0;
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) sel = (sel + 1) % rows;
            if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) sel = (sel + rows - 1) % rows;
            bool startRace = IsKeyPressed(KEY_R);
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                if (sel == ni) startRace = true;
                else if (buyItem(sel)) { snd(sPick, 0.7f, 1.0f); initCar(0); saveGame(); }
                else snd(sMine, 0.5f, 0.5f);
            }
            if (startRace) { saveGame(); newRace(); state = ST_COUNT; stateT = 0; }
        } else if (state == ST_COUNT && stateT > 3.5f) { state = ST_RACE; stateT = 0; }
        else if (state == ST_DONE && stateT > 0.6f && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE) || (autoplay && getenv("RACE_AUTOPLAY")[0] == '1' && stateT > 2))) {
            fprintf(stderr, "DONE->next (raceIdx=%d)\n", raceIdx);
            if (raceIdx >= NRACES) { state = ST_END; stateT = 0; }
            else enterStore();
        } else if (state == ST_END && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE))) {
            newGame(); loadTrack(0); newRace(); state = ST_MENU;   // campaign complete: start over
        }

        if ((state == ST_COUNT || state == ST_RACE || state == ST_DONE) && !paused) {
            Ctl kc[MAXCARS];
            memset(kc, 0, sizeof(kc));
            bool go = state == ST_RACE || state == ST_DONE;
            if (go) {
                Ctl *k = &kc[0];
                k->thr = (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) - (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN));
                k->steer = (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) - (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT));
                k->hb = IsKeyDown(KEY_SPACE);
                k->fire[W_GUN] = IsKeyDown(KEY_F) || IsKeyDown(KEY_LEFT_CONTROL) || IsMouseButtonDown(MOUSE_LEFT_BUTTON);
                k->fire[W_MISSILE] = IsKeyDown(KEY_G);
                k->fire[W_LASER] = IsKeyDown(KEY_H);
                k->fire[W_MINE] = IsKeyDown(KEY_B);
                k->fire[W_CALT] = IsKeyDown(KEY_V);
                if (autotest) { k->thr = 1; k->fire[W_GUN] = true; k->steer = 0; }
                if (IsKeyPressed(KEY_R) && cars[0].alive) { cars[0].hp -= 15; respawnCar(&cars[0]); cars[0].hp = fmaxf(cars[0].hp, 1); }
            }
            for (int i = autoplay ? 0 : 1; i < MAXCARS; i++) if (go) aiControl(&cars[i], i, &kc[i], dt);
            if (autoplay && getenv("RACE_NOBRAKE") && go) kc[0].thr = 1;   // test hook: never lift
            if (state == ST_DONE) memset(&kc[0], 0, sizeof kc[0]);  // results screen: no driving/firing input
            for (int i = 0; i < MAXCARS; i++) updateCar(&cars[i], i, &kc[i], dt);
            updateWorld(dt);
            {   // warning beeps (missile lock, mine ahead, caltrops ahead) - faster as the danger gets closer
                static float bt[3];
                for (int b = 0; b < 3; b++) bt[b] -= dt;
                if (warnMissile && bt[0] <= 0) { snd(sBeep, 0.8f, 1.0f); bt[0] = Clamp(warnMissileDist / 250, 0.1f, 0.5f); }
                if (warnMine && bt[1] <= 0) { snd(sBeepMine, 0.7f, 1.0f); bt[1] = Clamp(warnMineDist / 110, 0.12f, 0.5f); }
                if (warnCalt && bt[2] <= 0) { snd(sBeepCalt, 0.6f, 1.0f); bt[2] = Clamp(warnCaltDist / 110, 0.12f, 0.5f); }
            }
            if (state == ST_RACE) {
                raceT += dt;
                if (classic) {
                    bool anyAI = false;   // anyone still racing, or who already finished ahead of you
                    for (int i = 1; i < MAXCARS; i++) if (cars[i].alive || cars[i].finished) anyAI = true;
                    if (cars[0].alive && !anyAI && !cars[0].finished) {
                        cars[0].finished = true; cars[0].finishTime = raceT; addMsg("LAST CAR STANDING!");
                    }
                    if (!cars[0].alive) { deadT += dt; if (deadT > 2.5f) finishRace(true); }
                }
                if (getenv("RACE_KILL")) { static bool killed; if (!killed && raceT > 6) { killed = true; cars[0].invuln = 0; damageCar(&cars[0], 1e6f, 1); } }
                if (getenv("RACE_RANKTEST") && raceT > 1) {   // rank with the wreck (car 1) ahead vs. after passing it
                    static bool done; if (!done) { done = true;
                        float save = cars[0].prog; for (int i = 2; i < MAXCARS; i++) cars[i].prog = 1000;
                        cars[1].prog = 100; cars[0].prog = 95; int before = rankOf(0);
                        cars[0].prog = 105; int after = rankOf(0); cars[0].prog = save;
                        fprintf(stderr, "RANKTEST wreck at prog 100: player at 95 -> P%d, player at 105 -> P%d (5 cars ahead regardless: so wreck counts until passed = %s)\n", before, after, before == after + 1 ? "YES" : "NO");
                    }
                }
                if (getenv("RACE_MTEST")) {   // test hook: AI fires a missile at the player; a mine sits ahead; player "swerves" after 0.7s
                    static int mt; static float mtT;
                    if (mt == 0 && raceT > 4) {
                        Vector2 f = fwdOf(cars[0].h);
                        cars[1].idx = (cars[0].idx + NSEG - 14) % NSEG; cars[1].p = Vector2Subtract(cars[0].p, Vector2Scale(f, 50)); cars[1].h = cars[0].h; cars[1].v = cars[0].v;
                        cars[1].y = cars[0].y; cars[1].cd[W_MISSILE] = 0;
                        fireWeapon(&cars[1], 1, W_MISSILE);
                        mines[0] = (Mine){true, Vector2Add(cars[0].p, Vector2Scale(f, 40)), 1, 0.8f, 40, cars[0].y};
                        mt = 1; mtT = raceT; fprintf(stderr, "MTEST fired; lock target=%d\n", proj[0].target);
                    }
                    if (mt == 1) { fprintf(stderr, "MTEST t=%.2f warnMissile=%d dist=%.0f warnMine=%d\n", raceT - mtT, warnMissile, warnMissileDist, warnMine); }
                    if (mt == 1 && raceT > mtT + 0.3f) { mt = 2; cars[0].prevSteerIn = 0; cars[0].evadeT = 0.6f; fprintf(stderr, "MTEST player swerves\n"); }
                    if (mt == 2 && raceT > mtT + 1.5f) { fprintf(stderr, "MTEST after: warnMissile=%d lastmsg='%s' proj0 target=%d on=%d\n", warnMissile, msgs[0], proj[0].target, proj[0].on); mt = 3; }
                }
                if (state == ST_RACE && cars[0].finished) finishRace(false);
            }
            shake = fmaxf(0, shake - dt * 1.5f);
        }

        // engine sound
        bool driving = state == ST_COUNT || state == ST_RACE || state == ST_DONE;
        if (audioOK && driving && cars[0].alive) {
            float sp = Vector2Length(cars[0].v);
            SetSoundPitch(sEng, 0.7f + sp / MAXV * 1.6f);
            SetSoundVolume(sEng, 0.25f);
            if (!IsSoundPlaying(sEng)) PlaySound(sEng);
        } else if (audioOK && IsSoundPlaying(sEng)) StopSound(sEng);

        // title music: plays on the menu / car-select screens, fades out for the garage and races
        if (musicOK) {
            bool menuish = state == ST_MENU || state == ST_CARSEL;
            musicVol += ((menuish ? 0.5f : 0.0f) - musicVol) * fminf(1, 3 * dt);
            if (menuish || musicVol > 0.01f) {
                if (!IsMusicStreamPlaying(titleMusic)) PlayMusicStream(titleMusic);
                SetMusicVolume(titleMusic, musicVol);
                UpdateMusicStream(titleMusic);
            } else if (IsMusicStreamPlaying(titleMusic)) StopMusicStream(titleMusic);
        }

        // camera
        Car *pl = &cars[0];
        if (state == ST_MENU || state == ST_END) {
            float a = GetTime() * 0.15f;
            Vector2 ctr = {0, 0};
            for (int i = 0; i < NSEG; i += 8) ctr = Vector2Add(ctr, trk[i].p);
            ctr = Vector2Scale(ctr, 8.0f / NSEG);
            cam.position = (Vector3){ctr.x + sinf(a) * 420, 230, ctr.y + cosf(a) * 420};
            cam.target = (Vector3){ctr.x, 0, ctr.y};
            cam.fovy = 60;
        } else if (state == ST_STORE || state == ST_CARSEL) {
            cam.position = (Vector3){2, 3.2f, 9.5f};
            cam.target = (Vector3){-2.2f, 1.0f, 0};
            cam.fovy = 45;
        } else {
            camH += angDiff(camH, pl->h) * fminf(1, 5 * dt);
            Vector2 cf = fwdOf(camH);
            float sp = Vector2Length(pl->v);
            float back = 12 + sp * 0.06f;
            camY += (pl->y - camY) * fminf(1, 6 * dt);
            cam.position = (Vector3){pl->p.x - cf.x * back, camY + 5.2f, pl->p.y - cf.y * back};
            cam.target = (Vector3){pl->p.x + cf.x * 8, camY + 1.2f, pl->p.y + cf.y * 8};
            cam.fovy = 68 + sp / MAXV * 14;
            if (shake > 0) {
                cam.position.x += rf(-1, 1) * shake; cam.position.y += rf(-1, 1) * shake * 0.6f; cam.position.z += rf(-1, 1) * shake;
            }
        }

        BeginDrawing();
        int W = GetScreenWidth(), H = GetScreenHeight();
        const TrackDef *T = TDEF;
        ClearBackground(T->skyBot);
        DrawRectangleGradientV(0, 0, W, H * 0.55f, T->skyTop, T->skyBot);
        if (state == ST_STORE || state == ST_CARSEL) {
            DrawRectangleGradientV(0, 0, W, H, (Color){25, 25, 35, 255}, (Color){60, 45, 40, 255});
            drawStoreScene(cam);
            if (state == ST_STORE) drawStoreUI(sel); else drawCarSelUI();
        } else drawWorld(cam);
        if (state == ST_MENU) {
            DrawRectangle(0, 0, W, H, (Color){0, 0, 0, 120});
            drawCenter("IRON ROAD", 70, 100, ORANGE);
            drawCenter("combat racing  -  10 tracks  -  upgrade your ride", 170, 28, LIGHTGRAY);
            drawCenter("W/S or UP/DOWN  throttle / brake      A/D or LEFT/RIGHT  steer      SPACE  handbrake", 245, 20, WHITE);
            drawCenter("F / Ctrl / Left-click  machine gun      G  homing missile      H  laser", 275, 20, WHITE);
            drawCenter("B  drop mine      V  drop caltrops      R  reset onto track      P  pause", 305, 20, WHITE);
            static const char *mname[2] = {"ARCADE", "CLASSIC"};
            static const char *mdesc[2] = {"Respawn after wrecks. Free ammo and repairs each race. Fast cars from the start.",
                                           "No respawns, for anyone. Pay for repairs and ammo. Earn your speed and your grid spot."};
            for (int m = 0; m < 2; m++) {
                Save sv; bool has = readSave(m, &sv);
                int y = 370 + m * 110;
                bool on = m == menuSel;
                DrawRectangle(W / 2 - 420, y - 8, 840, 96, on ? (Color){255, 140, 0, 90} : (Color){0, 0, 0, 120});
                DrawRectangleLines(W / 2 - 420, y - 8, 840, 96, on ? WHITE : GRAY);
                DrawText(mname[m], W / 2 - 400, y, 36, on ? WHITE : LIGHTGRAY);
                DrawText(has ? TextFormat("continue: race %d/%d   $%d", sv.raceIdx + 1, NRACES, sv.money) : "new campaign", W / 2 + 60, y + 6, 24, has ? GREEN : YELLOW);
                DrawText(mdesc[m], W / 2 - 400, y + 50, 18, LIGHTGRAY);
            }
            drawCenter("UP/DOWN choose    ENTER play / continue    N new game in that mode    ESC quit", H - 60, 20, LIGHTGRAY);
        } else if (state == ST_END) {
            DrawRectangle(0, 0, W, H, (Color){0, 0, 0, 150});
            drawCenter("CAMPAIGN COMPLETE", 150, 80, GOLD);
            drawCenter(TextFormat("Championship points: %d / %d", score, NRACES * 10), 280, 36, WHITE);
            drawCenter(score >= 80 ? "ROAD LEGEND" : score >= 55 ? "SEASONED KILLER" : score >= 30 ? "ROOKIE WRECKER" : "SCRAPYARD MATERIAL", 340, 40, ORANGE);
            drawCenter("ENTER - back to menu", 450, 26, LIGHTGRAY);
        } else if (state != ST_STORE && state != ST_CARSEL) {
            drawHUD(cam);
            if (state == ST_COUNT) {
                int n = 3 - (int)stateT;
                drawCenter(n > 0 ? TextFormat("%d", n) : "GO!", H / 3, 140, n > 0 ? YELLOW : GREEN);
                drawCenter(TextFormat("RACE %d/%d - %s", raceIdx + 1, NRACES, T->name), H / 3 - 90, 40, WHITE);
                drawCenter(T->place, H / 3 - 50, 24, LIGHTGRAY);
                if (classic) drawCenter(TextFormat("CLASSIC - you start P%d (championship standing)", gridSlot(0) + 1), H / 3 - 20, 20, ORANGE);
            }
            if (paused) drawCenter("PAUSED", H / 3, 80, WHITE);
            if (state == ST_DONE) {
                DrawRectangle(W / 2 - 280, H / 2 - 220, 560, 440, (Color){0, 0, 0, 210});
                if (lostRace) drawCenter("DESTROYED", H / 2 - 205, 60, RED);
                else drawCenter(lastRank == 1 ? "VICTORY!" : TextFormat("FINISHED  P%d", lastRank), H / 2 - 205, 60, lastRank == 1 ? GOLD : WHITE);
                int order[MAXCARS];
                for (int i = 0; i < MAXCARS; i++) order[i] = i;
                for (int i = 0; i < MAXCARS; i++) for (int j = i + 1; j < MAXCARS; j++)
                    if (rankOf(order[j]) < rankOf(order[i])) { int t = order[i]; order[i] = order[j]; order[j] = t; }
                for (int i = 0; i < MAXCARS; i++) {
                    Car *c = &cars[order[i]];
                    DrawText(classic ? TextFormat("%d. %-8s kills %d  pts %d", i + 1, c->name, c->kills, order[i] == 0 ? score : champ[order[i]]) : TextFormat("%d. %-8s kills %d", i + 1, c->name, c->kills), W / 2 - 190, H / 2 - 130 + i * 32, 24, c->col);
                }
                drawCenter(TextFormat(lostRace ? "Kill bounties: $%d" : "Prize + kills: $%d", lastPay), H / 2 + 80, 28, GREEN);
                if (lostRace) drawCenter("Your wreck needs repairs - same track again", H / 2 + 50, 20, ORANGE);
                else if (bailout) drawCenter("Sponsor bailout: cash topped up to $600", H / 2 + 50, 20, ORANGE);
                drawCenter(TextFormat("Points: %d   Cash: $%d", score, money), H / 2 + 115, 22, WHITE);
                drawCenter(raceIdx >= NRACES ? "ENTER / SPACE - finish campaign" : "ENTER / SPACE - garage", H / 2 + 165, 24, LIGHTGRAY);
            }
        }
        DrawFPS(W / 2 - 30, H - 24);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
