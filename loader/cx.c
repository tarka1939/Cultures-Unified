/* cx.dll - runtime hooks for the Cultures Saga Game.exe (2003-11-05 build), loaded through an import that
 * build.py adds to Game.exe. Every patch site is checked against the original bytes first; on a mismatch that
 * feature stays off (see OutputDebugString / DebugView).
 *
 * Feature 1: Cultures 2 campaign (campaign id 1) on the Saga campaign-map screen.
 *   - a "'Gates of Asgard' Campaign" button on the single-player screen (screen 3), above "'Northland' Campaign",
 *     sending command 5006 (free slot 6 of the menu command jump table);
 *   - screen 5 (the Northland map; the C2 map background is the same picture, frame 2) is switched to campaign 1
 *     by re-pointing its campaign id, marker table and path-overlay table while it is shown for C2;
 *   - after a C2 mission the engine sets start_campaign_1_screen, which the Saga never consumes: we do;
 *   - C2's intro (intro00.mpg) plays before campaign 1 mission 10, like intro01/02 for Northland/8th Wonder.
 * Addresses: see re/NOTES.md (Menu system, Campaign map screen data).
 */
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define VA(x) ((void *)(x))

/* ------------------------------------------------------------------ engine functions */
typedef void *(__cdecl *new_t)(unsigned);
typedef void *(__fastcall *btn_ctor_t)(void *self, void *edx, void *rect, const char *label, const char *tip,
                                        int cmd, void *parent);
typedef void (__fastcall *list_add_t)(void *list, void *edx, void *item);
typedef void (__fastcall *gui_add_t)(void *gm, void *edx, void *w, char front);
typedef const char *(__cdecl *saga_str_t)(unsigned id);
typedef char (__fastcall *cfg_has_t)(void *cfg, void *edx, const char *key);
typedef void (__fastcall *cfg_del_t)(void *cfg, void *edx, const char *key);

static new_t      game_new  = (new_t)0x4e7efd;
static btn_ctor_t btn_ctor  = (btn_ctor_t)0x4b9e45;
static list_add_t list_add  = (list_add_t)0x43abc5;
static gui_add_t  gui_add   = (gui_add_t)0x47802d;
static saga_str_t saga_str  = (saga_str_t)0x4e3fd4;   /* string from global\saga001 */
static cfg_has_t  cfg_has   = (cfg_has_t)0x4e377e;
static cfg_del_t  cfg_del   = (cfg_del_t)0x4e380e;

#define MENU_MGR   (*(int **)0x56c6c4)
#define GUI_MGR    (*(void **)0x557f44)
#define CFG        (*(void **)0x56c9d4)

#define CMD_C2       5006
#define CMD_NL       0x1393
#define STR_C2       25     /* saga001 strings we add (translations/eng/global/saga001.ini) */
#define STR_C2_TIP   26

/* ------------------------------------------------------------------ patching helpers */
static FILE *trace_f;   /* cx_trace.log, open with cx.ini trace=1 */
static void dbg(const char *s)
{
    OutputDebugStringA("cx: "); OutputDebugStringA(s); OutputDebugStringA("\n");
    if (trace_f) { fprintf(trace_f, "cx: %s\n", s); fflush(trace_f); }
}

static void put(void *at, const void *src, SIZE_T n)
{
    DWORD old;
    VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old);
    memcpy(at, src, n);
    VirtualProtect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);
}

static int same(const void *at, const char *hex)
{
    const unsigned char *p = (const unsigned char *)at;
    for (; hex[0] && hex[1]; hex += 2, p++) {
        unsigned v;
        char b[3] = { hex[0], hex[1], 0 };
        v = strtoul(b, 0, 16);
        if (*p != v) return 0;
    }
    return 1;
}

