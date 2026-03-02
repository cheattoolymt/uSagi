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
#pragma once
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ast.h"
#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char **names; char **types; int count, cap; } TCScope;

static void tcs_add(TCScope *s, const char *name, const char *type) {
    for (int i=0;i<s->count;i++)
        if (!strcmp(s->names[i],name)) { free(s->types[i]); s->types[i]=strdup(type); return; }
    if (s->count>=s->cap) {
        s->cap=s->cap?s->cap*2:16;
        s->names=realloc(s->names,s->cap*sizeof(char*));
        s->types=realloc(s->types,s->cap*sizeof(char*));
    }
    s->names[s->count]=strdup(name); s->types[s->count]=strdup(type); s->count++;
}
static const char *tcs_get(TCScope *s, const char *name) {
    for (int i=0;i<s->count;i++) if(!strcmp(s->names[i],name)) return s->types[i];
    return NULL;
}
static void tcs_free(TCScope *s) {
    for (int i=0;i<s->count;i++) { free(s->names[i]); free(s->types[i]); }
    free(s->names); free(s->types); s->names=NULL; s->types=NULL; s->count=s->cap=0;
}

typedef struct { char *name; char *ret_type; char **param_types; int param_count; } FuncSig;
static FuncSig *tc_funcs=NULL; static int tc_func_count=0, tc_func_cap=0;
static void tcf_add(const char *name, const char *ret, char **ptypes, int pc) {
    for (int i=0;i<tc_func_count;i++) if(!strcmp(tc_funcs[i].name,name)) return;
    if (tc_func_count>=tc_func_cap) { tc_func_cap=tc_func_cap?tc_func_cap*2:8; tc_funcs=realloc(tc_funcs,tc_func_cap*sizeof(FuncSig)); }
    FuncSig *f=&tc_funcs[tc_func_count++];
    f->name=strdup(name); f->ret_type=strdup(ret?ret:"void"); f->param_count=pc;
    f->param_types=malloc(pc*sizeof(char*));
    for (int i=0;i<pc;i++) f->param_types[i]=strdup(ptypes[i]);
}
static FuncSig *tcf_get(const char *name) {
    for (int i=0;i<tc_func_count;i++) if(!strcmp(tc_funcs[i].name,name)) return &tc_funcs[i];
    return NULL;
}

typedef struct { char *name; char **field_names; char **field_types; int field_count; } StructDef;
static StructDef *tc_structs=NULL; static int tc_struct_count=0, tc_struct_cap=0;
static void tcs_def_add(const char *name, char **fnames, char **ftypes, int fc) {
    for (int i=0;i<tc_struct_count;i++) if(!strcmp(tc_structs[i].name,name)) return;
    if (tc_struct_count>=tc_struct_cap) { tc_struct_cap=tc_struct_cap?tc_struct_cap*2:8; tc_structs=realloc(tc_structs,tc_struct_cap*sizeof(StructDef)); }
    StructDef *sd=&tc_structs[tc_struct_count++];
    sd->name=strdup(name); sd->field_count=fc;
    sd->field_names=malloc(fc*sizeof(char*)); sd->field_types=malloc(fc*sizeof(char*));
    for (int i=0;i<fc;i++) { sd->field_names[i]=strdup(fnames[i]); sd->field_types[i]=strdup(ftypes[i]); }
}
static StructDef *tcs_def_get(const char *name) {
    for (int i=0;i<tc_struct_count;i++) if(!strcmp(tc_structs[i].name,name)) return &tc_structs[i];
    return NULL;
}
static const char *tcs_def_field_type(StructDef *sd, const char *field) {
    for (int i=0;i<sd->field_count;i++) if(!strcmp(sd->field_names[i],field)) return sd->field_types[i];
    return NULL;
}

static int types_compatible(const char *a, const char *b) {
    if (!a||!b) return 1;
    if (!strcmp(a,b)) return 1;
    
    char ta[128],tb[128]; strncpy(ta,a,127); strncpy(tb,b,127); ta[127]=tb[127]=0;
    size_t la=strlen(ta),lb=strlen(tb);
    if (la>0&&ta[la-1]=='?') ta[la-1]=0;
    if (lb>0&&tb[lb-1]=='?') tb[lb-1]=0;
    if (!strcmp(ta,tb)) return 1;
    if (!strcmp(ta,"nil")||!strcmp(tb,"nil")) return 1; 
    if ((!strcmp(ta,"int")&&!strcmp(tb,"float"))||(!strcmp(ta,"float")&&!strcmp(tb,"int"))) return 1;
    
    if (!strncmp(ta,"dict",4)&&!strncmp(tb,"dict",4)) return 1;
    return 0;
}

static const char *tc_expr_type(Node *n, TCScope *locals, TCScope *globals);

