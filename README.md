# BmpPalette / Pal16Edit

Windows用のC++20ソリューションです。BMP／PNGを処理するコンソールアプリ **BmpPalette** と、16色パレットを編集するGUIアプリ **Pal16Edit** を含みます。

## ビルドと出力先

Visual Studio（MSVC v145、Windows SDK、C++デスクトップ開発環境）で `BmpPalette.slnx` を開き、構成とプラットフォームを選んでビルドします。

最終バイナリと中間ファイルは分離します。ソリューションからでもプロジェクト単体からでも同じ配置になります。

| 構成 | 最終バイナリ（ルート基準） | 中間ファイル（各プロジェクト基準） |
| --- | --- | --- |
| Debug / x64 | `x64/Debug/` | `x64/Debug/` |
| Release / x64 | `x64/Release/` | `x64/Release/` |
| Debug / Win32 | `Win32/Debug/` | `Win32/Debug/` |
| Release / Win32 | `Win32/Release/` | `Win32/Release/` |

例: `x64/Release/Pal16Edit.exe` が最終ファイル、`Pal16Edit/x64/Release/` がその中間ファイルです。ルート直下の `Debug/`・`Release/` と、各プロジェクト内への最終バイナリ出力は使用しません。

Developer PowerShellで、ルートから実行する例:

```powershell
MSBuild BmpPalette.slnx /p:Configuration=Release /p:Platform=x64 /m
```

## Pal16Editの使い方

`x64/Release/Pal16Edit.exe` を起動します。UIはDear ImGui 1.92.9b＋DirectX 11で描画し、パレットを比較しやすいグレーの背景と、明るいグレーのボタン・濃い文字を使用します。

1. 色をクリックすると選択色が赤枠になり、下段にプレビューと値を表示します。
2. 6桁のHEX（`FF0000` など）、RGBの数値欄、スライダーで選択色を編集します。
3. 色またはプレビューをダブルクリックすると、ImGuiの色選択ダイアログを開きます。色相バー・RGB・HEXで調整し、「適用」で確定します。「キャンセル」またはEscで変更を破棄します。ダイアログ表示のためにウィンドウを拡大した場合は、閉じたときに元のサイズへ戻ります。
4. `<<` / `>>` で選択色と隣の色を交換します。選択位置も移動し、端ではボタンを無効にします。

| ボタン | 動作 |
| --- | --- |
| Open | 16色パレットを開く。16色パレット付きBMP／PNGからの取り込みも可能 |
| Save | 現在のファイルに保存。新規・BMP／PNGからの取り込み時は保存先を選択 |
| SaveAs | ファイルの種類と保存先を選んで保存。拡張子は選んだ種類に合わせる |
| Copy | 選択色を `#RRGGBB` としてクリップボードにコピー |
| Paste | `#RRGGBB` または `RRGGBB` を選択色に貼り付け |
| Quit | アプリを終了 |

未保存の変更がある状態で終了すると、「保存して終了」「保存せずに終了」「キャンセル（編集に戻る）」を選べます。別のファイルを開く際も保存を確認します。保存をキャンセルした場合や保存に失敗した場合は、終了・読み替えを中止します。タイトル末尾の `*` は未保存の変更を示します。

最小化中の終了要求も処理します。未保存の変更があればウィンドウを復元して保存確認を表示します。パレット保存は同じフォルダーの一時ファイルへ書き込み、ディスクへの書き込み完了後に保存先と置き換えます。書き込み・置き換えに失敗した場合は既存のパレットを保持します。

### パレット形式

| 形式 | Open | Save / SaveAs | 内容 |
| --- | --- | --- | --- |
| GPL | ○ | ○ | GIMP Palette、16色 |
| JSON | ○ | ○ | 16組のRGB整数配列 `[[255,0,0],...]`。読み込みは従来の整数配列にも対応 |
| ACT | ○ | ○ | 保存は16色を宣言した772バイト形式。読み込みは同形式または768バイト形式の先頭16色 |
| TXT | ○ | ○ | 各色を `#RRGGBB` で1行ずつ記述、16色 |
| C | ○ | ○ | `{0xRRGGBB,...}` の初期化子、16色。配列宣言は含まない |
| BMP / PNG | ○ | — | インデックスパレットが正確に16色ある画像から取り込み |