/* jmp-detour: copy `n` whole prologue instructions (no relative operands) into a trampoline. */
static void *detour(void *target, void *hook, int n, const char *orig_hex)
{
    unsigned char *tr, jmp[5] = { 0xE9 };
    if (!same(target, orig_hex)) { dbg("detour: unexpected bytes"); return 0; }
    tr = (unsigned char *)VirtualAlloc(0, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(tr, target, n);
    tr[n] = 0xE9;
    *(int *)(tr + n + 1) = (int)((unsigned char *)target + n - (tr + n + 5));
    *(int *)(jmp + 1) = (int)((unsigned char *)hook - ((unsigned char *)target + 5));
    put(target, jmp, 5);
    return tr;
}

/* ------------------------------------------------------------------ C2 campaign tables */
/* markers {mission, x, y} on the 800x600 campaign map - Cultures2.exe 0x4fc2e0 */
static int c2_markers[][3] = {
    {10, 290, 197}, {20, 372, 276}, {30, 335, 270}, {40, 418, 356}, {50, 479, 342},
    {60, 501, 382}, {70, 497, 355}, {80, 530, 421}, {100, 570, 386}, {110, 438, 406}, {0, 0, 0}};
/* path overlays {mission done, frame}: Cultures2.exe 0x4fcb48 frames 3..11, appended to the Saga
 * LS_menu_logos.bmd as frames 26..34 by build.py */
static int c2_paths[][2] = {
    {20, 26}, {30, 27}, {40, 28}, {50, 29}, {60, 30}, {70, 31}, {80, 32}, {100, 33}, {110, 34}, {0, 0}};

struct site { unsigned va; int size; unsigned orig; unsigned c2; };
static struct site sites[] = {
    {0x4d5839, 1, 2, 1},                                  /* builder: cmp ecx, 2 (campaign id)     */
    {0x4d5857, 4, 0x50ba18, 0},                           /* builder: mov ecx, [markers]           */
    {0x4d586c, 4, 0x50ba18, 0},                           /* builder: mov edx, markers             */
    {0x4d588c, 4, 0x50ba1c, 4},                           /* builder: [eax + markers.x]            */
    {0x4d5895, 4, 0x50ba20, 8},                           /* builder: [eax + markers.y]            */
    {0x4dc1e9, 4, 0x4faeb0, 0},                           /* paint: cmp [paths], 0                 */
    {0x4dc1f1, 4, 0x4faeb0, 0},                           /* paint: mov edi, paths                 */
    {0x4dc200, 1, 2, 1},                                  /* paint: push 2 (campaign id)           */
};
static int c2_ok, c2_on, c2_next;

static void c2_init(void)
{
    int i;
    sites[1].c2 = sites[2].c2 = (unsigned)c2_markers;
    sites[3].c2 = (unsigned)c2_markers + 4;
    sites[4].c2 = (unsigned)c2_markers + 8;
    sites[5].c2 = sites[6].c2 = (unsigned)c2_paths;
    for (i = 0; i < sizeof sites / sizeof *sites; i++) {
        unsigned v = 0;
        memcpy(&v, VA(sites[i].va), sites[i].size);
        if (v != sites[i].orig) { dbg("c2: campaign screen site mismatch"); return; }
    }
    c2_ok = 1;
}

static void c2_mode(int on)
{
    int i;
    if (!c2_ok || on == c2_on) return;
    for (i = 0; i < sizeof sites / sizeof *sites; i++)
        put(VA(sites[i].va), on ? &sites[i].c2 : &sites[i].orig, sites[i].size);
    c2_on = on;
}

/* ------------------------------------------------------------------ hooks */
/* FUN_004d4499 menu::switch_screen(id, flag) */
typedef void (__fastcall *switch_t)(void *mgr, void *edx, int id, char flag);
static switch_t switch_orig;
static void __fastcall switch_hook(void *mgr, void *edx, int id, char flag)
{
    if (id == 5) c2_mode(c2_next);
    c2_next = 0;
    switch_orig(mgr, edx, id, flag);
}

/* FUN_004d4b40 menu::build_widgets() */
typedef void (__fastcall *build_t)(int *mgr);
static build_t build_orig;
struct node { struct node *next; int *w; };
#define W_Y(w)   ((w)[2])
#define W_H(w)   ((w)[4])
#define W_CMD(w) ((w)[0x3c / 4])
static void __fastcall build_hook(int *mgr)
{
    struct node *n;
    int *nl = 0, rect[4], dy;
    void *b;
    const char *label, *tip;
    build_orig(mgr);
    if (!c2_ok || mgr[0] != 3) return;
    for (n = *(struct node **)(mgr + 2 + 2); n; n = n->next)
        if (n->w && W_CMD(n->w) == CMD_NL) nl = n->w;
    if (!nl) return;
    /* make room: every campaign/custom/load button from Northland down moves one row */
    dy = W_H(nl) + 10;
    for (n = *(struct node **)(mgr + 2 + 2); n; n = n->next)
        if (n->w && W_CMD(n->w) >= CMD_NL && W_CMD(n->w) <= 0x139a && W_CMD(n->w) != 0x1399)
            W_Y(n->w) += dy;
    memcpy(rect, nl + 1, sizeof rect);
    rect[1] -= dy;
    label = saga_str(STR_C2); tip = saga_str(STR_C2_TIP);
    if (!label || !*label) label = "'Gates of Asgard' Campaign";
    if (!tip || !*tip) tip = "To the 'Gates of Asgard' campaign screen";
    b = game_new(0x60);
    if (!b) return;
    b = btn_ctor(b, 0, rect, label, tip, CMD_C2, mgr + 0x85b);
    list_add(mgr + 2, 0, b);
    gui_add(GUI_MGR, 0, b, 1);
}

/* FUN_0040212b app::enter_menus(): consumes start_campaign_2..4_screen; we add 1 */
typedef int (__fastcall *menus_t)(void *app);
static menus_t menus_orig;
static int __fastcall menus_hook(void *app)
{
    int r = menus_orig(app);
    if (c2_ok && cfg_has(CFG, 0, "start_campaign_1_screen")) {
        c2_next = 1;
        switch_hook(MENU_MGR, 0, 5, 0);
        cfg_del(CFG, 0, "start_campaign_1_screen");
    }
    return r;
}

/* jump-table slot 6 of the menu command handler FUN_004d86b6 (cmd 5006). Entered by `jmp [eax*4+tbl]`
 * inside the handler's frame (ebx = 1); continue at its common exit 0x4d8af9 (mov al, bl; ...; ret 0x14). */
static void c2_button(void) { c2_next = 1; switch_hook(MENU_MGR, 0, 5, 0); }
static __declspec(naked) void cmd_c2_stub(void)
{
    __asm {
        call c2_button
        mov eax, 0x4d8af9
        jmp eax
    }
}

/* FUN_00403100 (pre-mission video): at 0x4031a7 it compares the campaign of the map about to start with 2/3/4 and
 * picks intro01/02/03 for mission 10. A mid-function detour adds campaign 1 -> intro00 (C2's intro, converted by
 * build.py), then re-runs the original `cmp dword [edi+0x124], 2` and returns to the jne at 0x4031ae. */
static void __cdecl fmv_c2(int *map, char *out)
{
    if (map[0x124 / 4] == 1 && map[0x128 / 4] == 10) { out[0] = 1; strcpy(out + 1, "intro00"); }
}
static __declspec(naked) void fmv_stub(void)
{
    __asm {
        pushad
        push esi
        push edi
        call fmv_c2
        add esp, 8
        popad
        cmp dword ptr [edi + 0x124], 2
        push 0x4031ae
        ret
    }
}

static void fmv_install(void)
{
    unsigned char jmp[7] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90 };
    if (!same(VA(0x4031a7), "83bf2401000002751c")) { dbg("fmv: unexpected bytes"); return; }
    *(int *)(jmp + 1) = (int)((unsigned char *)fmv_stub - (unsigned char *)0x4031ac);
    put(VA(0x4031a7), jmp, 7);
    dbg("fmv hook installed");
}

