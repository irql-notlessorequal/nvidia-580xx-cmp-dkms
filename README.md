# nvidia-580xx-cmp-dkms

A modified version version of the Arch Linux packaging of the NVIDIA 580.xx
series drivers for Linux with additional patches that enable the following.

- Disables artificial driver-side throttling of graphical applications
- Disables PCIe downclocking to Gen 1 speeds

## Unlocking PCIe Gen 2 speeds

This requires an additional kernel module in order to brute-force the GPU
into running at Generation 2 speeds, this repository includes a utility to do the most of the work
for you.

You need to run `nvidia-gen2.sh` as root, while providing the PCIe path
for your GPU to the script, for example in my use-case this is `0000:01:00.0`. Of course it may vary on your PC.

The following conditions must be met to run it

- Running the script as root
- Compiled the optional kernel module
- You are running in a TTY with no GPU applications
- The NVIDIA driver is unloaded

## Supported Devices

| Models | Device IDs |
| --- | --- |
| P102-100, P104-100, P104-101, P106-100, P106-090 | `1b07`, `1b87`, `1bc7`, `1c07`, `1c09` |
| CMP 30HX, 40HX, 50HX | `2189`, `1f0b`, `1e09` |
| CMP 70HX, 90HX, 170HX | `248a`, `220d`, `2082`, `20c2` |

CMP series is untested...

I'm not paying current prices for one...

## License

```
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <https://unlicense.org/>
```