画像ファイルの拡張子を省略した場合は `.bmp` を補います。同名のPNGを自動検索することはありません。RGB／RGBA／グレースケールのPNGはGUIに直接取り込まず、`BmpPalette.exe -makepal image.png -o colors.json` で16色パレットを作成してから開いてください。PNGの透過情報は取り込まず、RGB値だけを扱います。

GUIでは16色をそのまま編集します。コンソールの減色処理に渡す場合は、下記のインデックス0の扱いに注意してください。

## BmpPaletteの使い方

BMP／PNGの情報表示、パレットの抽出・生成、16色への減色を行います。成功時の終了コードは0、エラー時は1で、コンソールメッセージは英語です。

## コマンド

```bat
BmpPalette.exe -help
BmpPalette.exe -info aaa
BmpPalette.exe -info image.png
BmpPalette.exe -pal aaa
BmpPalette.exe -pal aaa -o aaa.txt
BmpPalette.exe -pal aaa -o aaa.json
BmpPalette.exe -pal aaa -o aaa.c
BmpPalette.exe -pal aaa -o aaa.act
BmpPalette.exe -pal aaa -o aaa.gpl
BmpPalette.exe -pal aaa -o aaa
BmpPalette.exe -makepal aaa -o colors.gpl
BmpPalette.exe -makepal aaa -o colors.json
BmpPalette.exe -makepal image.png -o colors.json
BmpPalette.exe -redu "a*.bmp" -p colors.gpl -o ./new/
BmpPalette.exe -redu "a*" -p colors.json
BmpPalette.exe -redu "*.png" -p colors.json -o ./new/
BmpPalette.exe -code aaa.bmp -o aaa_code.cpp
BmpPalette.exe -code aaa.bmp
BmpPalette.exe -code "*.bmp" -o ./code/
```

入力画像は `.bmp` / `.png` に対応します。拡張子は大文字でも使用できます。拡張子省略時は必ず `.bmp` を追加し、PNGを自動検索しません。ワイルドカードも同様で、`"a*"` は `"a*.bmp"`、PNGは `"*.png"` と指定してください。
空白を含むパスは引用符で囲んでください。日本語パスに対応しています。
新コマンドはオプション先頭の形式です。以前の `-getinfo` / `-getpal` / `-getact` / `-getgpl` は置き換えています。

### -info

ファイルサイズ、幅・高さ、上下方向、ビット深度、最大色数、パレット色数、ヘッダーサイズ、圧縮方式番号、画像データ位置・サイズを表示します。
最大色数はビット深度から算出します。実際に使用された色数の集計は行いません。
BMPはCORE (12)、INFO (40)、V2/V3 (52/56)、V4/V5 (108/124バイト) のDIBヘッダーに対応します。PNGは形式、ファイルサイズ、幅・高さ、元のビット深度、パレット色数を表示します。PNGにDIBヘッダーはありません。

### -pal

既存パレットを抽出します。`-o` がなければ標準出力に小文字の `#rrggbb` を1行ずつ表示します。
パレットのないBMP／PNGではエラーになります。PNGはインデックス形式の既存パレットを順序どおりに抽出します。RGB画像からの生成には `-makepal` を使用してください。

| 出力拡張子 | 内容 |
| --- | --- |
| `.txt` | 標準出力と同じテキスト |
| `.json` | RGB値の配列 `[[255,0,255],[0,0,0],[255,0,0],...]` |
| `.c` | Cの初期化子 `{0xFF00FF,0x000000,0xFF0000,...}` |
| `.act` | Adobe Color Table: RGB 256組と色数・透明色なしのフッター（772バイト） |
| `.gpl` | GIMP Palette |
| なし | `.gpl` を追加してGIMP Paletteを出力 |