/* ------------------------------------------------------------------ UI scale
 * The game draws its GUI 1:1 and cnc-ddraw upscales the frame to the window/screen. The resolution the player picks
 * (Options > Video, saved in Saves\opt_glob.ini) is taken as the display size; NXSys2DApp::CApplication::SetDisplayMode
 * (0x479ad4) is given the largest standard mode that fits it divided by ui_scale (cx.ini), so the GUI comes out
 * ~ui_scale times larger. The options file keeps the display size: the options writer (0x4e3929) is given it in place
 * of the engine's mode, or the next start would scale the scaled mode again. ui_scale=1 turns this off. */
typedef int (__fastcall *setmode_t)(void *app, void *edx, int w, int h, int depth, int flag);
static setmode_t setmode_orig;
static double ui_scale = 1.0;
static int last_w, last_h;   /* the mode we last handed to the engine: re-applying it is not scaled again */
static int disp_w, disp_h;   /* the display size that mode was picked for */
static const short modes[][2] = {
    {800, 600}, {1024, 768}, {1152, 864}, {1280, 720}, {1280, 768}, {1280, 800}, {1280, 960}, {1280, 1024},
    {1360, 768}, {1366, 768}, {1440, 900}, {1600, 900}, {1600, 1200}, {1680, 1050}, {1920, 1080}, {1920, 1200}};

