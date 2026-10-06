# MediaTek hostapd patches vs. this repo (main @ f8639fa)

Source: `mediatek/mtk-openwrt-feeds` @ `0320b8d0`,
`autobuild/unified/filogic/mac80211/25.12/files/package/network/services/hostapd/patches` (289 patches).
Per-patch results are in [`analysis.csv`](analysis.csv). This branch holds the 86 patches that port cleanly.

## Verdict

**The series cannot be applied to this repo as-is, and a full port needs real engineering.**
A mechanical port yields 86 of 289 patches (compiles for aarch64, nothing runtime-tested).
The remaining hardware-specific functionality is blocked by five things:

| # | Blocker | Affected patches |
|---|---------|------------------|
| 1 | **nl80211 ABI collision** between MediaTek's kernel header and main's newer upstream header | 0015 + every patch using private ids (ATTLM/TTLM, critical update, TSF offset, broadcast TWT, DFS state update, MTK STA vendor attr) |
| 2 | **Series is layered on OpenWrt's package patches**, not on upstream: 0009/0010 add ubus/ucode/apup/mbedTLS glue that ~140 later patches' context depends on | 0009, 0010 and everything touching `ucode.c`, `ubus.c`, `apup.c`, plus many `ctrl_iface.c`/`hostapd.c` hunks |
| 3 | **Main is 1,245 upstream commits newer** than the series' base, including its own MLO work | 165 patches conflict |
| 4 | **Upstream already contains overlapping features**: AFC (daemon, client, TPE, JSON double, tests), WDS in AP MLD, `LINK_REMOVE`/link-add primitives | 0011-0014, 0235, 0252, 0256-0260, 0065/0101/0103, 0090/0095/0223/0226 |
| 5 | **Kernel and firmware dependencies** (below) | ~145 patches |

## Which upstream base the series needs

The series applies cleanly (all 289) to 27 upstream commits with author dates 2026-02-11 to 2026-03-06. The nearest to main is
`7dd0c04d` ("Make parsing of MIC element from IE buffer more generic"), which is 1,245 commits behind main.
Patches 0001-0008 revert upstream MLD commits dated 2026-01-26 or earlier, so the base must be after those.
Some commits inside that window fail (for example `64a5ce5e` stops at patch 0013, whose `hostapd/defconfig` hunk collides with an upstream addition),
so the range is not contiguous. Everything newer than the 27 working bases was not individually characterised beyond main HEAD.

Applied to main HEAD directly: patch 0001 applies, 0002 fails; 92 of 289 apply independently.
Taken alone against main: 95 clean / 194 conflict.
A whole-series 3-way merge conflicts in 49 files / 257 hunks (worst: `src/ap/afc.c` 66, `tests/hwsim/test_afc.py` 22,
`nl80211_copy.h` 15, `ieee802_11.c` 14, `hostapd/config_file.c` 13).
Cherry-picking with "series wins" on conflicts gives 235 clean + 54 forced, but the result does not build
(`hostapd/Makefile` ends up with unbalanced `endif`s), so forced resolution is not an option.

## Blocker 1: nl80211 numeric ABI

hostapd must use the same numeric values as the kernel it talks to. MediaTek's mac80211 (wireless-next 2026-02-04
backport + 142 patches) adds private commands/attributes in slots that current upstream has since assigned to
other entries. 22 collide, for example:

| MediaTek name | value | main uses this value for |
|---|---|---|
| `NL80211_CMD_ATTLM_EVENT` | 160 | `NL80211_CMD_INCUMBENT_SIGNAL_DETECT` |
| `NL80211_CMD_SET_ATTLM` | 161 | `NL80211_CMD_NAN_SET_LOCAL_SCHED` |
| `NL80211_CMD_SET_STA_TTLM` | 162 | `NL80211_CMD_NAN_SCHED_UPDATE_DONE` |
| `NL80211_CMD_NOTIFY_CRIT_UPDATE` | 163 | `NL80211_CMD_NAN_SET_PEER_SCHED` |
| `NL80211_CMD_TSF_OFFSET_EVENT` | 164 | `NL80211_CMD_NAN_ULW_UPDATE` |
| `NL80211_CMD_ADD/DEL_BROADCAST_TWT` | 165/166 | `NL80211_CMD_NAN_CHANNEL_EVAC` / `NL80211_CMD_START_PD` |
| `NL80211_ATTR_CNTDWN_OFFS_STA_PROF` | 349 | `NL80211_ATTR_INCUMBENT_SIGNAL_INTERFERENCE_BITMAP` |
| `NL80211_ATTR_MLO_LINK_DISABLED_BMP` | 350 | `NL80211_ATTR_UHR_OPERATION` |
| `NL80211_ATTR_MLO_ATTLM_*` | 351-354 | NAN channel/time-slot/RX-NSS attrs |
| `NL80211_ATTR_CRTI_UPDATE_EVENT`, `MLO_TSF_OFFSET_VAL`, `VENDOR_MTK_STA`, `DFS_STATE_UPDATE`, `BROADCAST_TWT_PARAMS` | 355-359 | NAN attrs |

