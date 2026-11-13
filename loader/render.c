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

static int r_on;
static int r_mm_grow = 112;   /* cx.ini minimap=: picture size in GUI pixels is 112 * minimap */                 /* render=native */
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

/* ---- world at 1x ----
 * The world renderer (0x47d522, 0x994 bytes) has a zoom level at +0x78 (-1..1, set by 0x47dd84). At -1 it draws
 * into its own bitmap (+0x68) twice the size of its view (+0x64) and its draw (0x48250a) ends by shrinking one into
 * the other (0x40e09b, call at 0x48255e). With world=native that shrink is skipped for views on the screen: the
 * view is filled with a key colour and present puts the full-size world where the key still shows, so the world
 * is drawn 1:1 on the display while the GUI around and over it is scaled up. */
#define R_KEY 0xf81f
static int r_world = 1;                /* cx.ini world=native (default) | scaled */
static int r_wsrc, r_wx, r_wy, r_ww, r_wh;   /* this frame's full-size world and the view it belongs in */
static int r_last_ren;
typedef void (__fastcall *shrink_t)(int src, void *edx, int dst);
static void __fastcall r_shrink(int src, void *edx, int dst)
{
    int root = dst, depth = 0, scr = r_screen(), off, y;
    while (root && BF(root, 0x3c) == 1 && BF(root, 0x48) && depth++ < 8) root = BF(root, 0x48);
    if (!scr || root != scr || BF(scr, 0x34) != 2 || BF(src, 0x34) != 2 ||
        BF(src, 0x10) != 2 * BF(dst, 0x10) || BF(src, 0x14) != 2 * BF(dst, 0x14)) {
        ((shrink_t)VA(0x40e09b))(src, edx, dst);
        return;
    }
    off = (BF(dst, 0x28) - BF(scr, 0x28)) / 2;
    r_wsrc = src; r_wx = off % BF(scr, 0x30); r_wy = off / BF(scr, 0x30); r_ww = BF(dst, 0x10); r_wh = BF(dst, 0x14);
    for (y = 0; y < r_wh; y++) {
        unsigned short *d = (unsigned short *)BF(dst, 0x28) + y * BF(dst, 0x30);
        int x;
        for (x = 0; x < r_ww; x++) d[x] = R_KEY;
    }
}

