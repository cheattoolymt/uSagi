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
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char **names; char **types; int count, cap; } AsmSymTable;

static void asm_sym_add(AsmSymTable *st, const char *name, const char *type) {
    for (int i=0;i<st->count;i++)
        if (!strcmp(st->names[i],name)) { free(st->types[i]); st->types[i]=strdup(type); return; }
    if (st->count>=st->cap) {
        st->cap=st->cap?st->cap*2:16;
        st->names=realloc(st->names,st->cap*sizeof(char*));
        st->types=realloc(st->types,st->cap*sizeof(char*));
    }
    st->names[st->count]=strdup(name); st->types[st->count]=strdup(type); st->count++;
}
static const char *asm_sym_get(AsmSymTable *st, const char *name) {
    for (int i=0;i<st->count;i++) if(!strcmp(st->names[i],name)) return st->types[i];
    return NULL;
}
static void asm_sym_clear(AsmSymTable *st) {
    for (int i=0;i<st->count;i++) { free(st->names[i]); free(st->types[i]); }
    free(st->names); free(st->types); st->names=NULL; st->types=NULL; st->count=st->cap=0;
}

typedef struct { char *name; char **field_names; char **field_types; int field_count; } AsmStructDef;
static AsmStructDef *asm_structs=NULL; static int asm_struct_count=0, asm_struct_cap=0;
static void asm_struct_add(const char *name, char **fn, char **ft, int fc) {
    for (int i=0;i<asm_struct_count;i++) if(!strcmp(asm_structs[i].name,name)) return;
    if (asm_struct_count>=asm_struct_cap) { asm_struct_cap=asm_struct_cap?asm_struct_cap*2:8; asm_structs=realloc(asm_structs,asm_struct_cap*sizeof(AsmStructDef)); }
    AsmStructDef *sd=&asm_structs[asm_struct_count++];
    sd->name=strdup(name); sd->field_count=fc;
    sd->field_names=malloc(fc*sizeof(char*)); sd->field_types=malloc(fc*sizeof(char*));
    for (int i=0;i<fc;i++) { sd->field_names[i]=strdup(fn[i]); sd->field_types[i]=strdup(ft[i]); }
}
static AsmStructDef *asm_struct_get(const char *name) {
    for (int i=0;i<asm_struct_count;i++) if(!strcmp(asm_structs[i].name,name)) return &asm_structs[i];
    return NULL;
}
static int asm_struct_field_offset(AsmStructDef *sd, const char *field) {
    
    for (int i=0;i<sd->field_count;i++) if(!strcmp(sd->field_names[i],field)) return i*8;
    return -1;
}
static int asm_struct_size(AsmStructDef *sd) { return sd->field_count * 8; }

static AsmSymTable asm_func_rets={0};
static void asm_reg_func(const char *name, const char *ret) { asm_sym_add(&asm_func_rets,name,ret?ret:"void"); }
static const char *asm_func_ret(const char *name) { return asm_sym_get(&asm_func_rets,name); }

#define MAX_STR_LITS 4096
typedef struct { char *value; int id; } StrLit;
static StrLit str_lits[MAX_STR_LITS];
static int    str_lit_count=0;

static int intern_string(const char *s) {
    for (int i=0;i<str_lit_count;i++) if(!strcmp(str_lits[i].value,s)) return str_lits[i].id;
    if (str_lit_count>=MAX_STR_LITS) return 0;
    str_lits[str_lit_count].value=strdup(s);
    str_lits[str_lit_count].id=str_lit_count;
    return str_lit_count++;
}

static int asm_label_counter=0;
static int asm_new_label(void) { return asm_label_counter++; }

typedef struct {
    FILE        *out;
    AsmSymTable  globals;
    AsmSymTable  locals;       
    int          local_offsets[256]; 
    char        *local_names[256];
    int          local_count;
    int          stack_size;   
    int          in_func;
    const char  *func_ret_type;
    
    int          loop_end_label;
    int          loop_top_label;
    
    char        *func_name;
} AsmCG;

static int acg_local_offset(AsmCG *cg, const char *name) {
    if (!name) return INT_MIN;
    for (int i=0;i<cg->local_count;i++)
        if (cg->local_names[i]&&!strcmp(cg->local_names[i],name)) return cg->local_offsets[i];
    return INT_MIN;
}

static int acg_alloc_local(AsmCG *cg, const char *name, int bytes) {
    if (cg->local_count>=256) return INT_MIN;
    cg->stack_size+=bytes;
    int off=-cg->stack_size;
    cg->local_names[cg->local_count]=strdup(name);
    cg->local_offsets[cg->local_count]=off;
    cg->local_count++;
    return off;
}

static const char *acg_node_type(AsmCG *cg, Node *n);
static const char *acg_node_type(AsmCG *cg, Node *n) {
    if (!n) return NULL;
    switch (n->type) {
        case NODE_INT_LIT:    return "int";
        case NODE_FLOAT_LIT:  return "float";
        case NODE_STR_LIT:    return "str";
        case NODE_BOOL_LIT:   return "bool";
        case NODE_FSTRING:    return "str";
        case NODE_DICT_LIT:   return "dict";
        case NODE_INDEX:      { const char *bt=acg_node_type(cg,n->children[0]); return (bt&&!strncmp(bt,"dict",4))?"str":"int"; }
        case NODE_NIL_LIT:    return "nil";
        case NODE_STRUCT_INIT: return n->str_val;
        case NODE_IDENT: {
            const char *t=asm_sym_get(&cg->locals,n->str_val);
            if (!t) t=asm_sym_get(&cg->globals,n->str_val);
            return t;
        }
        case NODE_BINOP: {
            const char *l=acg_node_type(cg,n->children[0]);
            const char *r=acg_node_type(cg,n->children[1]);
            if (!strcmp(n->str_val,"==")||!strcmp(n->str_val,"!=")||
                !strcmp(n->str_val,"<") ||!strcmp(n->str_val,">")||
                !strcmp(n->str_val,"<=")||!strcmp(n->str_val,">=")) return "bool";
            /* bitwise ops return int */
            if (!strcmp(n->str_val,"&")||!strcmp(n->str_val,"|")||
                !strcmp(n->str_val,"^")||!strcmp(n->str_val,"<<")||
                !strcmp(n->str_val,">>")) return "int";
            if ((l&&!strcmp(l,"float"))||(r&&!strcmp(r,"float"))) return "float";
            return l?l:r;
        }
        case NODE_CONCAT:  return "str";
        case NODE_FUNC_CALL: {
            if (!strcmp(n->str_val,"len")) return "int";
            return asm_func_ret(n->str_val);
        }
        case NODE_FIELD_ACCESS: {
            const char *ot=acg_node_type(cg,n->children[0]); if(!ot)return NULL;
            char base[128]; strncpy(base,ot,127); base[127]=0;
            size_t bl=strlen(base); if(bl>0&&base[bl-1]=='?')base[bl-1]=0;
            AsmStructDef *sd=asm_struct_get(base); if(!sd)return NULL;
            for (int i=0;i<sd->field_count;i++) if(!strcmp(sd->field_names[i],n->str_val)) return sd->field_types[i];
            return NULL;
        }
        default: return NULL;
    }
}

