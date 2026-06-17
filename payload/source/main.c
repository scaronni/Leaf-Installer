/*
 * Leaf Updater payload.
 *
 * Hekate auto-boots this from `bootloader/payloads/leaf-updater.bin` (the
 * [Leaf Update] entry in hekate_ipl.ini that Leaf flips `autoboot=` to at
 * install time). With HOS not running, Atmosphère's fs-mitm isn't holding
 * package3/stratosphere.romfs open, so we can freely rewrite them.
 *
 * Steps:
 *   1) Read sdmc:/leaf-offline-update/.autoboot-prev (the autoboot= value
 *      Leaf snapshotted before arming us). If absent, the user launched us
 *      from the hekate menu by hand — we leave the ini alone.
 *   2) Recursively move /leaf-offline-update/* into the SD root, merging
 *      into existing directories and overwriting clashing files.
 *   3) If we had a saved autoboot value, rewrite the `autoboot=` line in
 *      /bootloader/hekate_ipl.ini back to it.
 *   4) Remove /leaf-offline-update.
 *   5) Reboot — hekate will pick up the restored autoboot on next boot.
 */

#include <stdarg.h>
#include <string.h>

#include <display/di.h>
#include <libs/fatfs/ff.h>
#include <mem/heap.h>
#include <mem/minerva.h>
#include <memory_map.h>
#include <power/max77620.h>
#include <rtc/max77620-rtc.h>
#include <soc/bpmp.h>
#include <soc/hw_init.h>
#include <soc/t210.h>
#include <storage/sd.h>
#include <utils/btn.h>
#include <utils/util.h>

#include "gfx.h"

/* Staging area we set up in Leaf and consume here. Paths are bare (no drive
 * prefix) because the BDK's `sd_mount()` registers the SD as FATFS volume 0
 * — using `"sd:/..."` here would route to a non-existent volume and every
 * f_stat / f_open silently returns NOT_ENABLED. */
#define STAGING_DIR     "leaf-offline-update"
#define AUTOBOOT_SAVED  "leaf-offline-update/.autoboot-prev"
#define HEKATE_INI      "bootloader/hekate_ipl.ini"

/* Forward decls. */
static int  load_saved_autoboot(int *out_value);
static int  merge_move_dir(const char *src, const char *dst);
static int  rewrite_autoboot(int new_value);
static void halt_with_error(const char *msg);
static void show_exit_menu(void);

/* Globals — kept tiny to leave the stack budget for recursion. */
static u32 g_files_moved = 0;
static u32 g_dirs_moved  = 0;

int main(void)
{
    /* ----- Bring up the HW the way hekate does it. ----- */
    hw_init();
    heap_init((void *)IPL_HEAP_START);
    max77620_rtc_prep_read();

    display_init();
    u32 *fb = display_init_window_a_pitch();
    gfx_init_ctxt(fb, 720, 1280, 720);
    gfx_con_init();
    display_backlight_pwm_init();
    /* pwm_init only configures the PWM controller — without an explicit
       brightness write the panel stays at 0% and the screen looks dead.
       Match hekate's idle-menu brightness so it's clearly visible. */
    display_backlight_brightness(150, 1000);

    gfx_clear_grey(0x1B);
    gfx_con_setpos(0, 0);
    gfx_printf("%kLeaf Updater\n", 0xFF40A040);
    gfx_printf("%kApplying staged CFW components...\n\n", 0xFFC0C0C0);

    /* ----- Mount the SD card. -----
     * This is built against hekate's BDK pinned at v6.5.2 on purpose. v6.5.3
     * rewrote the SD UHS / 1.8V-signaling init path (bdk/storage/sdmmc.c) and
     * sd_mount() hangs here at runtime: the default SD_UHS_SDR104 mode drives
     * the new SD_VOLTAGE_SWITCH / sdmmc_enable_low_voltage sequence, which
     * doesn't complete under this payload's minimal hw_init. Bumping the
     * submodule past v6.5.2 will silently reintroduce that hang. */
    if (sd_mount())
        halt_with_error("Failed to mount SD card.");

    /* ----- Check there is anything to apply. ----- */
    FILINFO fi;
    if (f_stat(STAGING_DIR, &fi) != FR_OK)
    {
        gfx_printf("%kNothing staged at /%s — nothing to do.\n",
                   0xFFC0C0C0, STAGING_DIR);
        goto finish;
    }

    /* ----- Snapshot the autoboot value Leaf saved. ----- */
    int saved_autoboot = -1;
    int has_saved      = (load_saved_autoboot(&saved_autoboot) == 0);
    if (has_saved)
        gfx_printf("Saved autoboot value: %d\n\n", saved_autoboot);
    else
        gfx_printf("%kNo saved autoboot — ini will not be rewritten.%k\n\n",
                   0xFFFFD080, 0xFFFFFFFF);

    /* Get the saved file out of the staging tree before we walk it. */
    f_unlink(AUTOBOOT_SAVED);

    /* ----- Recursively move staging contents to the SD root. ----- */
    gfx_printf("Moving staged files into place...\n");
    if (merge_move_dir(STAGING_DIR, "") != 0)
        halt_with_error("Move failed mid-way. SD may be in a partial state.");
    f_unlink(STAGING_DIR); /* Remove now-empty dir. */

    gfx_printf("\n%k%d files, %d directories moved.\n",
               0xFF40A040, (int)g_files_moved, (int)g_dirs_moved);

    /* ----- Restore hekate's autoboot if we had a snapshot. ----- */
    if (has_saved)
    {
        gfx_printf("\nRestoring autoboot=%d in /%s...\n",
                   saved_autoboot, HEKATE_INI);
        if (rewrite_autoboot(saved_autoboot) != 0)
            gfx_printf("%kCould not rewrite hekate_ipl.ini — please restore"
                       " autoboot manually.%k\n", 0xFFFFD080, 0xFFFFFFFF);
        else
            gfx_printf("%kautoboot restored.%k\n", 0xFF40A040, 0xFFFFFFFF);
    }

finish:
    sd_unmount();

    show_exit_menu();
    while (1) { }
    return 0;
}

