#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#include <ctype.h>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#ifndef FW_SEMIBOLD
#define FW_SEMIBOLD 600
#endif

#define C_BG_TOP   0x141720
#define C_BG_BOT   0x0B0D13
#define C_GLOW_A   0xFF5A3C
#define C_GLOW_B   0x2E7BFF
#define C_FIELD    0x1B1F2B
#define C_FIELD_HI 0x222736
#define C_STROKE   0x2C3243
#define C_TEXT     0xEDF0F7
#define C_MUTED    0x7C8499
#define C_DIM      0x59617A
#define C_ACC1     0xFF6A3D
#define C_ACC2     0xFF3D7F
#define C_OK       0x3DDC97
#define C_ERR      0xFF5D5D
#define C_TRACK    0x232838


#define WM_APP_PROG (WM_APP+1)
#define WM_APP_TEXT (WM_APP+2)
#define WM_APP_DONE (WM_APP+3)


#define S(v) MulDiv((v), A.dpi, 96)
#define CR(c) RGB(((c)>>16)&255, ((c)>>8)&255, (c)&255)

enum { EL_NONE=0, EL_CLOSE, EL_MIN, EL_PASTE, EL_BTN1, EL_BTN2, EL_FOOT, EL_COUNT };

typedef struct { int x, y, w, h; } R;
typedef struct { unsigned *px; int w, h; } Canvas;

typedef struct {
    HWND    hwnd, edUrl, edName;
    HFONT   fTitle, fLabel, fInput, fBtn, fStat, fSmall;
    HDC     memDC;  HBITMAP memBmp;  unsigned *px;  int cw, ch;
    HBRUSH  brField;
    int     dpi;
    int     hot, press;
    float   hover[EL_COUNT];
    float   progT, progS;
    float   phase;
    int     busy;
    int     kind;
    wchar_t status[200];
    wchar_t appDir[MAX_PATH];
    wchar_t outDir[MAX_PATH];
    int     hasYt, hasFf, cancelled;
    HANDLE  worker;
} App;

static HANDLE          g_child;
static CRITICAL_SECTION g_lock;

static App A;
static int  W, H;
static R    rBar, rMark, rClose, rMin, rF1, rF2, rPaste, rBtn1, rBtn2, rTrack, rFoot;

static R  mk(int x,int y,int w,int h){ R r={x,y,w,h}; return r; }
static int inR(R r,int x,int y){ return x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h; }
static float clampf(float v,float a,float b){ return v<a?a:(v>b?b:v); }

static void blendPx(unsigned *p, unsigned col, float a) {
    int dr, dg, db, da, sr, sg, sb, ia;
    unsigned d;
    if (a <= 0.0015f) return;
    if (a > 1.0f) a = 1.0f;
    d  = *p;
    da = (d>>24)&255; dr = (d>>16)&255; dg = (d>>8)&255; db = d&255;
    sr = (col>>16)&255; sg = (col>>8)&255; sb = col&255;
    ia = (int)(a*256.0f + 0.5f);
    dr += ((sr-dr)*ia)>>8;
    dg += ((sg-dg)*ia)>>8;
    db += ((sb-db)*ia)>>8;
    da += ((255-da)*ia)>>8;
    *p = ((unsigned)da<<24)|((unsigned)dr<<16)|((unsigned)dg<<8)|(unsigned)db;
}

static unsigned mixCol(unsigned a, unsigned b, float t) {
    int ar=(a>>16)&255, ag=(a>>8)&255, ab=a&255;
    int br=(b>>16)&255, bg=(b>>8)&255, bb=b&255;
    int r=ar+(int)((br-ar)*t), g=ag+(int)((bg-ag)*t), bl=ab+(int)((bb-ab)*t);
    return ((unsigned)r<<16)|((unsigned)g<<8)|(unsigned)bl;
}

static float sdBox(float px,float py,float cx,float cy,float hx,float hy,float r) {
    float qx = fabsf(px-cx)-(hx-r), qy = fabsf(py-cy)-(hy-r);
    float ax = qx>0?qx:0, ay = qy>0?qy:0;
    float m  = qx>qy?qx:qy;
    return sqrtf(ax*ax+ay*ay) + (m<0?m:0) - r;
}
static float sdSeg(float px,float py,float ax,float ay,float bx,float by) {
    float pax=px-ax, pay=py-ay, bax=bx-ax, bay=by-ay;
    float h = (pax*bax+pay*bay)/(bax*bax+bay*bay+1e-5f);
    h = clampf(h,0,1);
    { float dx=pax-bax*h, dy=pay-bay*h; return sqrtf(dx*dx+dy*dy); }
}

static void rrFill(Canvas *c, float x,float y,float w,float h,float r,
                   unsigned c1, unsigned c2, float alpha) {
    int x0=(int)floorf(x-1), y0=(int)floorf(y-1);
    int x1=(int)ceilf(x+w+1), y1=(int)ceilf(y+h+1), ix, iy;
    float cx=x+w*0.5f, cy=y+h*0.5f, hx=w*0.5f, hy=h*0.5f;
    if (r>hx) r=hx; if (r>hy) r=hy; if (r<0) r=0;
    if (x0<0)x0=0; if (y0<0)y0=0; if (x1>c->w)x1=c->w; if (y1>c->h)y1=c->h;
    for (iy=y0; iy<y1; iy++) {
        float t   = h>0 ? clampf(((float)iy+0.5f-y)/h,0,1) : 0;
        unsigned col = (c1==c2) ? c1 : mixCol(c1,c2,t);
        unsigned *row = c->px + (size_t)iy*c->w;
        for (ix=x0; ix<x1; ix++) {
            float cov = 0.5f - sdBox((float)ix+0.5f,(float)iy+0.5f,cx,cy,hx,hy,r);
            if (cov <= 0) continue;
            blendPx(row+ix, col, (cov>1?1:cov)*alpha);
        }
    }
}

