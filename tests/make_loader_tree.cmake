# ModLoader のテスト用のフォルダを作り直す。
#   <DEST>/dinput8.dll, ignored.dll, wots4_multi_autoload_probe.exe
#   <DEST>/mods/a_first/{mod.dll, helper.dll, nested/ignored.dll}   入口あり・受け入れる
#   <DEST>/mods/b_refused/mod.dll                                   入口あり・断る
#   <DEST>/mods/dependency/helper.dll                               mod.dll がない（読み込まない）
#   <DEST>/mods/invalid/{mod.dll, bad.dll}                          PE ではない（読み込みに失敗する）
#   <DEST>/mods/missing_entry/mod.dll                               入口がない
#   <DEST>/mods/z_last/mod.dll                                      入口あり・受け入れる（名前が同じ別 Mod の確認）
file(REMOVE_RECURSE ${DEST})
file(MAKE_DIRECTORY ${DEST}/mods/a_first/nested ${DEST}/mods/b_refused ${DEST}/mods/dependency
    ${DEST}/mods/invalid ${DEST}/mods/missing_entry ${DEST}/mods/z_last)
file(COPY_FILE ${LOADER} ${DEST}/dinput8.dll)
file(COPY_FILE ${AUTOLOAD} ${DEST}/wots4_multi_autoload_probe.exe)
file(COPY_FILE ${HELPER} ${DEST}/ignored.dll)
file(COPY_FILE ${MOD} ${DEST}/mods/a_first/mod.dll)
file(COPY_FILE ${HELPER} ${DEST}/mods/a_first/helper.dll)
file(COPY_FILE ${HELPER} ${DEST}/mods/a_first/nested/ignored.dll)
file(COPY_FILE ${REFUSED} ${DEST}/mods/b_refused/mod.dll)
file(COPY_FILE ${HELPER} ${DEST}/mods/dependency/helper.dll)
file(WRITE ${DEST}/mods/invalid/mod.dll "not a PE image\n")
file(WRITE ${DEST}/mods/invalid/bad.dll "not a PE image\n")
file(COPY_FILE ${HELPER} ${DEST}/mods/missing_entry/mod.dll)
file(COPY_FILE ${MOD} ${DEST}/mods/z_last/mod.dll)