/* --------------------------------------------------------------------------
 * Exit menu — picks between reboot and power-off with a 5-second auto-reboot
 * fallback. On non-PicoFly Mariko consoles (no modchip) a cold reboot ends
 * up in stock HOS, so the user may want to power-off instead.
 * --------------------------------------------------------------------------*/

static void exit_menu_draw(u32 base_x, u32 base_y, int selected, int seconds_left)
{
    /* Repaint in place: reposition to the captured base and reprint. No
       partial-clear is needed because the 16px renderer fills each glyph's
       background (fillbg=1) and every line variant below is a fixed length,
       so reprinting fully overwrites the previous frame. (A coordinate-based
       partial clear wouldn't map to text rows under the landscape software
       rotation anyway.) */
    gfx_con_setpos(base_x, base_y);

    const u32 hot = TXT_CLR_GREENISH;
    const u32 cold = TXT_CLR_GREY;

    gfx_printf("\n");
    gfx_printf("  %k%s  Reboot%k\n",     selected == 0 ? hot : cold,
               selected == 0 ? ">" : " ", TXT_CLR_DEFAULT);
    gfx_printf("  %k%s  Power off%k\n",  selected == 1 ? hot : cold,
               selected == 1 ? ">" : " ", TXT_CLR_DEFAULT);
    /* Both status strings are deliberately the same length (38 chars) so one
       cleanly overwrites the other when the countdown is cancelled. */
    if (seconds_left >= 0)
        gfx_printf("\n  %kAuto-rebooting in %d... (VOL to cancel)%k",
                   TXT_CLR_GREY, seconds_left, TXT_CLR_DEFAULT);
    else
        gfx_printf("\n  %kVOL: change selection   POWER: confirm%k",
                   TXT_CLR_GREY, TXT_CLR_DEFAULT);
}

