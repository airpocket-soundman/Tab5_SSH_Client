# デモスクリプト

[English](DEMOS.md)

`demos/` にはTab5のmicroSDへコピーして実行するMicroPythonスクリプトが入っています。
各デモにはTab5上で `cat` できる `.txt` 説明ファイルもあります。

```text
cat /mandel.txt
cat /plasma.txt
cat /life.txt
```

描画デモはファームウェアの `gfx` オブジェクトを使い、M5GFXスプライト経由で描画します。
実行中の描画スクリプトは `Ctrl-C` または `q` で中断できます。

## SDへのコピー

Tab5のSSHプロファイルを使う例:

```text
scp get /home/demo/mandel.py /mandel.py 0
scp get /home/demo/mandel.txt /mandel.txt 0
```

直接SCPエンドポイントを指定する例:

```text
scp get demo@192.0.2.10:/home/demo/plasma.py /plasma.py
```

## Mandelbrot

```text
python /mandel.py [wait_ms] [frames] [final_cell_px] [hold_ms]
```

段階的に精細化するマンデルブロー描画です。粗いブロックから始め、同じ画面を
小さいブロックで上書きして `final_cell_px` まで細かくします。

- `wait_ms`: 各精細化パス後の待ち時間。デフォルト `0`。
- `frames`: ズームフレーム数。デフォルト `1`。
- `final_cell_px`: 最終ブロックサイズ。デフォルト `10`、最小 `1`。
- `hold_ms`: 最終画像の保持時間。デフォルト `60000`。`-1` で中断まで保持。

例:

```text
python /mandel.py
python /mandel.py 0 1 8 -1
python /mandel.py 0 1 1 -1
```

`final_cell_px=1` は可能ですが、MicroPythonで全ピクセルを計算するためかなり重いです。

## Plasma

```text
python /plasma.py [wait_ms] [frames] [cell_px]
```

古典的なsine plasmaです。複数のsin波と距離ベースの波を合成して、うねる色場を作ります。
1フレーム全体をスプライトに描いてから一度だけ表示します。

- `wait_ms`: 各フレーム後の待ち時間。デフォルト `0`。
- `frames`: アニメーションフレーム数。デフォルト `160`。
- `cell_px`: ブロックサイズ。デフォルト `12`、最小 `6`。

例:

```text
python /plasma.py
python /plasma.py 0 160 24
python /plasma.py 0 160 16
```

`cell_px` を小さくすると滑らかになりますが、描画コマンド数が増えて重くなります。

## Hat

```text
python /hat.py [wait_ms] [frames] [hold_ms]
```

レトロPCでよく見られたテンガロンハット/ソンブレロ風ワイヤーフレームです。
ワイヤー行ごとに表示するため、描画されていく過程が見えます。

- `wait_ms`: 各ワイヤー行後の待ち時間。デフォルト `0`。
- `frames`: 描画フレーム数。デフォルト `1`。
- `hold_ms`: 最終画像の保持時間。デフォルト `60000`。`-1` で中断まで保持。

例:

```text
python /hat.py
python /hat.py 5 1 -1
```

## Life

```text
python /life.py [wait_ms] [frames] [cell_px]
```

25x25固定盤面のConway's Game of Lifeです。`gfx.mono()` で盤面を1bitビットマップとして
送り、ファームウェア側で効率よく描画します。

- `wait_ms`: 各フレーム後の待ち時間。デフォルト `0`。
- `frames`: 最大世代数。デフォルト `200`。
- `cell_px`: セルサイズ。デフォルト `12`、最小 `6`。

例:

```text
python /life.py
python /life.py 0 500 10
```

## Starfield

```text
python /starfield.py [wait_ms] [frames] [speed]
```

中心から星が流れるグラフィカルなstarfieldです。

- `wait_ms`: 各フレーム後の待ち時間。デフォルト `0`。
- `frames`: フレーム数。デフォルト `30`。
- `speed`: 星の移動速度。デフォルト `5`。

例:

```text
python /starfield.py 0 120 7
```

## Maze

```text
python /maze.py [wait_ms] [cols] [rows]
```

迷路生成と解探索のアニメーションです。

- `wait_ms`: アニメーションステップ後の待ち時間。デフォルト `0`。
- `cols`: 迷路の幅。デフォルト `31`、奇数へ丸めます。
- `rows`: 迷路の高さ。デフォルト `19`、奇数へ丸めます。

例:

```text
python /maze.py 10 25 17
```

## termgfx.py

`termgfx.py` はgfxスクリプト作成用の小さな参照/ヘルパーファイルです。現在のSD importには
制限があるため、同梱デモは基本的に単独で動くようにしています。