static int acg_is_float_type(const char *t) { return t&&!strcmp(t,"float"); }
static int acg_is_str_type(const char *t)   { return t&&(!strcmp(t,"str")||!strcmp(t,"str?")||!strncmp(t,"dict",4)); }

static const char *acg_safe(const char *name) {
    
    static char buf[256];
    static const char *reserved[]={"abs","pow","min","max","printf","scanf",
        "malloc","free","strlen","strcmp","strdup","exit","main",NULL};
    for (int i=0;reserved[i];i++) if(!strcmp(name,reserved[i])) { snprintf(buf,sizeof(buf),"usagi_%s",name); return buf; }
    return name;
}

#define OUT(cg,...) fprintf((cg)->out, __VA_ARGS__)

static void acg_emit_expr(AsmCG *cg, Node *n);
static void acg_emit_stmt(AsmCG *cg, Node *n);
static void acg_emit_block(AsmCG *cg, Node *block);

static const char *int_arg_regs[6]={"rdi","rsi","rdx","rcx","r8","r9"};
static const char *xmm_arg_regs[8]={"xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7"};

static void acg_emit_expr(AsmCG *cg, Node *n) {
    if (!n) { OUT(cg,"    xorq %%rax, %%rax\n"); return; }
    const char *ntype=acg_node_type(cg,n);

    switch (n->type) {

        
        case NODE_INT_LIT:
        case NODE_BOOL_LIT:
            OUT(cg,"    movq $%ld, %%rax\n", n->int_val);
            break;

        case NODE_NIL_LIT:
            OUT(cg,"    xorq %%rax, %%rax\n");
            break;

        case NODE_FLOAT_LIT: {
            
            union { double d; long l; } u; u.d=n->float_val;
            OUT(cg,"    movq $%ld, %%rax\n", u.l);
            OUT(cg,"    movq %%rax, %%xmm0\n");
            break;
        }

        case NODE_STR_LIT: {
            int id=intern_string(n->str_val);
            OUT(cg,"    leaq .Ls%d(%%rip), %%rax\n", id);
            break;
        }

        case NODE_FSTRING: {
            
            
            
            
            int id=intern_string(n->str_val ? n->str_val : "");
            if (n->child_count==0) {
                OUT(cg,"    leaq .Ls%d(%%rip), %%rax\n", id);
                break;
            }
            
            OUT(cg,"    leaq .Ls%d(%%rip), %%rdi\n", intern_string(""));
            OUT(cg,"    leaq .Ls%d(%%rip), %%rsi\n", id);
            OUT(cg,"    call usagi_concat\n");
            
            
            for (int i=0;i<n->child_count;i++) {
                OUT(cg,"    pushq %%rax\n");  
                acg_emit_expr(cg, n->children[i]);
                const char *et=acg_node_type(cg,n->children[i]);
                if (acg_is_float_type(et)) {
                    OUT(cg,"    call usagi_tostr_f\n");
                } else if (acg_is_str_type(et)) {
                    
                } else {
                    OUT(cg,"    movq %%rax, %%rdi\n");
                    OUT(cg,"    call usagi_tostr\n");
                }
                OUT(cg,"    movq %%rax, %%rsi\n");
                OUT(cg,"    popq %%rdi\n");
                OUT(cg,"    call usagi_concat\n");
            }
            break;
        }

        
        case NODE_IDENT: {
            int off=acg_local_offset(cg,n->str_val);
            if (off==INT_MIN) {
                
                if (acg_is_float_type(ntype)) {
                    OUT(cg,"    movsd %s_g(%%rip), %%xmm0\n", n->str_val);
                } else {
                    OUT(cg,"    movq %s_g(%%rip), %%rax\n", n->str_val);
                }
            } else {
                if (acg_is_float_type(ntype)) {
                    OUT(cg,"    movsd %d(%%rbp), %%xmm0\n", off);
                } else {
                    OUT(cg,"    movq %d(%%rbp), %%rax\n", off);
                }
            }
            break;
        }

        
        case NODE_INDEX: {
            
            const char *base_type = acg_node_type(cg, n->children[0]);
            if (base_type && !strncmp(base_type, "dict", 4)) {
                
                acg_emit_expr(cg, n->children[1]);  
                OUT(cg,"    pushq %%rax\n");
                acg_emit_expr(cg, n->children[0]);  
                OUT(cg,"    movq %%rax, %%rdi\n");
                OUT(cg,"    popq %%rsi\n");          
                OUT(cg,"    call usagi_dict_get\n");
            } else {
                
                acg_emit_expr(cg, n->children[1]);  
                OUT(cg,"    pushq %%rax\n");
                acg_emit_expr(cg, n->children[0]);  
                OUT(cg,"    popq %%rcx\n");
                OUT(cg,"    movq (%%rax,%%rcx,8), %%rax\n");
            }
            break;
        }

        
        case NODE_FIELD_ACCESS: {
            
            acg_emit_expr(cg, n->children[0]);  
            const char *ot=acg_node_type(cg,n->children[0]);
            char base[128]; strncpy(base,ot?ot:"",127); base[127]=0;
            size_t bl=strlen(base); if(bl>0&&base[bl-1]=='?')base[bl-1]=0;
            AsmStructDef *sd=asm_struct_get(base);
            int off2=sd?asm_struct_field_offset(sd,n->str_val):0;
            
            const char *ft=NULL;
            if (sd) { for(int fi=0;fi<sd->field_count;fi++) if(!strcmp(sd->field_names[fi],n->str_val)){ft=sd->field_types[fi];break;} }
            if (ft && !strcmp(ft,"float")) {
                OUT(cg,"    movsd %d(%%rax), %%xmm0\n", off2);
            } else {
                OUT(cg,"    movq %d(%%rax), %%rax\n", off2);
            }
            break;
        }

        
        case NODE_STRUCT_INIT: {
            AsmStructDef *sd=asm_struct_get(n->str_val);
            int sz=sd?asm_struct_size(sd):8;
            
            OUT(cg,"    movq $%d, %%rdi\n", sz);
            OUT(cg,"    call malloc\n");
            OUT(cg,"    pushq %%rax\n");  
            
            if (sd) {
                for (int fi=0;fi<sd->field_count;fi++) {
                    
                    for (int ci=0;ci+1<n->child_count;ci+=2) {
                        if (!strcmp(n->children[ci]->str_val,sd->field_names[fi])) {
                            acg_emit_expr(cg,n->children[ci+1]);
                            OUT(cg,"    movq %%rax, %%rcx\n");
                            OUT(cg,"    movq (%%rsp), %%rax\n");  
                            OUT(cg,"    movq %%rcx, %d(%%rax)\n", fi*8);
                            break;
                        }
                    }
                }
            }
            OUT(cg,"    popq %%rax\n");  
            break;
        }

        
        case NODE_BINOP: {
            const char *lt=acg_node_type(cg,n->children[0]);
            const char *rt=acg_node_type(cg,n->children[1]);
            int is_float=acg_is_float_type(lt)||acg_is_float_type(rt);
            int is_str=acg_is_str_type(lt)||acg_is_str_type(rt);

            
            int left_is_nil  = (n->children[0]->type==NODE_NIL_LIT);
            int right_is_nil = (n->children[1]->type==NODE_NIL_LIT);
            if ((left_is_nil||right_is_nil) && (!strcmp(n->str_val,"==")||!strcmp(n->str_val,"!="))) {
                acg_emit_expr(cg,n->children[1]);
                OUT(cg,"    pushq %%rax\n");
                acg_emit_expr(cg,n->children[0]);
                OUT(cg,"    popq %%rcx\n");
                OUT(cg,"    cmpq %%rcx, %%rax\n");
                if (!strcmp(n->str_val,"==")) OUT(cg,"    sete %%al\n");
                else                          OUT(cg,"    setne %%al\n");
                OUT(cg,"    movzbq %%al, %%rax\n");
                break;
            }
            
            if (is_str && (!strcmp(n->str_val,"==")||!strcmp(n->str_val,"!="))) {
                acg_emit_expr(cg,n->children[1]);
                OUT(cg,"    pushq %%rax\n");
                acg_emit_expr(cg,n->children[0]);
                OUT(cg,"    movq %%rax, %%rdi\n");
                OUT(cg,"    popq %%rsi\n");
                OUT(cg,"    call strcmp\n");
                OUT(cg,"    testq %%rax, %%rax\n");
                if (!strcmp(n->str_val,"==")) OUT(cg,"    sete %%al\n");
                else                          OUT(cg,"    setne %%al\n");
                OUT(cg,"    movzbq %%al, %%rax\n");
                break;
            }

            if (is_float) {
                
                acg_emit_expr(cg,n->children[1]);
                OUT(cg,"    movsd %%xmm0, -8(%%rsp)\n");  
                acg_emit_expr(cg,n->children[0]);
                
                OUT(cg,"    movsd -8(%%rsp), %%xmm1\n");
                if      (!strcmp(n->str_val,"+"))  OUT(cg,"    addsd %%xmm1, %%xmm0\n");
                else if (!strcmp(n->str_val,"-"))  OUT(cg,"    subsd %%xmm1, %%xmm0\n");
                else if (!strcmp(n->str_val,"*"))  OUT(cg,"    mulsd %%xmm1, %%xmm0\n");
                else if (!strcmp(n->str_val,"/"))  OUT(cg,"    divsd %%xmm1, %%xmm0\n");
                else if (!strcmp(n->str_val,"==")||!strcmp(n->str_val,"!=")||
                         !strcmp(n->str_val,"<") ||!strcmp(n->str_val,">")||
                         !strcmp(n->str_val,"<=")||!strcmp(n->str_val,">=")) {
                    OUT(cg,"    ucomisd %%xmm1, %%xmm0\n");
                    if      (!strcmp(n->str_val,"==")) OUT(cg,"    sete %%al\n");
                    else if (!strcmp(n->str_val,"!=")) OUT(cg,"    setne %%al\n");
                    else if (!strcmp(n->str_val,"<"))  OUT(cg,"    setb %%al\n");
                    else if (!strcmp(n->str_val,">"))  OUT(cg,"    seta %%al\n");
                    else if (!strcmp(n->str_val,"<=")) OUT(cg,"    setbe %%al\n");
                    else if (!strcmp(n->str_val,">=")) OUT(cg,"    setae %%al\n");
                    OUT(cg,"    movzbq %%al, %%rax\n");
                }
                break;
            }

            
            acg_emit_expr(cg,n->children[1]);
            OUT(cg,"    pushq %%rax\n");
            acg_emit_expr(cg,n->children[0]);
            OUT(cg,"    popq %%rcx\n");

            if      (!strcmp(n->str_val,"+"))  OUT(cg,"    addq %%rcx, %%rax\n");
            else if (!strcmp(n->str_val,"-"))  OUT(cg,"    subq %%rcx, %%rax\n");
            else if (!strcmp(n->str_val,"*"))  OUT(cg,"    imulq %%rcx, %%rax\n");
            else if (!strcmp(n->str_val,"/"))  { OUT(cg,"    cqto\n"); OUT(cg,"    idivq %%rcx\n"); }
            else if (!strcmp(n->str_val,"%"))  { OUT(cg,"    cqto\n"); OUT(cg,"    idivq %%rcx\n"); OUT(cg,"    movq %%rdx, %%rax\n"); }
            else if (!strcmp(n->str_val,"&&")) OUT(cg,"    andq %%rcx, %%rax\n");
            else if (!strcmp(n->str_val,"||")) OUT(cg,"    orq %%rcx, %%rax\n");
            /* bitwise operators */
            else if (!strcmp(n->str_val,"&"))  OUT(cg,"    andq %%rcx, %%rax\n");
            else if (!strcmp(n->str_val,"|"))  OUT(cg,"    orq %%rcx, %%rax\n");
            else if (!strcmp(n->str_val,"^"))  OUT(cg,"    xorq %%rcx, %%rax\n");
            else if (!strcmp(n->str_val,"<<")) OUT(cg,"    shlq %%cl, %%rax\n");
            else if (!strcmp(n->str_val,">>")) OUT(cg,"    sarq %%cl, %%rax\n");
            else if (!strcmp(n->str_val,"==")||!strcmp(n->str_val,"!=")||
                     !strcmp(n->str_val,"<") ||!strcmp(n->str_val,">")||
                     !strcmp(n->str_val,"<=")||!strcmp(n->str_val,">=")) {
                OUT(cg,"    cmpq %%rcx, %%rax\n");
                if      (!strcmp(n->str_val,"==")) OUT(cg,"    sete %%al\n");
                else if (!strcmp(n->str_val,"!=")) OUT(cg,"    setne %%al\n");
                else if (!strcmp(n->str_val,"<"))  OUT(cg,"    setl %%al\n");
                else if (!strcmp(n->str_val,">"))  OUT(cg,"    setg %%al\n");
                else if (!strcmp(n->str_val,"<=")) OUT(cg,"    setle %%al\n");
                else if (!strcmp(n->str_val,">=")) OUT(cg,"    setge %%al\n");
                OUT(cg,"    movzbq %%al, %%rax\n");
            }
            break;
        }

        
        case NODE_UNOP:
            acg_emit_expr(cg,n->children[0]);
            if (!strcmp(n->str_val,"not")||!strcmp(n->str_val,"!")) {
                OUT(cg,"    testq %%rax, %%rax\n");
                OUT(cg,"    sete %%al\n");
                OUT(cg,"    movzbq %%al, %%rax\n");
            } else if (!strcmp(n->str_val,"~")) {
                OUT(cg,"    notq %%rax\n");
            } else {
                OUT(cg,"    negq %%rax\n");
            }
            break;

        
        case NODE_CONCAT: {
            
            acg_emit_expr(cg,n->children[1]);
            { const char *rt=acg_node_type(cg,n->children[1]);
              if (acg_is_float_type(rt)) { OUT(cg,"    call usagi_tostr_f\n"); }
              else if (!acg_is_str_type(rt)) { OUT(cg,"    movq %%rax, %%rdi\n"); OUT(cg,"    call usagi_tostr\n"); }
            }
            OUT(cg,"    pushq %%rax\n");
            
            acg_emit_expr(cg,n->children[0]);
            { const char *lt=acg_node_type(cg,n->children[0]);
              if (acg_is_float_type(lt)) { OUT(cg,"    call usagi_tostr_f\n"); }
              else if (!acg_is_str_type(lt)) { OUT(cg,"    movq %%rax, %%rdi\n"); OUT(cg,"    call usagi_tostr\n"); }
            }
            OUT(cg,"    movq %%rax, %%rdi\n");
            OUT(cg,"    popq %%rsi\n");
            OUT(cg,"    call usagi_concat\n");
            break;
        }

        
        case NODE_FUNC_CALL: {
            
            if (!strcmp(n->str_val,"len")&&n->child_count==1&&n->children[0]->type==NODE_IDENT) {
                char lenname[256]; snprintf(lenname,sizeof(lenname),"%s_len",n->children[0]->str_val);
                int off3=acg_local_offset(cg,lenname);
                if (off3==INT_MIN) OUT(cg,"    movq %s_len_g(%%rip), %%rax\n", n->children[0]->str_val);
                else        OUT(cg,"    movq %d(%%rbp), %%rax\n", off3);
                break;
            }

            
            int argc=n->child_count;
            
            
            
            int int_arg=0, float_arg=0;
            
            
            for (int i=0;i<argc;i++) {
                acg_emit_expr(cg,n->children[i]);
                const char *at=acg_node_type(cg,n->children[i]);
                if (acg_is_float_type(at)) {
                    OUT(cg,"    subq $8, %%rsp\n");
                    OUT(cg,"    movsd %%xmm0, (%%rsp)\n");
                } else {
                    OUT(cg,"    pushq %%rax\n");
                }
            }
            
            int_arg=0; float_arg=0;
            
            
            
            
            
            
            
            int_arg=0; float_arg=0;
            for (int i=0;i<argc&&i<6;i++) {
                const char *at=acg_node_type(cg,n->children[i]);
                int stack_pos=(argc-1-i)*8;  
                if (acg_is_float_type(at)) {
                    if (float_arg<8) OUT(cg,"    movsd %d(%%rsp), %%%s\n", stack_pos, xmm_arg_regs[float_arg++]);
                } else {
                    if (int_arg<6) OUT(cg,"    movq %d(%%rsp), %%%s\n", stack_pos, int_arg_regs[int_arg++]);
                }
            }
            
            if (argc>0) {
                
                int extra=(argc*8)%16; if(extra) OUT(cg,"    subq $%d, %%rsp\n",16-extra);
            } else {
                
                OUT(cg,"    subq $8, %%rsp\n");
            }
            OUT(cg,"    xorb %%al, %%al\n");  
            OUT(cg,"    call %s\n", acg_safe(n->str_val));
            
            if (argc>0) {
                int extra=(argc*8)%16;
                int total=argc*8+(extra?(16-extra):0);
                OUT(cg,"    addq $%d, %%rsp\n", total);
            } else {
                OUT(cg,"    addq $8, %%rsp\n");
            }
            break;
        }

        
        case NODE_DICT_LIT: {
            int npairs=n->child_count/2;
            
            OUT(cg,"    call usagi_dict_new\n");
            OUT(cg,"    pushq %%rax\n");  
            
            for (int i=0;i<npairs;i++) {
                
                OUT(cg,"    movq (%%rsp), %%rdi\n");  
                acg_emit_expr(cg,n->children[i*2]);    
                OUT(cg,"    movq %%rax, %%rsi\n");
                acg_emit_expr(cg,n->children[i*2+1]);  
                OUT(cg,"    movq %%rax, %%rdx\n");
                OUT(cg,"    call usagi_dict_set\n");
            }
            OUT(cg,"    popq %%rax\n");  
            break;
        }

        default:
            OUT(cg,"    xorq %%rax, %%rax\n");
            break;
    }
}

