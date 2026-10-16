# DAW backup association rules

Verified against vendor documentation on 2026-10-15. Implemented centrally in
`ProjectIdentity::preferredProjectFile`, used by discovery, identity/name/hash
lookup, stored preview repair, manual association and Open in DAW validation.

| DAW | Project format | Backup location or format |
| --- | --- | --- |
| FL Studio | `.flp` | `Backup` in the project data folder; also a configurable global backup location |
| Ableton Live | `.als` | `Backup` in the Project; timestamped Sets. `Bakup` is also accepted as a user-requested spelling. |
| Studio One | `.song` | `History`, containing autosaves and explicitly saved historical versions |
| Pro Tools | `.ptx` | `Session File Backups` |
| Bitwig Studio | `.bwproject` | `auto-backup`, including the nested `versions` folder |
| REAPER | `.rpp` | `.rpp-bak`; `Backups`, `Auto-Saves` / `AutoSaves` subdirectories and configurable additional locations |
| Cubase / Nuendo | `.cpr` / `.npr` | `.bak`; current Cubase documents `Auto Saves` in the project folder |
| Logic Pro | package-based project | Backups and alternatives belong to the project; they are not separate ordinary project candidates in the current inventory. |

Folder rules are case insensitive and specific to the project extension. A backup
subtree is excluded when the parent of its known backup folder contains a primary
of the same format. This also works when the search starts inside the backup folder.
A historical or explicit backup path redirects only when there is exactly one
primary; with several primaries it remains unresolved. Regular saved versions
outside these folders remain eligible. No timestamp or generic substring heuristic
is used to guess that an arbitrary project is a backup.

Backup-only `.bak`, `.rpp-bak` and `.autosave` formats are rejected even through
direct host paths or existing metadata, which otherwise bypass extension indexing.
Standalone folders with no primary retain their previous behavior. Arbitrary
user-configured backup locations cannot be reliably distinguished from ordinary
project libraries by folder name alone. Restoring a copy outside the known backup
subtree makes it eligible as an ordinary project.

Sources:

- [FL Studio file settings](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/envsettings_files.htm)
- [Ableton Backup Sets](https://help.ableton.com/hc/en-us/articles/360000377870-Backup-Sets)
- [Studio One save options](https://support.presonus.com/hc/en-us/articles/360013508792-Exploring-the-Save-Options-of-Studio-One)
- [Pro Tools Reference Guide, Session File Backups Folder](https://cdn.avid.com/ProTools/2021.3/A6F1378B/Pro%20Tools%20Reference%20Guide.pdf)
- [Bitwig 6.0 release notes, Project Backups on Update](https://downloads.bitwig.com/6.0/Release-Notes-6.0.pdf)
- [REAPER User Guide, Project Backups](https://dlz.reaper.fm/userguide/ReaperUserGuide729d.pdf)
- [REAPER release history, Backups/AutoSaves relative path handling](https://www.reaper.fm/download-old.php)
- [Cubase Auto Save](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/project_handling/project_handling_about_the_auto_save_option_c.html)
- [Logic Pro alternatives and backups](https://support.apple.com/guide/logicpro/use-project-alternatives-and-backups-lgcpa158ef77/mac)
