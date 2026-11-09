/* ── Controller button prompts (2026-10-09) ────────────────────────────────
 *
 * Included into nv2a_gpu_d3d11.inc.c. NFL 2K5 draws every button icon in its
 * menus and HUD from one 128x128 paletted sheet: a 4x4 grid of 32x32 cells
 *
 *     A      Y      X      B
 *     START  BACK   L-stk  (!)
 *     L-trg  R-trg  White  Black
 *     D-pad  L3     R3     R-stk
 *
 * When the title says PlayStation prompts are wanted (g_xbox_prompt_style,
 * set by its controller layer), the decoded sheet is recognised by a hash of
 * its pixels and the cells are repainted with PlayStation glyphs before it is
 * uploaded: Cross/Triangle/Square/Circle for A/Y/X/B, OPTIONS/SHARE for
 * START/BACK, L2/R2 for the triggers, R1/L1 for White/Black (this port maps
 * RB/LB there), L3/R3 for the stick clicks. The D-pad and stick cells stay.
 * The texture cache keys the sheet with the style (gpu_tex_hash), so a change
 * of controller re-uploads it.
 */

int (*g_xbox_prompt_style)(void);           /* 0 Xbox, 1 PlayStation; NULL = Xbox */

static int prompt_style(void) { return g_xbox_prompt_style ? g_xbox_prompt_style() : 0; }

/* The four shoulder cells, (0,2) (1,2) (2,2) (3,2), name the PlayStation
 * button that drives that game button now: the controller layer fills them
 * from its button map (a remap or the PlayStation 2 layout moves them) and
 * returns a value that changes with them, which the texture cache key and
 * the rebuilt sheet follow. NULL: the standard layout. (2026-11-09) */
unsigned (*g_xbox_prompt_layout)(char labels[4][8]);
static char s_lab[4][8] = { "L2", "R2", "R1", "L1" };
static unsigned s_lab_sig, s_built_sig = ~0u;

static unsigned prompt_layout(void)
{
    char l[4][8];
    if (!g_xbox_prompt_layout) return 0;
    memcpy(l, s_lab, sizeof l);
    s_lab_sig = g_xbox_prompt_layout(l);
    memcpy(s_lab, l, sizeof l);
    return s_lab_sig;
}

static uint32_t prompt_hash(const uint32_t *p, uint32_t n)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

/* The retail sheet's pixels (decoded BGRA) hash to this. */
#define PROMPT_SHEET_HASH 0xBD8DD7ADu   /* retail sheet at 0x813D7980 in menus, 2026-10-09 */

static uint32_t s_ps_sheet[128 * 128];
static int s_ps_sheet_built;

