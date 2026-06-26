# Tab5 SSH Client

[English](README.md) | 日本語

M5Stack Tab5 をSSH端末として使うためのPlatformIOプロジェクトです。Tab5
Keyboardを接続したTab5を対象に、Wi-Fiプロファイル、SSHプロファイル、キー入力、
スクロール可能なターミナル表示を提供します。

## 機能

- M5Stack Tab5 / ESP32-P4 向けPlatformIOプロジェクト。
- LittleFS上のJSONからWi-Fi/SSHプロファイルを読み込み。
- SSH接続先を複数保存可能。
- 直接入力形式: `ssh user@host[:port] [password]`。
- `LibSSH-ESP32` を使ったインタラクティブSSHシェル。
- 基本的なANSIエスケープ処理を持つスクロール可能なターミナルバッファ。
- `M5Unit-KEYBOARD` 経由のTab5 Keyboard入力。
- Tab5側でのUS/JPキーボードレイアウト変換。
- 動作確認用のUSBキーボード入力パス。
- 診断用のシリアルモニターAPI。

## 必要なもの

- M5Stack Tab5
- Tab5 Keyboard
- 書き込み・シリアル確認用USBケーブル
- Tab5から接続できるWi-Fiネットワーク

アプリケーション構成とハードウェアメモは
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) を参照してください。

## ビルド

PlatformIOをインストールし、このフォルダを開いて `tab5` 環境をビルドします。

```powershell
pio run
```

日本語Windows環境でパッケージ出力が `UnicodeEncodeError` になる場合は、UTF-8を
有効にして実行してください。

```powershell
$env:PYTHONUTF8='1'; pio run
```

ファームウェアを書き込みます。

```powershell
pio run -t upload
```

LittleFSのプロファイルデータを書き込みます。

```powershell
pio run -t uploadfs
```

## 設定

`uploadfs` の前に `data/profiles.json` を編集します。

- `wifi`: 上から順に接続を試すWi-Fiプロファイル。
- `ssh`: Tab5のSSHプロファイル一覧に出る接続先。
- `keyboard.layout`: `us` または `jp`。
- `system.region` / `system.utcOffsetMinutes`: ローカル時刻表示用。

SSHプロファイル例:

```json
{
  "name": "linux-box",
  "host": "192.0.2.10",
  "port": 22,
  "user": "demo",
  "password": "change-me",
  "terminal": "xterm-256color"
}
```

実際のWi-FiパスワードやSSH認証情報はコミットしないでください。

## 使い方

1. `data/profiles.json` にWi-FiプロファイルとSSHプロファイルを1つ以上設定します。
2. `pio run -t upload` でファームウェアを書き込みます。
3. `pio run -t uploadfs` でプロファイルファイルを書き込みます。
4. Tab5を再起動します。
5. ステータス行にWi-Fi接続状態とIPアドレスが出るまで待ちます。
6. `SSH` 画面を開き、プロファイルを選んで `CONNECT` を押します。

ターミナルCLIから接続することもできます。

```text
ssh list
ssh connect 0
```

保存せずに一度だけ接続する場合:

```text
ssh demo@192.0.2.10:22
```

直接接続コマンドにパスワードを書かない場合、同じhost/userまたはhost/user/portの
保存済みプロファイルから認証情報を再利用します。

## 本体操作

上部メニューバー内のボタンで主要画面を切り替えます。

- `TERM`: ターミナルと内蔵CLI。
- `WIFI`: Wi-Fiプロファイルの一覧、スキャン、追加、編集、接続。
- `SSH`: SSHプロファイルの一覧、追加、編集、接続。
- `FONT`: ターミナルのフォントと行間。
- `CONF`: デバイス名、地域、時差、NTP、キーマップ設定。
- `CONN` / `DISC`: ターミナル画面から接続または切断。

キーボードショートカット:

- `Esc`: ターミナル/コンテンツ領域と上部メニューバーのフォーカスを切り替え。
- `Tab`: 上部メニューバー内、一覧画面、編集/設定画面内のフォーカスを移動。
- `Ctrl+Up`: ターミナルバッファを上へスクロール。
- `Ctrl+Down`: ターミナルバッファを下へスクロール。

SSH接続中のターミナル画面では、`Esc` はリモートのシェル/アプリケーションへ送信
され、上部メニューバーの有効化には使われません。

よく使う内蔵CLIコマンド:

```text
help
status
wifi status
wifi list
ip addr
ssh list
ssh connect <index>
ssh disconnect
time sync
clear
```

## Tailscaleホストへ接続する場合

このファームウェアはESP32-P4上でTailscaleノードを動かしません。tailnet上のホスト
へ接続したい場合は、Tab5が接続するネットワーク側にTailscaleゲートウェイ、サブ
ネットルーター、またはSSHリレーを用意してください。そのうえで、Tab5のSSHプロ
ファイルには到達可能なゲートウェイのアドレスとポートを設定します。

## シリアル診断

ファームウェアは `115200` baud で簡単なシリアルAPIを提供します。

```text
help
status
wifi status
ssh list
ssh connect [index]
ssh disconnect
term dump
```

`tools/serial_bridge.ps1` を使うと、起動確認中のシリアルログ保存やコマンド送信が
できます。

## M5Burner

M5Burnerへアップロードするパッケージは次のコマンドで作成できます。

```powershell
.\tools\package_m5burner.ps1 -Version 0.1.0
```

公開手順と入力するメタデータは [docs/M5BURNER.md](docs/M5BURNER.md) を参照して
ください。

## リポジトリ構成

```text
data/       LittleFSへ書き込むプロファイルデータ
docs/       アーキテクチャメモ
include/    ヘッダー
src/        ファームウェア本体
tools/      補助スクリプト
```

## ステータス

このプロジェクトはTab5ハードウェアの立ち上げとモバイルSSH用途のための実験的な
ファームウェアです。Wi-Fi挙動、フォントサイズ、ターミナルエスケープ処理、キー
ボードマッピングは、利用環境に合わせて調整してください。
