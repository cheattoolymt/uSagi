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
    fprintf(stderr,"\033[2m  $pull %s  →  %s\033[0m\n",modname,mod_path);

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
        "\033[1;35m🐰 uSagi v5\033[0m  "
        "\033[2m(lex→parse→typecheck→asm | $pull | struct | dict<K,V> | nullable)\033[0m\n");
}

static int link_asm(const char *asm_file, const char *out) {
    char obj[256]; snprintf(obj,sizeof(obj),"/tmp/usagi_%d.o",(int)getpid());
    char cmd[4096]; char buf[4096]=""; size_t n; FILE *p;
    char runtime_src[512], runtime_obj[512];
    int has_runtime=0;

    
    snprintf(cmd,sizeof(cmd),"as --64 '%s' -o '%s' 2>&1",asm_file,obj);
    p=popen(cmd,"r");
    if (p) { n=fread(buf,1,sizeof(buf)-1,p); buf[n]=0; int st=pclose(p);
        if (st) { fprintf(stderr,"\033[1;31m[asエラー]\033[0m\n%s\n",buf); return 1; } }

    
    {
        
        const char *rt_candidates[]={
            "./src/usagi_runtime.c",
            "/usr/local/lib/usagi/usagi_runtime.c",
            NULL
        };
        char rt_from_exe[512];
        snprintf(rt_from_exe,sizeof(rt_from_exe),"%s/src/usagi_runtime.c",g_exe_dir);
        snprintf(runtime_src,sizeof(runtime_src),"%s",rt_from_exe);
        if (!file_exists(runtime_src)) {
            for (int i=0;rt_candidates[i];i++) if(file_exists(rt_candidates[i])){snprintf(runtime_src,sizeof(runtime_src),"%s",rt_candidates[i]);break;}
        }
        snprintf(runtime_obj,sizeof(runtime_obj),"/tmp/usagi_rt_%d.o",(int)getpid());
        has_runtime=file_exists(runtime_src);
        if (has_runtime) {
            snprintf(cmd,sizeof(cmd),"gcc -O2 -c '%s' -o '%s' 2>&1",runtime_src,runtime_obj);
            p=popen(cmd,"r"); buf[0]=0;
            if (p) { n=fread(buf,1,sizeof(buf)-1,p); buf[n]=0; int st=pclose(p);
                if (st) { fprintf(stderr,"\033[1;33m[runtime警告]\033[0m dict機能が制限されます\n"); has_runtime=0; } }
        }
    }

    
    const char *ldso_cands[]={
        "/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2",
        "/lib64/ld-linux-x86-64.so.2",
        "/lib/ld-linux-x86-64.so.2", NULL};
    const char *libc_cands[]={
        "/lib/x86_64-linux-gnu/libc.so.6",
        "/lib64/libc.so.6",
        "/usr/lib/x86_64-linux-gnu/libc.so.6", NULL};
    const char *crtdirs[]={
        "/usr/lib/x86_64-linux-gnu",
        "/usr/lib/x86_64-linux-gnu/crt",
        "/usr/lib64","/usr/lib", NULL};

    const char *ldso=NULL,*libc=NULL;
    for (int i=0;ldso_cands[i];i++) if(file_exists(ldso_cands[i])){ldso=ldso_cands[i];break;}
    for (int i=0;libc_cands[i];i++) if(file_exists(libc_cands[i])){libc=libc_cands[i];break;}
    char crt1[256]="",crti[256]="",crtn[256]="";
    for (int i=0;crtdirs[i];i++) {
        char t[256];
        snprintf(t,sizeof(t),"%s/crt1.o",crtdirs[i]); if(file_exists(t)&&!crt1[0])memcpy(crt1,t,256);
        snprintf(t,sizeof(t),"%s/crti.o",crtdirs[i]); if(file_exists(t)&&!crti[0])memcpy(crti,t,256);
        snprintf(t,sizeof(t),"%s/crtn.o",crtdirs[i]); if(file_exists(t)&&!crtn[0])memcpy(crtn,t,256);
        if(crt1[0]&&crti[0]&&crtn[0]) break;
    }

    if (!ldso||!libc||!crt1[0]) {
        
        fprintf(stderr,"\033[2m  ld: gcc fallback\033[0m\n");
        if (has_runtime)
            snprintf(cmd,sizeof(cmd),"gcc -o '%s' '%s' '%s' -lm 2>&1",out,obj,runtime_obj);
        else
            snprintf(cmd,sizeof(cmd),"gcc -o '%s' '%s' -lm 2>&1",out,obj);
        p=popen(cmd,"r"); buf[0]=0;
        if (p) { n=fread(buf,1,sizeof(buf)-1,p); buf[n]=0; int st=pclose(p);
            if (st) { fprintf(stderr,"\033[1;31m[リンクエラー]\033[0m\n%s\n",buf); unlink(obj); return 1; } }
        unlink(obj); return 0;
    }

    
    if (has_runtime) {
        snprintf(cmd,sizeof(cmd),
            "ld -o '%s' -dynamic-linker '%s' '%s' '%s' '%s' '%s' '%s' '%s' -lc -lm 2>&1",
            out,ldso,crt1,crti,obj,runtime_obj,libc,crtn);
    } else {
        snprintf(cmd,sizeof(cmd),
            "ld -o '%s' -dynamic-linker '%s' '%s' '%s' '%s' '%s' '%s' -lc -lm 2>&1",
            out,ldso,crt1,crti,obj,libc,crtn);
    }
    fprintf(stderr,"\033[2m  ld: %s\033[0m\n",ldso);
    p=popen(cmd,"r"); buf[0]=0;
    if (p) { n=fread(buf,1,sizeof(buf)-1,p); buf[n]=0; int st=pclose(p);
        if (st) { fprintf(stderr,"\033[1;31m[リンクエラー]\033[0m\n%s\n",buf); unlink(obj); return 1; } }
    unlink(obj);
    if (has_runtime) unlink(runtime_obj);
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
        char cmd[2048]; char gcc_out[4096]=""; size_t gl;
        snprintf(cmd,sizeof(cmd),"gcc -O2 -o '%s' '%s' 2>&1",output_file,c_file);
        FILE *g=popen(cmd,"r");
        if (g) { gl=fread(gcc_out,1,sizeof(gcc_out)-1,g); gcc_out[gl]=0; int st=pclose(g);
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

    fprintf(stderr,"\033[1;32m✓\033[0m → \033[1m%s\033[0m\n",output_file);
    node_free(prog); free(src);
    return 0;
}
