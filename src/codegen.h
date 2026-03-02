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
#include <string.h>
#include <stdlib.h>

typedef struct { char **names; char **types; int count, cap; } SymTable;
static void sym_add(SymTable *st, const char *name, const char *type) {
    for (int i=0;i<st->count;i++)
        if (!strcmp(st->names[i],name)) { free(st->types[i]); st->types[i]=strdup(type); return; }
    if (st->count>=st->cap) {
        st->cap=st->cap?st->cap*2:16;
        st->names=realloc(st->names,st->cap*sizeof(char*));
        st->types=realloc(st->types,st->cap*sizeof(char*));
    }
    st->names[st->count]=strdup(name); st->types[st->count]=strdup(type); st->count++;
}
static const char *sym_get(SymTable *st, const char *name) {
    for (int i=0;i<st->count;i++) if(!strcmp(st->names[i],name)) return st->types[i];
    return NULL;
}
static void sym_clear(SymTable *st) {
    for (int i=0;i<st->count;i++) { free(st->names[i]); free(st->types[i]); }
    free(st->names); free(st->types); st->names=NULL; st->types=NULL; st->count=st->cap=0;
}

typedef struct { char *name; char **field_names; char **field_types; int field_count; } CgStructDef;
static CgStructDef *cg_structs=NULL; static int cg_struct_count=0, cg_struct_cap=0;
static void cg_struct_add(const char *name, char **fnames, char **ftypes, int fc) {
    for (int i=0;i<cg_struct_count;i++) if(!strcmp(cg_structs[i].name,name)) return;
    if (cg_struct_count>=cg_struct_cap) { cg_struct_cap=cg_struct_cap?cg_struct_cap*2:8; cg_structs=realloc(cg_structs,cg_struct_cap*sizeof(CgStructDef)); }
    CgStructDef *sd=&cg_structs[cg_struct_count++];
    sd->name=strdup(name); sd->field_count=fc;
    sd->field_names=malloc(fc*sizeof(char*)); sd->field_types=malloc(fc*sizeof(char*));
    for (int i=0;i<fc;i++) { sd->field_names[i]=strdup(fnames[i]); sd->field_types[i]=strdup(ftypes[i]); }
}
static CgStructDef *cg_struct_get(const char *name) {
    for (int i=0;i<cg_struct_count;i++) if(!strcmp(cg_structs[i].name,name)) return &cg_structs[i];
    return NULL;
}

static SymTable cg_func_rets={0};
static void cg_reg_func(const char *name, const char *ret) { sym_add(&cg_func_rets,name,ret?ret:"void"); }
static const char *cg_func_ret(const char *name) { return sym_get(&cg_func_rets,name); }

static const char *strip_nullable(const char *t, char *buf, size_t sz) {
    strncpy(buf,t,sz-1); buf[sz-1]=0;
    size_t l=strlen(buf); if (l>0&&buf[l-1]=='?') buf[l-1]=0;
    return buf;
}

static void dict_kv(const char *t, char *k, char *v, size_t sz) {
    k[0]=v[0]=0;
    if (strncmp(t,"dict<",5)!=0) { strncpy(k,"str",sz); strncpy(v,"str",sz); return; }
    const char *p=t+5;
    const char *comma=strchr(p,',');
    if (!comma) { strncpy(k,"str",sz); strncpy(v,"str",sz); return; }
    size_t kl=comma-p; if(kl>=sz) kl=sz-1;
    strncpy(k,p,kl); k[kl]=0;
    const char *vstart=comma+1;
    const char *close=strchr(vstart,'>');
    if (!close) { strncpy(v,vstart,sz-1); v[sz-1]=0; return; }
    size_t vl=close-vstart; if(vl>=sz) vl=sz-1;
    strncpy(v,vstart,vl); v[vl]=0;
}

static const char *usagi_type_to_c(const char *t) {
    static char ret_buf[256];
    if (!t) return "void";
    char base[128]; strip_nullable(t,base,sizeof(base));
    if (!strcmp(base,"int"))   return "long";
    if (!strcmp(base,"float")) return "double";
    if (!strcmp(base,"str"))   return "char *";
    if (!strcmp(base,"bool"))  return "int";
    if (!strcmp(base,"void"))  return "void";
    if (!strncmp(base,"dict",4)) return "UsagiDict *";
    
    CgStructDef *sd=cg_struct_get(base);
    if (sd) { snprintf(ret_buf,sizeof(ret_buf),"Usagi_%s *",base); return ret_buf; }
    snprintf(ret_buf,sizeof(ret_buf),"%s",base); return ret_buf;
}

