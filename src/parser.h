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
#include "lexer.h"
#include "ast.h"
#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { TokenList tl; int pos; } Parser;

static Token p_peek(Parser *p)    { return p->tl.tokens[p->pos]; }
static Token p_advance(Parser *p) { return p->tl.tokens[p->pos++]; }
static int   p_check(Parser *p, TokenType t) { return p_peek(p).type==t; }
static int   p_match(Parser *p, TokenType t) { if(p_check(p,t)){p_advance(p);return 1;} return 0; }

static Token p_expect(Parser *p, TokenType t, const char *msg) {
    Token tok=p_peek(p);
    if (tok.type!=t) {
        char buf[512];
        snprintf(buf,sizeof(buf),"%s (got '%s')", msg, tok.value?tok.value:"?");
        ec_add(tok.line, tok.col, buf);
        return tok;
    }
    return p_advance(p);
}

static Node *parse_expr(Parser *p);
static Node *parse_stmt(Parser *p);
static Node *parse_block(Parser *p);

static char *parse_type_str(Parser *p) {
    Token t=p_peek(p);
    char base[128]="";
    
    if (t.type==TOK_INT||t.type==TOK_FLOAT||t.type==TOK_STR||
        t.type==TOK_BOOL||t.type==TOK_VOID||t.type==TOK_DICT||t.type==TOK_IDENT) {
        p_advance(p);
        strncpy(base, t.value, sizeof(base)-1);
    } else {
        return strdup("int"); 
    }
    
    if (!strcmp(base,"dict") && p_check(p,TOK_LT)) {
        p_advance(p); 
        char *kt=parse_type_str(p);
        p_expect(p,TOK_COMMA,"expected ',' in dict<K,V>");
        char *vt=parse_type_str(p);
        p_expect(p,TOK_GT,"expected '>' after dict<K,V>");
        char buf[256]; snprintf(buf,sizeof(buf),"dict<%s,%s>",kt,vt);
        free(kt); free(vt);
        
        if (p_check(p,TOK_QUESTION)) { p_advance(p); strncat(buf,"?",sizeof(buf)-strlen(buf)-1); }
        return strdup(buf);
    }
    
    if (p_check(p,TOK_LBRACKET)) {
        p_advance(p); p_expect(p,TOK_RBRACKET,"expected ']'");
        char buf[128]; snprintf(buf,sizeof(buf),"%s[]",base);
        if (p_check(p,TOK_QUESTION)) { p_advance(p); strncat(buf,"?",sizeof(buf)-strlen(buf)-1); }
        return strdup(buf);
    }
    
    if (p_check(p,TOK_QUESTION)) { p_advance(p); char buf[128]; snprintf(buf,sizeof(buf),"%s?",base); return strdup(buf); }
    return strdup(base);
}

static Node *parse_fstring(Parser *p __attribute__((unused)), Token t) {
    Node *n=node_new(NODE_FSTRING,t.line,t.col);
    const char *raw=t.value;
    int cap=512,bi=0; char *tmpl=malloc(cap); int slot=0;
    for (int i=0;raw[i];) {
        if (bi+32>=cap) { cap*=2; tmpl=realloc(tmpl,cap); }
        if (raw[i]=='{') {
            int j=i+1,depth=1;
            while (raw[j]&&depth>0) { if(raw[j]=='{')depth++; else if(raw[j]=='}')depth--; if(depth>0)j++; else break; }
            int elen=j-i-1; char *expr_src=malloc(elen+2); memcpy(expr_src,raw+i+1,elen); expr_src[elen]=0;
            TokenList inner_tl=tokenize(expr_src); Parser inner_p={inner_tl,0};
            Node *expr_node=parse_expr(&inner_p);
            token_list_free(&inner_tl); free(expr_src);
            node_add_child(n,expr_node);
            bi+=snprintf(tmpl+bi,cap-bi,"{%d}",slot++);
            i=j+1;
        } else { tmpl[bi++]=raw[i++]; }
    }
    tmpl[bi]=0; n->str_val=strdup(tmpl); free(tmpl); return n;
}

