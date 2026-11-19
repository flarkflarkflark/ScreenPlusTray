#!/bin/sh
# One-line user installer for ScreenPlusTray (screenplus-tray).
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/flarkflarkflark/ScreenPlusTray/master/install.sh | sh
#   curl -fsSL https://raw.githubusercontent.com/flarkflarkflark/ScreenPlusTray/master/install.sh | sh -s -- --uninstall

set -eu

main() {
    UNINSTALL=0
    AUTOSTART=1
    VERSION="latest"
    PREFIX="${HOME}/.local"
    FROM_FILE=""
    REPO="flarkflarkflark/ScreenPlusTray"
    ASSET="screenplus-tray-linux-x86_64.tar.gz"
    SUMS="SHA256SUMS.txt"

    while [ $# -gt 0 ]; do
        case "$1" in
            --uninstall)
                UNINSTALL=1
                shift
                ;;
            --no-autostart)
                AUTOSTART=0
                shift
                ;;
            --version)
                if [ $# -lt 2 ]; then
                    echo "screenplus-tray installer: --version requires an argument (e.g. v0.1.0)" >&2
                    exit 2
                fi
                VERSION="$2"
                shift 2
                ;;
            --version=*)
                VERSION="${1#*=}"
                shift
                ;;
            --prefix)
                if [ $# -lt 2 ]; then
                    echo "screenplus-tray installer: --prefix requires a directory argument" >&2
                    exit 2
                fi
                PREFIX="$2"
                shift 2
                ;;
            --prefix=*)
                PREFIX="${1#*=}"
                shift
                ;;
            --from-file)
                if [ $# -lt 2 ]; then
                    echo "screenplus-tray installer: --from-file requires a tarball path" >&2
                    exit 2
                fi
                FROM_FILE="$2"
                shift 2
                ;;
            --from-file=*)
                FROM_FILE="${1#*=}"
                shift
                ;;
            --help|-h)
                cat << 'EOF'
Usage: install.sh [options]

Options:
  --uninstall          Stop screenplus-tray, disable autostart, and remove installed files
  --no-autostart       Do not enable autostart at login
  --version vX.Y.Z     Install a specific release tag instead of latest
  --prefix DIR         Install prefix (default: ~/.local)
  --from-file TARBALL  Install from a local release tarball (skips download and checksum)
  -h, --help           Show this help message
EOF
                exit 0
                ;;
            *)
                echo "screenplus-tray installer: unknown option '$1' (try --help)" >&2
                exit 2
                ;;
        esac
    done

    # Normalize prefix (strip trailing slash unless root)
    case "$PREFIX" in
        /) ;;
        */) PREFIX="${PREFIX%/}" ;;
    esac

    MANIFEST_DIR="${PREFIX}/share/screenplus-tray"
    MANIFEST="${MANIFEST_DIR}/manifest.txt"
    BIN_PATH="${PREFIX}/bin/screenplus-tray"

    if [ "$UNINSTALL" -eq 1 ]; then
        do_uninstall
        return 0
    fi

    check_requirements
    do_install
}

stop_running_tray() {
    if [ -n "${SP_TRAY_NO_STOP:-}" ]; then
        return 0
    fi
    if command -v pkill >/dev/null 2>&1; then
        pkill -x screenplus-tray >/dev/null 2>&1 || true
    elif command -v killall >/dev/null 2>&1; then
        killall -q screenplus-tray >/dev/null 2>&1 || true
    fi
}

