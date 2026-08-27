# intro-heuristics-lagrangian-bound-beam

価格安定化ラグランジュ緩和と複数境界ビームサーチを、問題固有コードから分離したサンプルです。
多次元0/1ナップサックとIntroduction to Heuristics Contestの2問で、同じライブラリを利用します。

## ファイル構成

```text
.
├── combiner.sh
├── examples/
│   └── knapsack_small.txt
├── src/
│   ├── lib/
│   │   └── lagbeam.hpp
│   ├── 01_multidimensional_knapsack.cpp
│   └── 02_intro_heuristics.cpp
└── tests/
    └── test_knapsack.py
```

- `src/lib/lagbeam.hpp`: 問題に依存しないライブラリ
- `src/01_multidimensional_knapsack.cpp`: 多次元0/1ナップサックのアダプタ
- `src/02_intro_heuristics.cpp`: Introduction to Heuristics Contestのアダプタ
- `combiner.sh`: ローカルincludeを再帰展開し、提出用の1ファイルへ結合するスクリプト

2つの問題は、同じ`lagbeam.hpp`を一切変更せずに利用します。

## 必要環境

- Bash
- GNU C++20以降
- Python 3（テストを実行する場合）

以下の例ではGNU++23を使います。

## 多次元0/1ナップサック

入力形式は次の通りです。

```text
N M
capacity[0] ... capacity[M-1]
profit[0] consumption[0][0] ... consumption[0][M-1]
...
profit[N-1] consumption[N-1][0] ... consumption[N-1][M-1]
```

コンパイルと実行:

```bash
g++ -std=gnu++23 -O2 -Isrc src/01_multidimensional_knapsack.cpp -o a.out
./a.out < examples/knapsack_small.txt
```

サンプルでは、単純な貪欲解が14点、`lagbeam`が20点です。

## Introduction to Heuristics Contest

```bash
g++ -std=gnu++23 -O2 -Isrc src/02_intro_heuristics.cpp -o a.out
./a.out < input.txt > output.txt
```

固定設定は、双対価格調整60回、双対境界3本、ビーム幅14,500です。時間打ち切りと後処理はありません。

## 1ファイルへ結合する

AtCoderへ提出する場合など、ローカルヘッダを1ファイルへまとめたいときは次のように実行します。

```bash
./combiner.sh src/01_multidimensional_knapsack.cpp
./combiner.sh src/02_intro_heuristics.cpp
```

次のファイルが生成されます。

```text
combined/combined_01_multidimensional_knapsack.cpp
combined/combined_02_intro_heuristics.cpp
```

結合後のファイルは、追加のincludeパスなしでコンパイルできます。

```bash
g++ -std=gnu++23 -O2 combined/combined_01_multidimensional_knapsack.cpp -o a.out
```

## テスト

```bash
python3 tests/test_knapsack.py
```

小さなランダム100問を全探索と比較し、分割版と結合版の出力も比較します。また、Intro利用側の分割版と結合版がどちらもコンパイルできることを確認します。

## アダプタの役割

利用者は、`Problem`で双対価格を使った独立部分問題を、`SearchModel`で段階的な解構築を定義します。
通常の呼び出しは両問題とも同じです。

```cpp
auto result = lagbeam::solve(problem, config);
```

ライブラリは、価格更新、価格安定化、複数の価格選択、上界付きビーム、暫定解による枝刈り、状態重複除去を担当します。
問題側は、スコア、実行可能性、状態遷移、緩和部分問題、残り上界を担当します。
