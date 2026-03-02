# 🐰 uSagi v5

Cで実装されたコンパイル型プログラミング言語。  
x86-64 AT&T アセンブリを直接生成し、`as` + `gcc` でネイティブバイナリを出力します。

## ビルド

```bash
make
```

**GUI機能を使う場合は SDL2 が必要です：**
```bash
# Ubuntu/Debian
apt install libsdl2-dev

# macOS
brew install sdl2
```

## 使い方

```bash
./usagi hello.usagi             # コンパイル → ./hello
./usagi hello.usagi -o myapp    # 出力名を指定
./usagi hello.usagi --emit-asm  # 生成ASMを標準出力
./usagi hello.usagi --emit-c    # 生成Cコードを標準出力
./usagi hello.usagi --backend-c # gccバックエンドでビルド
./usagi hello.usagi --no-typecheck
```

拡張子は `.usagi` / `.usg` / `.rabi` に対応しています。

## コンパイルパイプライン

```
.usagi → Lexer → Parser → TypeChecker → ASM Codegen → as → gcc → binary
                                      ↘ (--backend-c) C Codegen → gcc → binary
```

## 構成

```
usagi/
├── src/
│   ├── main.c                エントリポイント・リンカ
│   ├── lexer.h               字句解析
│   ├── ast.h                 ASTノード定義
│   ├── parser.h              構文解析（再帰下降）
│   ├── typechecker.h         型チェック
│   ├── codegen.h             Cバックエンド
│   ├── asm_codegen.h         x86-64 ASMバックエンド（デフォルト）
│   ├── usagi_runtime.c       ランタイム（dict / concat など）
│   ├── usagi_gui_runtime.c   GUIランタイム（SDL2）
│   └── usagi_file_runtime.c  ファイルI/Oランタイム
├── lib/
│   ├── math.usagi
│   └── string.usagi
├── examples/
│   ├── hello.usagi
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
| `int` | 整数（64bit）|
| `float` | 浮動小数点（64bit）|
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
&  |  ^  ~  <<  >>   \\ ビット演算
..                   \\ 文字列連結
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

### 入出力 (terminal)

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

---

## GUI (gui.xxx) — SDL2

ウィンドウ作成、描画、入力を扱います。

```
\\ ウィンドウ初期化
gui.init()
gui.window("My Game", 640, 480)

\\ メインループ
loop {
    \\ イベント取得（1=終了要求）
    quit := gui.poll()
    if quit == 1 then { break end

    \\ キー入力（SDL_Scancode 値を使用）
    \\ 4=A, 7=D, 26=W, 22=S, 44=Space, 41=Escape
    left := gui.key(4)

    \\ 画面クリア（0xRRGGBB）
    gui.clear(0x112233)

    \\ ピクセル配列を描画
    \\ display = int[]  各要素 0xRRGGBB
    gui.blit(display, 0, 0, 284, 240)

    \\ 画面に反映
    gui.present()

    \\ フレームレート制限
    gui.delay(16)
end

gui.quit()
```

### GUI API 一覧

| 関数 | 説明 |
|---|---|
| `gui.init()` | SDL2 初期化 |
| `gui.window(title, w, h)` | ウィンドウ作成 |
| `gui.quit()` | 終了・リソース解放 |
| `gui.clear(color)` | 画面クリア（`0xRRGGBB`）|
| `gui.present()` | フレームバッファを表示 |
| `gui.blit(arr, x, y, w, h)` | `int[]` ピクセル配列を描画 |
| `gui.poll()` | イベント処理（`0`=続行, `1`=終了）|
| `gui.key(scancode)` | キー押下判定（`1`=押中）|
| `gui.delay(ms)` | ミリ秒ウェイト |

**よく使う SDL Scancode:**

| キー | Scancode |
|---|---|
| A | 4 |
| D | 7 |
| W | 26 |
| S | 22 |
| Space | 44 |
| Escape | 41 |
| Enter | 40 |
| 矢印左 | 80 |
| 矢印右 | 79 |
| 矢印上 | 82 |
| 矢印下 | 81 |

---

## ファイルI/O (file.xxx)

```
\\ ファイルが存在するか確認
exists := file.exists("/path/to/bios.rom")

\\ ファイルを開く（mode: "r" "w" "rb" "wb" "r+" "w+" など）
fh := file.open("/path/to/bios.rom", "rb")
if fh == 0 then {
    terminal.print("open failed");
end

\\ ファイルサイズ取得
sz := file.size(fh)
terminal.print("size:", sz "");

\\ 1バイト読み込み（-1 = EOF）
b := file.read_byte(fh)

\\ 複数バイト読み込み（int[] に格納）
buf = int[]
buf = [0, 0, 0, 0, 0, 0, 0, 0]
n := file.read_bytes(fh, buf, 8)

\\ シーク（先頭から）
file.seek(fh, 0x4000)

\\ 書き込み
wh := file.open("/tmp/out.bin", "wb")
file.write_byte(wh, 0xFF)
file.write_bytes(wh, buf, n)
file.close(wh)

\\ 閉じる
file.close(fh)
```

### File API 一覧

| 関数 | 説明 |
|---|---|
| `file.exists(path)` | ファイル存在確認 → `int`（1=存在）|
| `file.open(path, mode)` | ファイルを開く → `int` ハンドル（0=失敗）|
| `file.close(handle)` | ファイルを閉じる |
| `file.read_byte(handle)` | 1バイト読み込み → `int`（-1=EOF）|
| `file.read_bytes(handle, arr, n)` | 最大n バイト読み込み → `int` 実際の読込数 |
| `file.write_byte(handle, value)` | 1バイト書き込み |
| `file.write_bytes(handle, arr, n)` | n バイト書き込み |
| `file.seek(handle, offset)` | 先頭からシーク |
| `file.size(handle)` | ファイルサイズ → `int` |

---

## エミュレータ用例（MSX BIOS読み込み）

```
main(void) {
    \\ BIOSファイルを確認・読み込み
    bios_path = "/path/to/cbios.rom"
    if file.exists(bios_path) == 0 then {
        terminal.print("BIOS not found");
    end

    fh := file.open(bios_path, "rb")
    sz := file.size(fh)
    terminal.print("BIOS size:", sz "");

    bios = int[]
    bios = [...]  \\ 0x8000 要素
    file.read_bytes(fh, bios, sz)
    file.close(fh)

    \\ GUIウィンドウ起動
    gui.init()
    gui.window("TinyMSX", 568, 480)  \\ 284*2 x 240*2

    display = int[]
    display = [...]   \\ 68160 要素 (284*240)

    loop {
        if gui.poll() == 1 then { break end
        \\ ... エミュレータ tick ...
        gui.blit(display, 0, 0, 284, 240)
        gui.present()
        gui.delay(16)
    end

    gui.quit()
end
```

---

## v5 変更点

- x86-64 AT&T ASMを直接生成
- `struct` / `new` によるユーザー定義構造体
- `dict<K,V>` 型
- nullable型（`str?`）と `nil`
- `.usg` / `.rabi` 拡張子サポート
- `usagi_runtime.c` によるランタイムライブラリ分離

## v5.1 追加機能（このビルド）

- **`gui.xxx`** — SDL2 ウィンドウ・描画・入力 API
- **`file.xxx`** — ファイルオープン・読み書き・シーク API
- ASMバックエンドでも `usagi_gui_runtime.c` / `usagi_file_runtime.c` を自動リンク

---

## ライセンス

Apache License 2.0
