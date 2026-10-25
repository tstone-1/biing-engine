# Edition measurements

Editable input for `tools/engine_editions.py`. These are measured file identities,
address mappings and role labels, not executable code or game assets.
Measurements date from 2026-10-20; the English scheduled tick-39 mapping was
updated on 2026-10-24. Section numbers are retained for generator compatibility.

Run `python3 -I tools/engine_editions.py --check` to verify the generated header.
Edit these tables and run the same command without `--check` to regenerate it.

### 3.2 The segment maps

**German CD** (base `0x11C0`):

| Disk offsets | German CD offsets | CD - disk | Differing bytes inside |
|---|---|---:|---:|
| `0`-`4607` | `0`-`4607` | +`0` | 4 |
| `4607`-`4614` | `464B`-`4658` | +`44` |  |
| `4614`-`AFF0` | `4665`-`B041` | +`51` | 60 |
| `AFF0`-`B124` | `B045`-`B179` | +`55` |  |
| `B124`-`B292` | `B17B`-`B2E9` | +`57` |  |
| `B292`-`B366` | `B2F3`-`B3C7` | +`61` | 1 |
| `B366`-`B621` | `B3DD`-`B698` | +`77` |  |
| `B621`-`B622` | - | - | **1 B differ** |
| `B622`-`B9CA` | `B698`-`BA40` | +`76` |  |
| `B9CA`-`C8EF` | `BD78`-`CC9D` | +`3AE` |  |
| `C8EF`-`C90D` | `CCB1`-`CCCF` | +`3C2` |  |
| `C90D`-`C93E` | - | - | **49 B differ** |
| `C93E`-`E0CE` | `CD58`-`E4E8` | +`41A` |  |
| `E0CE`-`E312` | `E4EA`-`E72E` | +`41C` |  |
| `E312`-`E4FA` | - | - | **488 B differ** |
| `E4FA`-`12ED4` | `E9E9`-`133C3` | +`4EF` |  |
| `12ED4`-`194D7` | `133F9`-`199FC` | +`525` | 6 |
| `194D7`-`198D4` | - | - | **1021 B differ** |
| `198D4`-`19F1D` | `19E1B`-`1A464` | +`547` |  |
| `19F1D`-`19F1E` | - | - | **1 B differ** |
| `19F1E`-`1AE49` | `1A462`-`1B38D` | +`544` |  |
| `1AE49`-`2026A` | `1B3CF`-`207F0` | +`586` |  |
| `2026A`-`20A02` | - | - | **1944 B differ** |
| `20A02`-`2B6D4` | `210A0`-`2BD72` | +`69E` | 1 |
| `2B6D4`-`2B6F5` | - | - | **33 B differ** |
| `2B6F5`-`2B714` | `2BD83`-`2BDA2` | +`68E` |  |
| `2B714`-`2B71A` | - | - | **6 B differ** |
| `2B71A`-`2EB23` | `2BDBA`-`2F1C3` | +`6A0` |  |
| `2EB23`-`2EE23` | `2F1C4`-`2F4C4` | +`6A1` |  |
| `2EE23`-`35A74` | `2F4C5`-`36116` | +`6A2` |  |
| `35A74`-`3B02E` | `36120`-`3B6DA` | +`6AC` |  |
| `3B02E`-`3CE4C` | `3B6F9`-`3D517` | +`6CB` |  |
| `3CE4C`-`3CE54` | - | - | **8 B differ** |
| `3CE54`-`43271` | `3D529`-`43946` | +`6D5` |  |
| `43271`-`43282` | - | - | **17 B differ** |
| `43282`-`49AAA` | `43958`-`4A180` | +`6D6` |  |
| `49AAA`-`49AAE` | - | - | **4 B differ** |
| `49AAE`-`629E4` | `4A186`-`630BC` | +`6D8` | 2 |
| `629E4`-`62D14` | `63204`-`63534` | +`820` |  |
| `62D14`-`62D34` | - | - | **32 B differ** |
| `62D34`-`6F2A4` | `63567`-`6FAD7` | +`833` | 8 |
| `6F2A4`-`6F2D6` | - | - | **50 B differ** |
| `6F2D6`-`70360` | `6FB37`-`70BC1` | +`861` |  |
| `70360`-`7350A` | `70BC0`-`73D6A` | +`860` |  |

