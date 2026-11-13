/* Native-resolution renderer (Phase 9, milestone 1), included by cx.c. On with cx.ini [cx] render=native.
 *
 * DirectDraw runs at the display size from opt_glob.ini (physical, e.g. 2560x1600), so cnc-ddraw does no scaling.
 * The engine is told a smaller *logical* size (physical / scale, cx.ini [cx] scale, default 2): the 16 calls of the
 * width/height getters (0x479be8/0x479bf9) that build the GUI and clip the mouse get our getter instead. The
 * engine then draws its frame at the logical size as before.
 *
 * Present (0x479f24) scales that frame (nearest neighbour) into a physical-size 16-bit DIB and hands the DIB to
 * the original present as if it were the engine's bitmap. Text drawn into the frame (CFont::DrawChar 0x43a639)
 * is not drawn there: each glyph is recorded and drawn into the DIB afterwards with a TrueType font, at physical
 * resolution, at the original glyph positions.
 *
 * The mouse: the cursor API calls in the mouse code (GetCursorPos/SetCursorPos/ClipCursor) are redirected to
 * wrappers that convert between physical and logical coordinates.
 *
 * Bitmap (NXBasics::CBitmap) fields used: +0x10/+0x14 w/h, +0x18/+0x1c w-1/h-1, +0x24/+0x28 pixels,
 * +0x30 pitch in pixels, +0x34 bytes per pixel, +0x38 pitch in bytes, +0x3c view flag, +0x48 view parent. */

#define BF(b, off) (*(int *)((char *)(b) + (off)))

static int r_on;                 /* render=native */
static double r_scale = 2.0;     /* physical / logical */
static char r_face[64] = "Georgia";
static int r_weight = FW_BOLD;
static int r_both;               /* debug: draw the original glyphs too */

static int disp(void)            /* CDDDisplay: +0 hwnd, +4 w, +8 h, +0xc depth */
{
    int app = *(int *)VA(0x557f5c), d;
    if (!app || !(d = *(int *)(app + 0x4c))) return 0;
    return *(int *)(d + 8);
}
static int phys_w(void) { int d = disp(); return d ? BF(d, 4) : 0; }
static int phys_h(void) { int d = disp(); return d ? BF(d, 8) : 0; }
static int log_w(void) { return (int)(phys_w() / r_scale + 0.5); }
static int log_h(void) { return (int)(phys_h() / r_scale + 0.5); }
static int __fastcall logical_width(void *app) { (void)app; return log_w(); }
static int __fastcall logical_height(void *app) { (void)app; return log_h(); }

/* ---- mouse ---- */
typedef BOOL (WINAPI *getcur_t)(POINT *);
typedef BOOL (WINAPI *setcur_t)(int, int);
typedef BOOL (WINAPI *clipcur_t)(const RECT *);
static getcur_t real_getcur; static setcur_t real_setcur; static clipcur_t real_clipcur;

static void origin(POINT *o)
{
    int d = disp();
    o->x = o->y = 0;
    if (d) ClientToScreen(*(HWND *)d, o);
}
static int to_log(int v, int o, int p, int l) { return p ? o + MulDiv(v - o, l, p) : v; }
static int to_phys(int v, int o, int p, int l) { return l ? o + MulDiv(v - o, p, l) : v; }

static BOOL WINAPI my_getcur(POINT *pt)
{
    POINT o;
    BOOL r = real_getcur(pt);
    if (r) {
        origin(&o);
        pt->x = to_log(pt->x, o.x, phys_w(), log_w());
        pt->y = to_log(pt->y, o.y, phys_h(), log_h());
    }
    return r;
}
static BOOL WINAPI my_setcur(int x, int y)
{
    POINT o;
    origin(&o);
    return real_setcur(to_phys(x, o.x, phys_w(), log_w()), to_phys(y, o.y, phys_h(), log_h()));
}
static BOOL WINAPI my_clipcur(const RECT *r)
{
    POINT o;
    RECT p;
    if (!r) return real_clipcur(0);
    origin(&o);
    p.left = to_phys(r->left, o.x, phys_w(), log_w());
    p.top = to_phys(r->top, o.y, phys_h(), log_h());
    p.right = to_phys(r->right, o.x, phys_w(), log_w());
    p.bottom = to_phys(r->bottom, o.y, phys_h(), log_h());
    return real_clipcur(&p);
}
static void *p_getcur = my_getcur, *p_setcur = my_setcur, *p_clipcur = my_clipcur;