static const char *node_usagi_type(Node *n, SymTable *locals, SymTable *globals);
static const char *node_usagi_type(Node *n, SymTable *locals, SymTable *globals) {
    if (!n) return NULL;
    switch (n->type) {
        case NODE_INT_LIT:   return "int";
        case NODE_FLOAT_LIT: return "float";
        case NODE_STR_LIT:   return "str";
        case NODE_BOOL_LIT:  return "bool";
        case NODE_FSTRING:   return "str";
        case NODE_DICT_LIT:  return "dict";
        case NODE_NIL_LIT:   return "nil";
        case NODE_STRUCT_INIT: return n->str_val;
        case NODE_IDENT: { const char *t=sym_get(locals,n->str_val); if(!t)t=sym_get(globals,n->str_val); return t; }
        case NODE_INDEX: {
            const char *at=node_usagi_type(n->children[0],locals,globals); if(!at)return NULL;
            size_t l=strlen(at);
            if (l>2&&at[l-2]=='['&&at[l-1]==']') { static char base[64]; size_t bl=l-2; if(bl>63)bl=63; memcpy(base,at,bl); base[bl]=0; return base; }
            if (!strncmp(at,"dict",4)) return NULL;
            return at;
        }
        case NODE_FIELD_ACCESS: {
            const char *ot=node_usagi_type(n->children[0],locals,globals); if(!ot)return NULL;
            char base[128]; strip_nullable(ot,base,sizeof(base));
            CgStructDef *sd=cg_struct_get(base); if(!sd)return NULL;
            for (int i=0;i<sd->field_count;i++) if(!strcmp(sd->field_names[i],n->str_val)) return sd->field_types[i];
            return NULL;
        }
        case NODE_FUNC_CALL: { if(!strcmp(n->str_val,"len"))return"int"; const char *fr=cg_func_ret(n->str_val); if(fr&&strcmp(fr,"void"))return fr; return NULL; }
        case NODE_BINOP: {
            const char *l=node_usagi_type(n->children[0],locals,globals);
            const char *r=node_usagi_type(n->children[1],locals,globals);
            if (!strcmp(n->str_val,"==")||!strcmp(n->str_val,"!=")||!strcmp(n->str_val,"<")||
                !strcmp(n->str_val,">")||!strcmp(n->str_val,"<=")||!strcmp(n->str_val,">=")) return "bool";
            /* bitwise ops return int */
            if (!strcmp(n->str_val,"&")||!strcmp(n->str_val,"|")||
                !strcmp(n->str_val,"^")||!strcmp(n->str_val,"<<")||
                !strcmp(n->str_val,">>")) return "int";
            if ((l&&!strcmp(l,"float"))||(r&&!strcmp(r,"float"))) return "float";
            return l?l:r;
        }
        case NODE_CONCAT: return "str";
        default: return NULL;
    }
}

static const char *cg_safe_name(const char *name) {
    static char buf[256];
    static const char *reserved[]={"double","float","int","long","char","void","auto","register","extern",
        "static","const","struct","union","enum","typedef","sizeof","return","if","else","while","for",
        "do","switch","case","break","continue","goto","default","main",
        
        "abs","pow","min","max","printf","scanf","malloc","free","strlen","strcmp","strdup",
        "fopen","fclose","fgets","exit","rand","srand",NULL};
    for (int i=0;reserved[i];i++) if(!strcmp(name,reserved[i])) { snprintf(buf,sizeof(buf),"usagi_%s",name); return buf; }
    return name;
}

typedef struct { FILE *out; int indent; SymTable globals; SymTable locals; int in_func; } Codegen;
static void cg_indent(Codegen *cg) { for(int i=0;i<cg->indent;i++) fprintf(cg->out,"    "); }

static void emit_expr(Codegen *cg, Node *n);
static void emit_stmt(Codegen *cg, Node *n);
static void emit_block(Codegen *cg, Node *block);

static void emit_as_str(Codegen *cg, Node *n) {
    const char *t=node_usagi_type(n,&cg->locals,&cg->globals);
    if      (t&&!strcmp(t,"int"))        { fprintf(cg->out,"usagi_tostr(");   emit_expr(cg,n); fprintf(cg->out,")"); }
    else if (t&&!strcmp(t,"float"))      { fprintf(cg->out,"usagi_tostr_f("); emit_expr(cg,n); fprintf(cg->out,")"); }
    else if (t&&!strcmp(t,"bool"))       { fprintf(cg->out,"(char*)usagi_bool_str("); emit_expr(cg,n); fprintf(cg->out,")"); }
    else if (n->type==NODE_INT_LIT)      { fprintf(cg->out,"usagi_tostr(");   emit_expr(cg,n); fprintf(cg->out,")"); }
    else if (n->type==NODE_FLOAT_LIT)    { fprintf(cg->out,"usagi_tostr_f("); emit_expr(cg,n); fprintf(cg->out,")"); }
    else if (n->type==NODE_BOOL_LIT)     { fprintf(cg->out,"(char*)usagi_bool_str("); emit_expr(cg,n); fprintf(cg->out,")"); }
    else                                 { emit_expr(cg,n); }
}

