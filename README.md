# Basic Shell for the Picocomputer 6502
https://picocomputer.github.io/

## Building
This currently builds with the rp6502 fork of cc65 from https://github.com/picocomputer/cc65

Here's how I build cc65:
1. `git clone https://github.com/picocomputer/cc65`
2. `cd cc65`
3. `make`
4. `sudo make install PREFIX=/usr/local`

Additionally, you need to tell CC65 where to find the includes and other bits. This will
need to be done for every shell (probably put in your bashrc or equivalent):
1. `export CC65_HOME="/usr/local/share/cc65"`

Once you've got CC65 installed, you can build with the typical cmake spell:
1. `git clone <this git repo>`
2. `cd rp6502-shell`
3. `mkdir build`
4. `cd build`
5. `cmake --toolchain ../tools/rp6502.cmake ..`
6. `make`

You should now have a shell.rp6502 file in the build folder that you can upload with the
included tools/rp6502.py command or via a USB drive.


