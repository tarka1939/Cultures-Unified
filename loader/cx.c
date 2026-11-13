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
static void dbg(const char *s) { OutputDebugStringA("cx: "); OutputDebugStringA(s); OutputDebugStringA("\n"); }

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

static void install(void)
{
    unsigned slot = 0x4d8b02 + 6 * 4, def = 0x4d8af9, stub = (unsigned)cmd_c2_stub;
    fmv_install();
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
