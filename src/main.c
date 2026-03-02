/*
 * Copyright 2026 nyan<cheattoolymt>
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "lexer.h"
#include "ast.h"
#include "error.h"
#include "parser.h"
#include "typechecker.h"
#include "codegen.h"
#include "asm_codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static char *read_file(const char *path) {
    FILE *f=fopen(path,"r"); if (!f) return NULL;
    fseek(f,0,SEEK_END); long sz=ftell(f); rewind(f);
    char *buf=malloc(sz+1);
    size_t n=fread(buf,1,sz,f); buf[n]=0;
    fclose(f); return buf;
}
static int file_exists(const char *path) {
    struct stat st; return stat(path,&st)==0;
}

static const char *g_input_dir = ".";
static const char *g_exe_dir   = ".";

static char *find_module(const char *modname) {
    char path[1024];
    snprintf(path,sizeof(path),"%s/%s.usagi",g_input_dir,modname);
    if (file_exists(path)) return strdup(path);
    snprintf(path,sizeof(path),"%s/lib/%s.usagi",g_input_dir,modname);
    if (file_exists(path)) return strdup(path);
    snprintf(path,sizeof(path),"%s/lib/%s.usagi",g_exe_dir,modname);
    if (file_exists(path)) return strdup(path);
    snprintf(path,sizeof(path),"/usr/local/lib/usagi/%s.usagi",modname);
    if (file_exists(path)) return strdup(path);
    return NULL;
}

#define MAX_PULLED 64
static char *pulled_modules[MAX_PULLED];
static int   pulled_count=0;

static int already_pulled(const char *m) {
    for (int i=0;i<pulled_count;i++) if (!strcmp(pulled_modules[i],m)) return 1;
    return 0;
}

static void merge_module_into(Node *prog, const char *modname);

static void process_pulls(Node *prog) {
    for (int i=0;i<prog->child_count;i++)
        if (prog->children[i]->type==NODE_PULL)
            merge_module_into(prog,prog->children[i]->str_val);
}

static void merge_module_into(Node *prog, const char *modname) {
    if (already_pulled(modname)) return;
    char *mod_path=find_module(modname);
    if (!mod_path) {
        fprintf(stderr,"\033[1;31m[エラー]\033[0m モジュール '%s' が見つかりません\n"
            "  検索: %s/%s.usagi, %s/lib/%s.usagi, /usr/local/lib/usagi/%s.usagi\n\n",
            modname,g_input_dir,modname,g_input_dir,modname,modname);
        exit(1);
    }
    if (pulled_count<MAX_PULLED) pulled_modules[pulled_count++]=strdup(modname);
    char *src=read_file(mod_path);
    if (!src) { fprintf(stderr,"モジュール読み込み失敗: %s\n",mod_path); free(mod_path); exit(1); }
    fprintf(stderr,"\033[2m  $pull %s  ->  %s\033[0m\n",modname,mod_path);

    const char *pf=g_errors.filename, *ps=g_errors.source;
    ec_init(mod_path,src);
    TokenList tl=tokenize(src); Node *mp=parse_program(tl); token_list_free(&tl);
    ec_init(pf,ps);
    process_pulls(mp);

    int mc=mp->child_count;
    if (!mc) { node_free(mp); free(src); free(mod_path); return; }
    while (prog->child_count+mc>prog->child_cap) {
        prog->child_cap=prog->child_cap?prog->child_cap*2:(mc+16);
        prog->children=realloc(prog->children,prog->child_cap*sizeof(Node*));
    }
    memmove(prog->children+mc,prog->children,prog->child_count*sizeof(Node*));
    memcpy(prog->children,mp->children,mc*sizeof(Node*));
    prog->child_count+=mc;
    free(mp->children); mp->children=NULL; mp->child_count=0; mp->child_cap=0;
    node_free(mp); free(src); free(mod_path);
}

static void print_banner(void) {
    fprintf(stderr,
        "\033[1;35mraise uSagi v5\033[0m  "
        "\033[2m(lex->parse->typecheck->asm | $pull | struct | dict | nullable | gui | file)\033[0m\n");
}

/* runtime .c を候補ディレクトリから探す */
static int find_runtime_src(const char *name, char *out, int outsz) {
    char path[512];
    snprintf(path,sizeof(path),"%s/src/%s",g_exe_dir,name);
    if (file_exists(path)) { snprintf(out,outsz,"%s",path); return 1; }
    snprintf(path,sizeof(path),"./src/%s",name);
    if (file_exists(path)) { snprintf(out,outsz,"%s",path); return 1; }
    snprintf(path,sizeof(path),"/usr/local/lib/usagi/%s",name);
    if (file_exists(path)) { snprintf(out,outsz,"%s",path); return 1; }
    return 0;
}

