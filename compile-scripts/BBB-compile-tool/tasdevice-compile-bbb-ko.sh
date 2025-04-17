clear
OUTPUT_DIR1=build_dir
OUTPUT_DIR2=tasdevice-bbb-v5.10
function clear_stdin()
(
    old_tty_settings=`stty -g`
    stty -icanon min 0 time 0

    while read none; do :; done

    stty "$old_tty_settings"
)
if [ ! -d $OUTPUT_DIR1/$OUTPUT_DIR2-ko ];then
    mkdir -p $OUTPUT_DIR1/$OUTPUT_DIR2-ko
	if [ ! -d $OUTPUT_DIR1/$OUTPUT_DIR2-ko ];then
		echo "$OUTPUT_DIR1/$OUTPUT_DIR2-ko" not exist!
		return
	fi
fi
make W=1 ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- KCONFIG_CONFIG=config_dir/.$OUTPUT_DIR2-config O=$OUTPUT_DIR1/$OUTPUT_DIR2-ko CONFIG_SND_SOC_TASDEVICE=m modules -j $(expr $(nproc) - 1) 2>&1 | tee $OUTPUT_DIR1/$OUTPUT_DIR2-ko/compile.log
clear_stdin