static void rrStroke(Canvas *c, float x,float y,float w,float h,float r,
                     float lw, unsigned col, float alpha) {
    int x0=(int)floorf(x-lw-1), y0=(int)floorf(y-lw-1);
    int x1=(int)ceilf(x+w+lw+1), y1=(int)ceilf(y+h+lw+1), ix, iy;
    float cx=x+w*0.5f, cy=y+h*0.5f, hx=w*0.5f, hy=h*0.5f, hw=lw*0.5f;
    if (r>hx) r=hx; if (r>hy) r=hy; if (r<0) r=0;
    if (x0<0)x0=0; if (y0<0)y0=0; if (x1>c->w)x1=c->w; if (y1>c->h)y1=c->h;
    for (iy=y0; iy<y1; iy++) {
        unsigned *row = c->px + (size_t)iy*c->w;
        for (ix=x0; ix<x1; ix++) {
            float sd  = sdBox((float)ix+0.5f,(float)iy+0.5f,cx,cy,hx,hy,r);
            float cov = 0.5f - (fabsf(sd)-hw);
            if (cov <= 0) continue;
            blendPx(row+ix, col, (cov>1?1:cov)*alpha);
        }
    }
}

static void line(Canvas *c, float ax,float ay,float bx,float by,
                 float lw, unsigned col, float alpha) {
    float pad = lw+2;
    int x0=(int)floorf((ax<bx?ax:bx)-pad), y0=(int)floorf((ay<by?ay:by)-pad);
    int x1=(int)ceilf ((ax>bx?ax:bx)+pad), y1=(int)ceilf ((ay>by?ay:by)+pad), ix, iy;
    if (x0<0)x0=0; if (y0<0)y0=0; if (x1>c->w)x1=c->w; if (y1>c->h)y1=c->h;
    for (iy=y0; iy<y1; iy++) {
        unsigned *row = c->px + (size_t)iy*c->w;
        for (ix=x0; ix<x1; ix++) {
            float cov = 0.5f - (sdSeg((float)ix+0.5f,(float)iy+0.5f,ax,ay,bx,by) - lw*0.5f);
            if (cov <= 0) continue;
            blendPx(row+ix, col, (cov>1?1:cov)*alpha);
        }
    }
}

static void tri(Canvas *c, float x1f,float y1f,float x2f,float y2f,float x3f,float y3f,
                unsigned col, float alpha) {
    float minx=x1f, maxx=x1f, miny=y1f, maxy=y1f;
    int x0, y0, xe, ye, ix, iy, sx, sy;
    if (x2f<minx)minx=x2f; if (x3f<minx)minx=x3f;
    if (x2f>maxx)maxx=x2f; if (x3f>maxx)maxx=x3f;
    if (y2f<miny)miny=y2f; if (y3f<miny)miny=y3f;
    if (y2f>maxy)maxy=y2f; if (y3f>maxy)maxy=y3f;
    x0=(int)floorf(minx)-1; y0=(int)floorf(miny)-1;
    xe=(int)ceilf(maxx)+1;  ye=(int)ceilf(maxy)+1;
    if (x0<0)x0=0; if (y0<0)y0=0; if (xe>c->w)xe=c->w; if (ye>c->h)ye=c->h;
    for (iy=y0; iy<ye; iy++) {
        unsigned *row = c->px + (size_t)iy*c->w;
        for (ix=x0; ix<xe; ix++) {
            int hits=0;
            for (sy=0; sy<3; sy++) for (sx=0; sx<3; sx++) {
                float px=(float)ix+(sx+0.5f)/3.0f, py=(float)iy+(sy+0.5f)/3.0f;
                float d1=(px-x2f)*(y1f-y2f)-(x1f-x2f)*(py-y2f);
                float d2=(px-x3f)*(y2f-y3f)-(x2f-x3f)*(py-y3f);
                float d3=(px-x1f)*(y3f-y1f)-(x3f-x1f)*(py-y1f);
                int neg=(d1<0)||(d2<0)||(d3<0), pos=(d1>0)||(d2>0)||(d3>0);
                if (!(neg&&pos)) hits++;
            }
            if (hits) blendPx(row+ix, col, (hits/9.0f)*alpha);
        }
    }
}

static void glow(Canvas *c, float cx,float cy,float r, unsigned col, float a) {
    int x0=(int)(cx-r), y0=(int)(cy-r), x1=(int)(cx+r), y1=(int)(cy+r), ix, iy;
    if (x0<0)x0=0; if (y0<0)y0=0; if (x1>c->w)x1=c->w; if (y1>c->h)y1=c->h;
    for (iy=y0; iy<y1; iy++) {
        unsigned *row = c->px + (size_t)iy*c->w;
        for (ix=x0; ix<x1; ix++) {
            float dx=((float)ix-cx)/r, dy=((float)iy-cy)/r;
            float d2=dx*dx+dy*dy, f;
            if (d2 >= 1.0f) continue;
            f = 1.0f-d2; f *= f;
            blendPx(row+ix, col, a*f);
        }
    }
}

static void txt(HDC dc, HFONT f, R r, const wchar_t *s, unsigned col, UINT fmt, int extra) {
    RECT rc; rc.left=r.x; rc.top=r.y; rc.right=r.x+r.w; rc.bottom=r.y+r.h;
    SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, CR(col));
    SetTextCharacterExtra(dc, extra);
    DrawTextW(dc, s, -1, &rc, fmt|DT_SINGLELINE|DT_NOPREFIX);
    SetTextCharacterExtra(dc, 0);
}

static void drawMark(Canvas *c, float x, float y, float sz) {
    float cx = x + sz*0.5f;
    float r  = sz*0.30f;
    rrFill(c, x, y, sz, sz, r, C_ACC1, C_ACC2, 1.0f);
    rrStroke(c, x+0.5f, y+0.5f, sz-1, sz-1, r, 1.0f, 0xFFFFFF, 0.20f);
    line(c, cx, y+sz*0.24f, cx, y+sz*0.52f, sz*0.10f, 0xFFFFFF, 0.95f);
    tri (c, cx-sz*0.17f, y+sz*0.46f, cx+sz*0.17f, y+sz*0.46f, cx, y+sz*0.68f, 0xFFFFFF, 0.95f);
    rrFill(c, x+sz*0.26f, y+sz*0.73f, sz*0.48f, sz*0.085f, sz*0.05f, 0xFFFFFF, 0xFFFFFF, 0.95f);
}