/* `call dword ptr [iat]` at `at` -> `call dword ptr [slot]` */
static int patch_icall(unsigned at, unsigned iat, void **slot)
{
    unsigned char c[6] = {0xff, 0x15};
    if (*(unsigned short *)VA(at) != 0x15ff || *(unsigned *)VA(at + 2) != iat) return 0;
    *(void ***)(c + 2) = slot;
    put(VA(at), c, 6);
    return 1;
}
static int patch_call2(unsigned at, unsigned from, void *to)   /* like patch_call, reports success */
{
    unsigned char c[5] = {0xe8};
    int rel = (int)from - (int)(at + 5);
    if (*(unsigned char *)VA(at) != 0xe8 || *(int *)VA(at + 1) != rel) return 0;
    rel = (int)(unsigned)to - (int)(at + 5);
    memcpy(c + 1, &rel, 4);
    put(VA(at), c, 5);
    return 1;
}

/* ---- text ---- */
struct glyph { int font, key, ch, x, y, adv; RECT clip; };   /* adv: the bitmap font's advance */
static struct glyph glyphs[8192];
static int nglyphs;

struct tcol { int font, key; COLORREF fill, edge; int has_edge; };
static struct tcol tcols[64];
static int ntcols;

static COLORREF rgb565(unsigned short p)
{
    int r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    return RGB(r * 255 / 31, g * 255 / 63, b * 255 / 31);
}

/* the colours a font+palette draws with: draw a sample glyph into a scratch bitmap with the original DrawChar */
static struct tcol *text_colour(void *font, unsigned *color, int view)
{
    static unsigned short px[48 * 48];
    static char fake[0x100];
    int i, key = color ? (int)*color : 0, n = 0, best = -1;
    unsigned short vals[64]; int cnt[64];
    struct tcol *t;
    for (i = 0; i < ntcols; i++)
        if (tcols[i].font == (int)font && tcols[i].key == key) return &tcols[i];
    t = &tcols[ntcols < 64 ? ntcols++ : 63];
    t->font = (int)font; t->key = key; t->fill = RGB(255, 255, 255); t->edge = 0; t->has_edge = 0;
    for (i = 0; i < 48 * 48; i++) px[i] = 0xf81f;   /* magenta: never a font colour */
    memcpy(fake, (void *)view, 0x60);
    BF(fake, 0x10) = 48; BF(fake, 0x14) = 48; BF(fake, 0x18) = 47; BF(fake, 0x1c) = 47;
    BF(fake, 0x24) = BF(fake, 0x28) = (int)px; BF(fake, 0x30) = 48; BF(fake, 0x38) = 96;
    BF(fake, 0x3c) = 0; BF(fake, 0x48) = 0;
    drawchar_orig(font, 0, color, (int)fake, 'H', 8, 8);
    for (i = 0; i < 48 * 48; i++) {
        int j;
        if (px[i] == 0xf81f) continue;
        for (j = 0; j < n && vals[j] != px[i]; j++) ;
        if (j == n) { if (n == 64) continue; vals[n] = px[i]; cnt[n++] = 0; }
        cnt[j]++;
    }
    for (i = 0; i < n; i++) if (best < 0 || cnt[i] > cnt[best]) best = i;
    if (best >= 0) {
        int dark = -1, lum_best = 0, lf;
        t->fill = rgb565(vals[best]);
        lf = GetRValue(t->fill) * 3 + GetGValue(t->fill) * 6 + GetBValue(t->fill);
        for (i = 0; i < n; i++) {      /* edge: the darkest colour used for a fair share of pixels */
            COLORREF c = rgb565(vals[i]);
            int lum = GetRValue(c) * 3 + GetGValue(c) * 6 + GetBValue(c);
            if (i != best && cnt[i] * 6 >= cnt[best] && lum < lf - 300 && (dark < 0 || lum < lum_best)) { dark = i; lum_best = lum; }
        }
        if (dark >= 0) { t->edge = rgb565(vals[dark]); t->has_edge = 1; }
    }
    return t;
}

static int r_screen(void) { return screen_bmp(); }

