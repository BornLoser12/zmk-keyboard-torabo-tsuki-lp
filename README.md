
[torabo-tsuki LP](https://github.com/sekigon-gonnoc/torabo-tsuki-lp)用のZMKファームウェア

* _centralがついているuf2をトラックボールがついている方に、_peripheralを反対側に書き込んでください
* キーマップはkeymap-editorおよびzmk-studioで編集できます

## 通常版のファームウェア

通常の `firmware.zip` には次の3種類が入っています。

* `torabo_tsuki_lp_right_central.uf2`: 右側用。BLE再接続修正、USB Studio/DYA、トラックボール、AMLを含みます。
* `torabo_tsuki_lp_left_peripheral.uf2`: 左側用。
* `settings_reset-bmp_boost-zmk.uf2`: 設定消去専用。通常の更新には使いません。

USB接続時はStudioの通信だけをUSBへ切り替えます。Bluetoothのペアリング情報やキー入力の出力先は変更しません。USBを抜くとStudioは従来の通信先選択に戻ります。

以前の `_ble_reconnect_log.uf2` はログ専用の診断版で、USB Studioには接続できません。通常版を右側へ上書きしてください。診断版から通常版への更新に設定リセットは不要です。