static HICON MakeIcon(int sz) {
    BITMAPINFO bi; void *bits=NULL; HBITMAP color, mask; HDC dc; ICONINFO ii; HICON ic;
    Canvas c;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=sz; bi.bmiHeader.biHeight=-sz;
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
    dc = GetDC(NULL);
    color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!color) return NULL;
    ZeroMemory(bits, (size_t)sz*sz*4);
    c.px=(unsigned*)bits; c.w=sz; c.h=sz;
    drawMark(&c, sz*0.06f, sz*0.06f, sz*0.88f);
    mask = CreateBitmap(sz, sz, 1, 1, NULL);
    ZeroMemory(&ii, sizeof(ii));
    ii.fIcon=TRUE; ii.hbmColor=color; ii.hbmMask=mask;
    ic = CreateIconIndirect(&ii);
    DeleteObject(color); DeleteObject(mask);
    return ic;
}

static void Layout(void) {
    int p  = S(24);
    int cw;
    W = S(560); H = S(384);
    cw = W - 2*p;
    rBar    = mk(0, 0, W, S(52));
    rMark   = mk(p, S(13), S(26), S(26));
    rClose  = mk(W-p-S(28), S(12), S(28), S(28));
    rMin    = mk(rClose.x-S(34), S(12), S(28), S(28));
    rF1     = mk(p, S(88),  cw, S(44));
    rPaste  = mk(rF1.x+cw-S(12)-S(66), rF1.y+S(8), S(66), S(28));
    rF2     = mk(p, S(162), cw, S(44));
    rTrack  = mk(p, S(226), cw, S(8));
    rBtn1   = mk(p, S(272), S(320), S(48));
    rBtn2   = mk(p+S(332), S(272), cw-S(332), S(48));
    rFoot   = mk(p, S(344), cw, S(18));
}

static HFONT mkFont(int px, int weight) {
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH|FF_DONTCARE, L"Segoe UI");
}
static void MakeFonts(void) {
    if (A.fTitle) { DeleteObject(A.fTitle); DeleteObject(A.fLabel); DeleteObject(A.fInput);
                    DeleteObject(A.fBtn);   DeleteObject(A.fStat);  DeleteObject(A.fSmall); }
    A.fTitle = mkFont(S(15), FW_SEMIBOLD);
    A.fLabel = mkFont(S(10), FW_SEMIBOLD);
    A.fInput = mkFont(S(14), FW_NORMAL);
    A.fBtn   = mkFont(S(14), FW_SEMIBOLD);
    A.fStat  = mkFont(S(12), FW_NORMAL);
    A.fSmall = mkFont(S(11), FW_NORMAL);
}

static void PlaceChildren(void) {
    MoveWindow(A.edUrl,  rF1.x+S(16), rF1.y+S(11), rPaste.x-S(12)-(rF1.x+S(16)), S(22), TRUE);
    MoveWindow(A.edName, rF2.x+S(16), rF2.y+S(11), rF2.w-S(32)-S(48), S(22), TRUE);
    SendMessageW(A.edUrl,  WM_SETFONT, (WPARAM)A.fInput, TRUE);
    SendMessageW(A.edName, WM_SETFONT, (WPARAM)A.fInput, TRUE);
}

