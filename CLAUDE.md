# kotemado の作業方針

## リリース運用

**手順は共通の `~/.claude/CLAUDE.md`「修正が終わったら、リリースまで通す」に従う。**
ここにはこのリポジトリ固有の事情だけを書く。

- リモート: `https://github.com/iosxi/kotemado.git`（`iosxi/kotemado`、公開）
- ブランチ: **`master`**
- 最新バージョンの確認: `git tag --sort=-v:refname | head -1`
- リリースの添付物: **`kotemado.exe`**。改名せず、そのまま `gh release create` に渡す
  （単体で置いて使うものなので、版ごとに名前が変わると更新のたびに旧版が残る。
  sleep-guard / ramday と同じ方針）。
- バージョン: タグの `vN` とは別に、`src/kotemado.rc` の VERSIONINFO、
  `src/kotemado.manifest` の `assemblyIdentity`、`src/kotemado.h` の `APP_VERSION` がある。
  機能が変わったら 3 か所とも上げる。

### exe を変更したとき

ソースを直したら **`build.cmd` で exe を作り直してからコミットする**。exe はリポジトリに追跡させている。
アイコンは `python tools/make-icon.py` で `src/kotemado.ico` を作り直す。

## 動作確認について

- **自動検証は `tools/test.ps1`。** 検証用ウィンドウ（`tools/target.c`）を出し、終了時に
  自分の矩形・状態を書かせて期待値と突き合わせる。画面は撮らない。
  別の ini（`-ini`）で動かすので、利用者が使っている kotemado とは干渉しない
  （多重起動の判定は ini ごと）。
- **画面の見た目は `tools/capture.ps1` で、その窓だけを撮る。** 全画面は撮らない。
  ボタンは `WM_COMMAND` を親へ送って押す（キー操作・マウス操作は送らない）。
  - `PrintWindow` はライト表示のときに設定画面の下半分を描き落とすことがある
    （2026-10-03 実測。実際の画面では描けていた）。見た目が欠けていたら、
    その窓の矩形だけを `CopyFromScreen` で撮って確かめる。
- **照準は 2 クリック方式**（v2 から。v1 はドラッグ）。選択中だけ `WH_MOUSE_LL` を掛けて
  動きを追い、選ぶクリックの押下・離上を握りつぶす。
  **実際のマウス操作では自動検証していない**（利用者のマウスを動かすことになるため）。
  代わりに、照準へ `WM_LBUTTONUP` を送ると選択中になり、編集ダイアログへ
  `WM_APP+23`（wParam 1 = 左クリック / 0 = 右クリック、lParam = 画面座標）を送ると
  フックが送るのと同じ知らせになる。`WM_APP_PICKED`（`WM_APP+21`）は選んだ後の取り込みだけ。
  検証用の窓は `-topmost` で出す（他の窓の下だと、その位置の別のウィンドウを拾う）。
- **利用者が普段使っている kotemado は `kotemado.exe`（既定の ini、引数なし）で動いている。**
  直すときは止めてよい（`C:\projects\windows\CLAUDE.md` の全共通の方針）。
  `kotemado.exe -exit` で止め、作り直したら引数なしで起動し直す。
  `-exit` は同じ設定ファイルで動いているものにしか効かない。既定の設定ファイルは exe の隣なので、
  exe を別の場所から呼ぶときは `-ini C:\projects\windows\kotemado\kotemado.ini` を付ける。
  利用者の `kotemado.ini` はリポジトリのフォルダにできるが `.gitignore` 済み。
- 検証で起動した kotemado は **`-ini <検証用> -exit` で止める。** `Stop-Process` で殺すと
  トレイにアイコンの抜け殻が残る。
- 配色は利用者の設定を変えずに、ini の `[general] theme=light|dark` で切り替えて確かめる。
- PowerShell 5.1 は BOM なし UTF-8 を読み違えるので、日本語を含む `.ps1` は BOM 付きで保存する。
- テキストの改行は **LF** に揃えてある（v2 で統一）。Python で書き戻すときは
  `open(p, 'w', encoding='utf-8', newline='\n')` にする（既定だと Windows では CRLF になる）。

### この PC の画面（2026-10-03）

ディスプレイは 1 台（LG HDR 4K、物理 3840×2160、拡大率 125%、作業領域の高さ 2100）。
**複数ディスプレイ間の移動（番号指定で別の画面へ、拡大率の違う画面へ）は実機で確かめられていない。**
つながっていないディスプレイ番号を指定したときに動かさないことだけは確かめてある。

### 実測で分かったこと

- Windows 11 の透明な縁は、125% で左右下 8px・上 0px。最大化中は測れないので
  `SM_CXSIZEFRAME + SM_CXPADDEDBORDER − 1px(拡大率換算)` で見積もっていて、
  通常表示で測った値と一致した（test.ps1 の J）。
- `WINDOWPLACEMENT.rcNormalPosition` はワークスペース座標（メイン画面の作業領域の左上が原点。
  ツール ウィンドウは除く）。この PC はタスクバーが下なので差が出ず、上や左にある環境では未確認。
- `SetWindowPlacement` に `SW_SHOWNA` を渡すと、最大化のまま「元に戻したときの位置」だけを差し替えられる。
- 「定期的に」は、位置が合っていれば `SetWindowPos` を呼ばない。3 秒・100ms 間隔（約 30 回の見回り）で
  検証用の窓が受けた `WM_WINDOWPOSCHANGING` は 4 回（表示・初回配置・アプリ自身の移動・戻した 1 回）。
- v1 の「表示されるたびに」（`when=show`）は `EVENT_OBJECT_SHOW` でしか働かず、最小化からの復元や
  手で動かしたときには効かないので「効いていない」と言われた。v2 で「定期的に」に置き換え、
  ini の `when=show` は `periodic` として読む。