/* runtime .c をコンパイルして .o を生成、成功なら 1 */
static int compile_runtime(const char *src_path, const char *obj_path, const char *extra_cflags) {
    char cmd[4096]; char buf[2048]=""; size_t n; FILE *p;
    snprintf(cmd,sizeof(cmd),"gcc -O2 %s -c '%s' -o '%s' 2>&1",
             extra_cflags?extra_cflags:"", src_path, obj_path);
    p=popen(cmd,"r"); if (!p) return 0;
    n=fread(buf,1,sizeof(buf)-1,p); buf[n]=0;
    int st=pclose(p);
    if (st) { fprintf(stderr,"\033[1;33m[警告]\033[0m %s: %s\n",src_path,buf); return 0; }
    return 1;
}

static int link_asm(const char *asm_file, const char *out) {
    int pid=(int)getpid();
    char obj[256]; snprintf(obj,sizeof(obj),"/tmp/usagi_%d.o",pid);
    char cmd[8192]; char buf[4096]=""; size_t n; FILE *p;

    /* アセンブル */
    snprintf(cmd,sizeof(cmd),"as --64 '%s' -o '%s' 2>&1",asm_file,obj);
    p=popen(cmd,"r");
    if (p) { n=fread(buf,1,sizeof(buf)-1,p); buf[n]=0; int st=pclose(p);
        if (st) { fprintf(stderr,"\033[1;31m[asエラー]\033[0m\n%s\n",buf); return 1; } }

    /* usagi_runtime.c */
    char rt_src[512]="", rt_obj[256]=""; int has_rt=0;
    if (find_runtime_src("usagi_runtime.c",rt_src,sizeof(rt_src))) {
        snprintf(rt_obj,sizeof(rt_obj),"/tmp/usagi_rt_%d.o",pid);
        has_rt=compile_runtime(rt_src,rt_obj,NULL);
        if (!has_rt) fprintf(stderr,"\033[1;33m[runtime警告]\033[0m dict機能が制限されます\n");
    }

    /* SDL2 フラグ取得 */
    char sdl_cflags[512]="", sdl_lflags[512]="-lSDL2";
    { FILE *f=popen("sdl2-config --cflags 2>/dev/null","r");
      if(f){size_t k=fread(sdl_cflags,1,sizeof(sdl_cflags)-1,f);sdl_cflags[k]=0;
        for(int i=(int)k-1;i>=0;i--){if(sdl_cflags[i]=='\n'||sdl_cflags[i]=='\r')sdl_cflags[i]=0;else break;}
        pclose(f);}
      f=popen("sdl2-config --libs 2>/dev/null","r");
      if(f){size_t k=fread(sdl_lflags,1,sizeof(sdl_lflags)-1,f);sdl_lflags[k]=0;
        for(int i=(int)k-1;i>=0;i--){if(sdl_lflags[i]=='\n'||sdl_lflags[i]=='\r')sdl_lflags[i]=0;else break;}
        pclose(f);}
    }

    /* usagi_gui_runtime.c */
    char gui_src[512]="", gui_obj[256]=""; int has_gui=0;
    if (find_runtime_src("usagi_gui_runtime.c",gui_src,sizeof(gui_src))) {
        snprintf(gui_obj,sizeof(gui_obj),"/tmp/usagi_gui_%d.o",pid);
        has_gui=compile_runtime(gui_src,gui_obj,sdl_cflags);
        if (!has_gui) fprintf(stderr,"\033[1;33m[gui警告]\033[0m GUI機能が無効（SDL2をインストール）\n");
    }

    /* usagi_file_runtime.c */
    char file_src[512]="", file_obj[256]=""; int has_file=0;
    if (find_runtime_src("usagi_file_runtime.c",file_src,sizeof(file_src))) {
        snprintf(file_obj,sizeof(file_obj),"/tmp/usagi_file_%d.o",pid);
        has_file=compile_runtime(file_src,file_obj,NULL);
        if (!has_file) fprintf(stderr,"\033[1;33m[file警告]\033[0m File機能が制限されます\n");
    }

    /* リンク */
    fprintf(stderr,"\033[2m  ld: gcc\033[0m\n");
    {
        char extra[2048]="";
        if (has_rt)   { strcat(extra," '"); strcat(extra,rt_obj);   strcat(extra,"'"); }
        if (has_gui)  { strcat(extra," '"); strcat(extra,gui_obj);  strcat(extra,"'"); }
        if (has_file) { strcat(extra," '"); strcat(extra,file_obj); strcat(extra,"'"); }
        snprintf(cmd,sizeof(cmd),"gcc -o '%s' '%s' %s -lm %s 2>&1",
                 out, obj, extra, has_gui?sdl_lflags:"");
        p=popen(cmd,"r"); buf[0]=0;
        n=fread(buf,1,sizeof(buf)-1,p); buf[n]=0; int st=pclose(p);
        if (st) { fprintf(stderr,"\033[1;31m[リンクエラー]\033[0m\n%s\n",buf); unlink(obj); return 1; }
    }

    unlink(obj);
    if (has_rt)   unlink(rt_obj);
    if (has_gui)  unlink(gui_obj);
    if (has_file) unlink(file_obj);
    return 0;
}