static Node *parse_primary(Parser *p) {
    Token t=p_peek(p);
    if (t.type==TOK_INT_LIT)   { p_advance(p); Node *n=node_new(NODE_INT_LIT,t.line,t.col);   n->int_val=atol(t.value); return n; }
    if (t.type==TOK_FLOAT_LIT) { p_advance(p); Node *n=node_new(NODE_FLOAT_LIT,t.line,t.col); n->float_val=atof(t.value); return n; }
    if (t.type==TOK_STR_LIT)   { p_advance(p); Node *n=node_new(NODE_STR_LIT,t.line,t.col);   n->str_val=strdup(t.value); return n; }
    if (t.type==TOK_FSTR_LIT)  { p_advance(p); return parse_fstring(p,t); }
    if (t.type==TOK_NIL)       { p_advance(p); return node_new(NODE_NIL_LIT,t.line,t.col); }
    if (t.type==TOK_TRUE||t.type==TOK_FALSE) {
        p_advance(p); Node *n=node_new(NODE_BOOL_LIT,t.line,t.col); n->int_val=(t.type==TOK_TRUE)?1:0; return n;
    }
    
    if (t.type==TOK_LBRACKET) {
        p_advance(p); Node *n=node_new(NODE_ARRAY_LIT,t.line,t.col);
        while (!p_check(p,TOK_RBRACKET)&&!p_check(p,TOK_EOF)) { node_add_child(n,parse_expr(p)); p_match(p,TOK_COMMA); }
        p_expect(p,TOK_RBRACKET,"expected ']'"); return n;
    }
    
    if (t.type==TOK_LBRACE) {
        int la=p->pos+1;
        if (la<p->tl.count) {
            TokenType lat=p->tl.tokens[la].type;
            int la2=(la+1<p->tl.count)?p->tl.tokens[la+1].type:TOK_EOF;
            if ((lat==TOK_STR_LIT||lat==TOK_IDENT||lat==TOK_INT_LIT)&&la2==TOK_COLON) {
                p_advance(p);
                Node *n=node_new(NODE_DICT_LIT,t.line,t.col);
                while (!p_check(p,TOK_RBRACE)&&!p_check(p,TOK_EOF)) {
                    Node *key=parse_expr(p); p_expect(p,TOK_COLON,"expected ':' in dict literal");
                    Node *val=parse_expr(p); node_add_child(n,key); node_add_child(n,val); p_match(p,TOK_COMMA);
                }
                p_expect(p,TOK_RBRACE,"expected '}'"); return n;
            }
        }
    }
    
    if (t.type==TOK_NEW) {
        p_advance(p);
        Token name=p_expect(p,TOK_IDENT,"expected struct name after 'new'");
        p_expect(p,TOK_LBRACE,"expected '{' in struct init");
        Node *n=node_new(NODE_STRUCT_INIT,t.line,t.col); n->str_val=strdup(name.value);
        while (!p_check(p,TOK_RBRACE)&&!p_check(p,TOK_EOF)) {
            Token field=p_expect(p,TOK_IDENT,"expected field name");
            p_expect(p,TOK_COLON,"expected ':' after field name");
            Node *val=parse_expr(p);
            
            Node *fn=node_new(NODE_IDENT,field.line,field.col); fn->str_val=strdup(field.value);
            node_add_child(n,fn); node_add_child(n,val);
            p_match(p,TOK_COMMA);
        }
        p_expect(p,TOK_RBRACE,"expected '}' in struct init"); return n;
    }
    if (t.type==TOK_NOT) {
        p_advance(p); Node *n=node_new(NODE_UNOP,t.line,t.col); n->str_val=strdup("not");
        node_add_child(n,parse_primary(p)); return n;
    }
    if (t.type==TOK_MINUS) {
        p_advance(p); Node *inner=parse_primary(p);
        if (inner->type==NODE_INT_LIT)   { inner->int_val=-inner->int_val; return inner; }
        if (inner->type==NODE_FLOAT_LIT) { inner->float_val=-inner->float_val; return inner; }
        Node *n=node_new(NODE_UNOP,t.line,t.col); n->str_val=strdup("-"); node_add_child(n,inner); return n;
    }
    if (t.type==TOK_LPAREN) {
        p_advance(p); Node *n=parse_expr(p); p_expect(p,TOK_RPAREN,"expected ')'"); return n;
    }
    if (t.type==TOK_IDENT) {
        p_advance(p);
        if (p_check(p,TOK_LPAREN)) {
            p_advance(p); Node *n=node_new(NODE_FUNC_CALL,t.line,t.col); n->str_val=strdup(t.value);
            while (!p_check(p,TOK_RPAREN)&&!p_check(p,TOK_EOF)) { node_add_child(n,parse_expr(p)); p_match(p,TOK_COMMA); }
            p_expect(p,TOK_RPAREN,"expected ')'");
            
            Node *cur=n;
            while (p_check(p,TOK_DOT)||p_check(p,TOK_LBRACKET)) {
                if (p_check(p,TOK_DOT)) {
                    int dl=p_peek(p).line,dc=p_peek(p).col; p_advance(p);
                    Token f=p_expect(p,TOK_IDENT,"expected field name after '.'");
                    Node *fa=node_new(NODE_FIELD_ACCESS,dl,dc); fa->str_val=strdup(f.value);
                    node_add_child(fa,cur); cur=fa;
                } else {
                    int bl=p_peek(p).line,bc=p_peek(p).col; p_advance(p);
                    Node *idx=node_new(NODE_INDEX,bl,bc);
                    node_add_child(idx,cur); node_add_child(idx,parse_expr(p));
                    p_expect(p,TOK_RBRACKET,"expected ']'"); cur=idx;
                }
            }
            return cur;
        }
        Node *n=node_new(NODE_IDENT,t.line,t.col); n->str_val=strdup(t.value);
        
        while (p_check(p,TOK_DOT)||p_check(p,TOK_LBRACKET)) {
            if (p_check(p,TOK_DOT)) {
                int dl=p_peek(p).line,dc=p_peek(p).col; p_advance(p);
                Token f=p_expect(p,TOK_IDENT,"expected field name after '.'");
                Node *fa=node_new(NODE_FIELD_ACCESS,dl,dc); fa->str_val=strdup(f.value);
                node_add_child(fa,n); n=fa;
            } else {
                int bl=p_peek(p).line,bc=p_peek(p).col; p_advance(p);
                Node *idx=node_new(NODE_INDEX,bl,bc);
                node_add_child(idx,n); node_add_child(idx,parse_expr(p));
                p_expect(p,TOK_RBRACKET,"expected ']'"); n=idx;
            }
        }
        return n;
    }
    char buf[128]; snprintf(buf,sizeof(buf),"式の中で予期しないトークン '%s'",t.value?t.value:"?");
    ec_add(t.line,t.col,buf);
    p_advance(p); return node_new(NODE_INT_LIT,t.line,t.col);
}

