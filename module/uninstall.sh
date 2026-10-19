#!/system/bin/sh
#
# 卸载模块时执行
#
# 这里刻意不 rmmod：
#   - 卸载模块的时机是“下次开机正在删除模块目录”，此时本次开机根本没有加载过 ko
#   - 运行中 rmmod 需要内核文本里的 hook 全部反装，且容器活着时 keepalive 会让 rmmod -EBUSY
# 所以卸载的语义就是“重启后生效”，与所有 LKM 模块一致

MODDIR=${0%/*}
# 以相对路径手动执行时 $0 没有斜杠，退回当前目录
[ -d "$MODDIR" ] || MODDIR=$(pwd)
echo "phDroid: removed, kernel side leaves memory at next reboot"
if [ -w /dev/kmsg ]; then
	echo "<6>phDroid: removed, reboot to drop the module from kernel memory" > /dev/kmsg 2>/dev/null
fi
