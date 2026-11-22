# CPS Lab 版 SumatraPDF — Claude Code 向けの入口

このリポジトリは、SumatraPDF を CPS Lab 向けに改造したフォーク。いま、本家 3.6.2 への載せ替え(第1段階、x64)を進めている。

**作業を始める前に、必ず読む:** `D:\Development\PDFViewer\sumatrapdf-docs\port-work\README.md`
(固定値、決まりごと、タスクの順番、報告の書式。各タスクの手順は同じフォルダの `T*.md`、衝突の解決は `conflicts.md`)

## 作業場所
- メインのチェックアウト(`D:\Development\PDFViewer\sumatrapdf`、ブランチ `master`)= 今の出荷版のソース。**ここのソースは変更しない。**
  ここでやるのは、読むだけの作業(T2: DDE 仕様書)と、旧 exe の保管(T3a)だけ。
- 載せ替えの作業は、Claude Code のワークツリー(`.claude/worktrees/<名前>/`、ブランチ `worktree-<名前>`)で行う。名前は短く(例: `port36`)。
  ワークツリーは `master` から始まるので、最初に port-work README の4節「最初のコミット」で、中身を本家 3.6.2(`ee062cec54`)にする。

## 必ず守ること(要約。詳細は port-work README の2節)
- **コミットしない**(例外は、上の「最初のコミット」だけ。TKの了承を得てから)。区切りごとに、変更ファイルの一覧とコミットメッセージ案を示して止まる。コミットは TK が IDE で行う。
- 作業を始める前に `git rev-parse --abbrev-ref HEAD` と `pwd` で、ブランチと場所を確認する。
- ファイルの削除・改名・移動は、手順書に書いてある場合だけ。
- 実行していないことを「確認した」と書かない。事実・推測・未確認を区別する。
- 返答は日本語。
- 独自の変更には `// CPS Lab` の印を付ける。生成物(`src/Settings.h`、`src/Commands.h`、`src/Flags.cpp` の生成部分、`vs2022/`)は手で編集しない。
- シェルは Git Bash。`2>nul` は使わない(`2>/dev/null`)。
- 終了時にワークツリーを「残すか消すか」聞かれたら、TK に確認する(作業中は残す)。

## 場所
- 手順書・テスト・旧ソースの写し: `D:\Development\PDFViewer\sumatrapdf-docs\`(`port-work\`、`tests\`、`old-src\`)
- ビルド(ワークツリー、T0 以降): `bun cmd/cps-build.ts -dbg` / `-rel` → `out/dbg64`、`out/rel64`
- 本家 3.6.2 には、ルートに本家の `Claude.md` がある。そちらの決まり(STL を使わない、clang-format など)にも従う。食い違ったら、このファイルと port-work を優先する。
