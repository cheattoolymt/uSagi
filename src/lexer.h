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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef enum {
    TOK_INT_LIT, TOK_FLOAT_LIT, TOK_STR_LIT, TOK_FSTR_LIT,
    TOK_INT, TOK_FLOAT, TOK_STR, TOK_VOID, TOK_BOOL, TOK_DICT,
    TOK_IDENT,
    TOK_PULL,
    TOK_MAIN,
    TOK_IF, TOK_THEN, TOK_ELSE, TOK_ELSEIF,
    TOK_WHILE, TOK_FOR, TOK_LOOP,
    TOK_END, TOK_ALLEND,
    TOK_TERMINAL_PRINT, TOK_TERMINAL_INPUT,
    TOK_RETURN,
    TOK_BREAK, TOK_CONTINUE,
    TOK_IN, TOK_TO,
    TOK_TRUE, TOK_FALSE,
    TOK_NIL,
    TOK_MATCH, TOK_CASE,
    TOK_FATARROW,
    TOK_COLONASSIGN,
    
    TOK_STRUCT,
    TOK_NEW,
    TOK_QUESTION,
    TOK_DOT,
    
    TOK_ASSIGN,
    TOK_EQ, TOK_NEQ,
    TOK_LT, TOK_GT, TOK_LE, TOK_GE,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_CONCAT,
    TOK_ARROW,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_COMMA, TOK_SEMICOLON, TOK_DOLLAR, TOK_COLON,
    TOK_EOF,
    TOK_UNKNOWN
} TokenType;

typedef struct {
    TokenType type;
    char *value;
    int line;
    int col;
} Token;

typedef struct {
    Token *tokens;
    int    count, cap;
} TokenList;

typedef struct {
    const char *src;
    int pos, line, col;
} Lexer;