static void emit_escaped_str(FILE *out, const char *s) {
    for (;*s;s++) {
        if      (*s=='"')  fprintf(out,"\\\"");
        else if (*s=='\\') fprintf(out,"\\\\");
        else if (*s=='\n') fprintf(out,"\\n");
        else if (*s=='\t') fprintf(out,"\\t");
        else               fputc(*s,out);
    }
}

static void emit_fstring(Codegen *cg, Node *n) {
    const char *tmpl=n->str_val;
    int expr_count=n->child_count;
    if (expr_count==0) { fprintf(cg->out,"\""); emit_escaped_str(cg->out,tmpl); fprintf(cg->out,"\""); return; }
    char *parts[128]; int nparts=0; int slot_idx[128]; char seg_bufs[128][256];
    const char *cur=tmpl,*p=tmpl;
    while (*cur) {
        if (*cur=='{'&&cur[1]>='0'&&cur[1]<='9') {
            const char *end=cur+1; while(*end&&*end!='}')end++;
            int si=atoi(cur+1);
            int slen=(int)(cur-p); if(slen>0) { int pi=nparts; int cap=slen+1; if(cap>255)cap=255; memcpy(seg_bufs[pi],p,cap-1); seg_bufs[pi][cap-1]=0; parts[pi]=(char*)seg_bufs[pi]; slot_idx[pi]=-1; nparts++; }
            slot_idx[nparts]=si; parts[nparts]=NULL; nparts++;
            cur=end+1; p=cur;
        } else cur++;
    }
    if (*p) { int pi=nparts; int slen=(int)strlen(p); if(slen>255)slen=255; memcpy(seg_bufs[pi],p,slen); seg_bufs[pi][slen]=0; parts[pi]=(char*)seg_bufs[pi]; slot_idx[pi]=-1; nparts++; }
    for (int i=0;i<nparts-1;i++) fprintf(cg->out,"usagi_concat(");
    for (int i=0;i<nparts;i++) {
        if (i>0) fprintf(cg->out,", ");
        if (slot_idx[i]>=0) { int si=slot_idx[i]; if(si<expr_count) emit_as_str(cg,n->children[si]); else fprintf(cg->out,"\"\""); }
        else { if(parts[i]&&strlen(parts[i])>0) { fprintf(cg->out,"\""); emit_escaped_str(cg->out,parts[i]); fprintf(cg->out,"\""); } else fprintf(cg->out,"\"\""); }
        if (i>0) fprintf(cg->out,")");
    }
}

