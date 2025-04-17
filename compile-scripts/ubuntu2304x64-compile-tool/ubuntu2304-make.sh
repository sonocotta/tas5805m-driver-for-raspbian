OUTPUT_DIR1=build_dir
OUTPUT_DIR2=ubuntu-23.04
if [ ! -d $OUTPUT_DIR1 ];then
    mkdir $OUTPUT_DIR1
fi
if [ ! -d $OUTPUT_DIR1/$OUTPUT_DIR2 ];then
		pushd $OUTPUT_DIR1
    mkdir $OUTPUT_DIR2
    popd
fi
function clear_stdin()
(
    old_tty_settings=`stty -g`
    stty -icanon min 0 time 0

    while read none; do :; done

    stty "$old_tty_settings"
)
clear
KERNEL_COMPILE_CONFIG_FILE=config-6.6.0-rc2
make W=1 KCONFIG_CONFIG=config_dir/$KERNEL_COMPILE_CONFIG_FILE O=$OUTPUT_DIR1/$OUTPUT_DIR2 -j $(expr $(nproc) - 1) 2>&1 | tee $OUTPUT_DIR1/$OUTPUT_DIR2/compile-kernel.log
echo Please check the log, compiling is completed
clear_stdin
read -n1 -p "else press any key for tas2781 driver module compiling..."
make W=1 KCONFIG_CONFIG=config_dir/$KERNEL_COMPILE_CONFIG_FILE O=$OUTPUT_DIR1/$OUTPUT_DIR2 CONFIG_SND_SOC_TASDEVICE=m modules -j $(expr $(nproc) - 1) 2>&1 | tee $OUTPUT_DIR1/$OUTPUT_DIR2/compile-ko.log
echo Please check the log, compiling is completed!
clear_stdin
:<<!
read -n1 -p "else press any key to continue..."
make KCONFIG_CONFIG=config_dir/$KERNEL_COMPILE_CONFIG_FILE INSTALL_MOD_STRIP=1 O=$OUTPUT_DIR1/$OUTPUT_DIR2 modules_install 2>&1 | tee $OUTPUT_DIR1/$OUTPUT_DIR2/modules_install.log
echo Please check the log, if error, then press CTRL+C
clear_stdin
cp $OUTPUT_DIR1/$OUTPUT_DIR2/config_dir/$KERNEL_COMPILE_CONFIG_FILE /boot/
read -n1 -p "else press any key to install the newly-built kernel and modules"
make KCONFIG_CONFIG=config_dir/$KERNEL_COMPILE_CONFIG_FILE O=$OUTPUT_DIR1/$OUTPUT_DIR2 install 2>&1 | tee $OUTPUT_DIR1/$OUTPUT_DIR2/install.log
echo Installation completed! Pls reboot the system
clear_stdin
!