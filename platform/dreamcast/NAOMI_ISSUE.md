**Title:** Dreamcast port on real hardware (NAOMI 2): texture RAM byte writes break every texture; speed figures

Hi! I ran the Dreamcast port on a SEGA NAOMI 2 (512 MB NetDIMM, netbooted, Crazy Taxi upright with a 31 kHz monitor). The NAOMI has the Dreamcast's SH-4, PVR2, VRAM bus and AICA, so I think the two findings below apply to a real Dreamcast as well. The branch is here: https://github.com/0skarzer0/daytona-arcade-recomp/tree/naomi

### 1. Every texture is a grid of wrong texels on real hardware (fix: one line)

The game's texture RAM is placed in video RAM (`platform/dreamcast/game/main.cpp`, `pvr_mem_malloc(0x400000)`), and `M2Board::tex_write` (`src/runtime/m2_board.cpp`) writes it with two byte stores. Flycast accepts byte writes to VRAM; the hardware does not. On the board, every textured polygon showed a regular grid of dark or wrong texels, near and far, while the tile layers (written as 16-bit) looked fine.

Writing the halfword in one store fixes it completely:

```cpp
#ifdef M2_DC_MEMORY
    *reinterpret_cast<volatile uint16_t *>(w) = uint16_t(lane_data);
#else
    w[0] = uint8_t(lane_data);
    w[1] = uint8_t(lane_data >> 8);
#endif
```

`w` is always 2-aligned there. The desktop build compiles exactly what it did before. Commit: "Dreamcast: write texture RAM with 16-bit stores" on the branch above.

### 2. `pvr_txr_load_dma` for the tile layers never completed

With the KallistiOS naomi subarch, the first frame hung inside the DMA wait in `upload_layer` (pc in `arch_irq_restore`, draw stage 3). On the NAOMI I switched the layer uploads to `pvr_txr_load` (store queues), and the wait is skipped there. I haven't checked whether a Dreamcast does the same; it may be specific to the naomi subarch.

### 3. Speed on the hardware

Attract mode and a race with `kDrawEvery` 2, from the PROFILE lines (the on-screen log off):

| ms per game frame          |      |
|----------------------------|-----:|
| core (i960)                | 19.2 |
| geometry (TGP)             |  7.1 |
| video                      |  5.4 |
| draw (9.3 per drawn frame) |  1.7 |
| ROM reads                  | 0.97 |
| sound                      | 0.22 |
| **frame total**            | **37.2** |

- **Draw, per drawn frame:** sort+wait 3.5, materials 2.6, polygons 3.2, layer upload wait 0.0.
- **i960 frame wait:** skipped 77% of the i960's instructions.

That is about 27 game frames a second against the arcade's 57.5, so it plays at roughly half speed. That is the same as in Flycast (27–32 fps), so the emulator's speed figures look representative of the hardware. The i960 core is the limit. ROM reads are under 1 ms per frame here because the NAOMI's 32 MB RAM allows a 12 MB ROM page cache, so on a Dreamcast with its 2 MB cache, disc reads will add to that.

Those figures are averages, though. Every so often the game nearly stops for a while, at 1–2 frames a second, and then recovers. I haven't found the cause yet. My first suspect is ROM page cache misses: each miss is read from the NetDIMM through the cartridge port with 16-bit PIO, and a burst of misses (a new section of the track, for example) could explain it. Per-frame maximums rather than averages, plus a count of cache misses, should show whether that is it.

### Other things on the branch (all behind `_arch_sub_naomi` except the last two)

- **Data from the NetDIMM:** ROM images, sound pack and default NVRAM are read from the NetDIMM instead of a disc.
- **Controls:** JVS controls through the MIE.
- **Settings:** EEPROM and backup RAM are kept in the board's SRAM.
- **Default NVRAM:** single cabinet and free play. The factory Rev. A NVRAM is twin/master and stops at NETWORK CHECKING.
- **Mipmapped source textures:** square, with point-sampled levels. I tried this first for the grid, which it didn't cause, but it does reduce far shimmer. Not measured on a Dreamcast.
- **Build fixes:** casts and libstdc++ C99 defines needed by a GCC 15.2 / current KallistiOS toolchain.

Details and build steps are in `platform/dreamcast/NAOMI.md` on the branch. One caveat: I built only the naomi subarch, not the Dreamcast one, after these changes.

Thanks for the project. It was great to see Daytona running on the cabinet!
