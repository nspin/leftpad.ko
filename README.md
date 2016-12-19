leftpad.ko
==========

In March of 2016, the removal of a package called left-pad from npm created a [giant mess](https://github.com/stevemao/left-pad/issues/4) of failed builds all over the internet.
The package consisted only of a single function, `leftPad()`, which pads strings to a given width.
This event demonstrates the disaster potential of failing to take left-padding seriously.

[left-pad.io](left-pad.io), created soon after the situation described above, was the first ever LPAAS (left-pad-as-a-service) product.
LPAAS is a step in the right direction, but I believe we can do better.

To that end, here is an implementation of `leftPad()` *inside* the kernel.

## Parameters

* `width`: width to pad to (default 32)
* `fill`: value of the character to fill with (e.g. 32 for ' ') (default 32)
* `buffer_size`: size of the internal ring buffer (default 1024)

All parameters are mutable.
The values at the time the device is opened determine the behavior of that instance.

## IOCTL

* `800d3900`: set width
* `800d3901`: set fill

Changes apply only to a specific instance.

## Example Usage

```
$ make
$ insmod leftpad.ko width=10
$ mknod /dev/leftpad c 1337 0 -m 666
$ exec 8<>/dev/leftpad
$ echo foobar >&8
$ head -n 1 <&8
    foobar
$ echo 20 > /sys/module/leftpad/parameters/width
$ echo 43 > /sys/module/leftpad/parameters/fill
$ exec 9<>/dev/leftpad
$ echo bazqux >&9
$ head -n 1 <&9
++++++++++++++bazqux
$ exec 10<>/dev/leftpad
$ python -c 'import fcntl; fcntl.ioctl(10, 0x800d3900, 12); fcntl.ioctl(10, 0x800d3901, ord("_"))'
$ echo zyzzy >&10
$ head -n 1 <&10
_______xyzzy
$ #
$ # \(@_@)/
$ #
```

## Implementation Details

Each time `/dev/leftpad` is opened, a ring buffer of size `buffer_size` is associated with the open file.
Writing fails if there is not enough space in the buffer.
Reads happen by line, and block until there is a newline in the buffer.