/* GDI-draw the PlayStation cells into s_ps_sheet (BGRA, alpha from coverage). */
#ifdef _WIN32
static void prompt_build_ps(const uint32_t *orig)
{
    BITMAPINFO bi;
    void *bits = NULL;
    HDC dc;
    HBITMAP bmp, old;
    int i;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = 128; bi.bmiHeader.biHeight = -128;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    dc = CreateCompatibleDC(NULL);
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) { if (dc) DeleteDC(dc); return; }
    old = (HBITMAP)SelectObject(dc, bmp);
    memcpy(bits, orig, sizeof s_ps_sheet);       /* keep the cells that do not change */
    {
        /* Face buttons: a dark disc with the coloured symbol. */
        static const struct { int cx, cy; int sym; COLORREF col; } face[4] = {
            { 0, 0, 0, RGB(124, 178, 232) },     /* A -> Cross (blue) */
            { 1, 0, 1, RGB(64, 226, 160) },      /* Y -> Triangle (green) */
            { 2, 0, 2, RGB(230, 140, 220) },     /* X -> Square (pink) */
            { 3, 0, 3, RGB(255, 102, 102) },     /* B -> Circle (red) */
        };
        HBRUSH disc = CreateSolidBrush(RGB(32, 34, 40));
        HPEN ring = CreatePen(PS_SOLID, 2, RGB(150, 150, 160));
        for (i = 0; i < 4; i++) {
            int x0 = face[i].cx * 32, y0 = face[i].cy * 32;
            HPEN sp = CreatePen(PS_SOLID, 3, face[i].col);
            HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
            RECT cell = { x0, y0, x0 + 32, y0 + 32 };
            HBRUSH clear = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(dc, &cell, clear);
            DeleteObject(clear);
            SelectObject(dc, disc); SelectObject(dc, ring);
            Ellipse(dc, x0 + 2, y0 + 2, x0 + 30, y0 + 30);
            SelectObject(dc, sp); SelectObject(dc, nb);
            switch (face[i].sym) {
            case 0: MoveToEx(dc, x0 + 10, y0 + 10, NULL); LineTo(dc, x0 + 22, y0 + 22);
                    MoveToEx(dc, x0 + 22, y0 + 10, NULL); LineTo(dc, x0 + 10, y0 + 22); break;
            case 1: { POINT t[4] = { { x0 + 16, y0 + 8 }, { x0 + 24, y0 + 22 }, { x0 + 8, y0 + 22 }, { x0 + 16, y0 + 8 } };
                      Polyline(dc, t, 4); } break;
            case 2: Rectangle(dc, x0 + 10, y0 + 10, x0 + 23, y0 + 23); break;
            case 3: Ellipse(dc, x0 + 9, y0 + 9, x0 + 24, y0 + 24); break;
            }
            DeleteObject(sp);
        }
        DeleteObject(disc); DeleteObject(ring);
    }
    {
        /* Labelled cells: a dark rounded plate with white text. */
        const struct { int cx, cy; const char *t; } lab[8] = {
            { 0, 1, "OPTIONS" }, { 1, 1, "SHARE" }, { 0, 2, s_lab[0] }, { 1, 2, s_lab[1] },
            { 2, 2, s_lab[2] }, { 3, 2, s_lab[3] }, { 1, 3, "L3" }, { 2, 3, "R3" },
        };
        HFONT fbig = CreateFontA(15, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Arial");
        HFONT fsmall = CreateFontA(9, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, "Arial");
        HBRUSH plate = CreateSolidBrush(RGB(32, 34, 40));
        HPEN edge = CreatePen(PS_SOLID, 1, RGB(150, 150, 160));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        for (i = 0; i < 8; i++) {
            int x0 = lab[i].cx * 32, y0 = lab[i].cy * 32;
            RECT cell = { x0, y0, x0 + 32, y0 + 32 }, tr = { x0, y0 + 6, x0 + 32, y0 + 26 };
            HBRUSH clear = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(dc, &cell, clear);
            DeleteObject(clear);
            SelectObject(dc, plate); SelectObject(dc, edge);
            RoundRect(dc, x0 + 1, y0 + 5, x0 + 31, y0 + 27, 8, 8);
            SelectObject(dc, strlen(lab[i].t) > 2 ? fsmall : fbig);
            DrawTextA(dc, lab[i].t, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        DeleteObject(fbig); DeleteObject(fsmall); DeleteObject(plate); DeleteObject(edge);
    }
    GdiFlush();
    {
        /* Alpha: GDI leaves it 0. Cells drawn here are opaque where painted
         * and clear where black; untouched cells keep the original pixels. */
        static const unsigned char repainted[16] = { 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 0 };
        uint32_t *p = (uint32_t *)bits;
        int x, y;
        for (y = 0; y < 128; y++)
            for (x = 0; x < 128; x++) {
                int cell = (y / 32) * 4 + x / 32;
                uint32_t c = p[y * 128 + x] & 0x00FFFFFFu;
                if (!repainted[cell]) { s_ps_sheet[y * 128 + x] = orig[y * 128 + x]; continue; }
                s_ps_sheet[y * 128 + x] = c ? (c | 0xFF000000u) : 0u;
            }
    }
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    s_ps_sheet_built = 1;
    if (env_cached("RECOMP_PROMPT_LOG")) {           /* the sheet as built, for checking */
        FILE *f = fopen("logs\\prompt-sheet.bmp", "wb");
        if (f) {
            uint8_t hdr[54] = { 'B', 'M' };
            uint32_t size = 54 + 128 * 128 * 4;
            int32_t w = 128, h = -128;
            memcpy(hdr + 2, &size, 4); hdr[10] = 54; hdr[14] = 40;
            memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &h, 4); hdr[26] = 1; hdr[28] = 32;
            fwrite(hdr, 1, 54, f);
            fwrite(s_ps_sheet, 4, 128 * 128, f);
            fclose(f);
        }
    }
}
#else
/* No GDI (Android, Linux): the same cells painted by hand into the sheet --
 * discs and symbols for the face buttons, plates with a small pixel font for
 * the rest (2026-11-09: PlayStation prompts were Xbox letters on Android). */
static const uint16_t k_font35[36] = {        /* A-Z then 0-9, 3x5, rows top down, bit 14 = top left */
    0x2BED, 0x6BAE, 0x3923, 0x6B6E, 0x79A7, 0x79A4, 0x396B, 0x5BED, 0x7497, 0x126A, 0x5BAD, 0x4927, 0x5FED,
    0x6B6D, 0x2B6A, 0x6BA4, 0x2B73, 0x6BAD, 0x388E, 0x7492, 0x5B6F, 0x5B6A, 0x5BFD, 0x5AAD, 0x5A92, 0x72A7,
    0x7B6F, 0x2C97, 0x62A7, 0x628E, 0x5BC9, 0x798E, 0x39EF, 0x7292, 0x7BEF, 0x7BCE };

static void ps_px(uint32_t *p, int x, int y, uint32_t c)
{
    if (x >= 0 && x < 128 && y >= 0 && y < 128) p[y * 128 + x] = c;
}

static void ps_text(uint32_t *p, int x0, int y0, const char *t, int sc, uint32_t c)
{
    int i, r, b, dx, dy;
    for (i = 0; t[i]; i++) {
        int ch = t[i], g = ch >= 'A' && ch <= 'Z' ? ch - 'A' : ch >= '0' && ch <= '9' ? 26 + ch - '0' : -1;
        if (g < 0) continue;
        for (r = 0; r < 5; r++)
            for (b = 0; b < 3; b++)
                if (k_font35[g] >> (14 - r * 3 - b) & 1)
                    for (dy = 0; dy < sc; dy++)
                        for (dx = 0; dx < sc; dx++) ps_px(p, x0 + i * 4 * sc + b * sc + dx, y0 + r * sc + dy, c);
    }
}

static void ps_line(uint32_t *p, float x0, float y0, float x1, float y1, float w, uint32_t c, int cx, int cy)
{
    int x, y;
    for (y = cy; y < cy + 32; y++)
        for (x = cx; x < cx + 32; x++) {
            float px = x + 0.5f, py = y + 0.5f, dx = x1 - x0, dy = y1 - y0;
            float t = ((px - x0) * dx + (py - y0) * dy) / (dx * dx + dy * dy + 1e-6f), ex, ey;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            ex = x0 + t * dx - px; ey = y0 + t * dy - py;
            if (ex * ex + ey * ey <= w * w * 0.25f) ps_px(p, x, y, c);
        }
}

static void prompt_build_ps(const uint32_t *orig)
{
    static const unsigned char repainted[16] = { 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1, 1, 0 };
    static const uint32_t face_col[4] = { 0xFF7CB2E8u, 0xFF40E2A0u, 0xFFE68CDCu, 0xFFFF6666u };  /* Cross Triangle Square Circle */
    const uint32_t disc = 0xFF202228u, ring = 0xFF9696A0u, white = 0xFFFFFFFFu;
    const struct { int cx, cy; const char *t; } lab[8] = {
        { 0, 1, "OPTIONS" }, { 1, 1, "SHARE" }, { 0, 2, s_lab[0] }, { 1, 2, s_lab[1] },
        { 2, 2, s_lab[2] }, { 3, 2, s_lab[3] }, { 1, 3, "L3" }, { 2, 3, "R3" } };
    int i, x, y;
    memcpy(s_ps_sheet, orig, sizeof s_ps_sheet);
    for (i = 0; i < 16; i++)
        if (repainted[i])
            for (y = 0; y < 32; y++) for (x = 0; x < 32; x++) s_ps_sheet[((i / 4) * 32 + y) * 128 + (i % 4) * 32 + x] = 0;
    for (i = 0; i < 4; i++) {                 /* A Y X B -> Cross Triangle Square Circle */
        int cx = i * 32, cy = 0;
        uint32_t c = face_col[i];
        for (y = 0; y < 32; y++)
            for (x = 0; x < 32; x++) {
                float dx = x + 0.5f - 16, dy = y + 0.5f - 16, d2 = dx * dx + dy * dy;
                if (d2 <= 14.0f * 14.0f) s_ps_sheet[(cy + y) * 128 + cx + x] = d2 >= 12.6f * 12.6f ? ring : disc;
            }
        switch (i) {
        case 0: ps_line(s_ps_sheet, cx + 10, 10, cx + 22, 22, 3, c, cx, cy); ps_line(s_ps_sheet, cx + 22, 10, cx + 10, 22, 3, c, cx, cy); break;
        case 1: ps_line(s_ps_sheet, cx + 16, 8, cx + 24, 22, 3, c, cx, cy); ps_line(s_ps_sheet, cx + 24, 22, cx + 8, 22, 3, c, cx, cy);
                ps_line(s_ps_sheet, cx + 8, 22, cx + 16, 8, 3, c, cx, cy); break;
        case 2: ps_line(s_ps_sheet, cx + 10, 10, cx + 22, 10, 3, c, cx, cy); ps_line(s_ps_sheet, cx + 22, 10, cx + 22, 22, 3, c, cx, cy);
                ps_line(s_ps_sheet, cx + 22, 22, cx + 10, 22, 3, c, cx, cy); ps_line(s_ps_sheet, cx + 10, 22, cx + 10, 10, 3, c, cx, cy); break;
        default:
            for (y = 0; y < 32; y++)
                for (x = 0; x < 32; x++) {
                    float dx = x + 0.5f - 16, dy = y + 0.5f - 16, d2 = dx * dx + dy * dy;
                    if (d2 <= 7.5f * 7.5f && d2 >= 5.0f * 5.0f) s_ps_sheet[y * 128 + cx + x] = c;
                }
        }
    }
    for (i = 0; i < 8; i++) {                 /* plates with the button's name */
        int cx = lab[i].cx * 32, cy = lab[i].cy * 32, len = (int)strlen(lab[i].t);
        int sc = len <= 2 ? 3 : len == 3 ? 2 : 1, tw = len * 4 * sc - sc, th = 5 * sc;
        for (y = 5; y < 27; y++)
            for (x = 1; x < 31; x++) {
                int edge = y == 5 || y == 26 || x == 1 || x == 30;
                if ((x < 3 || x > 28) && (y < 7 || y > 24)) continue;     /* rounded corners */
                s_ps_sheet[(cy + y) * 128 + cx + x] = edge ? ring : disc;
            }
        ps_text(s_ps_sheet, cx + (32 - tw) / 2, cy + (32 - th) / 2, lab[i].t, sc, white);
    }
    s_ps_sheet_built = 1;
}
#endif

/* After a 128x128 paletted texture is decoded into `pix` (BGRA). */
static void prompt_patch(uint32_t *pix, uint32_t addr)
{
    uint32_t h = prompt_hash(pix, 128 * 128);
    static int logged;
    if (env_cached("RECOMP_PROMPT_LOG") && logged++ < 64)
        fprintf(stderr, "  [PROMPTS] 128x128 paletted texture %08X hash %08X\n", addr, h);
    if (h != PROMPT_SHEET_HASH || PROMPT_SHEET_HASH == 0)
        return;
    if (env_cached("RECOMP_PROMPT_LOG")) {
        static int dumped;
        fprintf(stderr, "  [PROMPTS] button sheet %08X: style %d\n", addr, prompt_style());
        if (!dumped++) {                         /* the retail sheet, for checking the cell order */
            char path[MAX_PATH];
            FILE *f = fopen(xbox_user_path("logs/prompt-sheet-original.bmp", path, sizeof path), "wb");
            if (f) {
                uint8_t hdr[54] = { 'B', 'M' };
                uint32_t size = 54 + 128 * 128 * 4;
                int32_t w = 128, hh = -128;
                memcpy(hdr + 2, &size, 4); hdr[10] = 54; hdr[14] = 40;
                memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &hh, 4); hdr[26] = 1; hdr[28] = 32;
                fwrite(hdr, 1, 54, f);
                fwrite(pix, 4, 128 * 128, f);
                fclose(f);
            }
        }
    }
    if (prompt_style() != 1)
        return;
    if (!s_ps_sheet_built || s_built_sig != s_lab_sig) {
        prompt_build_ps(pix);
        s_built_sig = s_lab_sig;
        if (env_cached("RECOMP_PROMPT_LOG")) {
            char path[MAX_PATH];
            FILE *f = fopen(xbox_user_path("logs/prompt-sheet-ps.bmp", path, sizeof path), "wb");
            if (f) {
                uint8_t hdr[54] = { 'B', 'M' };
                uint32_t size = 54 + 128 * 128 * 4;
                int32_t w = 128, hh = -128;
                memcpy(hdr + 2, &size, 4); hdr[10] = 54; hdr[14] = 40;
                memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &hh, 4); hdr[26] = 1; hdr[28] = 32;
                fwrite(hdr, 1, 54, f);
                fwrite(s_ps_sheet, 4, 128 * 128, f);
                fclose(f);
            }
            fprintf(stderr, "  [PROMPTS] PlayStation sheet built: shoulders %s %s %s %s\n", s_lab[0], s_lab[1], s_lab[2], s_lab[3]);
        }
    }
    if (s_ps_sheet_built) memcpy(pix, s_ps_sheet, sizeof s_ps_sheet);
    if (env_cached("RECOMP_PROMPT_LOG"))
        fprintf(stderr, "  [PROMPTS] button sheet %08X repainted (built %d)\n", addr, s_ps_sheet_built);
}
