exec 9<>/dev/leftpad
echo foobar >&9
cat <&9