static void PaintShapes(Canvas *c, HWND hwnd) {
    HWND foc = GetFocus();
    float ph = A.phase;
    int i;

    for (i=0; i<c->h; i++) {
        unsigned col = mixCol(C_BG_TOP, C_BG_BOT, (float)i/(float)(c->h-1));
        unsigned *row = c->px + (size_t)i*c->w;
        int x; for (x=0; x<c->w; x++) row[x] = 0xFF000000u | col;
    }
    glow(c, (float)S(70),  (float)S(10),  (float)S(320), C_GLOW_A, 0.16f);
    glow(c, (float)W-S(40), (float)H+S(30), (float)S(360), C_GLOW_B, 0.10f);

    drawMark(c, (float)rMark.x, (float)rMark.y, (float)rMark.w);

    for (i=0; i<2; i++) {
        R b = i ? rClose : rMin;
        int el = i ? EL_CLOSE : EL_MIN;
        float hv = A.hover[el];
        float cx = b.x + b.w*0.5f, cy = b.y + b.h*0.5f, g = b.w*0.18f;
        if (hv > 0.01f)
            rrFill(c, (float)b.x, (float)b.y, (float)b.w, (float)b.h, b.w*0.32f,
                   i?C_ERR:C_FIELD_HI, i?C_ERR:C_FIELD_HI, hv*(i?0.85f:1.0f));
        if (i) {
            unsigned gc = mixCol(C_MUTED, 0xFFFFFF, hv);
            line(c, cx-g, cy-g, cx+g, cy+g, S(1.4f*1), gc, 1);
            line(c, cx+g, cy-g, cx-g, cy+g, S(1.4f*1), gc, 1);
        } else {
            line(c, cx-g, cy, cx+g, cy, (float)S(1)*1.4f, mixCol(C_MUTED,0xFFFFFF,hv), 1);
        }
    }

    for (i=0; i<2; i++) {
        R f = i ? rF2 : rF1;
        HWND ed = i ? A.edName : A.edUrl;
        float fr = (foc==ed) ? 1.0f : 0.0f;
        float rad = (float)S(12);
        rrFill(c, (float)f.x, (float)f.y, (float)f.w, (float)f.h, rad,
               C_FIELD, mixCol(C_FIELD, 0x000000, 0.25f), 1.0f);
        rrStroke(c, f.x+0.5f, f.y+0.5f, f.w-1.0f, f.h-1.0f, rad, (float)S(1),
                 mixCol(C_STROKE, C_ACC1, fr), 1.0f);
        if (fr > 0)
            rrStroke(c, f.x-1.5f, f.y-1.5f, f.w+3.0f, f.h+3.0f, rad+2, (float)S(2),
                     C_ACC1, 0.18f*fr);
    }

    {
        float hv = A.hover[EL_PASTE];
        rrFill(c, (float)rPaste.x, (float)rPaste.y, (float)rPaste.w, (float)rPaste.h,
               rPaste.h*0.5f, mixCol(C_FIELD_HI, C_STROKE, hv),
               mixCol(C_FIELD_HI, C_STROKE, hv), A.busy?0.4f:1.0f);
    }

    {
        float x=(float)rTrack.x, y=(float)rTrack.y, w=(float)rTrack.w, h=(float)rTrack.h;
        float rad=h*0.5f, fw;
        rrFill(c, x, y, w, h, rad, C_TRACK, C_TRACK, 1.0f);
        if (A.progS > 0.0005f) {
            float fr = clampf(A.progS, 0, 1);
            fw = w*fr; if (fw < h) fw = h;
            glow(c, x+fw-h, y+h*0.5f, (float)S(46), C_ACC1, 0.22f);
            {
                int x0=(int)floorf(x-1), y0=(int)floorf(y-1);
                int x1=(int)ceilf(x+fw+1), y1=(int)ceilf(y+h+1), ix, iy;
                float cx=x+fw*0.5f, cy=y+h*0.5f, hx=fw*0.5f, hy=h*0.5f;
                float sh = fmodf(ph*0.9f, 1.6f)-0.3f;
                if (x0<0)x0=0; if (y0<0)y0=0;
                if (x1>c->w)x1=c->w; if (y1>c->h)y1=c->h;
                for (iy=y0; iy<y1; iy++) {
                    unsigned *row = c->px + (size_t)iy*c->w;
                    for (ix=x0; ix<x1; ix++) {
                        float cov = 0.5f - sdBox(ix+0.5f, iy+0.5f, cx, cy, hx, hy, rad);
                        float t, d; unsigned col;
                        if (cov <= 0) continue;
                        t = (ix+0.5f-x)/(fw<1?1:fw);
                        col = mixCol(C_ACC1, C_ACC2, clampf(t,0,1));
                        if (A.busy) {
                            d = (t-sh)/0.16f;
                            col = mixCol(col, 0xFFFFFF, clampf(expf(-d*d)*0.55f, 0, 1));
                        }
                        blendPx(row+ix, col, cov>1?1:cov);
                    }
                }
            }
        }
    }

    {
        unsigned dc = A.kind==2?C_OK : A.kind==3?C_ERR : A.kind==1?C_ACC1 : C_DIM;
        float pulse = A.kind==1 ? 0.55f+0.45f*sinf(ph*4.2f) : 1.0f;
        float cx=(float)rTrack.x+S(4), cy=(float)S(252);
        if (A.kind==1) glow(c, cx, cy, (float)S(14), dc, 0.5f*pulse);
        rrFill(c, cx-S(3), cy-S(3), (float)S(6), (float)S(6), (float)S(3), dc, dc, pulse);
    }

    for (i=0; i<2; i++) {
        R b   = i ? rBtn2 : rBtn1;
        float hv = A.hover[i?EL_BTN2:EL_BTN1];
        float pr = (A.press==(i?EL_BTN2:EL_BTN1)) ? 1.0f : 0.0f;
        float rad = (float)S(14);
        float dim = (i && A.busy) ? 0.45f : 1.0f;
        float off = pr*S(1);
        if (!i && !A.busy) {
            glow(c, b.x+b.w*0.5f, (float)b.y+b.h*0.9f, (float)S(120), C_ACC1, 0.10f+0.10f*hv);
            rrFill(c, (float)b.x, b.y+off, (float)b.w, (float)b.h, rad,
                   mixCol(C_ACC1, 0xFFFFFF, hv*0.10f),
                   mixCol(C_ACC2, 0xFFFFFF, hv*0.10f), 1.0f);
            rrStroke(c, b.x+0.5f, b.y+off+0.5f, b.w-1.0f, b.h-1.0f, rad, (float)S(1),
                     0xFFFFFF, 0.16f);
            {
                float gx=(float)b.x+S(34), gy=b.y+off+b.h*0.5f, u=(float)S(6);
                line(c, gx, gy-u*1.3f, gx, gy+u*0.2f, (float)S(2), 0xFFFFFF, 1.0f);
                tri (c, gx-u*0.75f, gy+u*0.05f, gx+u*0.75f, gy+u*0.05f, gx, gy+u*0.95f,
                     0xFFFFFF, 1.0f);
                rrFill(c, gx-u, gy+u*1.35f, u*2, (float)S(2), (float)S(1),
                       0xFFFFFF, 0xFFFFFF, 1.0f);
            }
        } else if (!i) {
            rrFill(c, (float)b.x, b.y+off, (float)b.w, (float)b.h, rad,
                   mixCol(C_FIELD, C_FIELD_HI, hv), mixCol(C_FIELD, C_FIELD_HI, hv), 1.0f);
            rrStroke(c, b.x+0.5f, b.y+off+0.5f, b.w-1.0f, b.h-1.0f, rad, (float)S(1),
                     mixCol(C_STROKE, C_ERR, hv*0.7f), 1.0f);
            {
                float gx=(float)b.x+S(34), gy=b.y+off+b.h*0.5f, u=(float)S(5);
                rrFill(c, gx-u, gy-u, u*2, u*2, (float)S(2),
                       mixCol(C_MUTED, C_ERR, hv), mixCol(C_MUTED, C_ERR, hv), 1.0f);
            }
        } else {
            rrFill(c, (float)b.x, b.y+off, (float)b.w, (float)b.h, rad,
                   mixCol(C_FIELD, C_FIELD_HI, hv), mixCol(C_FIELD, C_FIELD_HI, hv), dim);
            rrStroke(c, b.x+0.5f, b.y+off+0.5f, b.w-1.0f, b.h-1.0f, rad, (float)S(1),
                     mixCol(C_STROKE, C_MUTED, hv*0.6f), dim);
        }
    }

    rrFill(c, (float)rFoot.x, (float)S(328), (float)rFoot.w, 1.0f, 0, 0xFFFFFF, 0xFFFFFF, 0.06f);
    for (i=0; i<2; i++) {
        int ok = i ? A.hasFf : A.hasYt;
        float px = (float)(W - S(24) - (i ? S(74) : S(74)+S(80)));
        rrFill(c, px, (float)S(344), (float)(i?S(74):S(74)), (float)S(20), (float)S(10),
               C_FIELD, C_FIELD, 0.85f);
        rrFill(c, px+S(9), (float)S(351), (float)S(6), (float)S(6), (float)S(3),
               ok?C_OK:C_ERR, ok?C_OK:C_ERR, 1.0f);
    }
    (void)hwnd;
}

