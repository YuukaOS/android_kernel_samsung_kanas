#!/bin/bash
##
#  Copyright (C) 2015, Samsung Electronics, Co., Ltd.
#  Written by System S/W Group, S/W Platform R&D Team,
#  Mobile Communication Division.
##

set -o pipefail

# Use ccache if it's present
USE_CCACHE=1
# Name is too long to show in TWRP, so have a shorter one.
# But in doing so, we'll have to use build numbers
# The default is 0
USE_SHORTER_NAME=0

NAME=SandroidKernel
VERSION=v1.3
DEVICE=kanas
OWNER=MuhammadIhsan-Ih24n
SUFFIX=SA

DEFCONFIG=sandroid_kanas_defconfig
DEFCONFIG_DIR=arch/arm/configs
#ADB_PUSH_LOCATION=/storage/extSdCard/
ADB_PUSH_LOCATION=/storage/sdcard0/

KERNEL_PATH=$(pwd)
KERNEL_ZIP=${KERNEL_PATH}/kernel_zip
#Name of the folder zips will be written
ZIP_FOLDER=.
#ZIP_FOLDER=flashable_zips
# Absolute location where zips will be written, best when left alone
ZIP_LOCATION=${KERNEL_PATH}/${ZIP_FOLDER}

MODULES_PATH=${KERNEL_PATH}/drivers

JOBS=`grep processor /proc/cpuinfo | wc -l`

COLOR_RED=$(tput bold)$(tput setaf 1)
COLOR_BLUE=$(tput bold)$(tput setaf 4)
COLOR_YELLOW=$(tput bold)$(tput setaf 3)
COLOR_NEUTRAL="\033[0m"
COLOR_GREEN="\033[1;32m"

FAILED_STR=$COLOR_RED"(FAILED)"$COLOR_NEUTRAL
SUCCESS_STR=$COLOR_GREEN"(SUCCESS)$COLOR_NEUTRAL"

export ARCH=arm
export CROSS_COMPILE=${KERNEL_PATH}/builds/toolchains/bin/arm-eabi-
export LOCALVERSION=-`echo SandroidTeam-$USER`

# Checks and sets build date, and zip filename
check_build_date() {
	if [[ ${USE_SHORTER_NAME} == 1 ]]; then
		NOW=`date "+%Y%m%d%p"`
		KERNEL_ZIP_NAME=SAKernel-${VERSION}${NOW}-${BUILD_NUMBER}
	else
		NOW=`date "+%d%m%Y-%H%M%S"`
		KERNEL_ZIP_NAME=${NAME}-${VERSION}-${DEVICE}-${OWNER}${NOW}-${SUFFIX}
	fi
}

# Sets the kernel zip file location if it was already have been built
load_recent_build() {
	if [[ -e ${KERNEL_PATH}/.version ]]; then
		BUILD_NUMBER=`cat ${KERNEL_PATH}/.version`
	else
		BUILD_NUMBER=0
	fi

	if [[ -e ${KERNEL_PATH}/.build_time ]]; then
		BUILD_TIME=`cat ${KERNEL_PATH}/.build_time`
	else
		unset BUILD_TIME
	fi

	# Construct zip relative file location
	if [[ ${USE_SHORTER_NAME} == 1 ]]; then
		KERNEL_ZIP_NAME=SAKernel-${VERSION}${BUILD_TIME}-${BUILD_NUMBER}
	else
		KERNEL_ZIP_NAME=${NAME}-${VERSION}-${DEVICE}-${OWNER}${BUILD_TIME}-${SUFFIX}
	fi
	ZIP_MAYBE_DONE=${ZIP_FOLDER}/${KERNEL_ZIP_NAME}.zip

	# Check whether that file exists then set accordingly
	if [[ -e ${ZIP_MAYBE_DONE} ]]; then
		ZIP_FINISHED=$COLOR_GREEN"(@ ${ZIP_MAYBE_DONE})"$COLOR_NEUTRAL
	fi
}

# Checks which config file is being used
check_config() {
	if [[ ! -e ${KERNEL_PATH}/.config ]]; then
		CONFIG_FILE="nothing"
		return 0
	elif diff $DEFCONFIG_DIR/$DEFCONFIG ${KERNEL_PATH}/.config > /dev/null; then
		CONFIG_FILE=$DEFCONFIG
	else
		CONFIG_FILE=".config"
	fi
	return 1
}