static int binop_prec(TokenType t) {
    switch(t) {
        case TOK_OR: return 1; case TOK_AND: return 2;
        case TOK_EQ: case TOK_NEQ: case TOK_LT: case TOK_GT: case TOK_LE: case TOK_GE: return 3;
        case TOK_CONCAT: return 4;
        case TOK_PLUS: case TOK_MINUS: return 5;
        case TOK_STAR: case TOK_SLASH: case TOK_PERCENT: return 6;
        default: return 0;
    }
}
static const char *tok_op_str(TokenType t) {
    switch(t) {
        case TOK_EQ: return "=="; case TOK_NEQ: return "!=";
        case TOK_LT: return "<";  case TOK_GT:  return ">";
        case TOK_LE: return "<="; case TOK_GE:  return ">=";
        case TOK_PLUS: return "+"; case TOK_MINUS: return "-";
        case TOK_STAR: return "*"; case TOK_SLASH: return "/";
        case TOK_PERCENT: return "%";
        case TOK_AND: return "&&"; case TOK_OR: return "||";
        case TOK_CONCAT: return ".."; default: return "?";
    }
}
static Node *parse_binop(Parser *p, int min_prec) {
    Node *left=parse_primary(p);
    while (1) {
        TokenType op=p_peek(p).type; int prec=binop_prec(op);
        if (!prec||prec<min_prec) break;
        int line=p_peek(p).line,col=p_peek(p).col; p_advance(p);
        Node *right=parse_binop(p,prec+1);
        if (op==TOK_CONCAT) {
            Node *n=node_new(NODE_CONCAT,line,col); node_add_child(n,left); node_add_child(n,right); left=n;
        } else {
            Node *n=node_new(NODE_BINOP,line,col); n->str_val=strdup(tok_op_str(op));
            node_add_child(n,left); node_add_child(n,right); left=n;
        }
    }
    return left;
}
static Node *parse_expr(Parser *p) { return parse_binop(p,1); }