static void PaintText(HDC dc) {
    wchar_t buf[64];
    R r;

    {
        int tx = rMark.x+rMark.w+S(12);
        SIZE sz;
        SelectObject(dc, A.fTitle);
        GetTextExtentPoint32W(dc, L"Sonora", 6, &sz);
        txt(dc, A.fTitle, mk(tx, S(13), S(200), S(26)),
            L"Sonora", C_TEXT, DT_LEFT|DT_VCENTER, 0);
        txt(dc, A.fSmall, mk(tx+sz.cx+S(10), S(14), S(220), S(26)),
            L"audio extractor", C_DIM, DT_LEFT|DT_VCENTER, 0);
    }

    txt(dc, A.fLabel, mk(rF1.x, S(66), S(300), S(16)),
        L"VIDEO LINK", C_MUTED, DT_LEFT|DT_VCENTER, S(1));
    txt(dc, A.fLabel, mk(rF2.x, S(140), S(300), S(16)),
        L"SAVE AS", C_MUTED, DT_LEFT|DT_VCENTER, S(1));

    txt(dc, A.fSmall, rPaste, L"Paste", A.busy?C_DIM:C_TEXT, DT_CENTER|DT_VCENTER, 0);
    txt(dc, A.fInput, mk(rF2.x, rF2.y, rF2.w-S(16), rF2.h),
        L".mp3", C_DIM, DT_RIGHT|DT_VCENTER, 0);

    r = mk(rTrack.x+S(18), S(244), rTrack.w-S(70), S(18));
    txt(dc, A.fStat, r, A.status,
        A.kind==3?C_ERR : A.kind==2?C_OK : A.kind==1?C_TEXT : C_MUTED,
        DT_LEFT|DT_VCENTER, 0);

    if (A.busy || A.progS > 0.002f) {
        _snwprintf(buf, 64, L"%d%%", (int)(clampf(A.progS,0,1)*100.0f+0.5f));
        txt(dc, A.fStat, mk(rTrack.x, S(244), rTrack.w, S(18)), buf,
            C_MUTED, DT_RIGHT|DT_VCENTER, 0);
    }

    txt(dc, A.fBtn, mk(rBtn1.x+S(20), rBtn1.y, rBtn1.w-S(20), rBtn1.h),
        A.busy ? L"Cancel" : L"Download MP3",
        A.busy ? mixCol(C_MUTED, C_TEXT, A.hover[EL_BTN1]) : 0xFFFFFF,
        DT_CENTER|DT_VCENTER, 0);
    txt(dc, A.fBtn, rBtn2, L"Convert File",
        A.busy ? C_DIM : mixCol(C_MUTED, C_TEXT, A.hover[EL_BTN2]),
        DT_CENTER|DT_VCENTER, 0);

    txt(dc, A.fSmall, mk(rFoot.x, S(344), rFoot.w-S(170), S(20)), A.outDir,
        A.hover[EL_FOOT]>0.5f ? C_ACC1 : C_DIM,
        DT_LEFT|DT_VCENTER|DT_PATH_ELLIPSIS, 0);

    txt(dc, A.fSmall, mk(W-S(24)-S(74)-S(80)+S(20), S(344), S(54), S(20)),
        L"yt-dlp", A.hasYt?C_MUTED:C_ERR, DT_LEFT|DT_VCENTER, 0);
    txt(dc, A.fSmall, mk(W-S(24)-S(74)+S(20), S(344), S(54), S(20)),
        L"ffmpeg", A.hasFf?C_MUTED:C_ERR, DT_LEFT|DT_VCENTER, 0);
}

static void EnsureBuffer(HDC ref) {
    BITMAPINFO bi;
    if (A.memDC && A.cw==W && A.ch==H) return;
    if (A.memBmp) { DeleteObject(A.memBmp); A.memBmp=NULL; }
    if (A.memDC)  { DeleteDC(A.memDC);      A.memDC=NULL; }
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=W; bi.bmiHeader.biHeight=-H;
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
    A.memDC  = CreateCompatibleDC(ref);
    A.memBmp = CreateDIBSection(ref, &bi, DIB_RGB_COLORS, (void**)&A.px, NULL, 0);
    SelectObject(A.memDC, A.memBmp);
    A.cw=W; A.ch=H;
}

static void OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    Canvas c;
    EnsureBuffer(dc);
    c.px=A.px; c.w=A.cw; c.h=A.ch;
    PaintShapes(&c, hwnd);
    PaintText(A.memDC);
    GdiFlush();
    BitBlt(dc, 0, 0, W, H, A.memDC, 0, 0, SRCCOPY);
    EndPaint(hwnd, &ps);
}

typedef struct {
    wchar_t cmd[3072];
    int     mode;
    wchar_t done[200];
} Job;

static void CancelJob(void);
static void postText(int kind, const wchar_t *s) {
    wchar_t *cp = (wchar_t*)malloc((wcslen(s)+1)*sizeof(wchar_t));
    if (!cp) return;
    wcscpy(cp, s);
    PostMessageW(A.hwnd, WM_APP_TEXT, (WPARAM)kind, (LPARAM)cp);
}
static void postProg(float f) {
    PostMessageW(A.hwnd, WM_APP_PROG, (WPARAM)(int)(clampf(f,0,1)*1000.0f), 0);
}

static double hmsToSec(const char *s) {
    int h=0, m=0; double sec=0;
    if (sscanf(s, "%d:%d:%lf", &h, &m, &sec) != 3) return -1;
    return h*3600.0 + m*60.0 + sec;
}

