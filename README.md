leftpad.ko
==========

In March of 2016, the removal of a package called left-pad from npm created a [giant mess][https://github.com/stevemao/left-pad/issues/4] of failed builds all over the internet.
The package consisted only of a single function, `leftPad()`, which pads strings to a given width.
This event demonstrates the disaster potential of failing to take left-padding seriously.

[left-pad.io](left-pad.io), created soon after the situation described above, was the first ever LPAAS (left-pad-as-a-service) product.
LPAAS is a step in the right direction, but I believe we can do better.

To that end, here is an implementation of `leftPad()` **inside** the kernel.

## Example Usage

```
$ make
$ insmod leftpad.ko leftpad_width=10
$ 1337<>/dev/leftpad
$ echo "foobar" >&1337
$ cat <&1337
    foobar
$
```

## Implementation Details

Each time `/dev/leftpad` is opened, a ring buffer of size `leftpad_buffer_size` is associated with the open file.
Writing blocks if there is not enough space.
Reading blocks if there is no newline in the buffer.
