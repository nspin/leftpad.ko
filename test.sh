exec 8<>/dev/leftpad
echo foobar >&8
head -n 1 <&8
echo 43 > /sys/module/leftpad/parameters/fill
echo 20 > /sys/module/leftpad/parameters/width
exec 9<>/dev/leftpad
echo bazqux >&9
head -n 1 <&9
exec 10<>/dev/leftpad
python -c 'import fcntl; fcntl.ioctl(10, 0x800d3900, 12); fcntl.ioctl(10, 0x800d3901, ord("_"))'
echo xyzzy >&10
head -n 1 <&10