static void pick_mode(int tw, int th, int *w, int *h)   /* same rule as build.py pick_game_res */
{
    double mw = (disp_w = tw) / ui_scale + 1, mh = th / ui_scale + 1, ta = (double)tw / th;
    int i, best = -1, best_same = -1;
    for (i = 0; i < sizeof modes / sizeof modes[0]; i++) {
        double a = (double)modes[i][0] / modes[i][1];
        if (modes[i][0] > mw || modes[i][1] > mh) continue;
        if (best < 0 || modes[i][0] * modes[i][1] > modes[best][0] * modes[best][1]) best = i;
        if ((a - ta < 0.03 && ta - a < 0.03) &&
            (best_same < 0 || modes[i][0] * modes[i][1] > modes[best_same][0] * modes[best_same][1])) best_same = i;
    }
    if (best_same >= 0) best = best_same;
    if (best < 0) best = 0;
    disp_h = th;
    *w = last_w = modes[best][0]; *h = last_h = modes[best][1];
}

static int __fastcall setmode_hook(void *app, void *edx, int w, int h, int depth, int flag)
{
    int sw = w, sh = h;
    char b[96];
    if ((w > 800 || h > 600) && !(w == last_w && h == last_h)) pick_mode(w, h, &sw, &sh);
    _snprintf(b, sizeof b, "ui scale: display %dx%d -> game %dx%d", w, h, sw, sh); b[sizeof b - 1] = 0; dbg(b);
    return setmode_orig(app, edx, sw, sh, depth, flag);
}

/* startup: the app init (0x479f9f, thiscall) copies a settings block filled from opt_glob (width +0x14, height +0x18) */
typedef void *(__fastcall *appinit_t)(void *app, void *edx, int *cfg);
static appinit_t appinit_orig;
static void *__fastcall appinit_hook(void *app, void *edx, int *cfg)
{
    int w = cfg[0x14 / 4], h = cfg[0x18 / 4];
    if (w > 800 || h > 600) {
        char b[96];
        pick_mode(w, h, &cfg[0x14 / 4], &cfg[0x18 / 4]);
        _snprintf(b, sizeof b, "ui scale: startup %dx%d -> %dx%d", w, h, cfg[0x14 / 4], cfg[0x18 / 4]);
        b[sizeof b - 1] = 0; dbg(b);
    }
    return appinit_orig(app, edx, cfg);
}

/* ---- Renderer trace (Phase 9 research; cx.ini [cx] trace=1) ----
 * The engine draws every frame in software into one bitmap, the GUI manager's screen ([[0x557f44]+0x10]),
 * and converts it to the display in one call (present 0x479f24 -> BitmapToScreen 0x47c893).
 * All text goes through CFont::DrawChar 0x43a639. This logs, per second: frames, glyphs, and how many glyphs
 * went to the screen bitmap, with the screen's size and depth. Output: cx_trace.log next to Game.exe. */
