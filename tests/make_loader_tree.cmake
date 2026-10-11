# Recreate the folder for the ModLoader tests.
#   <DEST>/dinput8.dll, ignored.dll, wots4_multi_autoload_probe.exe
#   <DEST>/mods/a_first/{mod.dll, helper.dll, nested/ignored.dll}   has entry point, accepts
#   <DEST>/mods/b_refused/mod.dll                                   has entry point, refuses
#   <DEST>/mods/dependency/helper.dll                               no mod.dll (not loaded)
#   <DEST>/mods/invalid/{mod.dll, bad.dll}                          not PE (loading fails)
#   <DEST>/mods/missing_entry/mod.dll                               no entry point
#   <DEST>/mods/z_last/mod.dll                                      has entry point, accepts (checks a separate Mod with the same file name)
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
