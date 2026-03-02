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

#define USAGI_MAX_ERRORS 128

typedef struct {
    char *filename;
    int   line, col;
    char *message;
    char *source_line;  
} UsagiError;

typedef struct {
    UsagiError errors[USAGI_MAX_ERRORS];
    int        count;
    const char *filename;
    const char *source;   
} ErrorCollector;

static ErrorCollector g_errors = {0};

static void ec_init(const char *filename, const char *source) {
    g_errors.count    = 0;
    g_errors.filename = filename;
    g_errors.source   = source;
}

static char *ec_get_line(const char *src, int lineno) {
    if (!src) return NULL;
    int cur=1;
    const char *p=src;
    while (*p && cur<lineno) { if (*p=='\n') cur++; p++; }
    const char *end=p;
    while (*end && *end!='\n') end++;
    int len=(int)(end-p);
    char *buf=malloc(len+1); memcpy(buf,p,len); buf[len]=0;
    return buf;
}

static void ec_add(int line, int col, const char *msg) {
    if (g_errors.count>=USAGI_MAX_ERRORS) return;
    UsagiError *e=&g_errors.errors[g_errors.count++];
    e->filename    = g_errors.filename ? strdup(g_errors.filename) : strdup("<unknown>");
    e->line        = line;
    e->col         = col;
    e->message     = strdup(msg);
    e->source_line = ec_get_line(g_errors.source, line);
}

static int ec_has_errors(void) { return g_errors.count > 0; }

static void ec_flush_and_exit(void) {
    if (!g_errors.count) return;
    fprintf(stderr, "\n");
    for (int i=0;i<g_errors.count;i++) {
        UsagiError *e=&g_errors.errors[i];
        
        fprintf(stderr,
            "\033[1;31m[エラー]\033[0m \033[1m%s:%d:%d\033[0m\n",
            e->filename, e->line, e->col);
        
        if (e->source_line) {
            fprintf(stderr, "  %s\n", e->source_line);
            
            fprintf(stderr, "  ");
            for (int c=1;c<e->col;c++) fprintf(stderr," ");
            fprintf(stderr, "\033[1;31m^\033[0m \033[1m%s\033[0m\n", e->message);
        } else {
            fprintf(stderr, "  \033[1;31m%s\033[0m\n", e->message);
        }
        fprintf(stderr, "\n");
        free(e->filename); free(e->message); free(e->source_line);
    }
    fprintf(stderr,
        "\033[1;31m%d個のエラーが見つかりました。\033[0m\n\n",
        g_errors.count);
    g_errors.count=0;
    exit(1);
}

static void ec_warn(int line, int col, const char *msg) {
    if (!g_errors.filename) return;
    char *src_line=ec_get_line(g_errors.source,line);
    fprintf(stderr,
        "\033[1;33m[警告]\033[0m \033[1m%s:%d:%d\033[0m\n",
        g_errors.filename, line, col);
    if (src_line) {
        fprintf(stderr,"  %s\n  ",src_line);
        for (int c=1;c<col;c++) fprintf(stderr," ");
        fprintf(stderr,"\033[1;33m^\033[0m \033[1m%s\033[0m\n\n",msg);
        free(src_line);
    } else {
        fprintf(stderr,"  \033[1;33m%s\033[0m\n\n",msg);
    }
}