typedef void (__fastcall *drawchar_t)(void *font, void *edx, unsigned *color, int bmp, int ch, int x, int y);
typedef void (__fastcall *present_t)(void *app, void *edx, int bmp, void *rect, int flags);
static drawchar_t drawchar_orig; static present_t present_orig;
static DWORD trace_t0; static int tr_frames, tr_glyphs, tr_screen, tr_other, tr_ow, tr_oh;
static int screen_bmp(void) { int m = *(int *)VA(0x557f44); return m ? *(int *)(m + 0x10) : 0; }
/* where is the bitmap `bmp` referenced from? scans the GUI manager, the display object and the top-level windows */
static int readable(int p, int n) { return p > 0x10000 && !IsBadReadPtr((void *)p, n); }
static void trace_where(int bmp)
{
    int gm = *(int *)VA(0x557f44), app = *(int *)VA(0x557f5c), objs[64][2], n = 0, i, o;
    if (readable(gm, 0x200)) { objs[n][0] = gm; objs[n++][1] = 0; }
    if (readable(app, 0x50) && readable(*(int *)(app + 0x4c), 12)) { objs[n][0] = *(int *)(*(int *)(app + 0x4c) + 8); objs[n++][1] = 1; }
    if (readable(gm, 0x18) && readable(*(int *)(gm + 0x14), 12)) {
        int node = *(int *)(*(int *)(gm + 0x14) + 8), c = 0;
        while (readable(node, 8) && c++ < 60 && n < 64) { objs[n][0] = *(int *)(node + 4); objs[n++][1] = 2; node = *(int *)node; }
    }
    { int sc = screen_bmp(), k;
      fprintf(trace_f, "glyph bitmap %08x (%dx%d), screen %08x:\n", bmp, *(int *)(bmp + 0x10), *(int *)(bmp + 0x14), sc);
      for (k = 0; k < 0x60; k += 4) fprintf(trace_f, "  +%02x  glyph %08x  screen %08x\n", k, *(int *)(bmp + k), sc ? *(int *)(sc + k) : 0); }
    for (i = 0; i < n; i++)
        for (o = 0; o < 0x400; o += 4)
            if (readable(objs[i][0] + o, 4) && *(int *)(objs[i][0] + o) == bmp)
                fprintf(trace_f, "  found in %s %08x (vtable %08x) at +0x%x\n",
                        objs[i][1] == 0 ? "gui manager" : objs[i][1] == 1 ? "display" : "window", objs[i][0],
                        readable(objs[i][0], 4) ? *(int *)objs[i][0] : 0, o);
    fflush(trace_f);
}
static int tr_where_done, tr_total;
static void __fastcall drawchar_hook(void *font, void *edx, unsigned *color, int bmp, int ch, int x, int y)
{
    tr_glyphs++;
    if (bmp && bmp == screen_bmp()) tr_screen++;
    else { tr_other++; if (bmp && !tr_where_done && ++tr_total > 2000) { tr_where_done = 1; trace_where(bmp); } if (bmp) { tr_ow = *(int *)(bmp + 0x10); tr_oh = *(int *)(bmp + 0x14); } }
    drawchar_orig(font, edx, color, bmp, ch, x, y);
}
static void __fastcall present_hook(void *app, void *edx, int bmp, void *rect, int flags)
{
    DWORD now = GetTickCount();
    tr_frames++;
    if (now - trace_t0 >= 1000) {
        fprintf(trace_f, "%6lu ms: %d frames, %d glyphs (%d on screen, %d elsewhere, last %dx%d); present bmp %dx%d %d B/px pitch %d%s\n",
                now - trace_t0, tr_frames, tr_glyphs, tr_screen, tr_other, tr_ow, tr_oh, *(int *)(bmp + 0x10), *(int *)(bmp + 0x14),
                *(int *)(bmp + 0x34), *(int *)(bmp + 0x30), bmp == screen_bmp() ? " (gui screen)" : "");
        fflush(trace_f);
        trace_t0 = now; tr_frames = tr_glyphs = tr_screen = tr_other = 0;
    }
    present_orig(app, edx, bmp, rect, flags);
}
static void trace_install(const char *ini)
{
    char path[MAX_PATH], *s;
    if (!GetPrivateProfileIntA("cx", "trace", 0, ini)) return;
    strcpy(path, ini);
    if ((s = strrchr(path, '\\')) != 0) strcpy(s + 1, "cx_trace.log");
    if (!(trace_f = fopen(path, "w"))) return;
    trace_t0 = GetTickCount();
    drawchar_orig = (drawchar_t)detour(VA(0x43a639), drawchar_hook, 6, "558becff7508");
    present_orig = (present_t)detour(VA(0x479f24), present_hook, 5, "8b414c85c0");
    fprintf(trace_f, "trace hooks %s\n", drawchar_orig && present_orig ? "installed" : "FAILED"); fflush(trace_f);
}

