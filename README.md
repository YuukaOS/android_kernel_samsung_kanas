This is Linux kernel release 3.x <http://kernel.org/>

If you want to try this kernel download this but have some bug. CPU clock max stuck on 1000 MHz but in this source I am done fix it. LINK : https://www.androidfilehost.com/?fid=24459283995307287

This kernel have some changes :
	1. Overclock upto 1.3 GHz
	2. Underclock updown 200 MHz
	3. Enable CPU Governon (powersave.performance.userspace.ondemand.sprdemand.conservative.interactive)
	4. Enable IO and + 3 New IO (noop.deadline.cfq.fiops.zen.bfq)
	5. Included mali400 driver
	6. Permixive SElinux

How to compile :
	1. Download all source
	2. Download toolchain
	3. Open terminal then go to kernel directory
	4. Write export ARCH=arm
	5. Write export CROSS_COMPILE=/home/<youre user>/<toolchaindirectory>/arm-linux-eabi-UB-4.9/bin/arm-eabi-  (For example I use Uberc toolchain)
	6. Write make cyanogen_sa_defconfig
	7. Write make -j4
	8. Done.

If you want to compile again to forget tu run make clean && make mrproper

