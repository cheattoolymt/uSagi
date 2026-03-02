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
#include <stdlib.h>
#include <string.h>

typedef enum {
    NODE_PROGRAM, NODE_PULL,
    NODE_VAR_DECL, NODE_ASSIGN,
    NODE_INT_LIT, NODE_FLOAT_LIT, NODE_STR_LIT, NODE_BOOL_LIT,
    NODE_IDENT, NODE_ARRAY_LIT, NODE_INDEX,
    NODE_BINOP, NODE_UNOP, NODE_CONCAT,
    NODE_FUNC_DEF, NODE_FUNC_CALL,
    NODE_TERMINAL_PRINT, NODE_TERMINAL_INPUT,
    NODE_IF, NODE_WHILE,
    NODE_FOR_RANGE, NODE_FOR_IN, NODE_LOOP,
    NODE_BREAK, NODE_CONTINUE,
    NODE_RETURN, NODE_BLOCK,
    NODE_FSTRING,
    NODE_DICT_LIT,
    NODE_DICT_ACCESS,
    NODE_MATCH,
    NODE_MATCH_ARM,
    NODE_TYPE_INFER,
    NODE_NIL_LIT,
    
    NODE_STRUCT_DEF,     
    NODE_STRUCT_INIT,    
    NODE_FIELD_ACCESS,   
    NODE_FIELD_ASSIGN,   
    NODE_NULLABLE_DECL,  
    /* gui.xxx / file.xxx API 呼び出し */
    NODE_GUI_CALL,       /* str_val = "init"/"window"/... children=引数 */
    NODE_FILE_CALL,      /* str_val = "open"/"close"/"read_byte"/... children=引数 */
} NodeType;

typedef struct Node Node;
struct Node {
    NodeType type;
    int line, col;
    char   *str_val;
    long    int_val;
    double  float_val;
    int     bool_val;
    Node  **children;
    int     child_count, child_cap;
    char  **params;
    char  **param_types;
    int     param_count;
    char   *ret_type;
    char   *var_type;
    char   *elem_type;
    Node   *cond, *body, *else_body;
    Node   *init, *limit;
    
    int     is_nullable;  
};

static Node *node_new(NodeType type, int line, int col) {
    Node *n=calloc(1,sizeof(Node)); n->type=type; n->line=line; n->col=col; return n;
}
static void node_add_child(Node *parent, Node *child) {
    if (parent->child_count>=parent->child_cap) {
        parent->child_cap=parent->child_cap?parent->child_cap*2:4;
        parent->children=realloc(parent->children,parent->child_cap*sizeof(Node*));
    }
    parent->children[parent->child_count++]=child;
}
static void node_free(Node *n) {
    if (!n) return;
    free(n->str_val); free(n->var_type); free(n->elem_type); free(n->ret_type);
    for (int i=0;i<n->param_count;i++) { free(n->params[i]); free(n->param_types[i]); }
    free(n->params); free(n->param_types);
    for (int i=0;i<n->child_count;i++) node_free(n->children[i]);
    free(n->children);
    node_free(n->cond); node_free(n->body); node_free(n->else_body);
    node_free(n->init); node_free(n->limit);
    free(n);
}
