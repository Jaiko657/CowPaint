# CowPaint

A simple paint app experiment to learn **Copy-on-Write (CoW) snapshots** inspired by Btrfs.  
Built with [raylib](https://www.raylib.com/) and [nob.h](https://github.com/tsoding/nob.h).

---

## Requirements

### Debian / Ubuntu
```bash
sudo apt-get install -y git gcc pkg-config libraylib-dev
````

### Fedora
```bash
sudo dnf install -y git gcc pkgconf-pkg-config raylib-devel
```

---

## Build & Run

```bash
# clone
cd CowPaint
# build tool
cc -O2 build.c -o nob

# build app
./nob

# run
./build/cowpaint
```

---