static void parseLine(const char *ln, int mode, double *dur) {
    if (mode == 0) {
        const char *p = strchr(ln, '%');
        if (strstr(ln, "[download]") && p) {
            const char *q = p;
            while (q > ln && (isdigit((unsigned char)q[-1]) || q[-1]=='.')) q--;
            if (q < p) { postProg((float)(atof(q)/100.0)); postText(1, L"Downloading audio..."); }
        } else if (strstr(ln, "[ExtractAudio]")) {
            postText(1, L"Converting to MP3...");
        } else if (strstr(ln, "[youtube") || strstr(ln, "[info]")) {
            postText(1, L"Fetching video info...");
        } else if (strstr(ln, "ERROR")) {
            postText(3, L"yt-dlp rejected this link.");
        }
    } else {
        const char *d = strstr(ln, "Duration:");
        const char *t = strstr(ln, "time=");
        if (d) { double v = hmsToSec(d+9+(d[9]==' '?1:0)); if (v>0) *dur = v; }
        if (t && *dur > 0) {
            double v = hmsToSec(t+5);
            if (v >= 0) { postProg((float)(v/(*dur))); postText(1, L"Converting to MP3..."); }
        }
    }
}

static DWORD WINAPI Worker(LPVOID param) {
    Job *j = (Job*)param;
    SECURITY_ATTRIBUTES sa;
    HANDLE rd=NULL, wr=NULL;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    char buf[4096], acc[1024];
    DWORD got=0, code=1;
    size_t n=0;
    double dur=-1;
    int i;

    sa.nLength=sizeof(sa); sa.lpSecurityDescriptor=NULL; sa.bInheritHandle=TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { free(j); PostMessageW(A.hwnd, WM_APP_DONE, 0, 0); return 0; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&si, sizeof(si)); ZeroMemory(&pi, sizeof(pi));
    si.cb=sizeof(si);
    si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
    si.wShowWindow=SW_HIDE;
    si.hStdOutput=wr; si.hStdError=wr; si.hStdInput=NULL;

    if (!CreateProcessW(NULL, j->cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                        NULL, A.appDir, &si, &pi)) {
        CloseHandle(rd); CloseHandle(wr);
        postText(3, j->mode ? L"ffmpeg.exe not found."
                            : L"yt-dlp.exe not found.");
        free(j);
        PostMessageW(A.hwnd, WM_APP_DONE, 0, 0);
        return 0;
    }
    CloseHandle(wr);
    EnterCriticalSection(&g_lock); g_child = pi.hProcess; LeaveCriticalSection(&g_lock);

    while (ReadFile(rd, buf, sizeof(buf)-1, &got, NULL) && got > 0) {
        for (i=0; i<(int)got; i++) {
            char ch = buf[i];
            if (ch=='\r' || ch=='\n') {
                if (n) { acc[n]=0; parseLine(acc, j->mode, &dur); n=0; }
            } else if (n < sizeof(acc)-1) {
                acc[n++] = ch;
            }
        }
    }
    if (n) { acc[n]=0; parseLine(acc, j->mode, &dur); }

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    EnterCriticalSection(&g_lock); g_child = NULL; LeaveCriticalSection(&g_lock);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(rd);

    if (code == 0)        { postProg(1.0f); postText(2, j->done); }
    else if (A.cancelled) { postProg(0.0f);  postText(0, L"Cancelled. Nothing saved."); }
    else                  { postText(3, j->mode ? L"Conversion failed. Check source file."
                                                : L"Download failed. Check the link."); }
    free(j);
    PostMessageW(A.hwnd, WM_APP_DONE, (WPARAM)(code==0), 0);
    return 0;
}

static BOOL FindExe(const wchar_t *name, wchar_t *out, int cch) {
    wchar_t local[MAX_PATH];
    _snwprintf(local, MAX_PATH, L"%s\\%s", A.appDir, name);
    local[MAX_PATH-1]=0;
    if (GetFileAttributesW(local) != INVALID_FILE_ATTRIBUTES) {
        wcsncpy(out, local, cch); out[cch-1]=0; return TRUE;
    }
    if (SearchPathW(NULL, name, NULL, cch, out, NULL)) return TRUE;
    return FALSE;
}

static void sanitize(wchar_t *s, const wchar_t *bad) {
    wchar_t *p;
    for (p=s; *p; p++) if (wcschr(bad, *p)) *p = L'_';
}

static void StartJob(Job *j) {
    if (A.busy) { free(j); return; }
    A.busy=1; A.kind=1; A.progT=0; A.progS=0; A.cancelled=0;
    wcscpy(A.status, L"Preparing...");
    EnableWindow(A.edUrl, FALSE); EnableWindow(A.edName, FALSE);
    if (A.worker) { CloseHandle(A.worker); A.worker=NULL; }
    A.worker = CreateThread(NULL, 0, Worker, j, 0, NULL);
    InvalidateRect(A.hwnd, NULL, FALSE);
}

static void ActionDownload(void) {
    wchar_t url[1024]={0}, name[256]={0}, exe[MAX_PATH];
    Job *j;
    GetWindowTextW(A.edUrl, url, 1024);
    GetWindowTextW(A.edName, name, 256);
    if (!url[0] || !wcsstr(url, L"http")) {
        A.kind=3; wcscpy(A.status, L"Paste a link starting with http.");
        SetFocus(A.edUrl); InvalidateRect(A.hwnd, NULL, FALSE); return;
    }
    if (!name[0]) wcscpy(name, L"%(title)s");
    else sanitize(name, L"\\/:*?\"<>|");
    sanitize(url, L"\"");
    if (!FindExe(L"yt-dlp.exe", exe, MAX_PATH)) {
        A.kind=3; wcscpy(A.status, L"Place yt-dlp.exe in the program folder.");
        InvalidateRect(A.hwnd, NULL, FALSE); return;
    }
    j = (Job*)calloc(1, sizeof(Job));
    if (!j) return;
    j->mode = 0;
    _snwprintf(j->cmd, 3072,
        L"\"%s\" --newline --no-playlist --no-warnings --ignore-config "
        L"-x --audio-format mp3 --audio-quality 0 "
        L"-o \"%s\\%s.%%(ext)s\" -- \"%s\"",
        exe, A.outDir, name, url);
    j->cmd[3071]=0;
    wcscpy(j->done, L"Done. MP3 is in the output folder.");
    StartJob(j);
}