do_uninstall() {
    echo "Stopping screenplus-tray (if running)..."
    stop_running_tray

    if [ -x "$BIN_PATH" ]; then
        "$BIN_PATH" --autostart off >/dev/null 2>&1 || true
    else
        AUTOSTART_DIR="${XDG_CONFIG_HOME:-${HOME}/.config}/autostart"
        rm -f "${AUTOSTART_DIR}/screenplus-tray.desktop"
    fi

    if [ ! -f "$MANIFEST" ]; then
        echo "No manifest found at ${MANIFEST}; nothing to remove."
        return 0
    fi

    REMOVED=0
    while IFS= read -r rel || [ -n "$rel" ]; do
        case "$rel" in
            ""|\#*) continue ;;
        esac
        target="${PREFIX}/${rel}"
        if [ -f "$target" ] || [ -L "$target" ]; then
            rm -f "$target"
            REMOVED=$((REMOVED + 1))
        fi
    done < "$MANIFEST"

    rm -f "$MANIFEST"

    if command -v gtk-update-icon-cache >/dev/null 2>&1 && [ -f "${PREFIX}/share/icons/hicolor/index.theme" ]; then
        gtk-update-icon-cache -f -t "${PREFIX}/share/icons/hicolor" >/dev/null 2>&1 || true
    fi

    # Remove empty directories created by the package (deepest first), never outside PREFIX
    for d in \
        "share/screenplus-tray/icons/hicolor/256x256/apps" \
        "share/screenplus-tray/icons/hicolor/256x256" \
        "share/screenplus-tray/icons/hicolor" \
        "share/screenplus-tray/icons" \
        "share/screenplus-tray" \
        "share/icons/hicolor/256x256/apps" \
        "share/icons/hicolor/256x256" \
        "share/icons/hicolor" \
        "share/icons" \
        "share/applications" \
        "share" \
        "bin"
    do
        rmdir "${PREFIX}/${d}" >/dev/null 2>&1 || true
    done

    echo ""
    echo "ScreenPlusTray uninstalled:"
    echo "  - Stopped running tray process and removed login autostart entry"
    echo "  - Removed ${REMOVED} file(s) from ${PREFIX}"
}

check_libdbus() {
    if command -v ldconfig >/dev/null 2>&1; then
        if ldconfig -p 2>/dev/null | grep -q 'libdbus-1\.so\.3'; then
            return 0
        fi
    fi
    for p in \
        /usr/lib/libdbus-1.so.3 \
        /usr/lib64/libdbus-1.so.3 \
        /lib/libdbus-1.so.3 \
        /lib64/libdbus-1.so.3 \
        /usr/lib/x86_64-linux-gnu/libdbus-1.so.3 \
        /lib/x86_64-linux-gnu/libdbus-1.so.3
    do
        if [ -e "$p" ]; then
            return 0
        fi
    done
    return 1
}

check_requirements() {
    ARCH=$(uname -m 2>/dev/null || echo "unknown")
    if [ "$ARCH" != "x86_64" ]; then
        echo "Error: unsupported architecture '${ARCH}'. ScreenPlusTray prebuilt binaries require x86_64." >&2
        exit 1
    fi

    for tool in curl tar sha256sum; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo "Error: required command '${tool}' was not found on PATH. Please install it first." >&2
            exit 1
        fi
    done

    if ! check_libdbus; then
        echo "Error: libdbus-1.so.3 was not found on this system." >&2
        echo "Install libdbus-1-3 (Debian/Ubuntu), dbus-libs (Fedora), or dbus (Arch) and try again." >&2
        exit 1
    fi

    if ! command -v screenplus >/dev/null 2>&1; then
        echo "Error: 'screenplus' was not found on PATH." >&2
        echo "ScreenPlusTray requires Screen+ for Linux to be installed first." >&2
        echo "See installation instructions at: https://github.com/CoolNinja2009/ScreenPlus" >&2
        exit 1
    fi
}