**English CD** (base `0x1130`):

| Disk offsets | English CD offsets | CD - disk | Differing bytes inside |
|---|---|---:|---:|
| `0`-`822` | `0`-`822` | +`0` | 6 |
| `822`-`826` | - | - | **4 B differ** |
| `826`-`949` | `823`-`946` | -`3` |  |
| `949`-`3BCF` | `949`-`3BCF` | +`0` | 729 (room catalog names, translated) |
| `3BCF`-`3DA4` | `3BCF`-`3DA4` | +`0` | hospital names, translated (the tool shows three accidental short matches here, section 2) |
| `3DA4`-`4607` | `3DA4`-`4607` | +`0` | 21 |
| `4607`-`461B` | `464B`-`465F` | +`44` |  |
| `461B`-`464F` | - | - | **52 B differ** |
| `464F`-`6DED` | `46A1`-`6E3F` | +`52` | 59 |
| `6DED`-`A3F5` | - | - | **13832 B differ**: translated strings (activity, mood and other sheet texts), reached through pointer tables |
| `A3F5`-`A769` | `9EA7`-`A21B` | -`54E` |  |
| `A769`-`A775` | - | - | **12 B differ** |
| `A775`-`A96C` | `A222`-`A419` | -`553` |  |
| `A96C`-`AD8F` | - | - | **1059 B differ** (recipe names) |
| `AD8F`-`AFF0` | `A7C3`-`AA24` | -`5CC` |  |
| `AFF0`-`B124` | `AA28`-`AB5C` | -`5C8` |  |
| `B124`-`B366` | `AB5E`-`ADA0` | -`5C6` | 1 |
| `B366`-`B3A2` | `ADB6`-`ADF2` | -`5B0` |  |
| `B3A2`-`B444` | - | - | **162 B differ** |
| `B444`-`B526` | `AE6B`-`AF4D` | -`5D9` |  |
| `B526`-`B53D` | - | - | **23 B differ** |
| `B53D`-`C8EF` | `AF5F`-`C311` | -`5DE` |  |
| `C8EF`-`C90D` | `C325`-`C343` | -`5CA` |  |
| `C90D`-`C93E` | - | - | **49 B differ** |
| `C93E`-`E312` | `C3CC`-`DDA0` | -`572` |  |
| `E312`-`E4FC` | - | - | **490 B differ** |
| `E4FC`-`E519` | `DFED`-`E00A` | -`50F` |  |
| `E519`-`E58E` | - | - | **117 B differ** (the nurse strings) |
| `E58E`-`FF35` | `E00A`-`F9B1` | -`584` | 4 |
| `FF35`-`FF3A` | - | - | **5 B differ** |
| `FF3A`-`12ED4` | `F980`-`1291A` | -`5BA` |  |
| `12ED4`-`194D7` | `12950`-`18F53` | -`584` | 6 |
| `194D7`-`198D4` | - | - | **1021 B differ** |
| `198D4`-`19C94` | `19372`-`19732` | -`562` |  |
| `19C94`-`19CBE` | - | - | **42 B differ** |
| `19CBE`-`1A963` | `19732`-`1A3D7` | -`58C` |  |
| `1A963`-`2026A` | `1A3D9`-`1FCE0` | -`58A` |  |
| `2026A`-`20A02` | - | - | **1944 B differ** |
| `20A02`-`2312D` | `20590`-`22CBB` | -`472` |  |
| `2312D`-`23144` | - | - | **23 B differ** |
| `23144`-`2674E` | `22CD4`-`262DE` | -`470` | 1 |
| `2674E`-`27D05` | `262E4`-`2789B` | -`46A` |  |
| `27D05`-`27D5E` | - | - | **89 B differ** |
| `27D5E`-`28763` | `2797D`-`28382` | -`3E1` |  |
| `28763`-`28777` | - | - | **20 B differ** |
| `28777`-`2B494` | `2838E`-`2B0AB` | -`3E9` | 1 |
| `2B494`-`2B498` | - | - | **4 B differ** |
| `2B498`-`2B6EF` | `2B0B1`-`2B308` | -`3E7` | 1 |
| `2B6EF`-`2B6F5` | - | - | **6 B differ** |
| `2B6F5`-`2CAFA` | `2B310`-`2C715` | -`3E5` | 1 |
| `2CAFA`-`2CB2C` | - | - | **50 B differ** |
| `2CB2C`-`2F702` | `2C735`-`2F30B` | -`3F7` |  |
| `2F702`-`2F707` | - | - | **5 B differ** |
| `2F707`-`2F7F4` | `2F318`-`2F405` | -`3EF` |  |
| `2F7F4`-`2F7F9` | - | - | **5 B differ** |
| `2F7F9`-`35A74` | `2F412`-`3568D` | -`3E7` |  |
| `35A74`-`380D0` | `35697`-`37CF3` | -`3DD` |  |
| `380D0`-`381DF` | `37CFD`-`37E0C` | -`3D3` |  |
| `381DF`-`3928B` | `37E16`-`38EC2` | -`3C9` |  |
| `3928B`-`39347` | - | - | **188 B differ** |
| `39347`-`3A1BD` | `38F92`-`39E08` | -`3B5` | 22 |
| `3A1BD`-`3A1E6` | - | - | **41 B differ** |
| `3A1E6`-`3B4C2` | `39E1E`-`3B0FA` | -`3C8` |  |
| `3B4C2`-`3B4DF` | - | - | **29 B differ** |
| `3B4DF`-`3B510` | `3B121`-`3B152` | -`3BE` |  |
| `3B510`-`3B541` | - | - | **49 B differ** |
| `3B541`-`3B568` | `3B18D`-`3B1B4` | -`3B4` |  |
| `3B568`-`3B572` | - | - | **10 B differ** |
| `3B572`-`3B5A3` | `3B1D0`-`3B201` | -`3A2` |  |
| `3B5A3`-`3B5D4` | `3B206`-`3B237` | -`39D` |  |
| `3B5D4`-`3CE4C` | `3B23C`-`3CAB4` | -`398` |  |
| `3CE4C`-`3CE54` | - | - | **8 B differ** |
| `3CE54`-`3F667` | `3CAC6`-`3F2D9` | -`38E` |  |
| `3F667`-`3F80C` | - | - | **421 B differ** |
| `3F80C`-`40F02` | `3F46F`-`40B65` | -`39D` |  |
| `40F02`-`411C8` | `40B75`-`40E3B` | -`38D` |  |
| `411C8`-`411CD` | - | - | **5 B differ** |
| `411CD`-`42C18` | `40E3B`-`42886` | -`392` |  |
| `42C18`-`43270` | `42898`-`42EF0` | -`380` |  |
| `43270`-`4343A` | `42EF1`-`430BB` | -`37F` |  |
| `4343A`-`4348E` | - | - | **84 B differ** |
| `4348E`-`4351A` | `43112`-`4319E` | -`37C` |  |
| `4351A`-`4351D` | - | - | **3 B differ** |
| `4351D`-`45C48` | `431A9`-`458D4` | -`374` |  |
| `45C48`-`45C4D` | - | - | **5 B differ** |
| `45C4D`-`47451` | `458D6`-`470DA` | -`377` |  |
| `47451`-`47458` | - | - | **7 B differ** |
| `47458`-`4747C` | `470C5`-`470E9` | -`393` |  |
| `4747C`-`4747F` | - | - | **3 B differ** |
| `4747F`-`487F5` | `47108`-`4847E` | -`377` |  |
| `487F5`-`487F9` | - | - | **4 B differ** |
| `487F9`-`48949` | `4848A`-`485DA` | -`36F` |  |
| `48949`-`4894D` | - | - | **4 B differ** |
| `4894D`-`4A359` | `485E6`-`49FF2` | -`367` |  |
| `4A359`-`4C5E2` | `49FF3`-`4C27C` | -`366` |  |
| `4C5E2`-`4C5E6` | - | - | **4 B differ** |
| `4C5E6`-`4DD61` | `4C282`-`4D9FD` | -`364` |  |
| `4DD61`-`4DD73` | - | - | **18 B differ** |
| `4DD73`-`4E3F4` | `4DA04`-`4E085` | -`36F` |  |
| `4E3F4`-`4E400` | - | - | **12 B differ** |
| `4E400`-`4F057` | `4E083`-`4ECDA` | -`37D` |  |
| `4F057`-`4F05B` | - | - | **4 B differ** |
| `4F05B`-`50F21` | `4ECE0`-`50BA6` | -`37B` |  |
| `50F21`-`5B0F5` | `50BA7`-`5AD7B` | -`37A` | 9 |
| `5B0F5`-`5B11E` | - | - | **41 B differ** |
| `5B11E`-`5B125` | `5AB0E`-`5AB15` | -`610` |  |
| `5B125`-`62888` | `5ADAB`-`6250E` | -`37A` |  |
| `62888`-`64ECC` | `6250D`-`64B51` | -`37B` |  |
| `64ECC`-`64EE5` | - | - | **25 B differ** |
| `64EE5`-`6516F` | `64B5C`-`64DE6` | -`389` |  |
| `6516F`-`6523E` | - | - | **207 B differ** |
| `6523E`-`65A20` | `64E4C`-`6562E` | -`3F2` |  |
| `65A20`-`65B11` | - | - | **241 B differ** |
| `65B11`-`6F706` | `6571F`-`6F314` | -`3F2` |  |
| `6F706`-`6F77D` | - | - | **119 B differ** |
| `6F77D`-`7035F` | `6F357`-`6FF39` | -`426` |  |
| `7035F`-`70B3E` | `6FF3F`-`7071E` | -`420` |  |
| `70B3E`-`70CF1` | `70764`-`70917` | -`3DA` |  |
| `70CF1`-`71B04` | `70924`-`71737` | -`3CD` |  |
| `71B04`-`71B27` | - | - | **35 B differ** |
| `71B27`-`71CA0` | `71773`-`718EC` | -`3B4` |  |
| `71CA0`-`71CA2` | - | - | **2 B differ** |
| `71CA2`-`71D9E` | `718F8`-`719F4` | -`3AA` | 1 |
| `71D9E`-`71DF9` | - | - | **91 B differ** |
| `71DF9`-`7205D` | `71A84`-`71CE8` | -`375` |  |
| `7205D`-`72082` | - | - | **37 B differ** |
| `72082`-`721A1` | `71D15`-`71E34` | -`36D` |  |
| `721A1`-`721B1` | - | - | **16 B differ** |
| `721B1`-`72922` | `71E50`-`725C1` | -`361` |  |
| `72922`-`729BA` | - | - | **152 B differ** |
| `729BA`-`72AB2` | `72659`-`72751` | -`361` |  |
| `72AB2`-`72B99` | - | - | **231 B differ** |
| `72B99`-`7350A` | `728DA`-`7324B` | -`2BF` |  |