/* DrawChar in native mode: glyphs headed for the screen are recorded, others drawn as before */
static void __fastcall r_drawchar(void *font, void *edx, unsigned *color, int bmp, int ch, int x, int y)
{
    int root = bmp, depth = 0, scr = r_screen(), off, bpp, ox, oy;
    struct glyph *g;
    while (root && BF(root, 0x3c) == 1 && BF(root, 0x48) && depth++ < 8) root = BF(root, 0x48);
    if (!bmp || !scr || root != scr || BF(scr, 0x34) != 2 || nglyphs >= sizeof glyphs / sizeof glyphs[0]) {
        drawchar_orig(font, edx, color, bmp, ch, x, y);
        return;
    }
    bpp = BF(scr, 0x34);
    off = (BF(bmp, 0x28) - BF(scr, 0x28)) / bpp;
    ox = off % BF(scr, 0x30); oy = off / BF(scr, 0x30);
    if (r_both) drawchar_orig(font, edx, color, bmp, ch, x, y);
    g = &glyphs[nglyphs++];
    g->font = (int)font; g->key = text_colour(font, color, bmp) - tcols; g->ch = ch & 0xff;
    g->adv = ((int (__fastcall *)(void *, void *, int))VA(0x43a65c))(font, 0, ch & 0xff);   /* CFont char width */
    g->x = ox + x; g->y = oy + y;
    if (trace_f && tr_total++ < 40)
        fprintf(trace_f, "glyph '%c' at %d,%d in view %dx%d (+%d,+%d) bpp %d screen %dx%d pitch %d/%d\n", ch, x, y,
                BF(bmp, 0x10), BF(bmp, 0x14), ox, oy, bpp, BF(scr, 0x10), BF(scr, 0x14), BF(scr, 0x30), BF(bmp, 0x30));
    g->clip.left = ox; g->clip.top = oy; g->clip.right = ox + BF(bmp, 0x10); g->clip.bottom = oy + BF(bmp, 0x14);
}

/* ---- present ---- */
static HDC r_dc; static HBITMAP r_dib; static unsigned short *r_px; static int r_w, r_h;
static int *r_xmap; static char r_fake[0x100];
static HFONT r_fonts[4]; static int r_font_ptr[4], r_font_h[4];

static int r_alloc(int w, int h)
{
    struct { BITMAPINFOHEADER h; DWORD masks[3]; } bi;
    if (w == r_w && h == r_h && r_dib) return 1;
    if (r_dib) { DeleteObject(r_dib); r_dib = 0; }
    if (!r_dc) r_dc = CreateCompatibleDC(0);
    memset(&bi, 0, sizeof bi);
    bi.h.biSize = sizeof bi.h; bi.h.biWidth = w; bi.h.biHeight = -h; bi.h.biPlanes = 1; bi.h.biBitCount = 16;
    bi.h.biCompression = BI_BITFIELDS; bi.masks[0] = 0xf800; bi.masks[1] = 0x07e0; bi.masks[2] = 0x001f;
    r_dib = CreateDIBSection(r_dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, (void **)&r_px, 0, 0);
    if (!r_dib) return 0;
    SelectObject(r_dc, r_dib);
    SetBkMode(r_dc, TRANSPARENT);
    SetTextAlign(r_dc, TA_TOP | TA_LEFT);
    r_w = w; r_h = h;
    free(r_xmap); r_xmap = (int *)malloc(sizeof(int) * w);
    return 1;
}

static HFONT r_font_for(int font)
{
    int i, lh = font ? BF(font, 8) : 10, h;
    for (i = 0; i < 4 && r_font_ptr[i]; i++) if (r_font_ptr[i] == font) return r_fonts[i];
    if (i == 4) return r_fonts[0];
    h = (int)(lh * r_scale * 1.05 + 0.5);
    r_fonts[i] = CreateFontA(-h, 0, 0, 0, r_weight, 0, 0, 0, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH, r_face);
    r_font_ptr[i] = font; r_font_h[i] = h;
    return r_fonts[i];
}

/* Text is drawn a word at a time: consecutive glyphs that touch in the bitmap font (x + advance = next x) and share
 * font, colour and clip. Each word gets the TrueType font's own spacing, centred in the box the bitmap font gave it
 * and squeezed if it would not fit, so layout and alignment stay the engine's. */
static int same_word(const struct glyph *a, const struct glyph *b)
{
    int gap = b->x - (a->x + a->adv);
    return a->font == b->font && a->key == b->key && a->y == b->y && gap >= -1 && gap <= 1 &&
           !memcmp(&a->clip, &b->clip, sizeof a->clip);
}