static void acg_store_var(AsmCG *cg, const char *name, const char *type) {
    int off=acg_local_offset(cg,name);
    if (off==INT_MIN) {
        
        if (acg_is_float_type(type)) OUT(cg,"    movsd %%xmm0, %s_g(%%rip)\n",name);
        else                         OUT(cg,"    movq %%rax, %s_g(%%rip)\n",name);
    } else {
        if (acg_is_float_type(type)) OUT(cg,"    movsd %%xmm0, %d(%%rbp)\n",off);
        else                         OUT(cg,"    movq %%rax, %d(%%rbp)\n",off);
    }
}

static void acg_emit_block(AsmCG *cg, Node *block) {
    if (!block) return;
    for (int i=0;i<block->child_count;i++) acg_emit_stmt(cg,block->children[i]);
}

static void acg_emit_stmt(AsmCG *cg, Node *n) {
    if (!n) return;
    switch (n->type) {

        case NODE_PULL: break;  

        
        case NODE_STRUCT_DEF:
            asm_struct_add(n->str_val,n->params,n->param_types,n->param_count);
            break;

        
        case NODE_VAR_DECL: {
            const char *t=n->var_type?n->var_type:"int";
            if (cg->in_func) {
                int sz=8; 
                int off=acg_alloc_local(cg,n->str_val,sz);
                asm_sym_add(&cg->locals,n->str_val,t);
                
                OUT(cg,"    movq $0, %d(%%rbp)\n",off);
                
                if (strlen(t)>2&&!strcmp(t+strlen(t)-2,"[]")) {
                    char lenname[256]; snprintf(lenname,sizeof(lenname),"%s_len",n->str_val);
                    acg_alloc_local(cg,lenname,8);
                    OUT(cg,"    movq $0, %d(%%rbp)\n",acg_local_offset(cg,lenname));
                }
            } else {
                asm_sym_add(&cg->globals,n->str_val,t);
            }
            break;
        }

        
        case NODE_TYPE_INFER: {
            const char *t=acg_node_type(cg,n->children[0]);
            if (!t) t="int";
            if (cg->in_func) {
                acg_alloc_local(cg,n->str_val,8);
                asm_sym_add(&cg->locals,n->str_val,t);
                acg_emit_expr(cg,n->children[0]);
                acg_store_var(cg,n->str_val,t);
            } else {
                asm_sym_add(&cg->globals,n->str_val,t);
                
            }
            break;
        }

        
        case NODE_ASSIGN: {
            if (n->var_type&&!strcmp(n->var_type,"indexed")) {
                
                acg_emit_expr(cg,n->children[1]);  
                OUT(cg,"    pushq %%rax\n");
                acg_emit_expr(cg,n->children[0]);  
                OUT(cg,"    pushq %%rax\n");
                
                int off4=acg_local_offset(cg,n->str_val);
                if (off4==INT_MIN) OUT(cg,"    movq %s_g(%%rip), %%rax\n",n->str_val);
                else        OUT(cg,"    movq %d(%%rbp), %%rax\n",off4);
                OUT(cg,"    popq %%rcx\n");  
                OUT(cg,"    popq %%rdx\n");  
                OUT(cg,"    movq %%rdx, (%%rax,%%rcx,8)\n");
                break;
            }
            if (n->children[0]&&n->children[0]->type==NODE_ARRAY_LIT) {
                
                Node *arr=n->children[0];
                int count=arr->child_count;
                OUT(cg,"    movq $%d, %%rdi\n",count*8);
                OUT(cg,"    call malloc\n");
                acg_store_var(cg,n->str_val,"int[]");
                
                char lenname[256]; snprintf(lenname,sizeof(lenname),"%s_len",n->str_val);
                int loff=acg_local_offset(cg,lenname);
                if (loff==INT_MIN) { OUT(cg,"    movq $%d, %%rax\n",count); OUT(cg,"    movq %%rax, %s_len_g(%%rip)\n",n->str_val); }
                else        { OUT(cg,"    movq $%d, %d(%%rbp)\n",count,loff); }
                
                for (int i=0;i<count;i++) {
                    acg_emit_expr(cg,arr->children[i]);
                    OUT(cg,"    pushq %%rax\n");
                }
                
                int off5=acg_local_offset(cg,n->str_val);
                if (off5==INT_MIN) OUT(cg,"    movq %s_g(%%rip), %%rbx\n",n->str_val);
                else        OUT(cg,"    movq %d(%%rbp), %%rbx\n",off5);
                for (int i=count-1;i>=0;i--) {
                    OUT(cg,"    popq %%rax\n");
                    OUT(cg,"    movq %%rax, %d(%%rbx)\n",i*8);
                }
                break;
            }
            const char *vtype=asm_sym_get(&cg->locals,n->str_val);
            if (!vtype) vtype=asm_sym_get(&cg->globals,n->str_val);
            acg_emit_expr(cg,n->children[0]);
            if (!vtype) vtype=acg_node_type(cg,n->children[0]);
            acg_store_var(cg,n->str_val,vtype?vtype:"int");
            break;
        }

        
        case NODE_FIELD_ASSIGN: {
            acg_emit_expr(cg,n->children[0]);  
            OUT(cg,"    pushq %%rax\n");
            
            int off6=acg_local_offset(cg,n->str_val);
            if (off6==INT_MIN) OUT(cg,"    movq %s_g(%%rip), %%rax\n",n->str_val);
            else        OUT(cg,"    movq %d(%%rbp), %%rax\n",off6);
            
            const char *vt=asm_sym_get(&cg->locals,n->str_val);
            if (!vt) vt=asm_sym_get(&cg->globals,n->str_val);
            char base[128]; strncpy(base,vt?vt:"",127); base[127]=0;
            size_t bl2=strlen(base); if(bl2>0&&base[bl2-1]=='?')base[bl2-1]=0;
            AsmStructDef *sd=asm_struct_get(base);
            int foff=sd?asm_struct_field_offset(sd,n->var_type):0;
            OUT(cg,"    popq %%rcx\n");
            OUT(cg,"    movq %%rcx, %d(%%rax)\n",foff);
            break;
        }

        
        case NODE_TERMINAL_PRINT: {
            
            
            if (n->child_count==0) {
                int nl_id=intern_string("\n");
                OUT(cg,"    leaq .Ls%d(%%rip), %%rdi\n",nl_id);
                OUT(cg,"    call puts\n");
                break;
            }
            
            int sp_id=intern_string(" ");
            int nl_id=intern_string("");
            
            OUT(cg,"    leaq .Ls%d(%%rip), %%rax\n",nl_id);
            OUT(cg,"    pushq %%rax\n");
            for (int i=0;i<n->child_count;i++) {
                acg_emit_expr(cg,n->children[i]);
                const char *at=acg_node_type(cg,n->children[i]);
                
                if (acg_is_float_type(at)) {
                    OUT(cg,"    call usagi_tostr_f\n");
                } else if (!acg_is_str_type(at)) {
                    OUT(cg,"    movq %%rax, %%rdi\n");
                    OUT(cg,"    call usagi_tostr\n");
                }
                OUT(cg,"    movq %%rax, %%rsi\n");
                OUT(cg,"    popq %%rdi\n");
                OUT(cg,"    call usagi_concat\n");
                OUT(cg,"    pushq %%rax\n");
                
                if (i<n->child_count-1) {
                    OUT(cg,"    leaq .Ls%d(%%rip), %%rsi\n",sp_id);
                    OUT(cg,"    movq %%rax, %%rdi\n");  
                    OUT(cg,"    popq %%rdi\n");
                    OUT(cg,"    call usagi_concat\n");
                    OUT(cg,"    pushq %%rax\n");
                }
            }
            OUT(cg,"    popq %%rdi\n");
            OUT(cg,"    call puts\n");  
            break;
        }

        
        case NODE_TERMINAL_INPUT: {
            
            acg_emit_expr(cg,n->children[0]);
            OUT(cg,"    movq %%rax, %%rdi\n");
            OUT(cg,"    call usagi_print_prompt\n");
            
            OUT(cg,"    call usagi_readline\n");
            const char *vname=n->children[1]->str_val;
            const char *vtype=asm_sym_get(&cg->locals,vname);
            if (!vtype) vtype=asm_sym_get(&cg->globals,vname);
            acg_store_var(cg,vname,vtype?vtype:"str");
            break;
        }

        
        case NODE_IF: {
            int lelse=asm_new_label(), lend=asm_new_label();
            acg_emit_expr(cg,n->cond);
            OUT(cg,"    testq %%rax, %%rax\n");
            OUT(cg,"    jz .L%d\n",lelse);
            acg_emit_block(cg,n->body);
            if (n->else_body) OUT(cg,"    jmp .L%d\n",lend);
            OUT(cg,".L%d:\n",lelse);
            if (n->else_body) { acg_emit_block(cg,n->else_body); OUT(cg,".L%d:\n",lend); }
            break;
        }

        
        case NODE_WHILE: {
            int ltop=asm_new_label(), lend=asm_new_label();
            int prev_top=cg->loop_top_label, prev_end=cg->loop_end_label;
            cg->loop_top_label=ltop; cg->loop_end_label=lend;
            OUT(cg,".L%d:\n",ltop);
            acg_emit_expr(cg,n->cond);
            OUT(cg,"    testq %%rax, %%rax\n");
            OUT(cg,"    jz .L%d\n",lend);
            acg_emit_block(cg,n->body);
            OUT(cg,"    jmp .L%d\n",ltop);
            OUT(cg,".L%d:\n",lend);
            cg->loop_top_label=prev_top; cg->loop_end_label=prev_end;
            break;
        }

        
        case NODE_LOOP: {
            int ltop=asm_new_label(), lend=asm_new_label();
            int prev_top=cg->loop_top_label, prev_end=cg->loop_end_label;
            cg->loop_top_label=ltop; cg->loop_end_label=lend;
            OUT(cg,".L%d:\n",ltop);
            acg_emit_block(cg,n->body);
            OUT(cg,"    jmp .L%d\n",ltop);
            OUT(cg,".L%d:\n",lend);
            cg->loop_top_label=prev_top; cg->loop_end_label=prev_end;
            break;
        }

        
        case NODE_FOR_RANGE: {
            
            if (cg->in_func) {
                acg_alloc_local(cg,n->str_val,8);
                asm_sym_add(&cg->locals,n->str_val,"int");
            }
            acg_emit_expr(cg,n->init);
            acg_store_var(cg,n->str_val,"int");
            int ltop=asm_new_label(), lend=asm_new_label();
            int prev_top=cg->loop_top_label, prev_end=cg->loop_end_label;
            cg->loop_top_label=ltop; cg->loop_end_label=lend;
            OUT(cg,".L%d:\n",ltop);
            
            acg_emit_expr(cg,n->limit);
            OUT(cg,"    pushq %%rax\n");
            int off7=acg_local_offset(cg,n->str_val);
            if (off7==INT_MIN) OUT(cg,"    movq %s_g(%%rip), %%rax\n",n->str_val);
            else        OUT(cg,"    movq %d(%%rbp), %%rax\n",off7);
            OUT(cg,"    popq %%rcx\n");
            OUT(cg,"    cmpq %%rcx, %%rax\n");
            OUT(cg,"    jg .L%d\n",lend);
            acg_emit_block(cg,n->body);
            
            if (off7==INT_MIN) { OUT(cg,"    movq %s_g(%%rip), %%rax\n",n->str_val); OUT(cg,"    incq %%rax\n"); OUT(cg,"    movq %%rax, %s_g(%%rip)\n",n->str_val); }
            else        { OUT(cg,"    incq %d(%%rbp)\n",off7); }
            OUT(cg,"    jmp .L%d\n",ltop);
            OUT(cg,".L%d:\n",lend);
            cg->loop_top_label=prev_top; cg->loop_end_label=prev_end;
            break;
        }

        
        case NODE_FOR_IN: {
            const char *arr=(n->init&&n->init->str_val)?n->init->str_val:"arr";
            
            char idxname[256]; snprintf(idxname,sizeof(idxname),"_i_%s",n->str_val);
            acg_alloc_local(cg,idxname,8); asm_sym_add(&cg->locals,idxname,"int");
            acg_alloc_local(cg,n->str_val,8); asm_sym_add(&cg->locals,n->str_val,"int");
            OUT(cg,"    movq $0, %d(%%rbp)\n",acg_local_offset(cg,idxname));
            int ltop=asm_new_label(), lend=asm_new_label();
            int prev_top=cg->loop_top_label, prev_end=cg->loop_end_label;
            cg->loop_top_label=ltop; cg->loop_end_label=lend;
            OUT(cg,".L%d:\n",ltop);
            
            char lenname2[256]; snprintf(lenname2,sizeof(lenname2),"%s_len",arr);
            int loff2=acg_local_offset(cg,lenname2);
            if (loff2==INT_MIN) OUT(cg,"    movq %s_len_g(%%rip), %%rax\n",arr);
            else         OUT(cg,"    movq %d(%%rbp), %%rax\n",loff2);
            int ioff=acg_local_offset(cg,idxname);
            OUT(cg,"    movq %d(%%rbp), %%rcx\n",ioff);
            OUT(cg,"    cmpq %%rax, %%rcx\n");
            OUT(cg,"    jge .L%d\n",lend);
            
            int aoff=acg_local_offset(cg,arr);
            if (aoff==INT_MIN) OUT(cg,"    movq %s_g(%%rip), %%rax\n",arr);
            else        OUT(cg,"    movq %d(%%rbp), %%rax\n",aoff);
            OUT(cg,"    movq %d(%%rbp), %%rcx\n",ioff);
            OUT(cg,"    movq (%%rax,%%rcx,8), %%rax\n");
            int eoff=acg_local_offset(cg,n->str_val);
            OUT(cg,"    movq %%rax, %d(%%rbp)\n",eoff);
            acg_emit_block(cg,n->body);
            
            OUT(cg,"    incq %d(%%rbp)\n",ioff);
            OUT(cg,"    jmp .L%d\n",ltop);
            OUT(cg,".L%d:\n",lend);
            cg->loop_top_label=prev_top; cg->loop_end_label=prev_end;
            break;
        }

        
        case NODE_BREAK:
            OUT(cg,"    jmp .L%d\n",cg->loop_end_label); break;
        case NODE_CONTINUE:
            OUT(cg,"    jmp .L%d\n",cg->loop_top_label); break;

        
        case NODE_RETURN:
            if (n->child_count) {
                acg_emit_expr(cg,n->children[0]);
                if (acg_is_float_type(cg->func_ret_type)) {
                    
                }
            }
            OUT(cg,"    jmp .Lret_%s\n",cg->func_name?cg->func_name:"_");
            break;

        
        case NODE_FUNC_DEF: {
            int saved_in_func=cg->in_func;
            AsmSymTable saved_locals=cg->locals;
            int saved_lc=cg->local_count;
            int saved_ss=cg->stack_size;
            char *saved_fn=cg->func_name;
            const char *saved_ret=cg->func_ret_type;
            
            char *saved_local_names[256];
            int   saved_local_offsets[256];
            memcpy(saved_local_names,  cg->local_names,   sizeof(cg->local_names));
            memcpy(saved_local_offsets,cg->local_offsets, sizeof(cg->local_offsets));

            cg->in_func=1;
            memset(&cg->locals,0,sizeof(cg->locals));
            memset(cg->local_names,0,sizeof(cg->local_names));
            cg->local_count=0; cg->stack_size=0;
            cg->func_name=strdup(acg_safe(n->str_val));
            cg->func_ret_type=n->ret_type;

            asm_reg_func(n->str_val,n->ret_type);

            int is_main=!strcmp(n->str_val,"main");
            const char *fname=is_main?"usagi_main":acg_safe(n->str_val);

            OUT(cg,"\n    .globl %s\n    .type %s, @function\n%s:\n",fname,fname,fname);
            
            OUT(cg,"    pushq %%rbp\n");
            OUT(cg,"    movq %%rsp, %%rbp\n");

            
            const char *param_regs_int[6]={"rdi","rsi","rdx","rcx","r8","r9"};
            int int_p=0, float_p=0;
            for (int i=0;i<n->param_count;i++) {
                acg_alloc_local(cg,n->params[i],8);
                asm_sym_add(&cg->locals,n->params[i],n->param_types[i]);
            }
            
            
            
            OUT(cg,"    subq $1024, %%rsp\n");

            
            
            fprintf(stderr,"DEBUG func=%s params=%d local_count=%d\n",n->str_val,n->param_count,cg->local_count);
            for (int _d=0;_d<cg->local_count;_d++) fprintf(stderr,"  local[%d]='%s' off=%d\n",_d,cg->local_names[_d]?cg->local_names[_d]:"NULL",cg->local_offsets[_d]);
            int_p=0; float_p=0;
            for (int i=0;i<n->param_count;i++) {
                int poff=acg_local_offset(cg,n->params[i]);
                if (acg_is_float_type(n->param_types[i])) {
                    if (float_p<8) OUT(cg,"    movsd %%%s, %d(%%rbp)\n",xmm_arg_regs[float_p++],poff);
                } else {
                    if (int_p<6) OUT(cg,"    movq %%%s, %d(%%rbp)\n",param_regs_int[int_p++],poff);
                }
            }

            acg_emit_block(cg,n->body);

            
            OUT(cg,".Lret_%s:\n",fname);
            OUT(cg,"    movq %%rbp, %%rsp\n");
            OUT(cg,"    popq %%rbp\n");
            OUT(cg,"    ret\n");
            OUT(cg,"    .size %s, .-%s\n",fname,fname);

            
            
            for (int i=0;i<cg->local_count;i++) if(cg->local_names[i]){free(cg->local_names[i]);cg->local_names[i]=NULL;}
            asm_sym_clear(&cg->locals);
            cg->locals=saved_locals;
            memcpy(cg->local_names,  saved_local_names,   sizeof(cg->local_names));
            memcpy(cg->local_offsets,saved_local_offsets, sizeof(cg->local_offsets));
            cg->local_count=saved_lc;
            cg->stack_size=saved_ss;
            free(cg->func_name); cg->func_name=saved_fn;
            cg->func_ret_type=saved_ret;
            cg->in_func=saved_in_func;
            break;
        }

        
        case NODE_MATCH: {
            int lend=asm_new_label();
            acg_emit_expr(cg,n->cond);
            OUT(cg,"    pushq %%rax\n");  
            const char *st=acg_node_type(cg,n->cond);
            for (int i=0;i<n->child_count;i++) {
                Node *arm=n->children[i];
                int lskip=asm_new_label();
                if (arm->str_val&&strcmp(arm->str_val,"_")) {
                    OUT(cg,"    movq (%%rsp), %%rax\n");  
                    if (acg_is_str_type(st)||!strcmp(arm->var_type,"str")) {
                        int pid=intern_string(arm->str_val);
                        OUT(cg,"    movq %%rax, %%rdi\n");
                        OUT(cg,"    leaq .Ls%d(%%rip), %%rsi\n",pid);
                        OUT(cg,"    call strcmp\n");
                        OUT(cg,"    testq %%rax, %%rax\n");
                        OUT(cg,"    jnz .L%d\n",lskip);
                    } else {
                        OUT(cg,"    cmpq $%s, %%rax\n",arm->str_val);
                        OUT(cg,"    jne .L%d\n",lskip);
                    }
                }
                acg_emit_block(cg,arm->body);
                OUT(cg,"    jmp .L%d\n",lend);
                OUT(cg,".L%d:\n",lskip);
            }
            OUT(cg,"    addq $8, %%rsp\n");  
            OUT(cg,".L%d:\n",lend);
            break;
        }

        case NODE_FUNC_CALL:
            acg_emit_expr(cg,n); break;

        case NODE_BLOCK:
            acg_emit_block(cg,n); break;

        default: break;
    }
}