Patch 0015 would replace main's header with MediaTek's, dropping 106 upstream identifiers that main's code uses.
Keeping main's header and appending the private ids makes hostapd send wrong ids to the MediaTek kernel.
**This needs a decision, not a merge**: (a) rebase MediaTek's kernel patches onto a newer wireless-next so the private ids move to free slots
(then hostapd can follow main's header), or (b) pin hostapd to the kernel's header and `#ifdef` out main's newer-only code.
Vendor commands (OUI `0x0ce7`, `src/common/mtk_vendor.h`) are not affected: they ride on `NL80211_CMD_VENDOR`.
Patch 0015 was therefore not ported.

## Kernel and firmware dependencies

Kernel base in the MediaTek feed: mac80211 = backports bump to wireless-next 2026-02-04 + 142 MediaTek patches
(`package/kernel/mac80211/patches/subsys`); mt76 = 124 patches (`package/kernel/mt76/patches`); target kernel patches are for 6.12.
Evidence levels: **direct** = the hostapd patch uses an identifier that only the named kernel patch defines;
**feature** = paired by feature (commit messages / names), no new identifier, so runtime behaviour depends on it. Feature pairings are my judgement and should be reviewed.

| Feature (hostapd patches) | mac80211 patches | mt76 patches | Evidence |
|---|---|---|---|
| MTK vendor control: EDCCA, MU, 3-wire PTA, iBF, AMSDU, BSS color, background radar, beacon, CSI, air monitor, CAPI, MURU, RFEATURE (0018-0022, 0025, 0030, 0032-0035, 0052-0054, 0070, 0089, 0092, 0098, 0121, 0131, 0164, 0165, 0180, 0284) | n/a | 0053 (`vendor.c/.h`), 0099 (mt7915/connac2) | direct |
| Txpower, AFC/LPI power, singlesku (0097, 0100, 0146, 0255) | 0113 | 0058, 0059, 0087 | direct |
| EPCS (0140) | 0134, 0135 | 0061 (+ firmware) | direct (vendor `EPCS_CTRL`) |
| EMLSR (0077, 0162, 0237) | 0124, 0134, 0135 | 0077 | feature |
| A-TTLM / Neg-TTLM (0096, 0126-0129, 0167, 0184, 0190, 0191, 0228-0231, 0274, 0276, 0280-0282, 0287, 0288) | 0071, 0073, 0084, 0085, 0087 | 0073 | direct (private nl80211 ids) |
| Critical update / per-STA-profile CSA (0076, 0102, 0105, 0122, 0141) | 0057, 0075, 0099, 0101, 0107 | 0055 | direct |
| TSF offset (0110) | 0076 | 0060 | direct |
| Broadcast TWT (0207, 0208, 0239, 0245, 0251) | 0114-0117, 0119-0121, 0125-0127, 0129, 0130, 0132 | 0070, 0078, 0079 | direct |
| MTK vendor IE / MTK STA (0185, 0186) | 0105, 0106 | n/a | direct |
| DFS state update / offchannel CAC (0028, 0029, 0051, 0120, 0200) | 0023, 0024, 0025, 0026, 0030, 0108, 0112, 0133 | 0019, 0056 | direct (0028, 0051, 0200), feature (rest) |
| Background radar / ZWDFS (0031, 0037, 0052, 0125, 0249, 0261, 0262, 0265) | 0025, 0026, 0030 | 0019, 0053, 0056, 0103 | feature |
| MLO radar, WDS-MLO, beacon-enable ordering, probe client (0064, 0065, 0072, 0079, 0101, 0103) | 0041-0042, 0045-0048, 0055, 0059, 0060, 0079, 0118 | 0019, 0056 | feature |
| AP-MLD link removal/add (0090, 0095, 0222, 0223, 0225, 0226, 0233, 0244) | 0070 | 0072 | feature |
| Preamble puncture / PP (0053, 0071, 0092, 0175, 0217) | 0069 | 0050, 0053, 0094 | direct (vendor `PP_CTRL`) |
| BSS color (0164) | 0020, 0090 | 0017, 0053 | direct (vendor `BSS_COLOR_CTRL`) |
| FT over MLD, 802.1X AP MLD (0117, 0134-0137, 0157, 0158, 0161, 0166, 0169, 0171, 0209, 0227, 0270-0273) | 0040, 0054, 0056 | 0085, 0086 | feature (mostly userspace) |
| MSCS/SCS (0150, 0156, 0243, 0266, 0269) | 0072, 0088 | n/a | **also needs a separate MediaTek QoS-management kernel module and MSCS daemon over private netlink** (not mac80211/mt76) |
| wpa_supplicant MLO STA (0059, 0060, 0063, 0073, 0080, 0128, 0138, 0145, 0178, 0179, 0187, 0210-0212, 0219, 0220, 0234, 0250, 0283, 0285) | 0038, 0068, 0073 | n/a | feature |

115 patches are pure userspace (config options, IE parsing, bug fixes, robustness fixes) and need nothing from the kernel
(`analysis.csv`, category `USERSPACE-ONLY`).

## Per-category result

| Category | Patches | Ported here |
|---|---|---|
| USERSPACE-ONLY | 115 | 55 |
| KERNEL-FEATURE | 85 | 28 |
| KERNEL-DIRECT | 60 | 3 (`mtk_vendor.h`, TTLM struct move, wpa_s Neg-TTLM setup) |
| AFC-UPSTREAMED | 11 | 0 (main has AFC upstream) |
| REVERT (0001-0008) | 8 | 0 |
| OPENWRT-GLUE (0009, 0010) | 2 | 0 |
| OPENWRT-UCODE/UBUS | 7 | 0 |
| NL80211-HEADER (0015) | 1 | 0 |

Other reasons patches were left out:
* **0001-0008** revert upstream MLD commits (BSS ML parsing, VLAN group SM, MLO link KDE) that main's later code builds on. They look like workarounds for the MediaTek STA/MLO flow of that kernel snapshot; not appropriate for main.
* **0009/0010** are OpenWrt packaging: ubus/ucode/uloop, `apup.c`, RADIUS DAS changes, and an mbedTLS crypto/TLS backend (~7,500 lines, `crypto_mbedtls.c`/`tls_mbedtls.c`, which main does not have). None of it is MediaTek hardware support, but later MediaTek patches rely on its context.
* **AFC set** is an older revision of work already upstream by the same contributors.
* **16 more patches** merged cleanly but were dropped because they reference code from patches that did not port (0029, 0075, 0107, 0134, 0135, 0137, 0154, 0158, 0182, 0201, 0204, 0206, 0209, 0211, 0270, 0286).

## What was built and tested

* `git am` of the whole series on `7dd0c04d`: 289/289 clean.
* This branch = main + 86 cherry-picked series commits (original authorship kept), no hand edits.
* Cross-compiled for aarch64 (GCC 13): `hostapd` with the `.config` supplied in this task and `wpa_supplicant` with `defconfig` + `CONFIG_LIBNL32`; both build with no errors.
* Not done: no runtime tests, no hwsim tests, no mt76/mac80211 build, no hardware. The OpenWrt-only code (ubus/ucode) was not built.

## Suggested path

1. Decide the nl80211 ABI strategy (blocker 1); everything vendor/MLO-specific depends on it.
2. Take 0001-0010 out of scope. Re-derive the few things later patches need from them (hunks in `hostapd.c`, `ctrl_iface.c`, `ieee802_11.c`).
3. Drop the AFC set and WDS-MLO/link-removal patches in favour of upstream, then port the MediaTek deltas on top (for example 0252's AFC info).
4. Port the vendor-command patches as a group together with `mtk_vendor.h`; they only work with mt76 patch 0053 and later vendor additions.
5. Port TTLM/EPCS/TWT/critical-update only with their kernel patches in place.
6. Run hwsim against a kernel/driver combination that matches.