`.json` は標準JSONのRGB配列です。各色は `[R,G,B]` の順で、各チャンネルは0～255の整数です。
`-makepal` も同じJSON形式で出力し、`-redu -p` で読み込めます。
従来の整数配列（10進数／`0xRRGGBB`）も互換性のため読み込みできます。
`.c` は配列宣言やセミコロンを含まない初期化子のみで、色間にカンマを1つずつ出力します。
パレットファイルの出力先が既にある場合は上書きします。入力BMPと同一のファイルは指定できません。

### -makepal

非圧縮RGB888（24ビット）BMP、またはPNGの色と出現頻度を調べ、15色を選びます。PNGはインデックス色・RGB・RGBA・グレースケールをRGB各8ビットに変換して処理し、アルファ値は使用しません。
インデックス0に `#FF00FF` を追加し、必ず16色のパレットを出力します。出力形式は `.gpl` / `.json`。
`-o` 省略時は入力BMPの拡張子を `.gpl` に変更して同じ場所に保存します。
`-o colors` のように出力拡張子だけを省略した場合は `colors.gpl` になります。

出現頻度を考慮した初期色選択と最大24回のk-means反復でRGB二乗誤差を小さくします。
32768色を超える画像はRGB各5ビットのヒストグラムで近似します。数学的な大域最適解を保証するものではありません。
15色以内の画像では元の色を保持し、15色に足りないスロットは同じ色で埋めます。
入力ピクセルの透過色判定はしません。元画像のマゼンタも通常の画素として集計されます。

### -code

非圧縮4ビット・16色パレット付きBMPを、C／C++で使用できるコードに変換します。PNG、RLE圧縮、その他のビット深度・パレット色数はエラーになります。

`-o` は単一入力では出力ファイル名です。省略時は入力と同じ場所に、拡張子を `.cpp` に変更して保存します。ファイル名部分に `*` / `?` を指定すると一括処理になり、`-o` は出力先フォルダーになります。フォルダーがなければ作成します。一括処理で `-o` を省略すると各入力の隣に保存します。同名の出力は上書きしますが、入力自身への上書きは拒否します。個別ファイルにエラーがあっても残りを処理し、終了コードは1になります。

生成コードには `<stdint.h>`、`const int aaa_width`、`const int aaa_height`、`static const uint32_t aaa_palette[16]`、`static uint8_t aaa_data[]` を記述します。`aaa` は入力ファイルの拡張子を除いた名前です。英数字以外は `_` に置き換え、先頭が数字または `_` なら `image_` を付け、連続する `_` は1つにまとめ、末尾の `_` は除去します。変換後に同じ配列名になるファイルを同一ソースに取り込む場合は名前を調整してください。

パレットは元の順序を保持した `0xRRGGBB`、画素は1バイト2画素（上位4ビットが左）です。上から下、左から右の順に並べ、BMPの行末4バイト境界パディングは除去します。幅が奇数の場合は各行末の下位4ビットを0にします。画素配列のサイズは `((幅 + 1) / 2) × 高さ` バイトです。

LovyanGFXでは生成コードを取り込み、次のように画素配列を直接参照できます（`lcd` は初期化済みとします）。画素配列はSpriteを使う間、有効である必要があります。

```cpp
LGFX_Sprite sprite(&lcd);
sprite.setColorDepth(4);
sprite.setBuffer(aaa_data, aaa_width, aaa_height);
sprite.createPalette(aaa_palette, 16);
sprite.pushSprite(0, 0);
```

### -redu

`-p` に16色の `.gpl` / `.json` パレットを指定します。インデックス0を除く1～15から、RGB距離が最も近い色を各画素に割り当てます。
ディザリングは行いません。4ビット・16色・非圧縮BMPとして保存し、画像サイズと上下方向を保持します。
インデックス0はパレット内に保存されますが、画像ピクセルには一切使用しません。BMP自体に透過属性を設定するものではありません。