static void r_text(int dx, int dy, double s)
{
    int i = 0, j, k, n, e = s >= 2 ? (int)(s / 2) : 1;
    WCHAR wbuf[256]; char abuf[256]; INT dxs[256];
    while (i < nglyphs) {
        struct glyph *g = &glyphs[i];
        struct tcol *t = &tcols[g->key];
        RECT c;
        SIZE sz;
        int X, Y, box, nat = 0;
        for (j = i + 1; j < nglyphs && j - i < 255 && same_word(&glyphs[j - 1], &glyphs[j]); j++) ;
        n = j - i;
        for (k = 0; k < n; k++) abuf[k] = (char)glyphs[i + k].ch;
        MultiByteToWideChar(1252, 0, abuf, n, wbuf, n);
        SelectObject(r_dc, r_font_for(g->font));
        for (k = 0; k < n; k++) {
            GetTextExtentPoint32W(r_dc, wbuf + k, 1, &sz);
            dxs[k] = sz.cx; nat += sz.cx;
        }
        box = (int)((glyphs[j - 1].x + glyphs[j - 1].adv - g->x) * s + 0.5);
        if (nat > box && nat > 0) {   /* too wide: squeeze evenly */
            int acc = 0, sum = 0;
            for (k = 0; k < n; k++) { acc += dxs[k]; dxs[k] = MulDiv(acc, box, nat) - sum; sum += dxs[k]; }
            nat = box;
        }
        X = dx + (int)(g->x * s) + (box - nat) / 2;
        Y = dy + (int)(g->y * s);
        c.left = dx + (int)(g->clip.left * s); c.top = dy + (int)(g->clip.top * s);
        c.right = dx + (int)(g->clip.right * s); c.bottom = dy + (int)(g->clip.bottom * s);
        if (t->has_edge) {
            SetTextColor(r_dc, t->edge);
            ExtTextOutW(r_dc, X + e, Y + e, ETO_CLIPPED, &c, wbuf, n, dxs);
        }
        SetTextColor(r_dc, t->fill);
        ExtTextOutW(r_dc, X, Y, ETO_CLIPPED, &c, wbuf, n, dxs);
        i = j;
    }
    GdiFlush();
}

static void __fastcall r_present(void *app, void *edx, int bmp, void *rect, int flags)
{
    int pw = phys_w(), ph = phys_h(), w, h, dw, dh, dx, dy, x, y, sp;
    double s = r_scale;
    unsigned short *src;
    if (!bmp || rect || flags || BF(bmp, 0x34) != 2 || !pw || !ph || !r_alloc(pw, ph)) {
        nglyphs = 0;
        present_orig(app, edx, bmp, rect, flags);
        return;
    }
    w = BF(bmp, 0x10); h = BF(bmp, 0x14); sp = BF(bmp, 0x30); src = (unsigned short *)BF(bmp, 0x28);
    if (trace_f && tr_frames % 300 == 0) {
        int d = disp(), *sf = d ? *(int **)(d + 0x20) : 0, *pr = d ? *(int **)(d + 0x1c) : 0;
        unsigned desc[31] = {0}, desc2[31] = {0}; RECT rc = {0};
        desc[0] = desc2[0] = 124;
        if (sf) ((int (__stdcall *)(void *, void *))(*(void ***)sf)[22])(sf, desc);
        if (pr) ((int (__stdcall *)(void *, void *))(*(void ***)pr)[22])(pr, desc2);
        if (d) GetClientRect(*(HWND *)d, &rc);
        fprintf(trace_f, "back surface %ux%u, primary %ux%u, window client %dx%d, screen %dx%d, windowed flag %d" "\n",
                desc[3], desc[2], desc2[3], desc2[2], rc.right, rc.bottom, GetSystemMetrics(0), GetSystemMetrics(1), d ? *(char *)(d + 0x34) : -1);
    }
    if (trace_f && tr_frames++ % 300 == 0) { fprintf(trace_f, "present %dx%d pitch %d -> %dx%d, %d glyphs\n", w, h, sp, pw, ph, nglyphs); fflush(trace_f); }
    if (w == log_w() && h == log_h()) { dw = pw; dh = ph; s = (double)pw / w; }   /* the frame: fill the display */
    else { dw = (int)(w * s); dh = (int)(h * s); if (dw > pw) dw = pw; if (dh > ph) dh = ph; }   /* menus, FMV */
    dx = (pw - dw) / 2; dy = (ph - dh) / 2;
    if (dw != pw || dh != ph) memset(r_px, 0, pw * ph * 2);
    for (x = 0; x < dw; x++) r_xmap[x] = (int)((long long)x * w / dw);
    for (y = 0; y < dh; y++) {
        unsigned short *d = r_px + (dy + y) * pw + dx, *s_row = src + (int)((long long)y * h / dh) * sp;
        for (x = 0; x < dw; x++) d[x] = s_row[r_xmap[x]];
    }
    r_text(dx, dy, (double)dw / w);
    nglyphs = 0;
    memcpy(r_fake, (void *)bmp, 0x60);
    BF(r_fake, 0x10) = pw; BF(r_fake, 0x14) = ph; BF(r_fake, 0x18) = pw - 1; BF(r_fake, 0x1c) = ph - 1;
    BF(r_fake, 0x24) = BF(r_fake, 0x28) = (int)r_px; BF(r_fake, 0x30) = pw; BF(r_fake, 0x38) = pw * 2;
    BF(r_fake, 0x3c) = 0; BF(r_fake, 0x48) = 0;
    present_orig(app, edx, (int)r_fake, rect, flags);
}