static void ActionConvert(void) {
    wchar_t file[MAX_PATH]=L"", exe[MAX_PATH], out[MAX_PATH], *dot;
    OPENFILENAMEW ofn;
    Job *j;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = A.hwnd;
    ofn.lpstrFilter = L"Video & Audio\0*.mp4;*.mkv;*.webm;*.m4a;*.wav;*.flac;*.mov;*.avi\0All files\0*.*\0";
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"Select file to convert";
    ofn.Flags       = OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return;
    if (!FindExe(L"ffmpeg.exe", exe, MAX_PATH)) {
        A.kind=3; wcscpy(A.status, L"Place ffmpeg.exe in the program folder.");
        InvalidateRect(A.hwnd, NULL, FALSE); return;
    }
    wcscpy(out, file);
    dot = wcsrchr(out, L'.');
    if (dot && !wcschr(dot, L'\\')) *dot = 0;
    wcscat(out, L".mp3");
    j = (Job*)calloc(1, sizeof(Job));
    if (!j) return;
    j->mode = 1;
    _snwprintf(j->cmd, 3072,
        L"\"%s\" -hide_banner -y -i \"%s\" -vn -c:a libmp3lame -q:a 2 \"%s\"",
        exe, file, out);
    j->cmd[3071]=0;
    wcscpy(j->done, L"Conversion complete.");
    StartJob(j);
}

static void CancelJob(void) {
    if (!A.busy) return;
    A.cancelled = 1;
    EnterCriticalSection(&g_lock);
    if (g_child) TerminateProcess(g_child, 1);
    LeaveCriticalSection(&g_lock);
    A.kind = 1;
    wcscpy(A.status, L"Cancelling...");
    InvalidateRect(A.hwnd, NULL, FALSE);
}

static void ActionPaste(void) {
    if (!OpenClipboard(A.hwnd)) return;
    {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            wchar_t *p = (wchar_t*)GlobalLock(h);
            if (p) { SetWindowTextW(A.edUrl, p); SetFocus(A.edUrl);
                     SendMessageW(A.edUrl, EM_SETSEL, (WPARAM)-1, -1); }
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
}

static int HitTest(int x, int y) {
    if (inR(rClose,x,y)) return EL_CLOSE;
    if (inR(rMin,x,y))   return EL_MIN;
    if (inR(rBtn1,x,y)) return EL_BTN1;
    if (!A.busy) {
        if (inR(rPaste,x,y)) return EL_PASTE;
        if (inR(rBtn2,x,y))  return EL_BTN2;
    }
    if (inR(rFoot,x,y) && x < rFoot.x+rFoot.w-S(170)) return EL_FOOT;
    return EL_NONE;
}

static void DoAction(int el) {
    switch (el) {
        case EL_CLOSE: SendMessageW(A.hwnd, WM_CLOSE, 0, 0); break;
        case EL_MIN:   ShowWindow(A.hwnd, SW_MINIMIZE); break;
        case EL_PASTE: ActionPaste(); break;
        case EL_BTN1:  if (A.busy) CancelJob(); else ActionDownload(); break;
        case EL_BTN2:  ActionConvert(); break;
        case EL_FOOT:  ShellExecuteW(A.hwnd, L"open", A.outDir, NULL, NULL, SW_SHOWNORMAL); break;
        default: break;
    }
}

static WNDPROC g_editProc;
static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(A.hwnd, NULL, FALSE);
            break;
        case WM_KEYDOWN:
            if (w == VK_RETURN) { DoAction(EL_BTN1); return 0; }
            if (w == VK_ESCAPE) { SendMessageW(A.hwnd, WM_CLOSE, 0, 0); return 0; }
            break;
        case WM_PAINT: {
            LRESULT r = CallWindowProcW(g_editProc, h, m, w, l);
            if (GetWindowTextLengthW(h) == 0 && GetFocus() != h) {
                HDC dc = GetDC(h);
                RECT rc; GetClientRect(h, &rc);
                SelectObject(dc, A.fInput);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, CR(C_DIM));
                DrawTextW(dc, (h==A.edUrl) ? L"https://www.youtube.com/watch?v=..."
                                           : L"filename (empty = video title)",
                          -1, &rc, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);
                ReleaseDC(h, dc);
            }
            return r;
        }
    }
    return CallWindowProcW(g_editProc, h, m, w, l);
}

static void ApplyRegion(HWND h) {
    HRGN rgn = CreateRoundRectRgn(0, 0, W+1, H+1, S(18), S(18));
    SetWindowRgn(h, rgn, TRUE);
}

