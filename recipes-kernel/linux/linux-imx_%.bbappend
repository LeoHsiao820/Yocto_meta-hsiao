# 告訴 Yocto 到哪裡找檔案 (搜尋路徑加上本目錄下的 files 夾)
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# 將你的 Patch 加入到編譯過程中
SRC_URI += "file://imx93-11x11-evk.dts"