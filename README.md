# 🐰 uSagi v5

Cで実装されたコンパイル型プログラミング言語。  
x86-64 AT&T アセンブリを直接生成し、`as` + `ld` でネイティブバイナリを出力します。

## ビルド

```bash
make
```

## 使い方

```bash
./usagi hello.usg              # コンパイル → ./hello
./usagi hello.usg -o myapp     # 出力名を指定
./usagi hello.usg --emit-asm   # 生成ASMを標準出力
./usagi hello.usg --emit-c     # 生成Cコードを標準出力
./usagi hello.usg --backend-c  # gccバックエンドでビルド
./usagi hello.usg --no-typecheck
```

拡張子は `.usagi` / `.usg` / `.rabi` に対応しています。

## コンパイルパイプライン

```
.usg → Lexer → Parser → TypeChecker → ASM Codegen → as → ld → binary
```

## 構成

```
usagi-v5/
├── src/
│   ├── main.c            エントリポイント
│   ├── lexer.h           字句解析
│   ├── ast.h             ASTノード定義
│   ├── parser.h          構文解析（再帰下降）
│   ├── typechecker.h     型チェック
│   ├── codegen.h         Cバックエンド
│   ├── asm_codegen.h     x86-64 ASMバックエンド（デフォルト）
│   └── usagi_runtime.c   ランタイム（dict / concat など）
├── lib/
│   ├── math.usagi
│   └── string.usagi
├── examples/
│   ├── hello.usagi
│   ├── v5_test.usagi
│   └── ...
└── Makefile
```

---

## 言語仕様

### 変数

```
x = int
y = float
s = str
b = bool

x = 42
s = "hello"

name := "usagi"    \\ 型推論
```

### nullable

```
note = str?
note = "hello"
note = nil

if note == nil then {
    terminal.print("nil");
end
```

### 型一覧

| 型 | 説明 |
|---|---|
| `int` | 整数 |
| `float` | 浮動小数点 |
| `str` | 文字列 |
| `bool` | `true` / `false` |
| `T?` | nullable |
| `dict<K,V>` | 辞書 |
| `T[]` | 配列 |
| `StructName` | 構造体 |

### 演算子

```
+  -  *  /  %
==  !=  <  >  <=  >=
and  or  not
..          \\ 文字列連結
```

### 文字列

```
s = "hello" .. " world"
msg := f"name={name} ver={ver}"
```

### 配列

```
arr = int[]
arr = [10, 20, 30]
terminal.print(arr[0] "");
terminal.print(len(arr) "");
```

### dict

```
env = dict<str, str>
env = {"lang": "usagi", "ver": "5"}
terminal.print(env["lang"]);
```

### struct

```
struct Point {
    x = int
    y = int
end

p = Point
p = new Point{ x: 10, y: 20 }
terminal.print(p.x "");
p.x = 99
```

### 関数

```
add(a = int, b = int) -> int {
    return a + b
end

greet(name = str) {
    terminal.print("Hello,", name);
end

greet("Alice");
result = add(10, 32);
```

### 制御構文

```
\\ if
if x > 10 then {
    terminal.print("big");
elseif x > 5 then {
    terminal.print("medium");
else
    terminal.print("small");
end

\\ for range
for i = 1 to 10 {
    terminal.print(i "");
end

\\ for in
for v in arr {
    terminal.print(v "");
end

\\ while
while x < 5 {
    x = x + 1
}

\\ loop
loop {
    if done then { break end
}

\\ match
match day {
    case 1 => {
        terminal.print("Monday");
    end
    case _ => {
        terminal.print("Other");
    end
}
```

### 入出力

```
terminal.print("x =", x "");
terminal.input("name?", name);
```

### モジュール

```
$pull math
$pull string

terminal.print(abs(-5) "");
s := str_repeat("=-", 10)
```

**math:** `abs` `max` `min` `pow` `clamp` `is_even` `is_odd`  
**string:** `str_repeat` `str_empty` `to_str` `to_str_f`

### ファイル末尾

すべての `.usagi` / `.usg` ファイルは `allend` で終わる必要があります。

---

## v5 変更点

- x86-64 AT&T ASMを直接生成（gccなしでリンク可能、デフォルトバックエンド）
- `struct` / `new` によるユーザー定義構造体
- `dict<K,V>` 型
- nullable型（`str?` など）と `nil`
- `.usg` / `.rabi` 拡張子サポート
- `usagi_runtime.c` によるランタイムライブラリ分離

---

## ライセンス

Apache License 2.0