/* options writer: the engine's width/height getters (thiscall, app in ecx) report the display size instead */
typedef int (__fastcall *getsize_t)(void *app);
static int __fastcall opt_width(void *app)
{
    int w = ((getsize_t)VA(0x479be8))(app);
    return w == last_w && disp_w ? disp_w : w;
}
static int __fastcall opt_height(void *app)
{
    int h = ((getsize_t)VA(0x479bf9))(app);
    return h == last_h && disp_h ? disp_h : h;
}
static void patch_call(unsigned at, unsigned from, void *to)
{
    unsigned char c[5] = {0xe8};
    int rel = (int)from - (int)(at + 5);
    if (*(unsigned char *)VA(at) != 0xe8 || *(int *)VA(at + 1) != rel) { dbg("ui scale: options writer call site mismatch"); return; }
    rel = (int)(unsigned)to - (int)(at + 5);
    memcpy(c + 1, &rel, 4);
    put(VA(at), c, 5);
}

#include "render.c"

static void scale_install(const char *path)
{
    char v[16];
    GetPrivateProfileStringA("cx", "ui_scale", "1", v, sizeof v, path);
    ui_scale = atof(v);
    render_install(path);
    if (r_on) return;   /* the native renderer replaces ui_scale and the trace */
    trace_install(path);
    if (ui_scale <= 1.0) return;
    setmode_orig = (setmode_t)detour(VA(0x479ad4), setmode_hook, 7, "558bec53568bf1");
    appinit_orig = (appinit_t)detour(VA(0x479f9f), appinit_hook, 5, "5356578bd9");
    patch_call(0x4e3987, 0x479be8, opt_width);
    patch_call(0x4e39bb, 0x479bf9, opt_height);
    dbg(setmode_orig && appinit_orig ? "ui scale hooks installed" : "ui scale: hook failed");
}

static void install(void)
{
    unsigned slot = 0x4d8b02 + 6 * 4, def = 0x4d8af9, stub = (unsigned)cmd_c2_stub;
    char ini[MAX_PATH], *e;
    /* DPI-aware before cnc-ddraw makes its window: otherwise, with Windows display scaling above 100%, Windows
     * shows the game a smaller desktop (2048x1280 at 125% on 2560x1600) and stretches the result, blurring it */
    {
        typedef BOOL (WINAPI *ctx_t)(void *);
        ctx_t f = (ctx_t)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext");
        if (!f || !f((void *)-4)) SetProcessDPIAware();
    }
    GetModuleFileNameA(0, ini, MAX_PATH);
    if ((e = strrchr(ini, '\\')) != 0) strcpy(e + 1, "cx.ini");
    scale_install(ini);
    /* feature toggles (cx.ini [cx]); each defaults to on */
    if (GetPrivateProfileIntA("cx", "fmv", 1, ini)) fmv_install(); else dbg("fmv hook off (cx.ini)");
    if (!GetPrivateProfileIntA("cx", "c2_campaign", 1, ini)) { dbg("c2 campaign off (cx.ini)"); return; }
    c2_init();
    if (!c2_ok) return;
    if (*(unsigned *)VA(slot) != def) { dbg("c2: jump table slot in use"); c2_ok = 0; return; }
    switch_orig = (switch_t)detour(VA(0x4d4499), switch_hook, 6, "56578b7c240c");
    build_orig = (build_t)detour(VA(0x4d4b40), build_hook, 9, "558bec81ecf4030000");
    menus_orig = (menus_t)detour(VA(0x40212b), menus_hook, 7, "568bf1807e2800");
    if (!switch_orig || !build_orig || !menus_orig) { dbg("c2: hooks failed"); c2_ok = 0; return; }
    put(VA(slot), &stub, 4);
    dbg("c2 campaign hooks installed");
}

__declspec(dllexport) int cx_version(void) { return 1; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD why, LPVOID r)
{
    if (why == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        install();
    }
    return TRUE;
}