# Builds a flashable zip file which "injects" the kernel
make_zip() {
	echo -e $COLOR_GREEN"Creating flashable zip..."$COLOR_NEUTRAL
	ZIP_FINISHED=$FAILED_STR

	mv ${KERNEL_ZIP}/tools/zImage ${KERNEL_ZIP}/tools/zImage.old
	find ${KERNEL_PATH}/arch -name zImage -exec cp -f {} ${KERNEL_ZIP}/tools \;

	if [[ ! -e ${KERNEL_ZIP}/tools/zImage ]]; then
		echo -e $COLOR_RED"Kernel Image is not found"$COLOR_NEUTRAL
		mv ${KERNEL_ZIP}/tools/zImage.old ${KERNEL_ZIP}/tools/zImage
		return -1
	fi

	rm ${KERNEL_ZIP}/tools/zImage.old
	copy_modules

	# Change directory to the kernel_zip
	cd ${KERNEL_PATH}/kernel_zip || return -1
	mkdir -p ${ZIP_LOCATION} || return -1
	zip -r ${ZIP_LOCATION}/${KERNEL_ZIP_NAME}.zip ./  || return -1

	if [[ -e ${ZIP_LOCATION}/${KERNEL_ZIP_NAME}.zip ]]; then
		ZIP_FINISHED=$COLOR_GREEN"(@ ${ZIP_FOLDER}/${KERNEL_ZIP_NAME}.zip)"$COLOR_NEUTRAL
	fi
	return 0
}

# Locates all .ko modules and copies them to a temporary location
copy_modules() {
	. ${KERNEL_PATH}/.config #Loads .config, apparently it has a sh-like syntax
	rm -r ${KERNEL_ZIP}/system

	if [[ $CONFIG_MODULES -eq y ]]; then
		modules=$(find ${MODULES_PATH} -name "*.ko" -type f | wc -l)
		if [[ $modules -eq 0 ]]; then
			echo "No module files *.ko found"
			return 0;
		fi

		mkdir -p ${KERNEL_ZIP}/system/lib/modules || return -1
		find ${MODULES_PATH} -name "*.ko" -exec cp -f {} ${KERNEL_ZIP}/system/lib/modules \; || return -1
	fi

}

# Builds kernel ... what do you expect?
build_kernel() {
	echo -e $COLOR_GREEN"Building kernel..."$COLOR_NEUTRAL
	if check_config == 0; then
		make ${DEFCONFIG}
	fi

	make -j${JOBS}
	if [[ $? == 0 ]]; then
		BUILD_RESULT=$SUCCESS_STR
		mkdir -p ${KERNEL_ZIP}/system/lib/modules

		#Set our finishing build time
		check_build_date
		echo ${NOW} > ${KERNEL_PATH}/.build_time

		unset ZIP_FINISHED #Of course, current zip build is now invalid
        return 0
	else
		BUILD_RESULT=$FAILED_STR
		echo -e $COLOR_RED"Compilation failed!"$COLOR_NEUTRAL
		return -1
	fi
}

# Clean kernel source and removes all modules from that temporary folder
make_clean(){
	echo -e $COLOR_GREEN"Cleaning kernel source directory..."$COLOR_NEUTRAL
	make mrproper clean
	rm -r ${KERNEL_ZIP}/system ${KERNEL_ZIP}/tools/zImage\
	   ${KERNEL_ZIP}/tools/default.prop
	unset BUILD_RESULT
	return 0
}

