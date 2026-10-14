# Installer tests. No network and no game folder: they use texts in the test and files in a temporary folder.
# The argument is the ModLoader built here, which the installer must recognize as its own dinput8.dll.
wots4_add_probe(wots4_installer_probe SOURCES wots4_installer_probe.cpp
    LIBS wots4_installer_core ARGS "$<TARGET_FILE:wots4_loader>")
