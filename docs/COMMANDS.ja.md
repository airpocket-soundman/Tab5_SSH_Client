# コマンド一覧

[English](COMMANDS.md)

Tab5内蔵CLIは、ターミナル操作、SDカード、SSH/SCP、Wi-Fi制御、MicroPython、診断用の
Linux風コマンドを提供します。完全なPOSIXシェルではありません。パイプ、リダイレクト、
シェル展開、ジョブ制御、任意の外部コマンド実行はありません。

短い一覧は `help`、内蔵コマンドの例は `man <command>` で確認できます。デモスクリプトの
説明はSD上の `.txt` ファイルを `cat /mandel.txt` のように読んでください。

## 基本

| コマンド | 説明 | 例 |
| --- | --- | --- |
| `help` | 短いコマンド一覧を表示します。 | `help` |
| `man <command>` | 内蔵コマンドのヘルプを表示します。 | `man scp` |
| `clear` | Tab5 CLI画面バッファを消去します。 | `clear` |
| `status` | デバイス、Wi-Fi、SSH、キーボード、BLE、SD状態を表示します。 | `status` |
| `history` | ローカルコマンド履歴を表示します。 | `history` |
| `echo <text>` | テキストを表示します。 | `echo hello` |
| `date` | NTP同期後のローカル時刻を表示します。 | `date` |
| `uptime` | ファームウェア起動後の経過時間を表示します。 | `uptime` |
| `time sync` | NTP時刻同期を要求します。 | `time sync` |
| `uname [-a]` | ファームウェア/プラットフォーム情報を表示します。 | `uname -a` |
| `whoami` | 設定済みデバイス名を表示します。 | `whoami` |
| `hostname` | 設定済みデバイス名を表示します。 | `hostname` |

## Wi-Fi / ネットワーク

| コマンド | 説明 | 例 |
| --- | --- | --- |
| `wifi status` | Wi-Fi状態、IP、SSIDを表示します。 | `wifi status` |
| `wifi list` | 保存済みWi-Fiプロファイルを表示します。 | `wifi list` |
| `wifi off` | Wi-Fiをオフにし、再接続を停止します。 | `wifi off` |
| `wifi on` | Wi-Fiをオンに戻し、再接続します。 | `wifi on` |
| `ip addr` | Tab5のネットワークアドレスを表示します。 | `ip addr` |
| `ip a` | `ip addr` の別名です。 | `ip a` |
| `ifconfig` | ネットワーク状態を表示します。 | `ifconfig` |

## SSH

| コマンド | 説明 | 例 |
| --- | --- | --- |
| `ssh list` | 保存済みSSHプロファイルを表示します。 | `ssh list` |
| `ssh connect <index>` | 保存済みSSHプロファイルへ接続します。 | `ssh connect 0` |
| `ssh disconnect` | アクティブなSSHセッションを切断します。 | `ssh disconnect` |
| `ssh user@host[:port] [password]` | プロファイル保存なしで直接接続します。 | `ssh demo@192.0.2.10` |

直接SSHコマンドでパスワードを省略した場合、同じhost/userまたはhost/user/portの保存済み
プロファイルから認証情報の再利用を試みます。

## SCPファイル転送

`scp get` はSSHサーバからTab5 microSDへコピーします。`scp put` はTab5 microSDから
SSHサーバへコピーします。

| コマンド | 説明 | 例 |
| --- | --- | --- |
| `scp get <remote> <sd-local> [profile]` | プロファイル接続先からダウンロードします。 | `scp get /home/demo/test.py /test.py 0` |
| `scp put <sd-local> <remote> [profile]` | プロファイル接続先へアップロードします。 | `scp put /test.py /home/demo/test.py 0` |
| `scp get user@host:/remote <sd-local> [password]` | 直接エンドポイント指定でダウンロードします。 | `scp get demo@192.0.2.10:/home/demo/test.py /test.py` |
| `scp put <sd-local> user@host:/remote [password]` | 直接エンドポイント指定でアップロードします。 | `scp put /test.py demo@192.0.2.10:/home/demo/test.py` |

転送後の確認例:

```text
ls -l /
cat /test.py
python /test.py
```