# Deletes all built zip files
clean_zip() {
	echo -e $COLOR_GREEN"Removing built zips..."$COLOR_NEUTRAL
	rm ${ZIP_LOCATION}/*.zip
	rmdir ${ZIP_LOCATION}
	unset ZIP_FINISHED
	return 0
}

# Copies the zip to your phone through adb
adb_push_zip() {
	echo -e $COLOR_GREEN"Pushing zip to ${ADB_PUSH_LOCATION}"$COLOR_NEUTRAL
	if [[ -e ${ZIP_LOCATION}/${KERNEL_ZIP_NAME}.zip ]]; then
		adb push ${ZIP_FOLDER}/${KERNEL_ZIP_NAME}.zip $ADB_PUSH_LOCATION/ || return -1;
	else
		echo -e $COLOR_RED"Zip file was not yet built."$COLOR_NEUTRAL
		wait_on_user_input
	fi
}

adb_push_zip_then_flash() {
	if [[ -e ${ZIP_LOCATION}/${KERNEL_ZIP_NAME}.zip ]]; then
		adb_push_zip || return -1
		echo -e $COLOR_GREEN"Pushing zip to /cache/update.zip"$COLOR_NEUTRAL
		adb push ${ZIP_FOLDER}/${KERNEL_ZIP_NAME}.zip /cache/update.zip || return -1;
		adb shell "echo '--update_package==/cache/update.zip' >> /cache/recovery/command" || return -1
		echo -e $COLOR_GREEN"Rebooting device..."$COLOR_NEUTRAL 
		adb shell sync  || return -1
		adb reboot recovery  || return -1
	else
		echo -e $COLOR_RED"Zip file was not yet built."$COLOR_NEUTRAL
		return -1
	fi
}

# Save the current .config as the DEFCONFIG
save_as_defconfig() {
	cp $DEFCONFIG_DIR/$DEFCONFIG $DEFCONFIG_DIR/$DEFCONFIG.old
	cp ${KERNEL_PATH}/.config $DEFCONFIG_DIR/$DEFCONFIG || wait_on_user_input;
}

# Call "configuration commands" as said in README
call_menu() {
	targets="nconfig menuconfig config" #pick those that needs user interaction
	for menu in ${targets}; do
		echo -e $COLOR_GREEN"Calling 'make $menu'"$COLOR_NEUTRAL

		make $menu
		if [[ $? == 0 ]]; then return 0; fi

		echo -e $COLOR_RED"make $menu Failed"$COLOR_NEUTRAL
	done
	echo -e $COLOR_RED"No configuration commands succeeded"$COLOR_NEUTRAL
	return -1;
}

make_defconfig(){
	echo -e $COLOR_GREEN"Calling 'make ${DEFCONFIG}'"$COLOR_NEUTRAL
    make ${DEFCONFIG} || return -1
    return 0
}

#### Wrappers ####
wait_on_user_input(){ echo "Press any key to continue"; read -n 1; }
command_1(){ make_clean; clean_zip; }
command_2(){ make_clean; }
command_3(){ clean_zip; }
command_4(){ make_defconfig || wait_on_user_input; }
command_5(){ call_menu || wait_on_user_input; }
command_6(){ build_kernel || wait_on_user_input; }
command_7(){ make_zip || wait_on_user_input; cd ${KERNEL_PATH}; }
command_8(){ adb_push_zip || wait_on_user_input; }
command_9(){ adb_push_zip_then_flash || wait_on_user_input; }
command_s(){ save_as_defconfig || wait_on_user_input; }
command_e(){ exit; }

#### Main ####
#Use ccache when it's both present and enabled
if [[ $(which ccache) ]] && [[ USE_CCACHE ]]; then
	export CROSS_COMPILE="ccache $CROSS_COMPILE"
	USING_CCACHE="Yes"
else
	USING_CCACHE="No"
fi

while true; do
	#Set zip name and zip file whether it exists
	load_recent_build
	check_config
	clear
	echo -e $COLOR_RED"===================================================================="
	echo -e $COLOR_BLUE"              BUILD SCRIPT FOR BUILDING SANDROID KERNEL"
	echo               "                 MODIFIED BY MUHAMMAD IHSAN <Ih24n>"
	echo               "                      EXTENEND BY ME <impasta>"
	echo -e $COLOR_RED"===================================================================="
	echo -e           "  Kernel name     : $NAME"  '\t'    "Kernel version : $VERSION"
	echo -e           "  Bob the Builder : $USER"  '\t\t' "Build number   : $BUILD_NUMBER"
	echo -e           "  Kbuild config   : $CONFIG_FILE"'\t\t' "Using ccache   : $USING_CCACHE"
	echo -e $COLOR_NEUTRAL"========================="$COLOR_BLUE"Function menu flag"$COLOR_NEUTRAL"========================="
	echo              "  1  = Clean kernel and zip files"
	echo              "  2  = Clean kernel"
	echo              "  3  = Clean all zip files"
	echo              "  4  = Set .config file to $DEFCONFIG"
	echo              "  5  = Configure Kernel"
	echo -e           "  6  = Build kernel $BUILD_RESULT"
	echo -e           "  7  = Zip kernel   $ZIP_FINISHED"
	echo              "  8  = Copy zip to $ADB_PUSH_LOCATION"
	echo              "  9  = Do task #8 then flash (CAUTION: reboots relentlessly!!!)"
	echo              "  s  = Save .config as $DEFCONFIG"
	echo              "  e  = Exit"
	echo -e $COLOR_YELLOW"===================================================================="
	echo              "NOTE : JUST CHOOSE THE NUMBER, AND WAIT UNTIL THE TASK IS DONE!"
	echo              "===================================================================="
	read -p "$COLOR_BLUE What's Your Choice? " -n 1 -s x
	echo -e $COLOR_NEUTRAL"$x"$COLOR_GREEN
	if [[ "$(type -t command_${x})" == function ]]; then command_${x}; fi
done