static int is_block_terminator(Parser *p) {
    TokenType t=p_peek(p).type;
    return t==TOK_END||t==TOK_ELSE||t==TOK_ELSEIF||t==TOK_ALLEND||t==TOK_EOF||t==TOK_RBRACE;
}
static Node *parse_block(Parser *p) {
    Node *block=node_new(NODE_BLOCK,p_peek(p).line,p_peek(p).col);
    while (!is_block_terminator(p)) { Node *s=parse_stmt(p); if(s) node_add_child(block,s); }
    return block;
}

static Node *parse_match(Parser *p, int line, int col) {
    Node *n=node_new(NODE_MATCH,line,col); n->cond=parse_expr(p);
    p_expect(p,TOK_LBRACE,"expected '{' after match expression");
    while (!p_check(p,TOK_RBRACE)&&!p_check(p,TOK_EOF)) {
        if (!p_match(p,TOK_CASE)) {
            char buf[64]; snprintf(buf,sizeof(buf),"match内で'case'が必要です");
            ec_add(p_peek(p).line,p_peek(p).col,buf); p_advance(p); continue;
        }
        Node *arm=node_new(NODE_MATCH_ARM,p_peek(p).line,p_peek(p).col);
        Token pat=p_peek(p);
        if (pat.type==TOK_INT_LIT||pat.type==TOK_FLOAT_LIT||pat.type==TOK_STR_LIT
            ||pat.type==TOK_TRUE||pat.type==TOK_FALSE||pat.type==TOK_NIL) {
            p_advance(p);
            arm->str_val=strdup(pat.value?(pat.value):(pat.type==TOK_TRUE?"true":"false"));
            arm->var_type=strdup(pat.type==TOK_INT_LIT?"int":pat.type==TOK_FLOAT_LIT?"float":
                pat.type==TOK_STR_LIT?"str":(pat.type==TOK_TRUE||pat.type==TOK_FALSE)?"bool":"nil");
        } else if (pat.type==TOK_IDENT&&!strcmp(pat.value,"_")) {
            p_advance(p); arm->str_val=strdup("_"); arm->var_type=strdup("wild");
        } else {
            ec_add(pat.line,pat.col,"match caseで有効なパターンが必要です");
            p_advance(p); arm->str_val=strdup("_"); arm->var_type=strdup("wild");
        }
        p_expect(p,TOK_FATARROW,"expected '=>' after match pattern");
        p_expect(p,TOK_LBRACE,"expected '{' after '=>'");
        arm->body=parse_block(p);
        p_expect(p,TOK_END,"expected 'end' to close match arm");
        node_add_child(n,arm);
    }
    p_expect(p,TOK_RBRACE,"expected '}' to close match"); return n;
}

static Node *parse_if(Parser *p, int line, int col) {
    Node *n=node_new(NODE_IF,line,col); n->cond=parse_expr(p);
    p_expect(p,TOK_THEN,"expected 'then' after if condition");
    p_expect(p,TOK_LBRACE,"expected '{'");
    n->body=parse_block(p);
    if (p_check(p,TOK_ELSEIF)) {
        int el=p_peek(p).line,ec2=p_peek(p).col; p_advance(p);
        Node *elif_node=parse_if(p,el,ec2);
        Node *blk=node_new(NODE_BLOCK,el,ec2); node_add_child(blk,elif_node); n->else_body=blk;
    } else if (p_check(p,TOK_ELSE)) {
        p_advance(p); n->else_body=parse_block(p);
        p_expect(p,TOK_END,"expected 'end' to close if");
    } else {
        p_expect(p,TOK_END,"expected 'end' to close if");
    }
    return n;
}