static int Animate(void) {
    int changed = 0, i;
    for (i=0; i<EL_COUNT; i++) {
        float target = (i==A.hot) ? 1.0f : 0.0f;
        float d = target - A.hover[i];
        if (fabsf(d) > 0.004f) { A.hover[i] += d*0.28f; changed = 1; }
        else if (A.hover[i] != target) { A.hover[i] = target; changed = 1; }
    }
    { float d = A.progT - A.progS;
      if (fabsf(d) > 0.0015f) { A.progS += d*0.16f; changed = 1; }
      else if (A.progS != A.progT) { A.progS = A.progT; changed = 1; } }
    if (A.busy || A.kind==1) { A.phase += 0.016f; changed = 1; }
    return changed;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE inst = ((LPCREATESTRUCTW)lp)->hInstance;
        HICON ic;
        A.hwnd = hwnd;
        A.edUrl = CreateWindowExW(0, L"EDIT", L"", WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
                                  0,0,10,10, hwnd, (HMENU)1, inst, NULL);
        A.edName = CreateWindowExW(0, L"EDIT", L"", WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
                                  0,0,10,10, hwnd, (HMENU)2, inst, NULL);
        g_editProc = (WNDPROC)SetWindowLongPtrW(A.edUrl, GWLP_WNDPROC, (LONG_PTR)EditProc);
        SetWindowLongPtrW(A.edName, GWLP_WNDPROC, (LONG_PTR)EditProc);
        MakeFonts(); PlaceChildren();
        A.brField = CreateSolidBrush(CR(C_FIELD));
        ic = MakeIcon(64);
        if (ic) { SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)ic);
                  SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)ic); }
        SetTimer(hwnd, 1, 16, NULL);
        SetFocus(A.edUrl);
        return 0;
    }

    case WM_ERASEBKGND: return 1;
    case WM_PAINT:      OnPaint(hwnd); return 0;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)wp, CR(C_FIELD));
        SetTextColor((HDC)wp, CR(A.busy ? C_MUTED : C_TEXT));
        SetBkMode((HDC)wp, OPAQUE);
        return (LRESULT)A.brField;

    case WM_NCHITTEST: {
        POINT p; RECT rc;
        GetWindowRect(hwnd, &rc);
        p.x = (short)LOWORD(lp) - rc.left;
        p.y = (short)HIWORD(lp) - rc.top;
        if (p.y < rBar.h && HitTest(p.x, p.y) == EL_NONE) return HTCAPTION;
        return HTCLIENT;
    }

    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme;
        int el = HitTest((short)LOWORD(lp), (short)HIWORD(lp));
        if (el != A.hot) { A.hot = el; InvalidateRect(hwnd, NULL, FALSE); }
        tme.cbSize=sizeof(tme); tme.dwFlags=TME_LEAVE; tme.hwndTrack=hwnd; tme.dwHoverTime=0;
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        A.hot = EL_NONE; InvalidateRect(hwnd, NULL, FALSE); return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp)==HTCLIENT && A.hot!=EL_NONE) {
            SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_HAND)); return TRUE;
        }
        break;

    case WM_LBUTTONDOWN:
        A.press = HitTest((short)LOWORD(lp), (short)HIWORD(lp));
        if (A.press != EL_NONE) { SetCapture(hwnd); InvalidateRect(hwnd, NULL, FALSE); }
        return 0;

    case WM_LBUTTONUP: {
        int el = HitTest((short)LOWORD(lp), (short)HIWORD(lp));
        int was = A.press;
        A.press = EL_NONE;
        ReleaseCapture();
        InvalidateRect(hwnd, NULL, FALSE);
        if (was != EL_NONE && was == el) DoAction(el);
        return 0;
    }

    case WM_TIMER:
        if (Animate()) InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_APP_PROG:
        A.progT = (float)((int)wp)/1000.0f;
        return 0;

    case WM_APP_TEXT: {
        wchar_t *s = (wchar_t*)lp;
        A.kind = (int)wp;
        wcsncpy(A.status, s, 199); A.status[199]=0;
        free(s);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_APP_DONE:
        A.busy = 0;
        if (!wp && A.kind != 3 && !A.cancelled) {
            A.kind = 3; wcscpy(A.status, L"Something went wrong.");
        }
        EnableWindow(A.edUrl, TRUE); EnableWindow(A.edName, TRUE);
        if (wp) A.progT = 1.0f;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_DPICHANGED: {
        RECT *pr = (RECT*)lp;
        A.dpi = HIWORD(wp);
        Layout(); MakeFonts(); PlaceChildren();
        SetWindowPos(hwnd, NULL, pr->left, pr->top, W, H, SWP_NOZORDER|SWP_NOACTIVATE);
        ApplyRegion(hwnd);
        if (A.memDC) { DeleteObject(A.memBmp); DeleteDC(A.memDC);
                       A.memDC=NULL; A.memBmp=NULL; A.cw=0; A.ch=0; }
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_CLOSE:
        if (A.busy) CancelJob();
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (A.brField) DeleteObject(A.brField);
        if (A.memBmp)  DeleteObject(A.memBmp);
        if (A.memDC)   DeleteDC(A.memDC);
        if (A.fTitle)  { DeleteObject(A.fTitle); DeleteObject(A.fLabel); DeleteObject(A.fInput);
                         DeleteObject(A.fBtn); DeleteObject(A.fStat); DeleteObject(A.fSmall); }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ShowFakeGame(void) {
    HMODULE u = GetModuleHandleW(L"user32.dll");
    typedef BOOL (WINAPI *PFNCTX)(HANDLE);
    typedef BOOL (WINAPI *PFNAWARE)(void);
    PFNCTX  ctx  = u ? (PFNCTX) GetProcAddress(u, "SetProcessDpiAwarenessContext") : NULL;
    PFNAWARE awr = u ? (PFNAWARE)GetProcAddress(u, "SetProcessDPIAware") : NULL;
    if (ctx) { if (ctx((HANDLE)(INT_PTR)-4)) { } }
    if (awr) awr();

    HINSTANCE inst = GetModuleHandleA(NULL);
    InitializeCriticalSection(&g_lock);
    A.dpi = 96;
    A.hot = A.press = EL_NONE;
    A.kind = 0;
    wcscpy(A.status, L"Ready when you are.");

    wchar_t exePath[MAX_PATH], *slash;
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    slash = wcsrchr(exePath, L'\\');
    if (slash) *slash = 0;
    wcscpy(A.appDir, exePath);
    wcscpy(A.outDir, exePath);
    A.hasYt = FindExe(L"yt-dlp.exe", exePath, MAX_PATH);
    A.hasFf = FindExe(L"ffmpeg.exe", exePath, MAX_PATH);

    Layout();

    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW|CS_VREDRAW|CS_DROPSHADOW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"SonoraWnd";
    if (!RegisterClassExW(&wc)) return;

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"SonoraWnd", L"Sonora",
                               WS_POPUP|WS_MINIMIZEBOX|WS_SYSMENU|WS_CLIPCHILDREN,
                               CW_USEDEFAULT, CW_USEDEFAULT, W, H,
                               NULL, NULL, inst, NULL);
    if (!hwnd) return;

    A.dpi = 96;
    Layout(); MakeFonts(); PlaceChildren();
    SetWindowPos(hwnd, NULL, 0, 0, W, H, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    ApplyRegion(hwnd);

    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(hwnd, NULL,
                 wa.left + ((wa.right-wa.left)-W)/2,
                 wa.top  + ((wa.bottom-wa.top)-H)/2,
                 0, 0, SWP_NOSIZE|SWP_NOZORDER);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)inst; (void)prev; (void)cmd; (void)show;
    ShowFakeGame();
    return 0;
}
