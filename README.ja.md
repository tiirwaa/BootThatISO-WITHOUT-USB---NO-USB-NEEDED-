# BootThatISO!

**[🇺🇸 English](README.md) | [🇪🇸 Español](README.es.md) | [🇧🇷 Português](README.pt.md) | [🇩🇪 Deutsch](README.de.md) | [🇫🇷 Français](README.fr.md) | [🇮🇹 Italiano](README.it.md) | [🇷🇺 Русский](README.ru.md) | [🇨🇳 中文](README.zh.md)**

<div style="display: flex; justify-content: center; align-items: center;">
<img src="res/logo.png" alt="Logo" style="margin-right: 20px;">
<img src="res/ag.png" alt="Company Logo">
</div>

## Videos

- [Hirens Boot — No USB Needed — Boot ISO Directly (No USB Required)](https://www.youtube.com/watch?v=RaRJTmek0W8)
- [Install Windows 10/11 Without USB | BootThatISO (No Flash Drive Needed!)](https://www.youtube.com/watch?v=Fo6poEWZNew)

BootThatISO! は、**USBドライブを必要とせずにISOファイルからオペレーティングシステムを起動**できる革新的なWindowsツールです。旅行中、借用機器、または緊急時など、USBデバイスが手元にない状況に最適です。内蔵ディスクへのEFIおよびデータパーティションの作成、ISOの直接読み取りとファイル抽出、BCD設定を自動化し、直感的なグラフィカルインターフェイスと無人実行のサポートを提供します。

このユーティリティは特に次の用途に便利です：
- **迅速なインストール**：USBを準備せずに、Windows、Linux のインストール、または回復ツールのためにISOから直接起動。
- **テスト環境**：外部ハードウェアを変更せずにOS ISOやユーティリティをテスト。
- **システム回復**：外部メディアに依存せずに、HBCD_PEやライブ環境などの修復ツールにアクセス。
- **自動化**：大量展開や自動設定のためのスクリプトへの統合。

**Andrey Rodríguez Araya** により開発されました。

ウェブサイト：[English](https://agsoft.co.cr/en/software-and-services/) | [Español](https://agsoft.co.cr/servicios/)

![Screenshot](screenshot.png?v=1)

![Boot screen](boot_screen.png?v=1)

## 主な機能
- システムディスク上に `ISOBOOT`（データ）および `ISOEFI`（EFI）パーティションを作成または再フォーマットし、FAT32、exFAT、またはNTFSフォーマットオプションを提供。
- 2つの起動モードをサポート：ディスクへの完全なISOロードまたはRAMDiskモード（メモリ内のboot.wim）。
- Windows ISOを検出し、BCD設定を自動調整；非Windows ISOはEFIパーティションから直接起動。
- オプションの整合性チェック（`chkdsk`）を実行し、詳細なログを生成し、キャンセルまたはスペース回復を許可。
- コマンドライン引数によるスクリプト統合のための無人モードを提供。
- **ISOハッシュキャッシュ（ISOBOOTHASH）**：ISOのMD5、選択された起動モード、およびフォーマットをターゲット上の `ISOBOOTHASH` ファイルに保存された値と比較。一致する場合、フォーマットとコンテンツのコピーをスキップして繰り返し実行を高速化。

## テスト済みISO

### RAMモード（メモリから起動）
- ✅ HBCD_PE_x64.iso（完全機能 - すべてのプログラムをRAMから読み込み）
- ✅ Win11_25H2_Spanish_x64.iso（完全機能 - 起動とインストール）
- ✅ Windows10_22H2_X64.iso（完全機能 - 起動とインストール）

### EXTRACTモード（完全インストール）
- ✅ HBCD_PE_x64.iso（ISOBOOT_RAMにフォールバック）
- ✅ Win11_25H2_Spanish_x64.iso（ISOBOOT_RAMにフォールバック）
- ✅ Windows10_22H2_X64.iso（ISOBOOT_RAMにフォールバック）

## 要件
- 管理者権限を持つWindows 10または11 64ビット。
- パーティションを作成およびフォーマットするために、ドライブ `C:` に少なくとも12 GBの空き容量（ツールは12 GBを縮小しようとします）。
- PowerShell、DiskPart、bcdedit、および利用可能なWindowsコマンドラインツール。
- コンパイル用：CMake付きVisual Studio 2022。外部パッケージマネージャーは不要；7‑Zip SDKは `third-party/` に含まれています。

## コンパイル
```powershell
# 設定とビルド（VS 2022、x64）
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

最終的な実行可能ファイルは `build/Release/BootThatISO!.exe` にあります。同等の手順を含む `compilar.bat` も含まれています。

### compilar.batでの迅速なコンパイル（推奨）
```powershell
# リポジトリのルートで
./compilar.bat
```

## 使用方法
### グラフィカルインターフェース
1. **管理者として** `BootThatISO!.exe` を実行します（マニフェストがすでに要求しています）。
2. ISOファイルを選択し、`ISOBOOT` のファイルシステム形式を選択します。
3. 起動モードを定義：
   - `完全インストール`：ISO全体の内容をディスクにコピー。
   - `メモリから起動`：RAMから起動するために `boot.wim` と依存関係をコピー。
4. `chkdsk` を実行するかどうかを決定（チェックを外すとプロセスを高速化するために検証をスキップ）。
5. **起動可能なパーティションを作成**をクリックし、メインバー、詳細バー、ログパネルを介して進行状況を監視します。
6. 完了すると、再起動の確認が表示されます。`ISOBOOT`/`ISOEFI` パーティションを削除して `C:` を拡張する必要がある場合は、**スペースを回復**ボタンを使用します。
7. **サービス**ボタンはサポートページ `https://agsoft.co.cr/servicios/` を開きます。

### 無人モード
昇格された権限と次の引数でバイナリを実行します：

```
BootThatISO!.exe ^
  -unattended ^
  -iso="C:\パス\イメージ.iso" ^
  -mode=RAM|EXTRACT ^
  -format=NTFS|FAT32|EXFAT ^
  -chkdsk=TRUE|FALSE ^
  -autoreboot=y|n ^
  -lang=en_us|es_cr|ja_jp
```

- `-mode=RAM` は*メモリから起動*モードを有効にし、`boot.wim`/`boot.sdi` をコピーします。
- `-mode=EXTRACT` は*完全インストール*に対応します。
- `-chkdsk=TRUE` はディスク検証を強制します（デフォルトでは省略）。
- `-lang` は `lang/` のファイルに一致する言語コードを設定します。
- `-autoreboot` は将来の自動化のために利用可能；現在は設定のみを記録します。

プロセスはイベントをログに記録し、メインウィンドウを表示せずに終了します。

## ログと診断
すべての操作は `logs/`（実行可能ファイルと一緒に作成）にファイルを生成します。最も関連性の高いものは：
- `general_log.log`：一般的なイベントのタイムラインとUIメッセージ。
- `diskpart_log.log`、`reformat_log.log`、`recover_script_log.txt`：パーティション分割と再フォーマットのステップ。
- `iso_extract_log.log`、`iso_content.log`：抽出されたISOコンテンツの詳細。
- `bcd_config_log.log`：BCD設定コマンドと結果。
- `copy_error_log.log`、`iso_file_copy_log.log`：ファイルのコピーとエラー。

障害を診断したりレポートを共有したりするときは、これらのログを確認してください。

## セキュリティと回復
- この操作はシステムディスクを変更します；ツールを実行する前にバックアップしてください。
- プロセス中にタスクマネージャーからアプリケーションを閉じないでください；統合されたキャンセルオプションを使用してください。
- 設定を元に戻す場合は、**スペースを回復**ボタンを使用して `ISOBOOT`/`ISOEFI` パーティションを削除し、`C:` ドライブを復元します。

## 制限事項
- ディスク0で動作し、ボリュームC:を約10.5 GB縮小します；他のディスクレイアウトは現在サポートされていません。
- 管理者権限とWindows PowerShellの可用性が必要です。
- `lang/` の言語ファイルが必要；見つからない場合、アプリはエラーを表示します。

## クレジット
**Andrey Rodríguez Araya** により2025年に開発されました。

## ライセンス
このプロジェクトはGPL 3.0ライセンスの下にあります。詳細については `LICENSE` ファイルを参照してください。

## サードパーティ通知
- 7‑Zip SDK：この製品の一部には、Igor Pavlovの7‑Zip SDKのコードが含まれています。
  - ライセンスの概要（`third-party/DOC/License.txt` による）：
    - ほとんどのファイルはGNU LGPL（v2.1以降）でライセンスされています。
    - 一部のファイルは、ヘッダーで明示的に述べられている場合、パブリックドメインです。
    - `CPP/7zip/Compress/LzfseDecoder.cpp` はBSD 3‑Clauseライセンスの下にあります。
    - `CPP/7zip/Compress/Rar*` は「unRARライセンス制限」付きのGNU LGPLの下にあります。
  - 最小限のサブセット（ISOハンドラーと共通ユーティリティ）を含めています。このプロジェクトではRARコードは使用されていません。
  - 全文：`third-party/DOC/License.txt`、`third-party/DOC/lzma.txt`、および `third-party/DOC/unRarLicense.txt` を参照してください。