do_install() {
    IS_UPDATE=0
    if [ -f "$MANIFEST" ] || [ -x "$BIN_PATH" ]; then
        IS_UPDATE=1
    fi

    TMPDIR_WORK=$(mktemp -d "${TMPDIR:-/tmp}/screenplus-tray-install.XXXXXX")
    trap 'rm -rf "$TMPDIR_WORK"' EXIT INT TERM HUP

    if [ -n "$FROM_FILE" ]; then
        if [ ! -f "$FROM_FILE" ]; then
            echo "Error: local tarball not found: ${FROM_FILE}" >&2
            exit 1
        fi
        echo "Using local tarball: ${FROM_FILE}"
        cp "$FROM_FILE" "${TMPDIR_WORK}/${ASSET}"
    else
        case "$VERSION" in
            latest)
                BASE_URL="https://github.com/${REPO}/releases/latest/download"
                ;;
            v*)
                BASE_URL="https://github.com/${REPO}/releases/download/${VERSION}"
                ;;
            *)
                BASE_URL="https://github.com/${REPO}/releases/download/v${VERSION}"
                ;;
        esac

        echo "Downloading ${ASSET} (${VERSION})..."
        if ! curl -fsSL "${BASE_URL}/${ASSET}" -o "${TMPDIR_WORK}/${ASSET}"; then
            echo "Error: failed to download ${BASE_URL}/${ASSET}" >&2
            exit 1
        fi
        if ! curl -fsSL "${BASE_URL}/${SUMS}" -o "${TMPDIR_WORK}/${SUMS}"; then
            echo "Error: failed to download ${BASE_URL}/${SUMS}" >&2
            exit 1
        fi

        echo "Verifying SHA-256 checksum..."
        if ! (cd "$TMPDIR_WORK" && sha256sum -c "$SUMS" >/dev/null 2>&1); then
            echo "Error: SHA-256 checksum verification failed! Aborting installation." >&2
            exit 1
        fi
    fi

    STAGE="${TMPDIR_WORK}/stage"
    mkdir -p "$STAGE"
    tar -xzf "${TMPDIR_WORK}/${ASSET}" -C "$STAGE"

    if [ ! -f "${STAGE}/bin/screenplus-tray" ]; then
        echo "Error: archive does not contain bin/screenplus-tray" >&2
        exit 1
    fi

    stop_running_tray

    mkdir -p "$PREFIX"
    mkdir -p "$MANIFEST_DIR"
    NEW_MANIFEST="${TMPDIR_WORK}/manifest.txt"
    : > "$NEW_MANIFEST"

    # Copy every file from stage into PREFIX and record relative path in manifest
    (
        cd "$STAGE"
        find . \( -type f -o -type l \) | sed 's|^\./||' | sort
    ) > "$NEW_MANIFEST"

    FILE_COUNT=0
    while IFS= read -r rel || [ -n "$rel" ]; do
        [ -z "$rel" ] && continue
        src="${STAGE}/${rel}"
        dst="${PREFIX}/${rel}"
        dst_dir=$(dirname "$dst")
        mkdir -p "$dst_dir"
        cp -f "$src" "$dst"
        case "$rel" in
            bin/*) chmod 0755 "$dst" ;;
            *)     chmod 0644 "$dst" ;;
        esac
        FILE_COUNT=$((FILE_COUNT + 1))
    done < "$NEW_MANIFEST"

    cp -f "$NEW_MANIFEST" "$MANIFEST"
    chmod 0644 "$MANIFEST"

    if command -v gtk-update-icon-cache >/dev/null 2>&1 && [ -f "${PREFIX}/share/icons/hicolor/index.theme" ]; then
        gtk-update-icon-cache -f -t "${PREFIX}/share/icons/hicolor" >/dev/null 2>&1 || true
    fi

    if [ "$AUTOSTART" -eq 1 ]; then
        "$BIN_PATH" --autostart on || true
    fi

    STARTED_MSG="not started (no D-Bus session bus in current environment)"
    if [ -n "${DBUS_SESSION_BUS_ADDRESS:-}" ] && [ -z "${SP_TRAY_NO_START:-}" ]; then
        if command -v setsid >/dev/null 2>&1; then
            setsid -f "$BIN_PATH" </dev/null >/dev/null 2>&1 || true
        else
            nohup "$BIN_PATH" </dev/null >/dev/null 2>&1 &
        fi
        STARTED_MSG="started in the background"
    fi

    INSTALLED_VER=$("$BIN_PATH" --version 2>/dev/null || echo "screenplus-tray")

    echo ""
    if [ "$IS_UPDATE" -eq 1 ]; then
        echo "Updated ${INSTALLED_VER} in ${PREFIX} (${FILE_COUNT} files)."
    else
        echo "Installed ${INSTALLED_VER} to ${PREFIX} (${FILE_COUNT} files)."
    fi
    echo "  - Binary:    ${BIN_PATH}"
    echo "  - Manifest:  ${MANIFEST}"
    if [ "$AUTOSTART" -eq 1 ]; then
        echo "  - Autostart: enabled (${XDG_CONFIG_HOME:-${HOME}/.config}/autostart/screenplus-tray.desktop)"
    else
        echo "  - Autostart: skipped (--no-autostart)"
    fi
    echo "  - Tray:      ${STARTED_MSG}"

    case ":${PATH:-}:" in
        *":${PREFIX}/bin:"*) ;;
        *)
            echo ""
            echo "Note: ${PREFIX}/bin is not in your PATH."
            echo "Autostart uses the full path automatically; to run 'screenplus-tray' from a terminal, add ${PREFIX}/bin to PATH."
            ;;
    esac
}

main "$@"