static void render_install(const char *ini)
{
    static const unsigned wsites[] = {0x40186d, 0x4018f6, 0x401c78, 0x402181, 0x402638, 0x40301a, 0x4035c5, 0x47a551};
    static const unsigned hsites[] = {0x401865, 0x4018ee, 0x401c70, 0x402179, 0x402630, 0x403011, 0x4035bd, 0x47a534};
    static const unsigned getcur[] = {0x47a2a3}, setcur[] = {0x47a325, 0x47a3b2, 0x47a477},
                          clipcur[] = {0x47a33d, 0x47a48f, 0x47a563};
    char v[16];
    int i, ok = 1;
    HMODULE u = GetModuleHandleA("user32.dll");
    GetPrivateProfileStringA("cx", "render", "", v, sizeof v, ini);
    if (_stricmp(v, "native")) return;
    GetPrivateProfileStringA("cx", "scale", "2", v, sizeof v, ini);
    r_scale = atof(v); if (r_scale < 1) r_scale = 1;
    GetPrivateProfileStringA("cx", "font", r_face, r_face, sizeof r_face, ini);
    r_weight = GetPrivateProfileIntA("cx", "font_weight", r_weight, ini);
    r_both = GetPrivateProfileIntA("cx", "text_both", 0, ini);
    /* check every site first, so nothing is half patched */
    for (i = 0; i < 8; i++)
        ok &= *(unsigned char *)VA(wsites[i]) == 0xe8 && *(int *)VA(wsites[i] + 1) == 0x479be8 - (int)(wsites[i] + 5)
           && *(unsigned char *)VA(hsites[i]) == 0xe8 && *(int *)VA(hsites[i] + 1) == 0x479bf9 - (int)(hsites[i] + 5);
    for (i = 0; i < 1; i++) ok &= *(unsigned *)VA(getcur[i] + 2) == 0x4f6264;
    for (i = 0; i < 3; i++) ok &= *(unsigned *)VA(setcur[i] + 2) == 0x4f6258 && *(unsigned *)VA(clipcur[i] + 2) == 0x4f6254;
    if (!ok || !u) { dbg("render: unexpected bytes, native renderer off"); return; }
    real_getcur = (getcur_t)GetProcAddress(u, "GetCursorPos");
    real_setcur = (setcur_t)GetProcAddress(u, "SetCursorPos");
    real_clipcur = (clipcur_t)GetProcAddress(u, "ClipCursor");
    drawchar_orig = (drawchar_t)detour(VA(0x43a639), r_drawchar, 6, "558becff7508");
    present_orig = (present_t)detour(VA(0x479f24), r_present, 5, "8b414c85c0");
    if (!drawchar_orig || !present_orig) { dbg("render: hook failed"); return; }
    for (i = 0; i < 8; i++) { patch_call2(wsites[i], 0x479be8, logical_width); patch_call2(hsites[i], 0x479bf9, logical_height); }
    patch_icall(getcur[0], 0x4f6264, &p_getcur);
    for (i = 0; i < 3; i++) { patch_icall(setcur[i], 0x4f6258, &p_setcur); patch_icall(clipcur[i], 0x4f6254, &p_clipcur); }
    r_on = 1;
    if (GetPrivateProfileIntA("cx", "trace", 0, ini)) {
        char path[MAX_PATH], *e;
        strcpy(path, ini);
        if ((e = strrchr(path, '\\')) != 0) strcpy(e + 1, "cx_trace.log");
        trace_f = fopen(path, "w");
    }
    dbg("native renderer on");
}