## SDファイルシステム

これらのコマンドはTab5 microSDを操作します。相対パスは現在のSD作業ディレクトリを基準に
正規化されます。

| コマンド | 説明 | 例 |
| --- | --- | --- |
| `sd status` | SDのマウント状態と容量を表示します。 | `sd status` |
| `df` | SDのサイズ、使用量、空き容量、マウント先を表示します。 | `df` |
| `sd df` | `df` と同じです。 | `sd df` |
| `pwd` | 現在のSDディレクトリを表示します。 | `pwd` |
| `cd <path>` | 現在のSDディレクトリを変更します。 | `cd /scripts` |
| `ls [-lah] [path]` | ファイル一覧を表示します。 | `ls /` |
| `dir [-lah] [path]` | `ls` 相当の一覧表示です。 | `dir /` |
| `cat <path>` | ファイル内容を表示します。 | `cat /life.txt` |
| `sd write <path> <text>` | 1行を書き込み、ファイルを置き換えます。 | `sd write /hello.py print(123)` |
| `sd append <path> <text>` | 1行を追記します。 | `sd append /notes.txt more text` |
| `mkdir <path>` | ディレクトリを作成します。 | `mkdir /scripts` |
| `sd mkdir <path>` | ディレクトリを作成します。 | `sd mkdir /logs` |
| `rmdir <path>` | 空ディレクトリを削除します。 | `rmdir /scripts` |
| `sd rmdir <path>` | 空ディレクトリを削除します。 | `sd rmdir /logs` |
| `sd rm <path>` | ファイルを削除します。 | `sd rm /old.py` |
| `chmod <mode> <path>` | 仮想パーミッションを設定します。 | `chmod 644 /test.py` |
| `sd chmod <mode> <path>` | 仮想パーミッションを設定します。 | `sd chmod 755 /scripts` |

`ls` オプション:

- `-l`: 詳細表示。1ファイル1行。
- `-a`: `.tab5perms` などのドットファイルも表示。
- `-h`: `-l` と併用してサイズを読みやすく表示。

通常の `ls` は複数列で名前を表示します。`ls -l` はmode、owner、size、time、nameを表示します。

パーミッションはSD上の仮想メタデータです。FAT/exFAT自体はUnixパーミッションを保持しません。

## MicroPython

| コマンド | 説明 | 例 |
| --- | --- | --- |
| `python` | 組み込みMicroPython REPLを開始します。 | `python` |
| `python <sd.py> [args...]` | SD上のPythonスクリプトを実行します。 | `python /life.py` |
| `python -c <statement>` | 1文を実行します。 | `python -c print('hello')` |
| `python --reset` | 組み込みPython VMをリセットします。 | `python --reset` |

スクリプト引数は `argv` として渡されます。対応している場合は `sys.argv` も設定します。

スクリプトにはM5GFXスプライト描画用のグローバル `gfx` オブジェクトも渡されます。APIは
[PYTHON.md](PYTHON.md)、デモは [DEMOS.ja.md](DEMOS.ja.md) を参照してください。

Tab5上ではデモ説明ファイルを `cat` で読めます。

```text
cat /mandel.txt
cat /plasma.txt
cat /life.txt
```

## Bluetoothキーボード

| コマンド | 説明 | 例 |
| --- | --- | --- |
| `ble status` | BLEキーボード状態を表示します。 | `ble status` |
| `ble enable` | BLEキーボード対応を有効化します。 | `ble enable` |
| `ble disable` | BLEキーボード対応を無効化します。 | `ble disable` |
| `ble scan` | BLEキーボード候補をスキャンします。 | `ble scan` |
| `ble pair <index>` | スキャン結果のキーボードとペアリングします。 | `ble pair 0` |
| `ble forget` | 保存済みBLEキーボード情報を忘れます。 | `ble forget` |

## メモ

- 対応している場所では `Tab` でSDパス補完できます。
- 内蔵CLIの例は `man <command>` を使ってください。
- デモ固有の説明は `.txt` ファイルを `cat` して確認してください。
- Serial APIはCLIと重なる部分がありますが、主に診断用途です。