static Token make_token(TokenType t, const char *val, int line, int col) {
    Token tok; tok.type=t; tok.value=val?strdup(val):NULL; tok.line=line; tok.col=col; return tok;
}
static void tl_push(TokenList *tl, Token tok) {
    if (tl->count>=tl->cap) { tl->cap=tl->cap?tl->cap*2:64; tl->tokens=realloc(tl->tokens,tl->cap*sizeof(Token)); }
    tl->tokens[tl->count++]=tok;
}
static void token_list_free(TokenList *tl) {
    for (int i=0;i<tl->count;i++) free(tl->tokens[i].value);
    free(tl->tokens); tl->tokens=NULL; tl->count=tl->cap=0;
}
static void skip_whitespace(Lexer *l) {
    while (l->src[l->pos]&&(l->src[l->pos]==' '||l->src[l->pos]=='\t'||l->src[l->pos]=='\r'||l->src[l->pos]=='\n')) {
        if (l->src[l->pos]=='\n') { l->line++; l->col=1; } else l->col++;
        l->pos++;
    }
}
static int match_comment(Lexer *l) {
    if (l->src[l->pos]=='\\'&&l->src[l->pos+1]=='\\') {
        if (l->src[l->pos+2]=='*') {
            l->pos+=3; l->col+=3;
            while (l->src[l->pos]) {
                if (l->src[l->pos]=='*'&&l->src[l->pos+1]=='\\'&&l->src[l->pos+2]=='\\') { l->pos+=3; l->col+=3; return 1; }
                if (l->src[l->pos]=='\n') { l->line++; l->col=1; } else l->col++;
                l->pos++;
            }
            return 1;
        }
        while (l->src[l->pos]&&l->src[l->pos]!='\n') { l->pos++; l->col++; }
        return 1;
    }
    return 0;
}
static Token lex_string(Lexer *l, int is_fstr) {
    int sc=l->col; l->pos++; l->col++;
    int cap=256,bi=0; char *buf=malloc(cap);
    while (l->src[l->pos]&&l->src[l->pos]!='"') {
        if (bi+8>=cap) { cap*=2; buf=realloc(buf,cap); }
        if (l->src[l->pos]=='\\') {
            l->pos++; l->col++;
            switch(l->src[l->pos]) {
                case 'n': buf[bi++]='\n'; break; case 't': buf[bi++]='\t'; break;
                case '"': buf[bi++]='"';  break; case '\\': buf[bi++]='\\'; break;
                default: buf[bi++]='\\'; buf[bi++]=l->src[l->pos]; break;
            }
            l->pos++; l->col++; continue;
        }
        buf[bi++]=l->src[l->pos++]; l->col++;
    }
    if (l->src[l->pos]=='"') { l->pos++; l->col++; }
    buf[bi]=0;
    Token tok=make_token(is_fstr?TOK_FSTR_LIT:TOK_STR_LIT,buf,l->line,sc);
    free(buf); return tok;
}
static Token lex_number(Lexer *l) {
    int start=l->pos,sc=l->col,is_float=0;
    while (isdigit(l->src[l->pos])) { l->pos++; l->col++; }
    if (l->src[l->pos]=='.'&&isdigit(l->src[l->pos+1])) { is_float=1; l->pos++; l->col++; while(isdigit(l->src[l->pos])) { l->pos++; l->col++; } }
    int len=l->pos-start; char *buf=malloc(len+1); memcpy(buf,l->src+start,len); buf[len]=0;
    Token tok=make_token(is_float?TOK_FLOAT_LIT:TOK_INT_LIT,buf,l->line,sc);
    free(buf); return tok;
}
static Token lex_ident(Lexer *l) {
    int start=l->pos,sc=l->col;
    while (l->src[l->pos]&&(isalnum(l->src[l->pos])||l->src[l->pos]=='_')) { l->pos++; l->col++; }
    if (l->src[l->pos]=='.') {
        int dot=l->pos,dc=l->col; l->pos++; l->col++;
        if (strncmp(l->src+l->pos,"print",5)==0&&!isalnum(l->src[l->pos+5])&&l->src[l->pos+5]!='_') {
            l->pos+=5; l->col+=5; return make_token(TOK_TERMINAL_PRINT,"terminal.print",l->line,sc);
        }
        if (strncmp(l->src+l->pos,"input",5)==0&&!isalnum(l->src[l->pos+5])&&l->src[l->pos+5]!='_') {
            l->pos+=5; l->col+=5; return make_token(TOK_TERMINAL_INPUT,"terminal.input",l->line,sc);
        }
        l->pos=dot; l->col=dc;
    }
    int len=l->pos-start; char *buf=malloc(len+1); memcpy(buf,l->src+start,len); buf[len]=0;
    TokenType kw=TOK_IDENT;
    if      (!strcmp(buf,"int"))      kw=TOK_INT;
    else if (!strcmp(buf,"float"))    kw=TOK_FLOAT;
    else if (!strcmp(buf,"str"))      kw=TOK_STR;
    else if (!strcmp(buf,"bool"))     kw=TOK_BOOL;
    else if (!strcmp(buf,"void"))     kw=TOK_VOID;
    else if (!strcmp(buf,"dict"))     kw=TOK_DICT;
    else if (!strcmp(buf,"if"))       kw=TOK_IF;
    else if (!strcmp(buf,"then"))     kw=TOK_THEN;
    else if (!strcmp(buf,"else"))     kw=TOK_ELSE;
    else if (!strcmp(buf,"elseif"))   kw=TOK_ELSEIF;
    else if (!strcmp(buf,"while"))    kw=TOK_WHILE;
    else if (!strcmp(buf,"for"))      kw=TOK_FOR;
    else if (!strcmp(buf,"loop"))     kw=TOK_LOOP;
    else if (!strcmp(buf,"end"))      kw=TOK_END;
    else if (!strcmp(buf,"allend"))   kw=TOK_ALLEND;
    else if (!strcmp(buf,"return"))   kw=TOK_RETURN;
    else if (!strcmp(buf,"break"))    kw=TOK_BREAK;
    else if (!strcmp(buf,"continue")) kw=TOK_CONTINUE;
    else if (!strcmp(buf,"in"))       kw=TOK_IN;
    else if (!strcmp(buf,"to"))       kw=TOK_TO;
    else if (!strcmp(buf,"true"))     kw=TOK_TRUE;
    else if (!strcmp(buf,"false"))    kw=TOK_FALSE;
    else if (!strcmp(buf,"nil"))      kw=TOK_NIL;
    else if (!strcmp(buf,"match"))    kw=TOK_MATCH;
    else if (!strcmp(buf,"case"))     kw=TOK_CASE;
    else if (!strcmp(buf,"and"))      kw=TOK_AND;
    else if (!strcmp(buf,"or"))       kw=TOK_OR;
    else if (!strcmp(buf,"not"))      kw=TOK_NOT;
    else if (!strcmp(buf,"struct"))   kw=TOK_STRUCT;
    else if (!strcmp(buf,"new"))      kw=TOK_NEW;
    else if (!strcmp(buf,"pull"))     kw=TOK_PULL;
    Token tok=make_token(kw,buf,l->line,sc);
    free(buf); return tok;
}
static TokenList tokenize(const char *src) {
    TokenList tl={0}; Lexer l={src,0,1,1};
    while (1) {
        skip_whitespace(&l);
        if (!l.src[l.pos]) { tl_push(&tl,make_token(TOK_EOF,NULL,l.line,l.col)); break; }
        if (match_comment(&l)) continue;
        char c=l.src[l.pos];
        int ln=l.line,co=l.col;
        if (c=='f'&&l.src[l.pos+1]=='"') { l.pos++; l.col++; tl_push(&tl,lex_string(&l,1)); continue; }
        if (c=='"') { tl_push(&tl,lex_string(&l,0)); continue; }
        if (isdigit(c)) { tl_push(&tl,lex_number(&l)); continue; }
        if (isalpha(c)||c=='_') { tl_push(&tl,lex_ident(&l)); continue; }
        if (c=='='&&l.src[l.pos+1]=='=') { l.pos+=2;l.col+=2; tl_push(&tl,make_token(TOK_EQ,"==",ln,co)); continue; }
        if (c=='!'&&l.src[l.pos+1]=='=') { l.pos+=2;l.col+=2; tl_push(&tl,make_token(TOK_NEQ,"!=",ln,co)); continue; }
        if (c=='<'&&l.src[l.pos+1]=='=') { l.pos+=2;l.col+=2; tl_push(&tl,make_token(TOK_LE,"<=",ln,co)); continue; }
        if (c=='>'&&l.src[l.pos+1]=='=') { l.pos+=2;l.col+=2; tl_push(&tl,make_token(TOK_GE,">=",ln,co)); continue; }
        if (c=='-'&&l.src[l.pos+1]=='>') { l.pos+=2;l.col+=2; tl_push(&tl,make_token(TOK_ARROW,"->",ln,co)); continue; }
        if (c=='='&&l.src[l.pos+1]=='>') { l.pos+=2;l.col+=2; tl_push(&tl,make_token(TOK_FATARROW,"=>",ln,co)); continue; }
        if (c==':'&&l.src[l.pos+1]=='=') { l.pos+=2;l.col+=2; tl_push(&tl,make_token(TOK_COLONASSIGN,":=",ln,co)); continue; }
        if (c=='.'&&l.src[l.pos+1]=='.') { l.pos+=2;l.col+=2; tl_push(&tl,make_token(TOK_CONCAT,"..",ln,co)); continue; }
        l.pos++; l.col++;
        switch(c) {
            case '=': tl_push(&tl,make_token(TOK_ASSIGN,"=",ln,co));    break;
            case '<': tl_push(&tl,make_token(TOK_LT,"<",ln,co));        break;
            case '>': tl_push(&tl,make_token(TOK_GT,">",ln,co));        break;
            case '+': tl_push(&tl,make_token(TOK_PLUS,"+",ln,co));      break;
            case '-': tl_push(&tl,make_token(TOK_MINUS,"-",ln,co));     break;
            case '*': tl_push(&tl,make_token(TOK_STAR,"*",ln,co));      break;
            case '/': tl_push(&tl,make_token(TOK_SLASH,"/",ln,co));     break;
            case '%': tl_push(&tl,make_token(TOK_PERCENT,"%",ln,co));   break;
            case '(': tl_push(&tl,make_token(TOK_LPAREN,"(",ln,co));    break;
            case ')': tl_push(&tl,make_token(TOK_RPAREN,")",ln,co));    break;
            case '{': tl_push(&tl,make_token(TOK_LBRACE,"{",ln,co));    break;
            case '}': tl_push(&tl,make_token(TOK_RBRACE,"}",ln,co));    break;
            case '[': tl_push(&tl,make_token(TOK_LBRACKET,"[",ln,co));  break;
            case ']': tl_push(&tl,make_token(TOK_RBRACKET,"]",ln,co));  break;
            case ',': tl_push(&tl,make_token(TOK_COMMA,",",ln,co));     break;
            case ';': tl_push(&tl,make_token(TOK_SEMICOLON,";",ln,co)); break;
            case '$': tl_push(&tl,make_token(TOK_DOLLAR,"$",ln,co));    break;
            case ':': tl_push(&tl,make_token(TOK_COLON,":",ln,co));     break;
            case '?': tl_push(&tl,make_token(TOK_QUESTION,"?",ln,co));  break;
            case '.': tl_push(&tl,make_token(TOK_DOT,".",ln,co));       break;
            default: { char unk[2]={c,0}; tl_push(&tl,make_token(TOK_UNKNOWN,unk,ln,co)); break; }
        }
    }
    return tl;
}