static const char *tc_expr_type(Node *n, TCScope *locals, TCScope *globals) {
    if (!n) return NULL;
    switch (n->type) {
        case NODE_INT_LIT:   return "int";
        case NODE_FLOAT_LIT: return "float";
        case NODE_STR_LIT:   return "str";
        case NODE_BOOL_LIT:  return "bool";
        case NODE_NIL_LIT:   return "nil";
        case NODE_FSTRING:   return "str";
        case NODE_DICT_LIT:  return "dict";
        case NODE_DICT_ACCESS: return NULL;
        case NODE_ARRAY_LIT: {
            if (n->child_count==0) return "int[]";
            const char *et=tc_expr_type(n->children[0],locals,globals);
            if (!et) return "int[]";
            static char buf[64]; snprintf(buf,sizeof(buf),"%s[]",et); return buf;
        }
        case NODE_IDENT: {
            const char *t=tcs_get(locals,n->str_val);
            if (!t) t=tcs_get(globals,n->str_val);
            if (!t) {
                char msg[256]; snprintf(msg,sizeof(msg),"未宣言の変数 '%s'",n->str_val);
                ec_add(n->line,n->col,msg);
            }
            return t;
        }
        case NODE_INDEX: {
            const char *at=tc_expr_type(n->children[0],locals,globals);
            if (!at) return NULL;
            size_t l=strlen(at);
            if (l>2&&at[l-2]=='['&&at[l-1]==']') {
                static char base[64]; size_t bl=l-2; if(bl>63)bl=63;
                memcpy(base,at,bl); base[bl]=0; return base;
            }
            if (!strncmp(at,"dict",4)) return NULL;
            return at;
        }
        case NODE_FIELD_ACCESS: {
            
            const char *ot=tc_expr_type(n->children[0],locals,globals);
            if (!ot) return NULL;
            
            char base[128]; strncpy(base,ot,127); base[127]=0;
            size_t bl=strlen(base); if (bl>0&&base[bl-1]=='?') base[bl-1]=0;
            StructDef *sd=tcs_def_get(base);
            if (!sd) return NULL;
            return tcs_def_field_type(sd,n->str_val);
        }
        case NODE_STRUCT_INIT: {
            return n->str_val; 
        }
        case NODE_BINOP: {
            const char *lt=tc_expr_type(n->children[0],locals,globals);
            const char *rt=tc_expr_type(n->children[1],locals,globals);
            if (!strcmp(n->str_val,"==")||!strcmp(n->str_val,"!=")||
                !strcmp(n->str_val,"<") ||!strcmp(n->str_val,">")||
                !strcmp(n->str_val,"<=")||!strcmp(n->str_val,">=")) return "bool";
            if (!strcmp(n->str_val,"&&")||!strcmp(n->str_val,"||")) return "bool";
            /* bitwise ops always return int */
            if (!strcmp(n->str_val,"&")||!strcmp(n->str_val,"|")||
                !strcmp(n->str_val,"^")||!strcmp(n->str_val,"<<")||
                !strcmp(n->str_val,">>")) return "int";
            if ((lt&&!strcmp(lt,"float"))||(rt&&!strcmp(rt,"float"))) return "float";
            return lt?lt:rt;
        }
        case NODE_UNOP:
            if (n->str_val&&!strcmp(n->str_val,"not")) return "bool";
            if (n->str_val&&!strcmp(n->str_val,"~")) return "int";
            return tc_expr_type(n->children[0],locals,globals);
        case NODE_CONCAT: return "str";
        case NODE_FUNC_CALL: {
            if (!strcmp(n->str_val,"len")) return "int";
            FuncSig *fs=tcf_get(n->str_val);
            if (!fs) return NULL;
            return fs->ret_type;
        }
        default: return NULL;
    }
}