int main(int argc, char **argv) {
    if (argc<2) {
        print_banner();
        fprintf(stderr,
            "使い方: usagi <file.usagi> [オプション]\n"
            "  -o <名前>        出力ファイル名\n"
            "  --emit-asm       x86-64 ASMを標準出力\n"
            "  --emit-c         Cコードを標準出力\n"
            "  --backend-c      gccバックエンドを使う\n"
            "  --no-typecheck   型チェックをスキップ\n");
        return 1;
    }

    const char *input_file=NULL, *output_file=NULL;
    int emit_asm=0, emit_c=0, backend_c=0, no_typecheck=0;

    for (int i=1;i<argc;i++) {
        if      (!strcmp(argv[i],"-o")&&i+1<argc)  output_file=argv[++i];
        else if (!strcmp(argv[i],"--emit-asm"))     emit_asm=1;
        else if (!strcmp(argv[i],"--emit-c"))       emit_c=1;
        else if (!strcmp(argv[i],"--backend-c"))    backend_c=1;
        else if (!strcmp(argv[i],"--no-typecheck")) no_typecheck=1;
        else input_file=argv[i];
    }
    if (!input_file) { fprintf(stderr,"入力ファイルが指定されていません\n"); return 1; }

    {
        char tmp[1024]; strncpy(tmp,input_file,1023); tmp[1023]=0;
        char *sl=strrchr(tmp,'/');
        if (sl) { *sl=0; g_input_dir=strdup(tmp); } else g_input_dir=strdup(".");
    }
    {
        char tmp[1024]; strncpy(tmp,argv[0],1023); tmp[1023]=0;
        char *sl=strrchr(tmp,'/');
        if (sl) { *sl=0; g_exe_dir=strdup(tmp); } else g_exe_dir=strdup(".");
    }

    static char default_out[1024];
    if (!output_file) {
        const char *base=strrchr(input_file,'/'); base=base?base+1:input_file;
        strncpy(default_out,base,1023); default_out[1023]=0;
        char *dot=strrchr(default_out,'.');
        if (dot&&(!strcmp(dot,".usagi")||!strcmp(dot,".usg")||!strcmp(dot,".rabi"))) *dot=0;
        output_file=default_out;
    }

    char *src=read_file(input_file);
    if (!src) { fprintf(stderr,"\033[1;31m[エラー]\033[0m 開けません: '%s'\n",input_file); return 1; }

    print_banner();
    ec_init(input_file,src);
    TokenList tl=tokenize(src);
    Node *prog=parse_program(tl);
    token_list_free(&tl);
    process_pulls(prog);
    ec_init(input_file,src);
    if (!no_typecheck) typecheck(prog);

    if (emit_c || backend_c) {
        const char *c_file="/tmp/usagi_out.c";
        FILE *cf=fopen(c_file,"w");
        if (!cf) { fprintf(stderr,"tmpファイル作成失敗\n"); return 1; }
        codegen(prog,cf); fclose(cf);
        if (emit_c) {
            char *cs=read_file(c_file); if(cs){printf("%s",cs);free(cs);}
            node_free(prog); free(src); return 0;
        }
        /* --backend-c: runtime をコンパイルして gcc でリンク */
        int pid=(int)getpid();
        char sdl_cf[512]="", sdl_lf[512]="-lSDL2";
        { FILE *f=popen("sdl2-config --cflags 2>/dev/null","r");
          if(f){size_t k=fread(sdl_cf,1,sizeof(sdl_cf)-1,f);sdl_cf[k]=0;
            for(int i=(int)k-1;i>=0;i--){if(sdl_cf[i]=='\n'||sdl_cf[i]=='\r')sdl_cf[i]=0;else break;}
            pclose(f);}
          f=popen("sdl2-config --libs 2>/dev/null","r");
          if(f){size_t k=fread(sdl_lf,1,sizeof(sdl_lf)-1,f);sdl_lf[k]=0;
            for(int i=(int)k-1;i>=0;i--){if(sdl_lf[i]=='\n'||sdl_lf[i]=='\r')sdl_lf[i]=0;else break;}
            pclose(f);}
        }
        char bc_rt_src[512]="",bc_rt_obj[256]=""; int bc_has_rt=0;
        if(find_runtime_src("usagi_runtime.c",bc_rt_src,sizeof(bc_rt_src))){
            snprintf(bc_rt_obj,sizeof(bc_rt_obj),"/tmp/usagi_bcrt_%d.o",pid);
            bc_has_rt=compile_runtime(bc_rt_src,bc_rt_obj,NULL);}
        char bc_gui_src[512]="",bc_gui_obj[256]=""; int bc_has_gui=0;
        if(find_runtime_src("usagi_gui_runtime.c",bc_gui_src,sizeof(bc_gui_src))){
            snprintf(bc_gui_obj,sizeof(bc_gui_obj),"/tmp/usagi_bcgui_%d.o",pid);
            bc_has_gui=compile_runtime(bc_gui_src,bc_gui_obj,sdl_cf);}
        char bc_file_src[512]="",bc_file_obj[256]=""; int bc_has_file=0;
        if(find_runtime_src("usagi_file_runtime.c",bc_file_src,sizeof(bc_file_src))){
            snprintf(bc_file_obj,sizeof(bc_file_obj),"/tmp/usagi_bcfile_%d.o",pid);
            bc_has_file=compile_runtime(bc_file_src,bc_file_obj,NULL);}
        char bc_extra[2048]="";
        if(bc_has_rt)  {strcat(bc_extra," '");strcat(bc_extra,bc_rt_obj);  strcat(bc_extra,"'");}
        if(bc_has_gui) {strcat(bc_extra," '");strcat(bc_extra,bc_gui_obj); strcat(bc_extra,"'");}
        if(bc_has_file){strcat(bc_extra," '");strcat(bc_extra,bc_file_obj);strcat(bc_extra,"'");}
        char cmd[8192]; char gcc_out[4096]=""; size_t gl;
        snprintf(cmd,sizeof(cmd),"gcc -O2 -o '%s' '%s' %s -lm %s 2>&1",
                 output_file,c_file,bc_extra,bc_has_gui?sdl_lf:"");
        FILE *g=popen(cmd,"r");
        if (g) { gl=fread(gcc_out,1,sizeof(gcc_out)-1,g); gcc_out[gl]=0; int st=pclose(g);
            if(bc_has_rt)  unlink(bc_rt_obj);
            if(bc_has_gui) unlink(bc_gui_obj);
            if(bc_has_file)unlink(bc_file_obj);
            if (st) { fprintf(stderr,"\033[1;31m[gccエラー]\033[0m\n%s\n",gcc_out); node_free(prog); free(src); return 1; } }
    } else {
        char asm_file[256];
        snprintf(asm_file,sizeof(asm_file),"/tmp/usagi_%d.s",(int)getpid());
        FILE *af=fopen(asm_file,"w");
        if (!af) { fprintf(stderr,"tmpファイル作成失敗\n"); return 1; }
        asm_codegen(prog,af); fclose(af);
        if (emit_asm) {
            char *as=read_file(asm_file); if(as){printf("%s",as);free(as);}
            unlink(asm_file); node_free(prog); free(src); return 0;
        }
        int r=link_asm(asm_file,output_file);
        unlink(asm_file);
        if (r) { node_free(prog); free(src); return 1; }
    }

    fprintf(stderr,"\033[1;32m\u2713\033[0m -> \033[1m%s\033[0m\n",output_file);
    node_free(prog); free(src);
    return 0;
}
