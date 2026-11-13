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

/* the enlarged minimap, as present last drew it: its logical rect and where it is on the display */
static int r_mm_scale = 2;                       /* cx.ini minimap=: size relative to the rest of the GUI */
static int r_mm_x, r_mm_y, r_mm_k;               /* logical origin, display pixels per logical pixel (0 = off) */
static RECT r_mm_phys;                           /* client pixels */

static BOOL WINAPI my_getcur(POINT *pt)
{
    POINT o;
    BOOL r = real_getcur(pt);
    if (r) {
        int cx, cy;
        origin(&o);
        cx = pt->x - o.x; cy = pt->y - o.y;
        if (r_mm_k && cx >= r_mm_phys.left && cx < r_mm_phys.right && cy >= r_mm_phys.top && cy < r_mm_phys.bottom) {
            pt->x = o.x + r_mm_x + (cx - r_mm_phys.left) / r_mm_k;
            pt->y = o.y + r_mm_y + (cy - r_mm_phys.top) / r_mm_k;
            return r;
        }
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

/* ---- smooth upscaling for enlarged GUI art ----
 * Catmull-Rom (bicubic) by an integer factor k. Key-coloured (transparent) pixels stay transparent: a display pixel
 * whose nearest source pixel is the key is skipped, and key pixels don't take part in the filter. */
#define FLOORI(v) ((v) < 0 ? (int)(v) - 1 : (int)(v))
static void cr_weights(double t, double *w)
{
    double t2 = t * t, t3 = t2 * t;
    w[0] = (-t3 + 2 * t2 - t) / 2; w[1] = (3 * t3 - 5 * t2 + 2) / 2;
    w[2] = (-3 * t3 + 4 * t2 + t) / 2; w[3] = (t3 - t2) / 2;
}
/* draw source rect (sx, sy, sw, sh) of src scaled by k; display rows y0..y0+rows of the scaled rect go to dst */
static void r_upscale(const unsigned short *src, int sp, int sx, int sy, int sw, int sh, int k,
                      unsigned short *dst, int dp, int y0, int rows, int cols)
{
    double wt[16][4];
    int X, Y, i, j;
    if (k > 16) return;
    for (i = 0; i < k; i++) cr_weights((i + 0.5) / k - 0.5 + (i + 0.5 < k / 2.0 ? 1 : 0), wt[i]);
    for (Y = 0; Y < rows; Y++) {
        int yy = y0 + Y, by = FLOORI((yy + 0.5) / k - 0.5), py = yy % k;
        const double *wy = wt[py];
        unsigned short *d = dst + Y * dp;
        for (X = 0; X < cols; X++) {
            int bx = FLOORI((X + 0.5) / k - 0.5), px = X % k, nx = X / k, ny = yy / k;
            const double *wx = wt[px];
            double r = 0, g = 0, b = 0, ws = 0;
            if (src[(sy + ny) * sp + sx + nx] == R_KEY) continue;
            for (j = 0; j < 4; j++) {
                int cy = by - 1 + j;
                if (cy < 0) cy = 0;
                if (cy >= sh) cy = sh - 1;
                for (i = 0; i < 4; i++) {
                    int cx = bx - 1 + i;
                    unsigned short p;
                    double w;
                    if (cx < 0) cx = 0;
                    if (cx >= sw) cx = sw - 1;
                    p = src[(sy + cy) * sp + sx + cx];
                    if (p == R_KEY) continue;
                    w = wx[i] * wy[j];
                    r += w * ((p >> 11) & 31); g += w * ((p >> 5) & 63); b += w * (p & 31); ws += w;
                }
            }
            if (ws <= 0.01) { d[X] = src[(sy + ny) * sp + sx + nx]; continue; }
            r /= ws; g /= ws; b /= ws;
            if (r < 0) r = 0;
            if (r > 31) r = 31;
            if (g < 0) g = 0;
            if (g > 63) g = 63;
            if (b < 0) b = 0;
            if (b > 31) b = 31;
            d[X] = (unsigned short)(((int)(r + 0.5) << 11) | ((int)(g + 0.5) << 5) | (int)(b + 0.5));
        }
    }
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
 * keeps x, y, w, h at +4..+0x10) is drawn again over the scaled frame, r_mm_scale times larger and anchored at its
 * bottom-left corner. my_getcur maps the cursor inside it back to the window. */
static void r_minimap(const unsigned short *src, int sp, int w, int h, int dx, int dy, int dw, int dh)
{
    int gui = *(int *)VA(0x56c2b4), mm, mx, my, mw, mh, k, L, T, W, H, X, Y;
    r_mm_k = 0;
    if (r_mm_scale <= 1 || dw != 2 * w || dh != 2 * h || !readable(gui, 0x48)) return;
    mm = BF(gui, 0x44);
    if (!readable(mm, 0x14) || BF(mm, 0) != 0x4fa924) return;
    mx = BF(mm, 4); my = BF(mm, 8); mw = BF(mm, 0xc); mh = BF(mm, 0x10);
    if (mw <= 0 || mh <= 0 || mx < 0 || my < 0 || mx + mw > w || my + mh > h) return;
    k = 2 * r_mm_scale; W = mw * k; H = mh * k;
    L = dx + 2 * mx; T = dy + 2 * (my + mh) - H;
    if (T < 0) { H += T; T = 0; }
    if (L + W > dx + dw) W = dx + dw - L;
    r_upscale(src, sp, mx, my, mw, mh, k, r_px + T * phys_w() + L, phys_w(), mh * k - H, H, W);

    if (k == r_hik) {   /* the minimap picture, 1:1 */
        int wg = BF(mm, 0x74), top = dy + 2 * (my + mh) - mh * k;
        struct mmhi *m = readable(wg, 0x14) && BF(wg, 0) == 0x4fab20 ? mm_find(wg) : 0;
        if (m) {
            int hi = m->hi, hw = BF(hi, 0x10), hh = BF(hi, 0x14), X0 = L + BF(wg, 4) * k, Y0 = top + BF(wg, 8) * k;
            for (Y = 0; Y < hh; Y++) {
                if (Y0 + Y < T || Y0 + Y >= T + H) continue;
                if (X0 < 0 || X0 + hw > phys_w()) break;
                memcpy(r_px + (Y0 + Y) * phys_w() + X0, (unsigned short *)BF(hi, 0x28) + Y * BF(hi, 0x30), hw * 2);
            }
        }
    }
    r_mm_x = mx; r_mm_y = my + (mh * k - H) / k; r_mm_k = k;
    r_mm_phys.left = L; r_mm_phys.top = T; r_mm_phys.right = L + W; r_mm_phys.bottom = T + H;
}

static void __fastcall r_present(void *app, void *edx, int bmp, void *rect, int flags)
{
    int pw = phys_w(), ph = phys_h(), w, h, dw, dh, dx, dy, x, y, sp;
    double s = r_scale;
    unsigned short *src;
    if (!bmp || rect || flags || BF(bmp, 0x34) != 2 || !pw || !ph || !r_alloc(pw, ph)) {
        nglyphs = 0; r_mm_k = 0;
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
    r_minimap(src, sp, w, h, dx, dy, dw, dh);
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
    r_mm_scale = GetPrivateProfileIntA("cx", "minimap", 2, ini);
    if (r_scale == 2 && r_mm_scale >= 1 && *(unsigned *)VA(0x4fab2c) == 0x4cf696 &&
        patch_call2(0x4cf757, 0x4b9b4c, r_mm_blit)) {
        static const unsigned mmsites[] = {0x4d0977, 0x4d09c3, 0x4d0ad3};
        void *p = r_mm_draw;
        for (i = 0; i < 3; i++) patch_call2(mmsites[i], 0x4d07f2, r_mm_tomap);
        put(VA(0x4fab2c), &p, 4);
        r_hik = 2 * r_mm_scale;
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
