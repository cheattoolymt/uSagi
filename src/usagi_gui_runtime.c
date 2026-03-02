/*
 * usagi_file_runtime.c  —  File I/O backend for uSagi
 * Copyright 2026 nyan<cheattoolymt>  Apache-2.0
 *
 * file.open(path, mode)            -> int handle  ("r","w","rb","wb","r+","w+"...)
 * file.close(handle)
 * file.read_byte(handle)           -> int  (-1 = EOF)
 * file.read_bytes(handle, arr, n)  -> int  (実際に読んだバイト数、arr は int[])
 * file.write_byte(handle, value)
 * file.write_bytes(handle, arr, n) (arr は int[])
 * file.seek(handle, offset)        先頭からのシーク
 * file.size(handle)                -> int ファイルサイズ
 * file.exists(path)                -> int (1=存在, 0=なし)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ハンドルテーブル（最大256ファイル同時） */
#define USAGI_MAX_FILES 256
static FILE *g_files[USAGI_MAX_FILES] = {0};

static int alloc_handle(FILE *f) {
    /* 0は予約（無効値） */
    for (int i=1;i<USAGI_MAX_FILES;i++) {
        if (!g_files[i]) { g_files[i]=f; return i; }
    }
    return -1;  /* テーブルが満杯 */
}

long usagi_file_open(const char *path, const char *mode) {
    if (!path||!mode) return 0;
    FILE *f=fopen(path,mode);
    if (!f) return 0;
    int h=alloc_handle(f);
    if (h<0) { fclose(f); return 0; }
    return (long)h;
}

void usagi_file_close(long handle) {
    int h=(int)handle;
    if (h<=0||h>=USAGI_MAX_FILES||!g_files[h]) return;
    fclose(g_files[h]);
    g_files[h]=NULL;
}

long usagi_file_read_byte(long handle) {
    int h=(int)handle;
    if (h<=0||h>=USAGI_MAX_FILES||!g_files[h]) return -1;
    int c=fgetc(g_files[h]);
    return (c==EOF) ? -1 : (long)c;
}

/* arr は uSagi int[] = long[] */
long usagi_file_read_bytes(long handle, long *arr, long count) {
    int h=(int)handle;
    if (h<=0||h>=USAGI_MAX_FILES||!g_files[h]||!arr||count<=0) return 0;
    long read=0;
    for (long i=0;i<count;i++) {
        int c=fgetc(g_files[h]);
        if (c==EOF) break;
        arr[i]=(long)(unsigned char)c;
        read++;
    }
    return read;
}

void usagi_file_write_byte(long handle, long value) {
    int h=(int)handle;
    if (h<=0||h>=USAGI_MAX_FILES||!g_files[h]) return;
    fputc((int)(value&0xFF), g_files[h]);
}

void usagi_file_write_bytes(long handle, long *arr, long count) {
    int h=(int)handle;
    if (h<=0||h>=USAGI_MAX_FILES||!g_files[h]||!arr||count<=0) return;
    for (long i=0;i<count;i++)
        fputc((int)(arr[i]&0xFF), g_files[h]);
}

void usagi_file_seek(long handle, long offset) {
    int h=(int)handle;
    if (h<=0||h>=USAGI_MAX_FILES||!g_files[h]) return;
    fseek(g_files[h],(long)offset,SEEK_SET);
}

long usagi_file_size(long handle) {
    int h=(int)handle;
    if (h<=0||h>=USAGI_MAX_FILES||!g_files[h]) return 0;
    long cur=ftell(g_files[h]);
    fseek(g_files[h],0,SEEK_END);
    long sz=ftell(g_files[h]);
    fseek(g_files[h],cur,SEEK_SET);
    return sz;
}

long usagi_file_exists(const char *path) {
    if (!path) return 0;
    struct stat st;
    return stat(path,&st)==0 ? 1 : 0;
}
