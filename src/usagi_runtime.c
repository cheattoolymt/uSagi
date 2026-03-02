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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef struct { char *key; char *val; } UsagiDictEntry;
typedef struct { UsagiDictEntry *entries; int count, cap; } UsagiDict;

UsagiDict *usagi_dict_new(void) {
    UsagiDict *d=calloc(1,sizeof(UsagiDict)); return d;
}
void usagi_dict_set(UsagiDict *d, const char *key, const char *val) {
    if (!d) return;
    for (int i=0;i<d->count;i++)
        if (!strcmp(d->entries[i].key,key)) { free(d->entries[i].val); d->entries[i].val=strdup(val); return; }
    if (d->count>=d->cap) {
        d->cap=d->cap?d->cap*2:8;
        d->entries=realloc(d->entries,d->cap*sizeof(UsagiDictEntry));
    }
    d->entries[d->count].key=strdup(key);
    d->entries[d->count].val=strdup(val);
    d->count++;
}
char *usagi_dict_get(UsagiDict *d, const char *key) {
    if (!d) return NULL;
    for (int i=0;i<d->count;i++) if (!strcmp(d->entries[i].key,key)) return d->entries[i].val;
    return NULL;
}
UsagiDict *usagi_dict_from(int n, ...) {
    UsagiDict *d=usagi_dict_new();
    va_list ap; va_start(ap,n);
    for (int i=0;i<n;i++) {
        char *k=va_arg(ap,char*);
        char *v=va_arg(ap,char*);
        usagi_dict_set(d,k,v);
    }
    va_end(ap); return d;
}