static int is_func_def(Parser *p) {
    int scan=p->pos+1; int depth=1;
    while (scan<p->tl.count&&depth>0) {
        if      (p->tl.tokens[scan].type==TOK_LPAREN) depth++;
        else if (p->tl.tokens[scan].type==TOK_RPAREN) { depth--; if(!depth) break; }
        scan++;
    }
    scan++;
    if (scan<p->tl.count&&p->tl.tokens[scan].type==TOK_ARROW) scan+=2;
    return (scan<p->tl.count&&p->tl.tokens[scan].type==TOK_LBRACE);
}

static Node *parse_struct_def(Parser *p, int line, int col) {
    Token name=p_expect(p,TOK_IDENT,"expected struct name");
    Node *n=node_new(NODE_STRUCT_DEF,line,col); n->str_val=strdup(name.value);
    n->params=NULL; n->param_types=NULL; n->param_count=0;
    p_expect(p,TOK_LBRACE,"expected '{' after struct name");
    while (!p_check(p,TOK_END)&&!p_check(p,TOK_EOF)) {
        Token fname=p_expect(p,TOK_IDENT,"expected field name in struct");
        p_expect(p,TOK_ASSIGN,"expected '=' after field name");
        char *ftype=parse_type_str(p);
        n->params     =realloc(n->params,     (n->param_count+1)*sizeof(char*));
        n->param_types=realloc(n->param_types,(n->param_count+1)*sizeof(char*));
        n->params[n->param_count]     =strdup(fname.value);
        n->param_types[n->param_count]=ftype;
        n->param_count++;
        p_match(p,TOK_SEMICOLON);
    }
    p_expect(p,TOK_END,"expected 'end' to close struct"); return n;
}