static void acg_emit_globals(AsmCG *cg, Node *prog) {
    OUT(cg,"    .bss\n");
    for (int i=0;i<prog->child_count;i++) {
        Node *n=prog->children[i];
        if (n->type==NODE_VAR_DECL) {
            asm_sym_add(&cg->globals,n->str_val,n->var_type?n->var_type:"int");
            OUT(cg,"    .globl %s_g\n    .align 8\n%s_g: .zero 8\n",n->str_val,n->str_val);
            
            const char *t=n->var_type?n->var_type:"int";
            if (strlen(t)>2&&!strcmp(t+strlen(t)-2,"[]"))
                OUT(cg,"    .globl %s_len_g\n    .align 8\n%s_len_g: .zero 8\n",n->str_val,n->str_val);
        }
        if (n->type==NODE_TYPE_INFER) {
            asm_sym_add(&cg->globals,n->str_val,"int");
            OUT(cg,"    .globl %s_g\n    .align 8\n%s_g: .zero 8\n",n->str_val,n->str_val);
        }
    }
}

static void acg_emit_strlits(AsmCG *cg) {
    OUT(cg,"    .section .rodata\n");
    for (int i=0;i<str_lit_count;i++) {
        OUT(cg,".Ls%d:\n",i);
        OUT(cg,"    .asciz \"");
        for (const char *p=str_lits[i].value;*p;p++) {
            if      (*p=='"')  OUT(cg,"\\\"");
            else if (*p=='\\') OUT(cg,"\\\\");
            else if (*p=='\n') OUT(cg,"\\n");
            else if (*p=='\t') OUT(cg,"\\t");
            else               fputc(*p,cg->out);
        }
        OUT(cg,"\"\n");
    }
}