### 3.5 The engine's build table

| Build | File size | CRC-32 | Base | Texts | Data | Segments | Heap |
|---|---:|---|---|---|---|---|---|
| Disk version 1.4 | 476874 | `BA2F1461` | `11C0` | `BITEXPZ` | `BIDATP` | identity | `0` |
| GOG German CD | 479018 | `6588520E` | `11C0` | `BITEXPZ` | `BIDATP` | German CD | `0` |
| GOG English CD | 476027 | `C5D8E577` | `1130` | `BITEXPE` | `BIDATPE` | English CD | `90` |

| Build | Disk | Build offset | Table |
|---|---|---|---|
| GOG English CD | `875B` | `844E` | ACT_0 (sheet) |
| GOG English CD | `8917` | `860A` | MOOD_0 (sheet) |
| GOG English CD | `A769` | `A21B` | SOME_DEVICE (translated) |
| GOG English CD | `AD18` | `A74C` | UTENSIL_NAMES (translated) |

| Build | Build routine | Disk routine | Role |
|---|---|---|---|
| GOG German CD | `207E6` | golf | golf flag (screen 42, hotspot 5) |
| GOG German CD | `207FE` | `E085` | quit "yes" (screens 47, 156, 159): disk end of program plus clearing the golf word |
| GOG German CD | `2080C` | `20260` | save confirmation (screen 155): disk routine with the `CDC` paths |
| GOG English CD | `1FCD6` | golf | golf flag (screen 42, hotspot 5) |
| GOG English CD | `1FCEE` | `E085` | quit "yes" |
| GOG English CD | `1FCFC` | `20260` | save confirmation |
| GOG English CD | `6250E` | `62888` | scheduled tick-39 role: active nurses' figure news generator, while disk/German return immediately (SIM-104; measured 2026-10-24) |

Character map check: CRC-32 of the 224 bytes at the mapped `0xB541` (codes 32-255): `2D567B33` in all three builds.
