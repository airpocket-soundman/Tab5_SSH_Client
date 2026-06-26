# Tab5 SSH Client

[English](README.md) | 日本語

M5Stack Tab5 をポータブルSSH端末として使うためのファームウェアです。開発環境は
PlatformIO、対象は Tab5、Tab5 Keyboard、microSD、Wi-Fi です。

## 主な機能

- M5Stack Tab5 / ESP32-P4 向け PlatformIO プロジェクト。
- Wi-Fi / SSH プロファイルをTab5上で管理し、フラッシュへ永続化。
- 直接SSH接続コマンド: `ssh user@host[:port] [password]`。
- `LibSSH-ESP32` による対話型SSHシェル。
- `vim`、`nano`、`sl` などを想定したANSI/VT系ターミナル処理。
- スクロールバック、コマンド編集、履歴呼び出し。
- Tab5 Keyboard、USBキーボード、BLEキーボード設定経路。
- Tab5側でのUS/JPキーレイアウト変換。
- SSHホストとTab5 microSD間のSCP風ファイル転送。
- SD操作、Wi-Fi、SSH/SCP、診断、Python実行用のLinux風ローカルCLI。
- MicroPython REPL、`python -c`、SD上の `.py` 実行。
- M5GFXスプライトを使うMicroPython向け `gfx` 描画API。
- progressive Mandelbrot、sine plasma、wireframe hat、Life、starfield、
  maze などのSDカードデモ。

## ドキュメント

- [Command list](docs/COMMANDS.md)
- [コマンド一覧](docs/COMMANDS.ja.md)
- [Python and graphics](docs/PYTHON.md)
- [Demo scripts](docs/DEMOS.md)
- [デモスクリプト](docs/DEMOS.ja.md)

## 必要なハードウェア

- M5Stack Tab5
- Tab5 Keyboard
- microSDカード
- 書き込みとシリアル診断用USBケーブル
- Tab5から接続できるWi-Fiネットワーク

## ビルド

PlatformIOをインストールし、このフォルダを開いて `tab5` 環境をビルドします。

```powershell
pio run -e tab5
```

Windowsコンソールで文字コード由来のエラーが出る場合はUTF-8を有効にします。

```powershell
$env:PYTHONUTF8='1'; pio run -e tab5
```

ファームウェア書き込み:

```powershell
pio run -e tab5 -t upload
```

## 設定

プロファイルはTab5 UI上で編集でき、フラッシュに保存されます。

- `WIFI`: Wi-Fiプロファイル、スキャン、追加、編集、接続、Wi-Fi on/off。
- `SSH`: SSHプロファイル、追加、編集、接続。
- `FONT`: ターミナルフォントと行間。
- `CONF`: デバイス名、地域、UTCオフセット、NTP、キーマップなど。

実際のWi-FiパスワードやSSH認証情報はコミットしないでください。

## 使い方

1. ファームウェアを書き込みます。
2. Tab5を再起動します。
3. `WIFI` 画面でWi-Fiを設定します。
4. `SSH` 画面でSSH接続先を設定します。
5. プロファイルを選択して `CONNECT` を押します。

ローカルCLIからも接続できます。

```text
ssh list
ssh connect 0
ssh demo@192.0.2.10:22
```

直接SSHコマンドでパスワードを省略した場合、同じhost/userまたはhost/user/portの保存済み
プロファイルから認証情報の再利用を試みます。

## 本体操作

- `Esc`: ターミナル/コンテンツ領域と上部メニューバーのフォーカス切り替え。
- `Tab`: メニュー、リスト、編集フィールド内のフォーカス移動。
- 矢印キー: メニュー/設定ではフォーカス移動、ターミナルではカーソルキー送信。
- `Ctrl+Up` / `Ctrl+Down`: ターミナルバッファのスクロール。

SSH接続中のターミナル画面では、`Esc` はリモートアプリへ送信されます。これにより
`vim` のinsert modeから抜けられます。

## 内蔵CLI

内蔵CLIはLinux風ですが、完全なPOSIXシェルではありません。パイプ、リダイレクト、
シェル展開、バックグラウンドジョブはありません。

```text
help
man <command>
status
wifi status
wifi off
wifi on
ssh list
ssh connect 0
ssh user@host[:port] [password]
ls /
ls -lah /
cat /life.txt
df
mkdir /scripts
rmdir /scripts
scp get /home/airpocket/test.py /test.py 0
scp put /test.py /home/airpocket/test.py 0
python /life.py
python /mandel.py 0 1 8 -1
python /plasma.py 0 160 16
```

通常の `ls` は複数列表示、`ls -l` は1ファイル1行の詳細表示です。

## MicroPython と描画デモ

ローカルCLIからREPL起動、1行実行、SD上の `.py` 実行ができます。

```text
python
python -c print('hello')
python /life.py
```

スクリプトには `argv` とグローバルな `gfx` オブジェクトが渡されます。描画命令は
ファームウェア側のスプライトに描き、`gfx.present()` で画面へ転送します。
描画スクリプトは `gfx.present()` のタイミングで `Ctrl-C` または `q` により中断できます。

APIは [docs/PYTHON.md](docs/PYTHON.md)、同梱デモは
[docs/DEMOS.ja.md](docs/DEMOS.ja.md) を参照してください。

## Tailscaleホストへの接続

このファームウェアはESP32-P4上でTailscaleノードを動かしません。tailnet上のホストへ
接続したい場合は、Tab5が接続するネットワーク側にTailscale gateway、subnet router、
テザリング中のTailscale端末、またはSSH relayを用意し、到達可能なアドレスとポートを
SSHプロファイルに設定します。

## シリアル診断

`115200` baudで診断用Serial APIを提供します。

```text
help
status
sd ls /
wifi status
ssh list
ssh connect [index]
ssh disconnect
term dump
python /life.py 0 5
```

ホスト側ツールからシリアルポートを開くときは、DTR/RTS変化でボードがリセットされることが
あるため注意してください。

## リポジトリ構成

```text
data/       LittleFSプロファイルデータ
demos/      SDカード用Pythonデモと説明テキスト
docs/       ドキュメント
include/    ヘッダ
lib/        組み込みMicroPythonとローカルライブラリ
src/        ファームウェア本体
tools/      補助スクリプト
```

## 状態

Tab5ハードウェア立ち上げとモバイルSSH用途の実験的ファームウェアです。Wi-Fi挙動、
ターミナルエスケープ処理、性能、フォント、キーボードマッピングは利用環境に合わせて
調整してください。