static void emit_expr(Codegen *cg, Node *n) {
    if (!n) return;
    switch (n->type) {
        case NODE_INT_LIT:   fprintf(cg->out,"%ld",n->int_val); break;
        case NODE_FLOAT_LIT: fprintf(cg->out,"%g",n->float_val); break;
        case NODE_BOOL_LIT:  fprintf(cg->out,"%ld",n->int_val); break;
        case NODE_NIL_LIT:   fprintf(cg->out,"NULL"); break;
        case NODE_STR_LIT:   fprintf(cg->out,"\""); emit_escaped_str(cg->out,n->str_val); fprintf(cg->out,"\""); break;
        case NODE_FSTRING:   emit_fstring(cg,n); break;
        case NODE_IDENT:     fprintf(cg->out,"%s",n->str_val); break;
        case NODE_ARRAY_LIT:
            fprintf(cg->out,"{");
            for (int i=0;i<n->child_count;i++) { if(i)fprintf(cg->out,", "); emit_expr(cg,n->children[i]); }
            fprintf(cg->out,"}"); break;
        case NODE_INDEX: {
            const char *ipt=node_usagi_type(n->children[0],&cg->locals,&cg->globals);
            if (ipt&&!strncmp(ipt,"dict",4)) {
                
                fprintf(cg->out,"usagi_dict_get("); emit_expr(cg,n->children[0]); fprintf(cg->out,", "); emit_as_str(cg,n->children[1]); fprintf(cg->out,")");
            } else {
                emit_expr(cg,n->children[0]); fprintf(cg->out,"["); emit_expr(cg,n->children[1]); fprintf(cg->out,"]");
            }
            break;
        }
        case NODE_FIELD_ACCESS:
            
            emit_expr(cg,n->children[0]); fprintf(cg->out,"->%s",n->str_val); break;
        case NODE_STRUCT_INIT: {
            
            fprintf(cg->out,"usagi_new_%s(",n->str_val);
            
            
            CgStructDef *sd=cg_struct_get(n->str_val);
            if (sd) {
                for (int fi=0;fi<sd->field_count;fi++) {
                    if (fi) fprintf(cg->out,", ");
                    
                    int found=0;
                    for (int ci=0;ci+1<n->child_count;ci+=2) {
                        if (!strcmp(n->children[ci]->str_val,sd->field_names[fi])) {
                            emit_expr(cg,n->children[ci+1]); found=1; break;
                        }
                    }
                    if (!found) fprintf(cg->out,"0");
                }
            }
            fprintf(cg->out,")"); break;
        }
        case NODE_DICT_LIT:
            fprintf(cg->out,"usagi_dict_from(%d",n->child_count/2);
            for (int i=0;i<n->child_count;i+=2) {
                fprintf(cg->out,", "); emit_as_str(cg,n->children[i]); fprintf(cg->out,", "); emit_as_str(cg,n->children[i+1]);
            }
            fprintf(cg->out,")"); break;
        case NODE_DICT_ACCESS:
            fprintf(cg->out,"usagi_dict_get("); emit_expr(cg,n->children[0]); fprintf(cg->out,", "); emit_as_str(cg,n->children[1]); fprintf(cg->out,")"); break;
        case NODE_BINOP: {
            int is_str_cmp=0;
            if (!strcmp(n->str_val,"==")||!strcmp(n->str_val,"!=")) {
                const char *lt=node_usagi_type(n->children[0],&cg->locals,&cg->globals);
                const char *rt=node_usagi_type(n->children[1],&cg->locals,&cg->globals);
                if ((lt&&!strcmp(lt,"str"))||(rt&&!strcmp(rt,"str"))||
                    n->children[0]->type==NODE_STR_LIT||n->children[1]->type==NODE_STR_LIT) is_str_cmp=1;
            }
            if (is_str_cmp) {
                if (!strcmp(n->str_val,"!=")) fprintf(cg->out,"!");
                fprintf(cg->out,"(strcmp("); emit_expr(cg,n->children[0]); fprintf(cg->out,", "); emit_expr(cg,n->children[1]); fprintf(cg->out,")==0)");
            } else {
                fprintf(cg->out,"("); emit_expr(cg,n->children[0]); fprintf(cg->out," %s ",n->str_val); emit_expr(cg,n->children[1]); fprintf(cg->out,")");
            }
            break;
        }
        case NODE_UNOP:
            if (!strcmp(n->str_val,"not")) { fprintf(cg->out,"!("); emit_expr(cg,n->children[0]); fprintf(cg->out,")"); }
            else if (!strcmp(n->str_val,"~")) { fprintf(cg->out,"~("); emit_expr(cg,n->children[0]); fprintf(cg->out,")"); }
            else { fprintf(cg->out,"-("); emit_expr(cg,n->children[0]); fprintf(cg->out,")"); }
            break;
        case NODE_CONCAT:
            fprintf(cg->out,"usagi_concat("); emit_as_str(cg,n->children[0]); fprintf(cg->out,", "); emit_as_str(cg,n->children[1]); fprintf(cg->out,")"); break;
        case NODE_FUNC_CALL:
            if (!strcmp(n->str_val,"len")&&n->child_count==1&&n->children[0]->type==NODE_IDENT)
                fprintf(cg->out,"%s_len",n->children[0]->str_val);
            else {
                fprintf(cg->out,"%s(",cg_safe_name(n->str_val));
                for (int i=0;i<n->child_count;i++) { if(i)fprintf(cg->out,", "); emit_expr(cg,n->children[i]); }
                fprintf(cg->out,")");
            }
            break;
        default: break;
    }
}

static void emit_block(Codegen *cg, Node *block) {
    if (!block) return;
    cg->indent++;
    for (int i=0;i<block->child_count;i++) emit_stmt(cg,block->children[i]);
    cg->indent--;
}

