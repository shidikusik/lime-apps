# Lime Apps

[Русский](README.md) | English

A lightweight collection of desktop applications written in C, built primarily
for **LimeOS**. Also runs on Arch/Omarchy, Debian/Ubuntu, and Fedora.
Five separate programs, system GTK 3, local data, and no background services.
This is an independent community project, not an official LimeOS component.

| Application | Features |
| --- | --- |
| **LimeCalc** | Expressions, parentheses, powers, percentages, `sqrt`, `pi`, `e`, history, and command-line calculations |
| **LimeNotes** | Notes with titles, search, and automatic saving |
| **LimeTasks** | Tasks, completion checkboxes, filters, and undo for removal |
| **LimeView** | Images, zoom, rotation, and navigation through files in a folder |
| **LimeClock** | Local time, calendar, stopwatch, and countdown timer |

LimeFiles is not included: it already exists as a separate application.

## Appearance

Each application's title bar offers four themes:

- **System** — follow the desktop preference through the desktop portal, GNOME color-scheme, or GTK.
- **Light** — light monochrome.
- **Dark** — dark monochrome.
- **Lime** — dark with a lime accent.

System is the default. The choice is saved for the collection and used the next
time any application starts. While applications are open, each selector changes
its own window's theme. System mode responds without a restart when the desktop
reports a preference change through GTK, GSettings, or the portal.

## Download and install

Packages for **x86_64** are distributed through
[GitHub Releases](https://github.com/shidikusik/lime-apps/releases/latest).
Packages are published after GitHub Actions completes its builds and checks.
Your system package manager installs the required GTK libraries.

**Arch Linux / Omarchy:**

```sh
sudo pacman -U ./lime-apps-*.pkg.tar.zst
```

**Debian / Ubuntu and compatible systems:**

```sh
sudo apt install ./lime-apps_*.deb
```

**Fedora:**

```sh
sudo dnf install ./lime-apps-*.rpm
```

The release baselines are current Arch, Ubuntu 22.04+, Debian 12+, and Fedora 43+.
CI builds the `.deb` on Ubuntu 22.04 and checks its installation and launch on
Debian 12 and Ubuntu 24.04. It separately builds and tests the RPM on Fedora 43
and the Arch package. Compatibility with derivative distributions depends on
the availability of the same system libraries. Omarchy uses the Arch package;
its desktop integration is checked locally. Source builds can target other
architectures; the first release's prebuilt packages target x86_64.

After downloading all release files into one directory, verify them:

```sh
sha256sum -c SHA256SUMS
```

The applications appear in your application menu. Commands: `lime-calc`,
`lime-notes`, `lime-tasks`, `lime-view [image]`, and `lime-clock`.

## Build from source

You need a C compiler, Make, pkg-config, GTK **3.24+**, and GLib **2.66+**, including
their development headers. Running the applications requires no WebView,
Electron, Node.js, Python, or network services.

```sh
# Arch / Omarchy
sudo pacman -S --needed base-devel gtk3 pkgconf

# Debian / Ubuntu
sudo apt install build-essential pkg-config libgtk-3-dev

# Fedora
sudo dnf install gcc make pkgconf-pkg-config gtk3-devel
```

Run the command for your distribution, then:

```sh
git clone https://github.com/shidikusik/lime-apps.git
cd lime-apps
make -j2
make test
make install PREFIX="$HOME/.local"
```

Ensure `$HOME/.local/bin` is in `PATH`. To remove this installation, run
`make uninstall PREFIX="$HOME/.local"`. For a system installation, use
`sudo make install`. Package recipes are in `packaging/`; for Arch, prefer the
checksummed `PKGBUILD` supplied with the release. Uninstallation preserves user
data.

## Shortcuts and limits

- **Calc:** Enter calculates; Esc clears. `lime-calc --eval 'sqrt(81)+2^3'` works
  without a graphical session. A percentage divides its operand by 100.
- **Notes:** Ctrl+N creates a note, Ctrl+S saves, and Ctrl+F searches.
- **Tasks:** Ctrl+N focuses the new-task field; Ctrl+S saves. Removing a task
  offers Undo.
- **View:** Ctrl+O opens an image, arrow keys navigate, Ctrl+scroll zooms, and
  F11 toggles fullscreen. Supported formats depend on the installed GdkPixbuf
  loaders; GIFs appear as still images. Rotation changes the view without
  overwriting the original file. Large images use a reduced preview; limits
  are 128 MiB per file and 64 megapixels.
- **Clock:** The timer runs while the window is open; closing it exits the
  application. Completion appears in the window and triggers a system bell
  where supported.

The first version's interface is in English. To try a theme without changing the
saved choice, run `LIME_THEME=light lime-calc`. Accepted values are `system`,
`light`, `dark`, and `lime`.

## Local data and resource use

Notes and tasks use separate files, `notes.ini` and `tasks.ini`, in
`$XDG_DATA_HOME/lime-apps/` (usually `~/.local/share/lime-apps/`). The theme setting
is stored in `$XDG_CONFIG_HOME/lime-apps/appearance.ini` (usually under
`~/.config/lime-apps/`). Saves are atomic with file permissions `0600`.
Malformed stores are not overwritten.

Each notes or tasks process holds an advisory lock in `notes.ini.lock` or
`tasks.ini.lock` while it is open. A second instance cannot edit the same store.
Close the corresponding application before manually moving or changing its
files. Storage is intended for personal, local use, with a limit of 10,000 notes
and 10,000 tasks. The notes store is limited to 32 MiB and the tasks store to
8 MiB.

GTK/GLib libraries come from the system and are not bundled into every package.
The applications do not start a browser, make network requests, collect
telemetry, or keep a process running after their windows close. The viewer holds
one decoded image, and the clock pauses updates for inactive tabs when its timer
does not need them. Actual memory use depends on the GTK theme, desktop
environment, and image being displayed.

## Checks and contributions

```sh
make test                 # calculations, storage, and versions
make smoke                # opens all windows; requires a graphical session
# CI uses a virtual X11 display:
dbus-run-session -- xvfb-run -a make smoke
```

The release workflow publishes packages only after successful build,
installation, window-launch, and removal checks. Package removal is checked for
preservation of user data. Automated checks do not replace full manual testing
on every desktop.

Author and maintainer: [Salih / @shidikusik](https://github.com/shidikusik).
Report bugs and suggestions through Issues; submit changes through Pull
Requests. Keep the code in C, the dependency list small, and the default
interface monochrome.

License: [MIT](LICENSE).