static void acg_emit_runtime(AsmCG *cg) {
    fputs(
    "    .text\n"
    "/* ── usagi runtime ──────────────────────────────────── */\n"

    
    "    .globl usagi_concat\n"
    "    .type usagi_concat, @function\n"
    "usagi_concat:\n"
    "    pushq %rbp\n    movq %rsp, %rbp\n"
    "    pushq %rbx\n    pushq %r12\n"
    "    movq %rdi, %rbx\n"   
    "    movq %rsi, %r12\n"   
    "    testq %rbx, %rbx\n    jnz .Lca1\n"
    "    leaq .Lrempty(%rip), %rbx\n"
    ".Lca1:\n"
    "    testq %r12, %r12\n    jnz .Lca2\n"
    "    leaq .Lrempty(%rip), %r12\n"
    ".Lca2:\n"
    "    movq %rbx, %rdi\n    call strlen\n    movq %rax, %r13\n"
    "    movq %r12, %rdi\n    call strlen\n    addq %rax, %r13\n"
    "    leaq 1(%r13), %rdi\n    call malloc\n"
    "    movq %rax, %rdi\n    movq %rbx, %rsi\n    call strcpy\n"
    "    movq %rax, %rdi\n    movq %r12, %rsi\n    call strcat\n"
    "    popq %r12\n    popq %rbx\n"
    "    popq %rbp\n    ret\n"
    "    .size usagi_concat, .-usagi_concat\n"

    
    "    .globl usagi_tostr\n"
    "    .type usagi_tostr, @function\n"
    "usagi_tostr:\n"
    "    pushq %rbp\n    movq %rsp, %rbp\n"
    "    pushq %rbx\n    pushq %r12\n"
    "    movq %rdi, %rbx\n"
    "    movq $32, %rdi\n    call malloc\n"
    "    movq %rax, %r12\n"
    
    "    movq %r12, %rdi\n"
    "    movq $32, %rsi\n"
    "    leaq .Lfmt_ld(%rip), %rdx\n"
    "    movq %rbx, %rcx\n"
    "    xorb %al, %al\n"
    "    call snprintf\n"
    "    movq %r12, %rax\n"
    "    popq %r12\n    popq %rbx\n    popq %rbp\n    ret\n"
    "    .size usagi_tostr, .-usagi_tostr\n"

    
    "    .globl usagi_tostr_f\n"
    "    .type usagi_tostr_f, @function\n"
    "usagi_tostr_f:\n"
    "    pushq %rbp\n    movq %rsp, %rbp\n"
    "    pushq %r12\n    pushq %r13\n"    "    movq $32, %rdi\n    call malloc\n"
    "    movq %rax, %r12\n"
    "    movq %r12, %rdi\n"
    "    movq $32, %rsi\n"
    "    leaq .Lfmt_g(%rip), %rdx\n"
    "    movb $1, %al\n"
    "    call snprintf\n"
    "    movq %r12, %rax\n"
    "    popq %r13\n    popq %r12\n    popq %rbp\n    ret\n"
    "    .size usagi_tostr_f, .-usagi_tostr_f\n"

    
    "    .globl usagi_readline\n"
    "    .type usagi_readline, @function\n"
    "usagi_readline:\n"
    "    pushq %rbp\n    movq %rsp, %rbp\n"
    "    movq $4096, %rdi\n    call malloc\n"
    "    pushq %rax\n"
    "    movq %rax, %rdi\n    movq $4096, %rsi\n"
    "    movq stdin(%rip), %rdx\n"
    "    call fgets\n"
    "    popq %rax\n"
    "    testq %rax, %rax\n    jz .Lrl_done\n"
    "    movq %rax, %rdi\n    call strlen\n"
    "    testq %rax, %rax\n    jz .Lrl_done\n"
    "    movq (%rsp), %rdi\n"
    "    leaq -1(%rax), %rcx\n"
    "    movq %rdi, %rsi\n"
    "    addq %rcx, %rsi\n"
    "    cmpb $0x0a, (%rsi)\n    jne .Lrl_done\n"
    "    movb $0, (%rsi)\n"
    ".Lrl_done:\n"
    "    popq %rax\n    popq %rbp\n    ret\n"
    "    .size usagi_readline, .-usagi_readline\n"

    
    "    .globl usagi_print_prompt\n"
    "    .type usagi_print_prompt, @function\n"
    "usagi_print_prompt:\n"
    "    pushq %rbp\n    movq %rsp, %rbp\n"
    "    movq stdout(%rip), %rsi\n"
    "    call fputs\n"
    "    leaq .Lrsp(%rip), %rdi\n"
    "    movq stdout(%rip), %rsi\n"
    "    call fputs\n"
    "    movq stdout(%rip), %rdi\n"
    "    call fflush\n"
    "    popq %rbp\n    ret\n"
    "    .size usagi_print_prompt, .-usagi_print_prompt\n"

    

    

    "    .section .rodata\n"
    ".Lfmt_ld:  .asciz \"%ld\"\n"
    ".Lfmt_g:   .asciz \"%g\"\n"
    ".Lrempty:  .asciz \"\"\n"
    ".Lrsp:     .asciz \" \"\n"
    , cg->out);
}