static void emit_stmt(Codegen *cg, Node *n) {
    if (!n) return;
    switch (n->type) {

        case NODE_PULL:
            cg_indent(cg); fprintf(cg->out,"/* $pull %s — merged at compile time */\n",n->str_val); break;

        case NODE_STRUCT_DEF: {
            
            fprintf(cg->out,"\ntypedef struct {\n");
            for (int i=0;i<n->param_count;i++) {
                char base[128]; strip_nullable(n->param_types[i],base,sizeof(base));
                fprintf(cg->out,"    %s %s;\n",usagi_type_to_c(base),n->params[i]);
            }
            fprintf(cg->out,"} Usagi_%s;\n",n->str_val);
            
            fprintf(cg->out,"static Usagi_%s *usagi_new_%s(",n->str_val,n->str_val);
            for (int i=0;i<n->param_count;i++) {
                if (i) fprintf(cg->out,", ");
                char base[128]; strip_nullable(n->param_types[i],base,sizeof(base));
                fprintf(cg->out,"%s %s",usagi_type_to_c(base),n->params[i]);
            }
            fprintf(cg->out,") {\n    Usagi_%s *_s=calloc(1,sizeof(Usagi_%s));\n",n->str_val,n->str_val);
            for (int i=0;i<n->param_count;i++)
                fprintf(cg->out,"    _s->%s=%s;\n",n->params[i],n->params[i]);
            fprintf(cg->out,"    return _s;\n}\n");
            
            cg_struct_add(n->str_val,n->params,n->param_types,n->param_count);
            sym_add(&cg->globals,n->str_val,n->str_val); 
            break;
        }

        case NODE_VAR_DECL: {
            const char *t=n->var_type;
            SymTable *st=cg->in_func?&cg->locals:&cg->globals;
            sym_add(st,n->str_val,t);
            cg_indent(cg);
            char base[128]; strip_nullable(t,base,sizeof(base));
            size_t bl=strlen(base);
            int is_arr=(int)bl>2&&!strcmp(base+bl-2,"[]");
            if (is_arr) {
                char elem[64]; size_t el=bl-2; if(el>63)el=63; memcpy(elem,base,el); elem[el]=0;
                fprintf(cg->out,"%s *%s = NULL; long %s_len = 0;\n",usagi_type_to_c(elem),n->str_val,n->str_val);
            } else if (!strcmp(base,"str")) {
                fprintf(cg->out,"char *%s = NULL;\n",n->str_val);
            } else if (!strcmp(base,"bool")) {
                fprintf(cg->out,"int %s = 0;\n",n->str_val);
            } else if (!strncmp(base,"dict",4)) {
                fprintf(cg->out,"UsagiDict *%s = NULL;\n",n->str_val);
            } else {
                CgStructDef *sd=cg_struct_get(base);
                if (sd) fprintf(cg->out,"Usagi_%s *%s = NULL;\n",base,n->str_val);
                else    fprintf(cg->out,"%s %s = 0;\n",usagi_type_to_c(base),n->str_val);
            }
            break;
        }

        case NODE_TYPE_INFER: {
            SymTable *st=cg->in_func?&cg->locals:&cg->globals;
            const char *inferred=node_usagi_type(n->children[0],&cg->locals,&cg->globals);
            if (!inferred) inferred="int";
            sym_add(st,n->str_val,inferred);
            cg_indent(cg);
            if (!cg->in_func) {
                char base[128]; strip_nullable(inferred,base,sizeof(base));
                CgStructDef *sd=cg_struct_get(base);
                if (sd)                     fprintf(cg->out,"Usagi_%s *%s = NULL;\n",base,n->str_val);
                else if (!strcmp(base,"str")) fprintf(cg->out,"char *%s = NULL;\n",n->str_val);
                else if (!strcmp(base,"float")) fprintf(cg->out,"double %s = 0;\n",n->str_val);
                else if (!strcmp(base,"bool"))  fprintf(cg->out,"int %s = 0;\n",n->str_val);
                else                            fprintf(cg->out,"long %s = 0;\n",n->str_val);
            } else {
                fprintf(cg->out,"%s %s = ",usagi_type_to_c(inferred),n->str_val);
                emit_expr(cg,n->children[0]); fprintf(cg->out,";\n");
            }
            break;
        }

        case NODE_ASSIGN:
            cg_indent(cg);
            if (n->var_type&&!strcmp(n->var_type,"indexed")) {
                fprintf(cg->out,"%s[",n->str_val); emit_expr(cg,n->children[0]); fprintf(cg->out,"] = "); emit_expr(cg,n->children[1]);
            } else if (n->children[0]&&n->children[0]->type==NODE_ARRAY_LIT) {
                Node *arr=n->children[0];
                const char *etype="long";
                if (arr->child_count>0) {
                    Node *first=arr->children[0];
                    if (first->type==NODE_FLOAT_LIT) etype="double";
                    else if (first->type==NODE_STR_LIT) etype="char *";
                }
                fprintf(cg->out,"%s = malloc(%d * sizeof(%s));\n",n->str_val,arr->child_count,etype);
                cg_indent(cg); fprintf(cg->out,"%s_len = %d;\n",n->str_val,arr->child_count);
                for (int i=0;i<arr->child_count;i++) { cg_indent(cg); fprintf(cg->out,"%s[%d] = ",n->str_val,i); emit_expr(cg,arr->children[i]); fprintf(cg->out,";\n"); }
                return;
            } else if (n->children[0]&&n->children[0]->type==NODE_DICT_LIT) {
                fprintf(cg->out,"%s = ",n->str_val); emit_expr(cg,n->children[0]);
            } else {
                fprintf(cg->out,"%s = ",n->str_val); emit_expr(cg,n->children[0]);
            }
            fprintf(cg->out,";\n"); break;

        case NODE_FIELD_ASSIGN:
            
            cg_indent(cg); fprintf(cg->out,"%s->%s = ",n->str_val,n->var_type); emit_expr(cg,n->children[0]); fprintf(cg->out,";\n"); break;

        case NODE_TERMINAL_PRINT: {
            cg_indent(cg); fprintf(cg->out,"printf(\"");
            for (int i=0;i<n->child_count;i++) {
                Node *arg=n->children[i];
                const char *sym_type=node_usagi_type(arg,&cg->locals,&cg->globals);
                if (arg->type==NODE_INT_LIT)         fprintf(cg->out,"%%ld");
                else if (arg->type==NODE_FLOAT_LIT)  fprintf(cg->out,"%%g");
                else if (arg->type==NODE_BOOL_LIT)   fprintf(cg->out,"%%s");
                else if (sym_type&&!strcmp(sym_type,"int"))   fprintf(cg->out,"%%ld");
                else if (sym_type&&!strcmp(sym_type,"float")) fprintf(cg->out,"%%g");
                else if (sym_type&&!strcmp(sym_type,"bool"))  fprintf(cg->out,"%%s");
                else                                          fprintf(cg->out,"%%s");
                if (i<n->child_count-1) fprintf(cg->out," ");
            }
            fprintf(cg->out,"\\n\"");
            for (int i=0;i<n->child_count;i++) {
                fprintf(cg->out,", ");
                Node *parg=n->children[i];
                int parg_is_bool=(parg->type==NODE_BOOL_LIT);
                if (!parg_is_bool) { const char *pt=node_usagi_type(parg,&cg->locals,&cg->globals); if(pt&&!strcmp(pt,"bool"))parg_is_bool=1; }
                if (parg_is_bool) fprintf(cg->out,"usagi_bool_str(");
                emit_expr(cg,parg);
                if (parg_is_bool) fprintf(cg->out,")");
            }
            fprintf(cg->out,");\n"); break;
        }

        case NODE_TERMINAL_INPUT: {
            cg_indent(cg); fprintf(cg->out,"{\n"); cg->indent++;
            cg_indent(cg); fprintf(cg->out,"printf(\"%%s \", "); emit_expr(cg,n->children[0]); fprintf(cg->out,");\n");
            cg_indent(cg); fprintf(cg->out,"fflush(stdout);\n");
            const char *vname=n->children[1]->str_val;
            const char *vtype=sym_get(&cg->locals,vname); if(!vtype)vtype=sym_get(&cg->globals,vname);
            cg_indent(cg);
            if (!vtype||!strcmp(vtype,"str")) fprintf(cg->out,"%s = usagi_readline();\n",vname);
            else if (!strcmp(vtype,"int"))    fprintf(cg->out,"scanf(\"%%ld\", &%s);\n",vname);
            else                              fprintf(cg->out,"scanf(\"%%lf\", &%s);\n",vname);
            cg->indent--; cg_indent(cg); fprintf(cg->out,"}\n"); break;
        }

        case NODE_IF:
            cg_indent(cg); fprintf(cg->out,"if ("); emit_expr(cg,n->cond); fprintf(cg->out,") {\n");
            emit_block(cg,n->body);
            if (n->else_body) { cg_indent(cg); fprintf(cg->out,"} else {\n"); emit_block(cg,n->else_body); }
            cg_indent(cg); fprintf(cg->out,"}\n"); break;

        case NODE_WHILE:
            cg_indent(cg); fprintf(cg->out,"while ("); emit_expr(cg,n->cond); fprintf(cg->out,") {\n");
            emit_block(cg,n->body); cg_indent(cg); fprintf(cg->out,"}\n"); break;

        case NODE_LOOP:
            cg_indent(cg); fprintf(cg->out,"while (1) {\n");
            emit_block(cg,n->body); cg_indent(cg); fprintf(cg->out,"}\n"); break;

        case NODE_FOR_RANGE:
            cg_indent(cg); fprintf(cg->out,"for (long %s = ",n->str_val);
            emit_expr(cg,n->init); fprintf(cg->out,"; %s <= ",n->str_val);
            emit_expr(cg,n->limit); fprintf(cg->out,"; %s++) {\n",n->str_val);
            sym_add(&cg->locals,n->str_val,"int");
            emit_block(cg,n->body); cg_indent(cg); fprintf(cg->out,"}\n"); break;

        case NODE_FOR_IN: {
            const char *arr=(n->init&&n->init->str_val)?n->init->str_val:"arr";
            const char *arr_type=sym_get(&cg->locals,arr); if(!arr_type)arr_type=sym_get(&cg->globals,arr);
            const char *etype="long",*utype="int";
            if (arr_type&&!strncmp(arr_type,"float",5)) { etype="double"; utype="float"; }
            else if (arr_type&&!strncmp(arr_type,"str",3)) { etype="char *"; utype="str"; }
            cg_indent(cg); fprintf(cg->out,"for (int _i_%s=0; _i_%s<%s_len; _i_%s++) {\n",n->str_val,n->str_val,arr,n->str_val);
            cg->indent++;
            cg_indent(cg); fprintf(cg->out,"%s %s = %s[_i_%s];\n",etype,n->str_val,arr,n->str_val);
            sym_add(&cg->locals,n->str_val,utype);
            cg->indent--;
            emit_block(cg,n->body); cg_indent(cg); fprintf(cg->out,"}\n"); break;
        }

        case NODE_MATCH: {
            for (int i=0;i<n->child_count;i++) {
                Node *arm=n->children[i];
                if (!arm->str_val||!strcmp(arm->str_val,"_")) {
                    if (i>0) { cg_indent(cg); fprintf(cg->out," else {\n"); }
                    else     { cg_indent(cg); fprintf(cg->out,"{\n"); }
                    emit_block(cg,arm->body); cg_indent(cg); fprintf(cg->out,"}\n");
                } else {
                    cg_indent(cg); if(i>0)fprintf(cg->out,"else ");
                    fprintf(cg->out,"if (");
                    const char *subj_type=node_usagi_type(n->cond,&cg->locals,&cg->globals);
                    int is_str=(subj_type&&!strcmp(subj_type,"str"))||(n->cond->type==NODE_STR_LIT)||(arm->var_type&&!strcmp(arm->var_type,"str"));
                    if (is_str) { fprintf(cg->out,"strcmp("); emit_expr(cg,n->cond); fprintf(cg->out,", \"%s\")==0",arm->str_val); }
                    else        { emit_expr(cg,n->cond); fprintf(cg->out," == %s",arm->str_val); }
                    fprintf(cg->out,") {\n");
                    emit_block(cg,arm->body); cg_indent(cg); fprintf(cg->out,"}");
                    if (i<n->child_count-1) fprintf(cg->out,"\n");
                }
            }
            fprintf(cg->out,"\n"); break;
        }

        case NODE_FUNC_DEF: {
            int saved=cg->in_func; cg->in_func=1;
            sym_clear(&cg->locals);
            for (int i=0;i<n->param_count;i++) sym_add(&cg->locals,n->params[i],n->param_types[i]);
            cg_reg_func(n->str_val,n->ret_type);
            const char *ret=n->ret_type?usagi_type_to_c(n->ret_type):"void";
            fprintf(cg->out,"\n%s %s(",ret,cg_safe_name(n->str_val));
            for (int i=0;i<n->param_count;i++) { if(i)fprintf(cg->out,", "); fprintf(cg->out,"%s %s",usagi_type_to_c(n->param_types[i]),n->params[i]); }
            fprintf(cg->out,") {\n");
            emit_block(cg,n->body); fprintf(cg->out,"}\n");
            cg->in_func=saved; break;
        }

        case NODE_RETURN:
            cg_indent(cg);
            if (n->child_count) { fprintf(cg->out,"return "); emit_expr(cg,n->children[0]); fprintf(cg->out,";\n"); }
            else fprintf(cg->out,"return;\n"); break;

        case NODE_BREAK:    cg_indent(cg); fprintf(cg->out,"break;\n"); break;
        case NODE_CONTINUE: cg_indent(cg); fprintf(cg->out,"continue;\n"); break;
        case NODE_FUNC_CALL: cg_indent(cg); emit_expr(cg,n); fprintf(cg->out,";\n"); break;
        case NODE_BLOCK:  emit_block(cg,n); break;
        case NODE_PROGRAM: for (int i=0;i<n->child_count;i++) emit_stmt(cg,n->children[i]); break;
        default: break;
    }
}