static Node *parse_stmt(Parser *p) {
    Token t=p_peek(p);

    
    if (t.type==TOK_DOLLAR) {
        p_advance(p);
        Token kw=p_peek(p);
        
        if (kw.type==TOK_PULL) p_advance(p);
        Token mod=p_expect(p,TOK_IDENT,"expected module name after $pull");
        Node *n=node_new(NODE_PULL,t.line,t.col); n->str_val=strdup(mod.value); return n;
    }
    if (t.type==TOK_PULL) {
        p_advance(p); Token mod=p_expect(p,TOK_IDENT,"expected module name after pull");
        Node *n=node_new(NODE_PULL,t.line,t.col); n->str_val=strdup(mod.value); return n;
    }

    
    if (t.type==TOK_STRUCT) {
        p_advance(p); return parse_struct_def(p,t.line,t.col);
    }

    
    if (t.type==TOK_TERMINAL_PRINT) {
        p_advance(p); p_expect(p,TOK_LPAREN,"expected '(' after terminal.print");
        Node *n=node_new(NODE_TERMINAL_PRINT,t.line,t.col);
        while (!p_check(p,TOK_RPAREN)&&!p_check(p,TOK_EOF)) { node_add_child(n,parse_expr(p)); p_match(p,TOK_COMMA); }
        p_expect(p,TOK_RPAREN,"expected ')'"); p_match(p,TOK_SEMICOLON); return n;
    }
    if (t.type==TOK_TERMINAL_INPUT) {
        p_advance(p); p_expect(p,TOK_LPAREN,"expected '(' after terminal.input");
        Node *n=node_new(NODE_TERMINAL_INPUT,t.line,t.col);
        node_add_child(n,parse_expr(p)); p_match(p,TOK_COMMA);
        Token var=p_expect(p,TOK_IDENT,"expected variable name");
        Node *vn=node_new(NODE_IDENT,var.line,var.col); vn->str_val=strdup(var.value);
        node_add_child(n,vn);
        p_expect(p,TOK_RPAREN,"expected ')'"); p_match(p,TOK_SEMICOLON); return n;
    }

    if (t.type==TOK_IF)    { p_advance(p); return parse_if(p,t.line,t.col); }
    if (t.type==TOK_WHILE) {
        p_advance(p); Node *n=node_new(NODE_WHILE,t.line,t.col);
        n->cond=parse_expr(p); p_expect(p,TOK_LBRACE,"expected '{'");
        n->body=parse_block(p); p_expect(p,TOK_END,"expected 'end'"); return n;
    }
    if (t.type==TOK_LOOP) {
        p_advance(p); Node *n=node_new(NODE_LOOP,t.line,t.col);
        p_expect(p,TOK_LBRACE,"expected '{'");
        n->body=parse_block(p); p_expect(p,TOK_END,"expected 'end'"); return n;
    }
    if (t.type==TOK_FOR) {
        p_advance(p); Token var=p_expect(p,TOK_IDENT,"expected loop variable");
        if (p_check(p,TOK_IN)) {
            p_advance(p); Node *n=node_new(NODE_FOR_IN,t.line,t.col); n->str_val=strdup(var.value);
            n->init=parse_expr(p); p_expect(p,TOK_LBRACE,"expected '{'");
            n->body=parse_block(p); p_expect(p,TOK_END,"expected 'end'"); return n;
        } else {
            p_expect(p,TOK_ASSIGN,"expected '=' in for range");
            Node *n=node_new(NODE_FOR_RANGE,t.line,t.col); n->str_val=strdup(var.value);
            n->init=parse_expr(p); p_expect(p,TOK_TO,"expected 'to'");
            n->limit=parse_expr(p); p_expect(p,TOK_LBRACE,"expected '{'");
            n->body=parse_block(p); p_expect(p,TOK_END,"expected 'end'"); return n;
        }
    }
    if (t.type==TOK_MATCH) { p_advance(p); return parse_match(p,t.line,t.col); }
    if (t.type==TOK_BREAK)    { p_advance(p); p_match(p,TOK_SEMICOLON); return node_new(NODE_BREAK,t.line,t.col); }
    if (t.type==TOK_CONTINUE) { p_advance(p); p_match(p,TOK_SEMICOLON); return node_new(NODE_CONTINUE,t.line,t.col); }
    if (t.type==TOK_RETURN) {
        p_advance(p); Node *n=node_new(NODE_RETURN,t.line,t.col);
        if (!is_block_terminator(p)) node_add_child(n,parse_expr(p));
        p_match(p,TOK_SEMICOLON); return n;
    }
    if (t.type==TOK_ALLEND) { p_advance(p); return NULL; }

    if (t.type==TOK_IDENT) {
        Token name=p_advance(p);

        
        if (p_check(p,TOK_COLONASSIGN)) {
            p_advance(p);
            Node *n=node_new(NODE_TYPE_INFER,t.line,t.col); n->str_val=strdup(name.value);
            node_add_child(n,parse_expr(p)); p_match(p,TOK_SEMICOLON); return n;
        }

        
        if (p_check(p,TOK_DOT)) {
            p_advance(p);
            Token field=p_expect(p,TOK_IDENT,"expected field name");
            p_expect(p,TOK_ASSIGN,"expected '=' after field");
            Node *val=parse_expr(p); p_match(p,TOK_SEMICOLON);
            Node *n=node_new(NODE_FIELD_ASSIGN,t.line,t.col);
            n->str_val=strdup(name.value);
            n->var_type=strdup(field.value); 
            node_add_child(n,val); return n;
        }

        
        if (p_check(p,TOK_LBRACKET)) {
            p_advance(p); Node *idx_expr=parse_expr(p);
            p_expect(p,TOK_RBRACKET,"expected ']' after index");
            p_expect(p,TOK_ASSIGN,"expected '=' after arr[i]");
            Node *val=parse_expr(p); p_match(p,TOK_SEMICOLON);
            Node *n=node_new(NODE_ASSIGN,t.line,t.col); n->str_val=strdup(name.value);
            node_add_child(n,idx_expr); node_add_child(n,val);
            n->var_type=strdup("indexed"); return n;
        }

        
        if (p_check(p,TOK_LPAREN)) {
            if (is_func_def(p)) {
                p_advance(p);
                Node *n=node_new(NODE_FUNC_DEF,t.line,t.col); n->str_val=strdup(name.value);
                n->params=NULL; n->param_types=NULL; n->param_count=0;
                while (!p_check(p,TOK_RPAREN)&&!p_check(p,TOK_EOF)) {
                    if (p_check(p,TOK_VOID)) { p_advance(p); break; }
                    Token pname=p_expect(p,TOK_IDENT,"expected param name");
                    p_expect(p,TOK_ASSIGN,"expected '=' after param name");
                    char *ptype=parse_type_str(p);
                    n->params     =realloc(n->params,     (n->param_count+1)*sizeof(char*));
                    n->param_types=realloc(n->param_types,(n->param_count+1)*sizeof(char*));
                    n->params[n->param_count]     =strdup(pname.value);
                    n->param_types[n->param_count]=ptype;
                    n->param_count++;
                    p_match(p,TOK_COMMA);
                }
                p_expect(p,TOK_RPAREN,"expected ')'");
                if (p_match(p,TOK_ARROW)) { n->ret_type=parse_type_str(p); }
                p_expect(p,TOK_LBRACE,"expected '{'");
                n->body=parse_block(p); p_expect(p,TOK_END,"expected 'end'"); return n;
            } else {
                p_advance(p);
                Node *n=node_new(NODE_FUNC_CALL,t.line,t.col); n->str_val=strdup(name.value);
                while (!p_check(p,TOK_RPAREN)&&!p_check(p,TOK_EOF)) {
                    if (p_check(p,TOK_VOID)) { p_advance(p); break; }
                    node_add_child(n,parse_expr(p)); p_match(p,TOK_COMMA);
                }
                p_expect(p,TOK_RPAREN,"expected ')'"); p_match(p,TOK_SEMICOLON); return n;
            }
        }

        
        p_expect(p,TOK_ASSIGN,"expected '=' after identifier");

        
        if (p_check(p,TOK_INT)||p_check(p,TOK_FLOAT)||p_check(p,TOK_STR)||
            p_check(p,TOK_BOOL)||p_check(p,TOK_DICT)||p_check(p,TOK_VOID)) {
            char *type_str=parse_type_str(p);
            Node *n=node_new(NODE_VAR_DECL,t.line,t.col); n->str_val=strdup(name.value);
            n->var_type=type_str;
            n->is_nullable=(strlen(type_str)>0&&type_str[strlen(type_str)-1]=='?');
            return n;
        }
        
        if (p_check(p,TOK_IDENT)) {
            
            int la=p->pos+1;
            TokenType la_t=(la<p->tl.count)?p->tl.tokens[la].type:TOK_EOF;
            if (la_t==TOK_SEMICOLON||la_t==TOK_END||la_t==TOK_ALLEND||la_t==TOK_EOF||
                la_t==TOK_IDENT||la_t==TOK_INT||la_t==TOK_STR||la_t==TOK_FLOAT||la_t==TOK_BOOL||
                la_t==TOK_DICT||la_t==TOK_STRUCT||la_t==TOK_IF||la_t==TOK_FOR||la_t==TOK_WHILE||
                la_t==TOK_LOOP||la_t==TOK_RETURN||la_t==TOK_BREAK||la_t==TOK_CONTINUE||
                la_t==TOK_TERMINAL_PRINT||la_t==TOK_TERMINAL_INPUT||la_t==TOK_DOLLAR) {
                Token type_tok=p_advance(p);
                int nullable=0;
                char type_buf[128]; strncpy(type_buf,type_tok.value,sizeof(type_buf)-1); type_buf[sizeof(type_buf)-1]=0;
                if (p_check(p,TOK_QUESTION)) { p_advance(p); strncat(type_buf,"?",sizeof(type_buf)-strlen(type_buf)-1); nullable=1; }
                Node *n=node_new(NODE_VAR_DECL,t.line,t.col); n->str_val=strdup(name.value);
                n->var_type=strdup(type_buf); n->is_nullable=nullable; return n;
            }
        }
        
        Node *n=node_new(NODE_ASSIGN,t.line,t.col); n->str_val=strdup(name.value);
        node_add_child(n,parse_expr(p)); p_match(p,TOK_SEMICOLON); return n;
    }

    char buf[128]; snprintf(buf,sizeof(buf),"文の先頭で予期しないトークン '%s'",t.value?t.value:"?");
    ec_add(t.line,t.col,buf);
    p_advance(p); return NULL;
}

static Node *parse_program(TokenList tl) {
    Parser p={tl,0}; Node *prog=node_new(NODE_PROGRAM,0,0);
    while (!p_check(&p,TOK_EOF)) {
        if (p_check(&p,TOK_ALLEND)) { p_advance(&p); break; }
        Node *s=parse_stmt(&p); if(s) node_add_child(prog,s);
    }
    if (ec_has_errors()) ec_flush_and_exit();
    return prog;
}
