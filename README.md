# エコー(拡張編集フィルタプラグイン)
- [ダウンロードはこちら](../../releases/) ※通常の使用には [残響オブジェクト](https://github.com/nazonoSAUNA/ReverberationObject.eef/) も必要になると思われます
- Echo.eefをpluginsフォルダに配置してください

- このプラグインを読み込むためにはpatch.aul r43_ss_58以降が必要です https://scrapbox.io/nazosauna/patch.aul
- 但し音声クロスフェードとの相性の関係上、patch.aul r43_ss_59以降が推奨されます（本プラグインリリース時点では最新はまだ58）

## パラメータ
- ミックス：原音とディレイ音の音量を調整する。0で原音100%ディレイ音0%、50で原音100%ディレイ音100%、100で原音0%ディレイ音100%
- 強さ：ディレイ音の大きさ。この強さに従って音が減衰していく
- 遅延(ms)
- クロス：ステレオ音声の場合に左右反対側の音がディレイ音に反映される強さ

- 種類：ディレイ・ステレオディレイ・ピンポンディレイから選択


# 開発者向け
- aviutl_exedit_sdk：ほぼhttps://github.com/nazonoSAUNA/aviutl_exedit_sdk/tree/command を使用しています