static void codegen(Node *prog, FILE *out) {
    Codegen cg={0}; cg.out=out;

    
    fprintf(out,
        "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stdarg.h>\n\n"
        "/* ─── uSagi v5 runtime ─────────────────────── */\n"
        "static char *usagi_readline(void) {\n"
        "    char buf[4096]; fgets(buf,sizeof(buf),stdin);\n"
        "    size_t l=strlen(buf); if(l&&buf[l-1]=='\\n')buf[l-1]=0;\n"
        "    return strdup(buf);\n}\n"
        "static char *usagi_tostr(long v) { char *b=malloc(32); snprintf(b,32,\"%%ld\",v); return b; }\n"
        "static char *usagi_tostr_f(double v) { char *b=malloc(32); snprintf(b,32,\"%%g\",v); return b; }\n"
        "static const char *usagi_bool_str(int v) { return v?\"true\":\"false\"; }\n"
        "static char *usagi_concat(const char *a, const char *b) {\n"
        "    if(!a)a=\"\"; if(!b)b=\"\";\n"
        "    size_t la=strlen(a),lb=strlen(b);\n"
        "    char *r=malloc(la+lb+1); memcpy(r,a,la); memcpy(r+la,b,lb); r[la+lb]=0; return r;\n}\n"
        "/* ─── dict runtime ──────────────────────────────── */\n"
        "typedef struct { char *key; char *val; } UsagiDictEntry;\n"
        "typedef struct { UsagiDictEntry *entries; int count, cap; } UsagiDict;\n"
        "static UsagiDict *usagi_dict_new(void) { UsagiDict *d=calloc(1,sizeof(UsagiDict)); return d; }\n"
        "static void usagi_dict_set(UsagiDict *d, const char *key, const char *val) {\n"
        "    for (int i=0;i<d->count;i++) if(!strcmp(d->entries[i].key,key)) {\n"
        "        free(d->entries[i].val); d->entries[i].val=strdup(val); return; }\n"
        "    if (d->count>=d->cap) { d->cap=d->cap?d->cap*2:8; d->entries=realloc(d->entries,d->cap*sizeof(UsagiDictEntry)); }\n"
        "    d->entries[d->count].key=strdup(key); d->entries[d->count].val=strdup(val); d->count++;\n}\n"
        "static char *usagi_dict_get(UsagiDict *d, const char *key) {\n"
        "    if(!d)return NULL;\n"
        "    for (int i=0;i<d->count;i++) if(!strcmp(d->entries[i].key,key)) return d->entries[i].val;\n"
        "    return NULL;\n}\n"
        "static UsagiDict *usagi_dict_from(int n, ...) {\n"
        "    UsagiDict *d=usagi_dict_new(); va_list ap; va_start(ap,n);\n"
        "    for (int i=0;i<n;i++) { char *k=va_arg(ap,char*); char *v=va_arg(ap,char*); usagi_dict_set(d,k,v); }\n"
        "    va_end(ap); return d;\n}\n\n"
    );

    
    for (int i=0;i<prog->child_count;i++) {
        Node *n=prog->children[i];
        if (n->type==NODE_FUNC_DEF)  cg_reg_func(n->str_val,n->ret_type);
        if (n->type==NODE_STRUCT_DEF) cg_struct_add(n->str_val,n->params,n->param_types,n->param_count);
    }

    
    for (int i=0;i<prog->child_count;i++) {
        Node *n=prog->children[i];
        if (n->type==NODE_STRUCT_DEF) emit_stmt(&cg,n);
    }

    
    for (int i=0;i<prog->child_count;i++) {
        Node *n=prog->children[i];
        if (n->type!=NODE_FUNC_DEF) continue;
        const char *fname=!strcmp(n->str_val,"main")?"usagi_main":cg_safe_name(n->str_val);
        const char *ret=n->ret_type?usagi_type_to_c(n->ret_type):"void";
        fprintf(out,"%s %s(",ret,fname);
        for (int j=0;j<n->param_count;j++) {
            if (j) fprintf(out,", ");
            fprintf(out,"%s %s",usagi_type_to_c(n->param_types[j]),n->params[j]);
        }
        fprintf(out,");\n");
    }
    fprintf(out,"\n");

    
    for (int i=0;i<prog->child_count;i++) {
        Node *n=prog->children[i];
        if (n->type==NODE_VAR_DECL||n->type==NODE_TYPE_INFER) emit_stmt(&cg,n);
    }
    fprintf(out,"\n");

    
    for (int i=0;i<prog->child_count;i++) {
        Node *n=prog->children[i];
        if (n->type!=NODE_FUNC_DEF) continue;
        char *orig=n->str_val; int is_main=!strcmp(n->str_val,"main");
        if (is_main) n->str_val=strdup("usagi_main");
        else { const char *safe=cg_safe_name(n->str_val); if(safe!=n->str_val){char *s2=strdup(safe);n->str_val=s2;} }
        emit_stmt(&cg,n);
        if (is_main||n->str_val!=orig) { free(n->str_val); n->str_val=orig; }
    }

    
    fprintf(out,"\nint main(void) {\n");
    cg.in_func=1; cg.indent=1;

    int has_main=0;
    for (int i=0;i<prog->child_count;i++)
        if (prog->children[i]->type==NODE_FUNC_DEF&&!strcmp(prog->children[i]->str_val,"main")) has_main=1;

    
    for (int i=0;i<prog->child_count;i++) {
        Node *n=prog->children[i];
        if (n->type==NODE_VAR_DECL||n->type==NODE_FUNC_DEF||n->type==NODE_PULL||n->type==NODE_STRUCT_DEF) continue;
        if (n->type==NODE_TYPE_INFER) {
            fprintf(out,"    %s = ",n->str_val);
            cg.in_func=1; cg.indent=1;
            emit_expr(&cg,n->children[0]);
            cg.in_func=0; cg.indent=0;
            fprintf(out,";\n"); continue;
        }
        emit_stmt(&cg,n);
    }
    if (has_main) fprintf(out,"    usagi_main();\n");
    fprintf(out,"    return 0;\n}\n");
}