static void show_exit_menu(void)
{
    gfx_printf("\n%kDone.%k\n", TXT_CLR_GREENISH, TXT_CLR_DEFAULT);

    u32 base_x = 0, base_y = 0;
    gfx_con_getpos(&base_x, &base_y);

    int selected     = 0; /* 0 = Reboot, 1 = Power off */
    int seconds_left = 5; /* -1 once the user has touched the volume keys */

    exit_menu_draw(base_x, base_y, selected, seconds_left);

    /*
     * Hand-rolled polling loop with edge detection. Don't use
     * btn_wait_timeout(ms, mask) here — its "mask" means "wait for all
     * those bits to be pressed *simultaneously*," not "wait for any one
     * of them." A single POWER press never matches the all-three mask, so
     * the previous version got stuck the moment the user touched VOL.
     */
    const u8 BTN_MASK = BTN_POWER | BTN_VOL_UP | BTN_VOL_DOWN;
    u8  prev_state    = btn_read() & BTN_MASK;
    u32 last_tick_ms  = get_tmr_ms();

    while (1)
    {
        msleep(20);

        u8 curr_state = btn_read() & BTN_MASK;
        u8 pressed    = curr_state & ~prev_state; /* rising-edge bits */
        prev_state    = curr_state;

        if (pressed & BTN_POWER)
        {
            if (selected == 0)
                power_set_state(POWER_OFF_REBOOT);
            else
                power_set_state(POWER_OFF);
            return; /* unreachable */
        }

        if (pressed & (BTN_VOL_UP | BTN_VOL_DOWN))
        {
            /* Touching VOL cancels the countdown — required on non-PicoFly
               consoles where a cold reboot always ends up in stock HOS. */
            seconds_left = -1;
            selected = (pressed & BTN_VOL_UP) ? 0 : 1;
            exit_menu_draw(base_x, base_y, selected, seconds_left);
            continue;
        }

        if (seconds_left >= 0)
        {
            u32 now = get_tmr_ms();
            if (now - last_tick_ms >= 1000)
            {
                last_tick_ms = now;
                seconds_left--;
                if (seconds_left < 0)
                {
                    power_set_state(POWER_OFF_REBOOT);
                    return;
                }
                exit_menu_draw(base_x, base_y, selected, seconds_left);
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------------*/

/* Read /leaf-offline-update/.autoboot-prev as a single integer. */
static int load_saved_autoboot(int *out_value)
{
    FIL fp;
    if (f_open(&fp, AUTOBOOT_SAVED, FA_READ) != FR_OK)
        return -1;

    char buf[16];
    UINT br = 0;
    FRESULT fr = f_read(&fp, buf, sizeof(buf) - 1, &br);
    f_close(&fp);
    if (fr != FR_OK)
        return -1;
    buf[br] = '\0';

    int value = 0;
    int saw_digit = 0;
    for (UINT i = 0; i < br; i++)
    {
        char c = buf[i];
        if (c >= '0' && c <= '9')
        {
            value = value * 10 + (c - '0');
            saw_digit = 1;
        }
        else if (saw_digit)
            break;
    }
    if (!saw_digit)
        return -1;

    *out_value = value;
    return 0;
}

/* Concatenate "a/b" into out_buf, returning 0 on success / -1 if it overflows. */
static int join_path(char *out_buf, size_t out_sz, const char *a, const char *b)
{
    size_t la = strlen(a);
    size_t lb = strlen(b);
    int needs_sep = (la > 0 && a[la - 1] != '/' && a[la - 1] != ':');
    size_t total = la + (needs_sep ? 1 : 0) + lb + 1;
    if (total > out_sz)
        return -1;
    memcpy(out_buf, a, la);
    size_t p = la;
    if (needs_sep)
        out_buf[p++] = '/';
    memcpy(out_buf + p, b, lb);
    out_buf[p + lb] = '\0';
    return 0;
}

/*
 * Recursively move every child of `src` into `dst`, merging directories and
 * overwriting clashing files. Leaves `src` empty (caller does the final
 * f_unlink on it).
 *
 * Important: `src` and `dst` live on the same FATFS volume, so f_rename is a
 * metadata op (cheap) — we use it whenever the destination doesn't exist
 * yet. When the destination *does* exist (a directory we're merging into),
 * we recurse so we don't clobber its contents.
 */
static int merge_move_dir(const char *src, const char *dst)
{
    DIR dp;
    FRESULT fr = f_opendir(&dp, src);
    if (fr != FR_OK)
    {
        gfx_printf("%kopendir(%s) failed: %d%k\n", 0xFFFF8080, src, fr, 0xFFFFFFFF);
        return -1;
    }

    char child_src[FF_MAX_LFN + 1];
    char child_dst[FF_MAX_LFN + 1];
    FILINFO fi;

    for (;;)
    {
        fr = f_readdir(&dp, &fi);
        if (fr != FR_OK || fi.fname[0] == '\0')
            break;

        if (join_path(child_src, sizeof(child_src), src, fi.fname) ||
            join_path(child_dst, sizeof(child_dst), dst, fi.fname))
        {
            gfx_printf("%kpath too long, skipping %s%k\n",
                       0xFFFF8080, fi.fname, 0xFFFFFFFF);
            continue;
        }

        if (fi.fattrib & AM_DIR)
        {
            FILINFO dst_fi;
            if (f_stat(child_dst, &dst_fi) == FR_OK && (dst_fi.fattrib & AM_DIR))
            {
                /* Destination dir exists — recurse to merge. */
                if (merge_move_dir(child_src, child_dst) != 0)
                {
                    f_closedir(&dp);
                    return -1;
                }
                f_unlink(child_src); /* now-empty source dir */
            }
            else
            {
                /* Destination doesn't exist — single rename is enough. */
                f_unlink(child_dst); /* in case it's a stale file with this name */
                fr = f_rename(child_src, child_dst);
                if (fr != FR_OK)
                {
                    gfx_printf("%krename dir failed (%d): %s -> %s%k\n",
                               0xFFFF8080, fr, child_src, child_dst, 0xFFFFFFFF);
                    f_closedir(&dp);
                    return -1;
                }
                g_dirs_moved++;
            }
        }
        else
        {
            f_unlink(child_dst); /* harmless if absent */
            fr = f_rename(child_src, child_dst);
            if (fr != FR_OK)
            {
                gfx_printf("%krename file failed (%d): %s -> %s%k\n",
                           0xFFFF8080, fr, child_src, child_dst, 0xFFFFFFFF);
                f_closedir(&dp);
                return -1;
            }
            g_files_moved++;
            if ((g_files_moved & 0x1F) == 0)
                gfx_printf("  [%d] %s\n", (int)g_files_moved, child_dst);
        }
    }

    f_closedir(&dp);
    return 0;
}

/*
 * Rewrite the autoboot= line under [config] in hekate_ipl.ini. Preserves
 * everything else in the file (comments, whitespace, other entries).
 */
static int rewrite_autoboot(int new_value)
{
    FIL fp;
    FILINFO fi;
    if (f_stat(HEKATE_INI, &fi) != FR_OK)
        return -1;
    if (fi.fsize > 64 * 1024) /* sanity cap */
        return -1;

    char *buf = (char *)malloc((u32)fi.fsize + 64);
    if (!buf)
        return -1;

    if (f_open(&fp, HEKATE_INI, FA_READ) != FR_OK)
    {
        free(buf);
        return -1;
    }
    UINT br = 0;
    f_read(&fp, buf, (UINT)fi.fsize, &br);
    f_close(&fp);

    /* Locate "autoboot=" and rewrite the trailing digits. We don't bother
     * with a real ini parser — the file is short and the key is unique. */
    char *p = buf;
    char *end = buf + br;
    char *autoboot = NULL;
    while (p < end)
    {
        if ((p == buf || p[-1] == '\n') &&
            (end - p) >= 9 && memcmp(p, "autoboot=", 9) == 0)
        {
            autoboot = p + 9;
            break;
        }
        p++;
    }
    if (!autoboot)
    {
        free(buf);
        return -1;
    }

    /* Find end of the existing digits (stop at newline / CR / space). */
    char *digits_end = autoboot;
    while (digits_end < end && *digits_end != '\n' && *digits_end != '\r' &&
           *digits_end != ' ' && *digits_end != '\t')
        digits_end++;

    /* Format the new value (1-3 digits is plenty for ini entry indexes). */
    char repl[8];
    int n = 0;
    int v = new_value;
    if (v < 0) v = 0;
    if (v == 0)
        repl[n++] = '0';
    else
    {
        char tmp[8];
        int t = 0;
        while (v > 0 && t < (int)sizeof(tmp))
        {
            tmp[t++] = '0' + (v % 10);
            v /= 10;
        }
        while (t > 0)
            repl[n++] = tmp[--t];
    }

    /* Splice: head | repl | tail. We allocated +64 slack so writing repl
     * never overflows even if it's longer than the original digits. */
    size_t head_len = autoboot - buf;
    size_t tail_len = end - digits_end;
    memmove(autoboot + n, digits_end, tail_len);
    memcpy(autoboot, repl, n);
    UINT new_size = (UINT)(head_len + n + tail_len);

    /* Write back. */
    if (f_open(&fp, HEKATE_INI, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    {
        free(buf);
        return -1;
    }
    UINT bw = 0;
    FRESULT fr = f_write(&fp, buf, new_size, &bw);
    f_close(&fp);
    free(buf);
    return (fr == FR_OK && bw == new_size) ? 0 : -1;
}

static void halt_with_error(const char *msg)
{
    gfx_printf("\n%k%s%k\n", 0xFFFF8080, msg, 0xFFFFFFFF);
    gfx_printf("Press POWER to reboot.\n");
    while (!(btn_read() & BTN_POWER))
        msleep(50);
    power_set_state(POWER_OFF_REBOOT);
    while (1) { }
}