static void asm_codegen(Node *prog, FILE *out) {
    AsmCG cg={0};
    cg.out=out;
    cg.loop_end_label=-1;
    cg.loop_top_label=-1;

    
    for (int i=0;i<prog->child_count;i++) {
        Node *n=prog->children[i];
        if (n->type==NODE_STRUCT_DEF)
            asm_struct_add(n->str_val,n->params,n->param_types,n->param_count);
        if (n->type==NODE_FUNC_DEF)
            asm_reg_func(n->str_val,n->ret_type);
    }

    fprintf(out,"    .file \"usagi_out.s\"\n");

    
    acg_emit_runtime(&cg);

    
    fprintf(out,"\n    .text\n");

    
    for (int i=0;i<prog->child_count;i++) {
        Node *n=prog->children[i];
        if (n->type==NODE_STRUCT_DEF)
            asm_struct_add(n->str_val,n->params,n->param_types,n->param_count);
    }

    
    for (int i=0;i<prog->child_count;i++) {
        Node *n=prog->children[i];
        if (n->type==NODE_FUNC_DEF) acg_emit_stmt(&cg,n);
    }

    
    fprintf(out,
    "\n    .globl main\n    .type main, @function\nmain:\n"
    "    pushq %%rbp\n    movq %%rsp, %%rbp\n"
    "    subq $8, %%rsp\n"
    "    call __usagi_init\n"
    );
    int has_main=0;
    for (int i=0;i<prog->child_count;i++)
        if (prog->children[i]->type==NODE_FUNC_DEF&&!strcmp(prog->children[i]->str_val,"main")) has_main=1;
    if (has_main) fprintf(out,"    call usagi_main\n");
    fprintf(out,
    "    xorq %%rax, %%rax\n"
    "    movq %%rbp, %%rsp\n    popq %%rbp\n    ret\n"
    "    .size main, .-main\n"
    );

    
    fprintf(out,
    "\n    .globl __usagi_init\n    .type __usagi_init, @function\n__usagi_init:\n"
    "    pushq %%rbp\n    movq %%rsp, %%rbp\n"
    "    subq $256, %%rsp\n"
    );
    cg.in_func=1; cg.local_count=0; cg.stack_size=0;
    for (int i=0;i<prog->child_count;i++) {
        Node *n=prog->children[i];
        if (n->type==NODE_ASSIGN||n->type==NODE_FUNC_CALL||
            n->type==NODE_TERMINAL_PRINT||n->type==NODE_IF||
            n->type==NODE_WHILE||n->type==NODE_FOR_RANGE||n->type==NODE_LOOP) {
            acg_emit_stmt(&cg,n);
        }
        if (n->type==NODE_TYPE_INFER) {
            const char *t=acg_node_type(&cg,n->children[0]);
            if (!t) t="int";
            acg_emit_expr(&cg,n->children[0]);
            fprintf(out,"    movq %%rax, %s_g(%%rip)\n",n->str_val);
        }
    }
    fprintf(out,
    "    movq %%rbp, %%rsp\n    popq %%rbp\n    ret\n"
    "    .size __usagi_init, .-__usagi_init\n"
    );
    cg.in_func=0;

    
    acg_emit_globals(&cg,prog);

    
    acg_emit_strlits(&cg);

    fprintf(out,"    .section .note.GNU-stack,\"\",@progbits\n");

    asm_sym_clear(&cg.globals);
}
