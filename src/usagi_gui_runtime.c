/*
 * usagi_gui_runtime.c  —  SDL2 GUI backend for uSagi
 * Copyright 2026 nyan<cheattoolymt>  Apache-2.0
 *
 * gui.init()               ウィンドウシステム初期化
 * gui.window(title, w, h)  ウィンドウ作成
 * gui.quit()               終了
 * gui.clear(color)         クリア  color = 0xRRGGBB
 * gui.present()            画面に反映
 * gui.blit(arr, x, y, w, h) int[] ピクセル配列を描画 (各要素 0xRRGGBB)
 * gui.poll()               イベントポーリング (0=続行, 1=終了要求)
 * gui.key(keycode)         キー押下中なら 1 (SDL_Scancode)
 * gui.delay(ms)            ミリ秒ウェイト
 */

#ifdef __has_include
#  if __has_include(<SDL2/SDL.h>)
#    include <SDL2/SDL.h>
#    define HAVE_SDL2 1
#  else
#    define HAVE_SDL2 0
#  endif
#else
#  include <SDL2/SDL.h>
#  define HAVE_SDL2 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if HAVE_SDL2

static SDL_Window   *g_win  = NULL;
static SDL_Renderer *g_ren  = NULL;
static SDL_Texture  *g_tex  = NULL;
static int g_tex_w=0, g_tex_h=0;
static int g_quit_flag=0;

void usagi_gui_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS) != 0) {
        fprintf(stderr,"[gui] SDL_Init失敗: %s\n",SDL_GetError());
    }
}

void usagi_gui_window(const char *title, long w, long h) {
    if (g_win) return;  /* already created */
    g_win = SDL_CreateWindow(
        title ? title : "uSagi",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        (int)w, (int)h,
        SDL_WINDOW_SHOWN);
    if (!g_win) { fprintf(stderr,"[gui] CreateWindow失敗: %s\n",SDL_GetError()); return; }
    g_ren = SDL_CreateRenderer(g_win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
}

void usagi_gui_quit(void) {
    if (g_tex) { SDL_DestroyTexture(g_tex); g_tex=NULL; }
    if (g_ren) { SDL_DestroyRenderer(g_ren); g_ren=NULL; }
    if (g_win) { SDL_DestroyWindow(g_win); g_win=NULL; }
    SDL_Quit();
}

void usagi_gui_clear(long color) {
    if (!g_ren) return;
    SDL_SetRenderDrawColor(g_ren,
        (color>>16)&0xFF, (color>>8)&0xFF, color&0xFF, 0xFF);
    SDL_RenderClear(g_ren);
}

void usagi_gui_present(void) {
    if (g_ren) SDL_RenderPresent(g_ren);
}

/*
 * blit: pixels は uSagi int[] (long[])、値は 0xRRGGBB
 * 領域 (x,y,w,h) に描画する
 */
void usagi_gui_blit(long *pixels, long x, long y, long w, long h) {
    if (!g_ren || !pixels || w<=0 || h<=0) return;
    /* テクスチャがサイズ違いなら再作成 */
    if (!g_tex || g_tex_w!=(int)w || g_tex_h!=(int)h) {
        if (g_tex) SDL_DestroyTexture(g_tex);
        g_tex = SDL_CreateTexture(g_ren,
            SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
            (int)w, (int)h);
        g_tex_w=(int)w; g_tex_h=(int)h;
    }
    if (!g_tex) return;
    /* ピクセルをコピー (long 0xRRGGBB → Uint32 ARGB) */
    void *tex_pixels; int pitch;
    if (SDL_LockTexture(g_tex,NULL,&tex_pixels,&pitch)!=0) return;
    Uint32 *dst=(Uint32*)tex_pixels;
    for (long i=0; i<w*h; i++) {
        long c=pixels[i];
        dst[i] = 0xFF000000u
               | (Uint32)(((c>>16)&0xFF)<<16)
               | (Uint32)(((c>> 8)&0xFF)<< 8)
               | (Uint32)( (c    )&0xFF);
    }
    SDL_UnlockTexture(g_tex);
    /* ウィンドウ全体に拡大描画 */
    int win_w=0, win_h=0;
    SDL_GetRendererOutputSize(g_ren, &win_w, &win_h);
    SDL_Rect dst_rect={(int)x,(int)y, win_w-(int)x, win_h-(int)y};
    SDL_RenderCopy(g_ren,g_tex,NULL,&dst_rect);
}

long usagi_gui_poll(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type==SDL_QUIT) { g_quit_flag=1; return 1; }
        if (e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_ESCAPE) { g_quit_flag=1; return 1; }
    }
    return g_quit_flag;
}

long usagi_gui_key(long keycode) {
    const Uint8 *state=SDL_GetKeyboardState(NULL);
    if (keycode<0||keycode>=SDL_NUM_SCANCODES) return 0;
    return state[(SDL_Scancode)keycode] ? 1 : 0;
}

void usagi_gui_delay(long ms) {
    if (ms>0) SDL_Delay((Uint32)ms);
}

#else  /* no SDL2 — stub implementations */

void usagi_gui_init(void)                              { fprintf(stderr,"[gui] SDL2 not available\n"); }
void usagi_gui_quit(void)                              {}
void usagi_gui_window(const char *t,long w,long h)     { (void)t;(void)w;(void)h; }
void usagi_gui_clear(long c)                           { (void)c; }
void usagi_gui_present(void)                           {}
void usagi_gui_blit(long *px,long x,long y,long w,long h){(void)px;(void)x;(void)y;(void)w;(void)h;}
long usagi_gui_poll(void)                              { return 1; }
long usagi_gui_key(long k)                             { (void)k; return 0; }
void usagi_gui_delay(long ms)                          { (void)ms; }

#endif