入力は非圧縮の1/4/8ビット・パレットBMP、24ビットRGB、32ビットRGB、およびPNGに対応します。BMP／PNGのアルファ値は使用しません。PNGはインデックス色・RGB・RGBA・グレースケールをRGB各8ビットに変換して処理します。PNG入力の出力は同じ名前の `.bmp`（4ビット・16色）となり、行の向きはtop-downになります。
16ビット、RLE圧縮、BI_BITFIELDS等は減色対象外です。
ファイル名部分に `*` / `?` のワイルドカードを使用できます。サブフォルダーの再帰検索はしません。

`-o` は保存先フォルダーです。存在しなければ作成し、省略時は実行時のカレントフォルダーになります。
元のファイル名で保存し、同名があれば `aaa_1.bmp`、`aaa_2.bmp` と番号を付けて既存ファイルを保護します。
入力の一覧を確定してから出力するため、同一フォルダーへ出力しても今回生成したファイルを再処理しません。
一括処理で個別ファイルにエラーがあっても残りは処理し、終了コードは1になります。


## 構成と依存ライブラリ

| パス | 用途 |
| --- | --- |
| `BmpPalette.slnx` | 2つのアプリを含むソリューション |
| `BmpPalette/BmpPalette.cpp` | コンソールアプリ |
| `Pal16Edit/Pal16Edit.cpp` | GUI・色選択・保存確認 |
| `Shared/ImageIO.h` | 両アプリで共有するPNGデコードと画像形式の振り分け |
| `Shared/PaletteIO.h` | 両アプリで共有するBMPパレット読み込み、GPL／JSON読み込み、各形式の保存 |
| `Pal16Edit/imgui/` | Dear ImGui本体とWin32／DirectX 11バックエンド |
| `tests/smoke.ps1` | コンソールアプリの回帰テスト |
| `AGENTS.md` | 出力先などの開発ルール |

Dear ImGuiはMITライセンスです。同梱の `Pal16Edit/imgui/LICENSE.txt` を参照してください。PNGのデコードにはWindows標準の [Windows Imaging Component](https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nn-wincodec-iwicbitmapdecoder) を使用します。展開後の画像バッファーは最大4GB未満です。DirectX 11、ファイルダイアログ、保存確認を使用し、日本語表示にはWindowsのメイリオを読み込みます。色選択はWindows標準ダイアログを使わず、ImGuiで実装しています。

設計の参考画像は [名称未設定.png](名称未設定.png) です。既存のサンプルBMP・パレットは `x64/Release/eyeex1.*`、書き出し例は `x64/Release/aaa.*` にあります。

## 検証

ビルド後、ルートから実行します。

```powershell
powershell -ExecutionPolicy Bypass -File tests/smoke.ps1
python tests/png.py
python tests/code.py
$test = Start-Process ./x64/Release/Pal16Edit.exe -ArgumentList '--self-test' -PassThru -Wait
$test.ExitCode
```

コンソール側はBMP処理・パレット出力・減色の回帰テストです。GUI側の自己テストはGPL／JSON／ACT／TXT／Cの保存・再読み込み、日本語パス、上書き保存、不正な色数の拒否を検証し、成功時は0を返します。PNGテスト（Python 3・標準ライブラリのみ）は色形式、RGB値と行順、パレット抽出、透明度の扱い、拡張子省略、日本語パス、不正なPNG、BMPへの減色を検証します。GUIの自己テストも `tests/fixtures/` の画像でPNG取り込み・拡張子省略・RGB画像の拒否を確認します。ルートから実行してください。GUIの表示・クリック操作は実際のウィンドウで確認してください。

テストデータはルートから実行した場合 `tests/generated/` に作成され、次回のテストで再生成できます。Debug／Win32を確認する場合はその構成をビルドし、`tests/smoke.ps1 -Configuration Debug -Platform Win32` と対応する実行ファイルを使用してください。