static void tc_block(Node *block, TCScope *locals, TCScope *globals, int loop_depth, const char *func_ret);
static void tc_stmt(Node *n, TCScope *locals, TCScope *globals, int loop_depth, const char *func_ret) {
    if (!n) return;
    switch (n->type) {
        case NODE_VAR_DECL:
            tcs_add(locals,n->str_val,n->var_type?n->var_type:"int");
            break;
        case NODE_TYPE_INFER: {
            const char *it=tc_expr_type(n->children[0],locals,globals);
            tcs_add(locals,n->str_val,it?it:"int");
            break;
        }
        case NODE_ASSIGN: {
            if (n->var_type&&!strcmp(n->var_type,"indexed")) break;
            const char *vt=tcs_get(locals,n->str_val);
            if (!vt) vt=tcs_get(globals,n->str_val);
            if (!vt) break;
            const char *et=tc_expr_type(n->children[0],locals,globals);
            if (et&&!types_compatible(vt,et)) {
                char msg[256]; snprintf(msg,sizeof(msg),"型ミスマッチ: '%s'型の変数'%s'に'%s'を代入しています",vt,n->str_val,et);
                ec_add(n->line,n->col,msg);
            }
            break;
        }
        case NODE_FIELD_ASSIGN: {
            
            const char *vt=tcs_get(locals,n->str_val);
            if (!vt) vt=tcs_get(globals,n->str_val);
            if (vt) {
                char base[128]; strncpy(base,vt,127); base[127]=0;
                size_t bl=strlen(base); if(bl>0&&base[bl-1]=='?') base[bl-1]=0;
                StructDef *sd=tcs_def_get(base);
                if (!sd) {
                    char msg[256]; snprintf(msg,sizeof(msg),"'%s'はstructではありません",base);
                    ec_add(n->line,n->col,msg);
                } else {
                    const char *ft=tcs_def_field_type(sd,n->var_type);
                    if (!ft) {
                        char msg[256]; snprintf(msg,sizeof(msg),"struct '%s'にフィールド'%s'はありません",base,n->var_type);
                        ec_add(n->line,n->col,msg);
                    } else {
                        const char *et=tc_expr_type(n->children[0],locals,globals);
                        if (et&&!types_compatible(ft,et)) {
                            char msg[256]; snprintf(msg,sizeof(msg),"フィールド'%s'の型'%s'に'%s'を代入しています",n->var_type,ft,et);
                            ec_add(n->line,n->col,msg);
                        }
                    }
                }
            }
            break;
        }
        case NODE_RETURN: {
            if (!func_ret) break;
            if (n->child_count==0) {
                if (strcmp(func_ret,"void")) {
                    char msg[128]; snprintf(msg,sizeof(msg),"戻り値がありません (期待: '%s')",func_ret);
                    ec_add(n->line,n->col,msg);
                }
            } else {
                const char *rt=tc_expr_type(n->children[0],locals,globals);
                if (rt&&!types_compatible(func_ret,rt)) {
                    char msg[256]; snprintf(msg,sizeof(msg),"戻り値の型ミスマッチ: '%s'を返しましたが'%s'が必要です",rt,func_ret);
                    ec_add(n->line,n->col,msg);
                }
            }
            break;
        }
        case NODE_BREAK: case NODE_CONTINUE:
            if (loop_depth<=0) ec_add(n->line,n->col,"ループの外でbreak/continueを使っています");
            break;
        case NODE_IF:
            tc_block(n->body,locals,globals,loop_depth,func_ret);
            tc_block(n->else_body,locals,globals,loop_depth,func_ret);
            break;
        case NODE_WHILE: tc_block(n->body,locals,globals,loop_depth+1,func_ret); break;
        case NODE_LOOP:  tc_block(n->body,locals,globals,loop_depth+1,func_ret); break;
        case NODE_FOR_RANGE:
            tcs_add(locals,n->str_val,"int");
            tc_block(n->body,locals,globals,loop_depth+1,func_ret); break;
        case NODE_FOR_IN: tc_block(n->body,locals,globals,loop_depth+1,func_ret); break;
        case NODE_MATCH:
            for (int i=0;i<n->child_count;i++) tc_block(n->children[i]->body,locals,globals,loop_depth,func_ret); break;
        case NODE_FUNC_DEF: {
            TCScope fn_locals={0};
            for (int i=0;i<n->param_count;i++) tcs_add(&fn_locals,n->params[i],n->param_types[i]);
            tc_block(n->body,&fn_locals,globals,0,n->ret_type?n->ret_type:"void");
            tcs_free(&fn_locals); break;
        }
        case NODE_STRUCT_DEF:
            tcs_def_add(n->str_val,n->params,n->param_types,n->param_count);
            break;
        case NODE_BLOCK: tc_block(n,locals,globals,loop_depth,func_ret); break;
        default: break;
    }
}
static void tc_block(Node *block, TCScope *locals, TCScope *globals, int loop_depth, const char *func_ret) {
    if (!block) return;
    for (int i=0;i<block->child_count;i++) tc_stmt(block->children[i],locals,globals,loop_depth,func_ret);
}

static void typecheck(Node *prog) {
    
    for (int i=0;i<prog->child_count;i++) {
        Node *n=prog->children[i];
        if (n->type==NODE_STRUCT_DEF)
            tcs_def_add(n->str_val,n->params,n->param_types,n->param_count);
        if (n->type==NODE_FUNC_DEF)
            tcf_add(n->str_val,n->ret_type,n->param_types,n->param_count);
    }
    
    TCScope globals={0},locals={0};
    for (int i=0;i<prog->child_count;i++) {
        Node *n=prog->children[i];
        if (n->type==NODE_VAR_DECL) tcs_add(&globals,n->str_val,n->var_type?n->var_type:"int");
    }
    for (int i=0;i<prog->child_count;i++) tc_stmt(prog->children[i],&locals,&globals,0,"void");
    tcs_free(&globals); tcs_free(&locals);
    if (ec_has_errors()) ec_flush_and_exit();
}