/* the in-game map window starts zoomed out once; the zoom key still cycles from there */
static void r_default_zoom(void)
{
    int gui = *(int *)VA(0x56c2b4), win, ren;
    if (!gui || !readable(gui, 8)) return;
    win = BF(gui, 4);
    if (!win || !readable(win, 0x34) || BF(win, 0) != 0x4f9fd4) return;   /* map window vtable */
    ren = BF(win, 0x30);
    if (!ren || ren == r_last_ren || !readable(ren, 0x80)) return;
    r_last_ren = ren;
    if (BF(ren, 0x78) == 0) ((void (__fastcall *)(int, void *, int))VA(0x47dd84))(ren, 0, -1);
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

/* ---- minimap image at display resolution ----
 * The minimap picture is a child widget of the minimap window (class 0x4cf4a0, vtable 0x4fab20, kept at window +0x74,
 * 112x112 at 4,4). It draws into its own bitmap (+0x2c) from a cached terrain bitmap (+0x8040), and 0x4cf9a0 picks the
 * map tiles per pixel (+0x8038) from that bitmap's size. So the widget gets bitmaps r_mm_k times larger and the engine
 * draws the minimap with that much more detail. Its blit into the GUI (call to 0x4b9b4c at 0x4cf757) gets a
 * downsampled copy; present puts the full one on the display 1:1. Mouse positions inside it are scaled up before
 * 0x4d07f2 turns them into map positions (three calls in the event handler 0x4d08a2). */
struct mmhi { int widget, hi, lo; };
static struct mmhi r_mmhi[4];
static int r_hik;                                /* hi-res factor, = display pixels per GUI pixel in the minimap */

static struct mmhi *mm_find(int widget)
{
    int i;
    for (i = 0; i < 4; i++) if (r_mmhi[i].widget == widget && BF(widget, 0x2c) == r_mmhi[i].hi) return &r_mmhi[i];
    return 0;
}
static int new_bitmap(int w, int h, int fmt)
{
    int o = ((int (__cdecl *)(int))VA(0x4e7efd))(0x54);     /* operator new */
    return o ? ((int (__fastcall *)(int, void *, int, int, int))VA(0x40d2c7))(o, 0, w, h, fmt) : 0;
}
typedef void (__fastcall *wdraw_t)(int self, void *edx, int target, int *clip);
static void __fastcall r_mm_draw(int self, void *edx, int target, int *clip)
{
    int lo = BF(self, 0x2c), cache = BF(self, 0x8040);
    if (r_hik > 1 && !mm_find(self) && lo && cache && BF(lo, 0x34) == 2 && BF(cache, 0x34) == 2) {
        int i, w = BF(lo, 0x10) * r_hik, h = BF(lo, 0x14) * r_hik;
        int hi = new_bitmap(w, h, BF(lo, 4)), hc = new_bitmap(w, h, BF(cache, 4));
        for (i = 0; i < 4 && r_mmhi[i].widget && mm_find(r_mmhi[i].widget); i++) ;
        if (hi && hc && i < 4 && BF(hi, 0x34) == 2 && BF(hc, 0x34) == 2) {
            r_mmhi[i].widget = self; r_mmhi[i].hi = hi; r_mmhi[i].lo = lo;
            BF(self, 0x2c) = hi; BF(self, 0x8040) = hc;    /* the engine frees these with the widget */
            *(char *)(self + 0x8044) = 1;                  /* full redraw at the new size */
        }
    }
    ((wdraw_t)VA(0x4cf696))(self, edx, target, clip);
}
static void __fastcall r_mm_blit(int self, void *edx, int target, int *clip)
{
    struct mmhi *m = mm_find(self);
    if (m) {   /* hand the GUI a downsampled copy (box filter) */
        int hi = m->hi, lo = m->lo, k = r_hik, x, y, i, j, w = BF(lo, 0x10), h = BF(lo, 0x14);
        for (y = 0; y < h; y++) {
            unsigned short *d = (unsigned short *)BF(lo, 0x28) + y * BF(lo, 0x30);
            for (x = 0; x < w; x++) {
                int r = 0, g = 0, b = 0, n = k * k;
                for (j = 0; j < k; j++) {
                    const unsigned short *s = (unsigned short *)BF(hi, 0x28) + (y * k + j) * BF(hi, 0x30) + x * k;
                    for (i = 0; i < k; i++) { r += s[i] >> 11; g += (s[i] >> 5) & 63; b += s[i] & 31; }
                }
                d[x] = (unsigned short)(((r / n) << 11) | ((g / n) << 5) | (b / n));
            }
        }
        BF(self, 0x2c) = lo;
        ((wdraw_t)VA(0x4b9b4c))(self, edx, target, clip);
        BF(self, 0x2c) = hi;
        return;
    }
    ((wdraw_t)VA(0x4b9b4c))(self, edx, target, clip);
}
typedef void (__fastcall *mm2map_t)(int self, void *edx, int *pt, short *out);
static void __fastcall r_mm_tomap(int self, void *edx, int *pt, short *out)
{
    int p[2];
    p[0] = pt[0]; p[1] = pt[1];
    if (mm_find(self)) { p[0] = p[0] * r_hik + r_hik / 2; p[1] = p[1] * r_hik + r_hik / 2; }
    ((mm2map_t)VA(0x4d07f2))(self, edx, p, out);
}

/* The in-game minimap window (class at 0x4ca767, vtable 0x4fa924, kept at +0x44 of the GUI object [0x56c2b4]; a window
 * keeps x, y, w, h at +4..+0x10) is made r_mm_grow GUI pixels wider and taller by adjusting the rects its constructors
 * build (CRect calls at 0x4ca786 for the window, 0x4ca8e8 for the picture, five more for the buttons). Its frame is
 * one sprite, GUI bob 0x37 drawn 13 pixels above the window (0x4ca883, vtable slot 3); r_mm_frame draws it again in
 * pieces around the bigger picture: bar ends, tiled bar, corner, tiled rope column, column foot, black interior. */
static int __fastcall rect_window(int *r, void *edx, int x, int y, int w, int h)
{ (void)edx; r[0] = x; r[1] = y - r_mm_grow; r[2] = w + r_mm_grow; r[3] = h + r_mm_grow; return (int)r; }
static int __fastcall rect_picture(int *r, void *edx, int x, int y, int w, int h)
{ (void)edx; r[0] = x; r[1] = y; r[2] = w + r_mm_grow; r[3] = h + r_mm_grow; return (int)r; }
static int __fastcall rect_button(int *r, void *edx, int x, int y, int w, int h)
{ (void)edx; r[0] = x + r_mm_grow; r[1] = y; r[2] = w; r[3] = h; return (int)r; }

typedef void (__fastcall *bobdraw_t)(int lib, void *edx, int id, int target, int x, int y, int pal, int flags);
/* bob 0x37 piece (sx, sy, sw, sh), in sprite coordinates relative to the window (the bar starts at y -13), to
 * window position (dx, dy); (ax, ay) is the window on the root bitmap */
static void frame_piece(int root, int ax, int ay, int sx, int sy, int sw, int sh, int dx, int dy)
{
    int view[0x60 / 4], X = ax + dx, Y = ay + dy, W = sw, H = sh;
    if (X < 0) { W += X; sx -= X; dx -= X; X = 0; }
    if (Y < 0) { H += Y; sy -= Y; dy -= Y; Y = 0; }
    if (X + W > BF(root, 0x10)) W = BF(root, 0x10) - X;
    if (Y + H > BF(root, 0x14)) H = BF(root, 0x14) - Y;
    if (W <= 0 || H <= 0) return;
    ((void (__fastcall *)(int *, void *, int, int, int, int, int))VA(0x40d325))(view, 0, root, X, Y, W, H);
    ((bobdraw_t)VA(0x467b20))(*(int *)VA(0x56cb00), 0, 0x37, (int)view, -sx, -sy - 13, *(int *)VA(0x56cb1c), 0x80);
    ((void (__fastcall *)(int *))VA(0x40d362))(view);
}
static void __stdcall r_mm_frame_body(int target)
{
    int root = target, ax = 0, ay = 0, d = r_mm_grow, x, y;
    const int C = 104, T = 6, cw = 50;          /* column starts at sprite x 104, the bar ends at y 6 */
    while (BF(root, 0x48)) { ax += BF(root, 0x40); ay += BF(root, 0x44); root = BF(root, 0x48); }
    for (y = T; y < 120 + d; y += 114)          /* black interior */
        for (x = 0; x < C + d; x += C)
            frame_piece(root, ax, ay, 0, T, x + C <= C + d ? C : C + d - x, y + 114 <= 120 + d ? 114 : 120 + d - y, x, y);
    frame_piece(root, ax, ay, 0, -13, C, T + 13, 0, -13);                               /* bar, left end */
    for (x = C; x < C + d; x += 80)                                                      /* bar, tiled */
        frame_piece(root, ax, ay, 20, -13, x + 80 <= C + d ? 80 : C + d - x, T + 13, x, -13);
    frame_piece(root, ax, ay, C, -13, cw, 53, C + d, -13);                              /* corner, column top */
    for (y = 40; y < 100 + d; y += 60)                                                   /* column, tiled */
        frame_piece(root, ax, ay, C, 40, cw, y + 60 <= 100 + d ? 60 : 100 + d - y, C + d, y);
    frame_piece(root, ax, ay, C, 100, cw, 20, C + d, 100 + d);                          /* column foot */
}
/* thiscall (window; target, clip), ret 8 */
static void __declspec(naked) r_mm_frame(void)
{
    __asm {
        push dword ptr [esp + 4]
        call r_mm_frame_body
        ret 8
    }
}

/* debug (trace=1): save GUI bob 0x37 (the minimap frame) to cx_bob37.bmp */
static void r_dump_bob(int id)
{
    static int done;
    int lib = *(int *)VA(0x56cb00), scr = r_screen(), b, x, y, W = 200, H = 200;
    FILE *f;
    if (done || !trace_f || !lib || !scr) return;
    done = 1;
    b = new_bitmap(W, H, BF(scr, 4));
    if (!b || BF(b, 0x34) != 2) return;
    for (y = 0; y < H; y++) for (x = 0; x < W; x++) ((unsigned short *)BF(b, 0x28))[y * BF(b, 0x30) + x] = R_KEY;
    ((bobdraw_t)VA(0x467b20))(lib, 0, id, b, 20, 33, *(int *)VA(0x56cb1c), 0x80);
    f = fopen("cx_bob37.bmp", "wb");
    if (!f) return;
    {
        unsigned char hdr[54] = {'B', 'M'};
        int sz = 54 + W * H * 3;
        memcpy(hdr + 2, &sz, 4); hdr[10] = 54; hdr[14] = 40;
        memcpy(hdr + 18, &W, 4); memcpy(hdr + 22, &H, 4); hdr[26] = 1; hdr[28] = 24;
        fwrite(hdr, 1, 54, f);
        for (y = H - 1; y >= 0; y--)
            for (x = 0; x < W; x++) {
                unsigned short p = ((unsigned short *)BF(b, 0x28))[y * BF(b, 0x30) + x];
                unsigned char c[3] = {(unsigned char)((p & 31) << 3), (unsigned char)(((p >> 5) & 63) << 2), (unsigned char)((p >> 11) << 3)};
                fwrite(c, 1, 3, f);
            }
    }
    fclose(f);
}

/* the minimap picture at display resolution, over the scaled frame */
static void r_minimap(int w, int h, int dx, int dy, int dw, int dh)
{
    int gui = *(int *)VA(0x56c2b4), mm, wg, Y;
    struct mmhi *m;
    if (r_hik != 2 || dw != 2 * w || dh != 2 * h || !readable(gui, 0x48)) return;
    mm = BF(gui, 0x44);
    if (!readable(mm, 0x78) || BF(mm, 0) != 0x4fa924) return;
    r_dump_bob(0x37);
    wg = BF(mm, 0x74);
    m = readable(wg, 0x14) && BF(wg, 0) == 0x4fab20 ? mm_find(wg) : 0;
    if (m) {
        int hi = m->hi, hw = BF(hi, 0x10), hh = BF(hi, 0x14);
        int X0 = dx + 2 * (BF(mm, 4) + BF(wg, 4)), Y0 = dy + 2 * (BF(mm, 8) + BF(wg, 8));
        if (X0 < 0 || Y0 < 0 || X0 + hw > phys_w() || Y0 + hh > phys_h()) return;
        for (Y = 0; Y < hh; Y++)
            memcpy(r_px + (Y0 + Y) * phys_w() + X0, (unsigned short *)BF(hi, 0x28) + Y * BF(hi, 0x30), hw * 2);
    }
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
    if (r_wsrc && dw == 2 * w && dh == 2 * h && r_wx + r_ww <= w && r_wy + r_wh <= h) {   /* the world, 1:1 */
        unsigned short *hs = (unsigned short *)BF(r_wsrc, 0x28);
        int hp = BF(r_wsrc, 0x30);
        for (y = 0; y < r_wh; y++) {
            unsigned short *s_row = src + (r_wy + y) * sp + r_wx;
            unsigned short *d0 = r_px + (dy + 2 * (r_wy + y)) * pw + dx + 2 * r_wx, *d1 = d0 + pw;
            unsigned short *h0 = hs + 2 * y * hp, *h1 = h0 + hp;
            for (x = 0; x < r_ww; x++)
                if (s_row[x] == R_KEY) {
                    d0[2 * x] = h0[2 * x]; d0[2 * x + 1] = h0[2 * x + 1];
                    d1[2 * x] = h1[2 * x]; d1[2 * x + 1] = h1[2 * x + 1];
                }
        }
    }
    r_wsrc = 0;
    if (r_world) r_default_zoom();
    r_minimap(w, h, dx, dy, dw, dh);
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
    i = GetPrivateProfileIntA("cx", "minimap", 2, ini);
    r_mm_grow = i > 1 ? 112 * (i - 1) : 0;
    if (r_scale == 2 && *(unsigned *)VA(0x4fab2c) == 0x4cf696 && *(unsigned *)VA(0x4fa930) == 0x4ca883 &&
        patch_call2(0x4cf757, 0x4b9b4c, r_mm_blit)) {
        static const unsigned mmsites[] = {0x4d0977, 0x4d09c3, 0x4d0ad3};
        static const unsigned btn[] = {0x4ca930, 0x4ca989, 0x4ca9e8, 0x4caa44, 0x4caaa0};
        void *p = r_mm_draw;
        for (i = 0; i < 3; i++) patch_call2(mmsites[i], 0x4d07f2, r_mm_tomap);
        put(VA(0x4fab2c), &p, 4);
        r_hik = 2;
        if (r_mm_grow) {
            p = r_mm_frame;
            put(VA(0x4fa930), &p, 4);
            patch_call2(0x4ca786, 0x410fe3, rect_window);
            patch_call2(0x4ca8e8, 0x410fe3, rect_picture);
            for (i = 0; i < 5; i++) patch_call2(btn[i], 0x410fe3, rect_button);
        }
    }
    GetPrivateProfileStringA("cx", "world", "native", v, sizeof v, ini);
    r_world = !_stricmp(v, "native") && r_scale == 2;
    if (r_world && !patch_call2(0x48255e, 0x40e09b, r_shrink)) { r_world = 0; dbg("render: world shrink call not found"); }
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
