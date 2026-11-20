#!/usr/bin/env bash
# Auto-generate showcase screenshots for GoTV into site/
# - Linux GTK+ 3.0 Setup UI (compiled & captured via xvfb-run + import/scrot)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SITE_DIR="${ROOT_DIR}/site"
TMP_DIR="$(mktemp -d)"

cleanup() {
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

mkdir -p "${SITE_DIR}"

generate_linux_setup_screenshot() {
  if ! pkg-config --exists gtk+-3.0 gio-2.0; then
    echo "⚠️  Bỏ qua ảnh Linux: thiếu gtk+-3.0 hoặc gio-2.0."
    return 0
  fi
  local capture_cmd=""
  if command -v import >/dev/null 2>&1; then
    capture_cmd="import"
  elif command -v scrot >/dev/null 2>&1; then
    capture_cmd="scrot"
  fi
  if ! command -v xvfb-run >/dev/null 2>&1 || [ -z "${capture_cmd}" ]; then
    echo "⚠️  Bỏ qua ảnh Linux: thiếu xvfb-run hoặc công cụ chụp màn hình (import/scrot)."
    return 0
  fi

  echo "🐧 Đang biên dịch và chụp giao diện Linux (GTK+ 3.0)..."
  local extra_inc=""
  local extra_libs=""

  if pkg-config --exists ayatana-appindicator3-0.1; then
    extra_libs="$(pkg-config --cflags --libs ayatana-appindicator3-0.1)"
  else
    mkdir -p "${TMP_DIR}/shim/libayatana-appindicator"
    cat > "${TMP_DIR}/shim/libayatana-appindicator/app-indicator.h" <<'EOF'
#ifndef APP_INDICATOR_SHIM_H
#define APP_INDICATOR_SHIM_H
#include <gtk/gtk.h>
typedef struct _AppIndicator AppIndicator;
typedef enum { APP_INDICATOR_CATEGORY_APPLICATION_STATUS = 0 } AppIndicatorCategory;
typedef enum { APP_INDICATOR_STATUS_ACTIVE = 1 } AppIndicatorStatus;
static inline AppIndicator *app_indicator_new(const gchar *id, const gchar *icon_name, AppIndicatorCategory category) { (void)id; (void)icon_name; (void)category; return NULL; }
static inline void app_indicator_set_status(AppIndicator *self, AppIndicatorStatus status) { (void)self; (void)status; }
static inline void app_indicator_set_icon_theme_path(AppIndicator *self, const gchar *icon_theme_path) { (void)self; (void)icon_theme_path; }
static inline void app_indicator_set_icon_full(AppIndicator *self, const gchar *icon_name, const gchar *icon_desc) { (void)self; (void)icon_name; (void)icon_desc; }
static inline void app_indicator_set_title(AppIndicator *self, const gchar *title) { (void)self; (void)title; }
static inline void app_indicator_set_label(AppIndicator *self, const gchar *label, const gchar *guide) { (void)self; (void)label; (void)guide; }
static inline void app_indicator_set_menu(AppIndicator *self, GtkMenu *menu) { (void)self; (void)menu; }
static inline void app_indicator_set_secondary_activate_target(AppIndicator *self, GtkWidget *menuitem) { (void)self; (void)menuitem; }
#endif
EOF
    extra_inc="-I${TMP_DIR}/shim"
  fi

  local bin="${TMP_DIR}/ibus-setup-gotv"
  # shellcheck disable=SC2046
  gcc -O2 -std=gnu11 -I"${ROOT_DIR}/engine" ${extra_inc} \
    $(pkg-config --cflags gtk+-3.0 gio-2.0) \
    "${ROOT_DIR}"/engine/*.c \
    "${ROOT_DIR}/linux/setup/main.c" \
    "${ROOT_DIR}/linux/setup/data.c" \
    $(pkg-config --libs gtk+-3.0 gio-2.0) ${extra_libs} -lm -o "${bin}"

  local out="${SITE_DIR}/showcase-linux.png"
  local fake_home="${TMP_DIR}/home"
  mkdir -p "${fake_home}/.config/gotv"

  local shot_step=""
  if [ "${capture_cmd}" = "import" ]; then
    shot_step="import -window root '${out}'"
  else
    shot_step="scrot -o '${out}'"
  fi

  xvfb-run -a -s "-screen 0 1024x768x24" bash -c "
    HOME='${fake_home}' GDK_BACKEND=x11 WAYLAND_DISPLAY= GTV_DATA_DIR='${ROOT_DIR}/data' '${bin}' &
    PID=\$!
    sleep 2
    ${shot_step}
    kill \$PID 2>/dev/null || true
  "

  if command -v convert >/dev/null 2>&1; then
    convert "${out}" -trim +repage "${out}"
  elif command -v python3 >/dev/null 2>&1; then
    python3 -c "
from PIL import Image, ImageChops
im = Image.open('${out}')
bg = Image.new(im.mode, im.size, im.getpixel((0, 0)))
diff = ImageChops.difference(im, bg)
bbox = diff.getbbox()
if bbox:
    im.crop(bbox).save('${out}')
"
  fi
  echo "✅ Đã tạo ${out}"
}

main() {
  local target="${1:-linux}"
  case "${target}" in
    linux|all)
      generate_linux_setup_screenshot
      ;;
    *)
      echo "Cách dùng: $0 [linux]" >&2
      exit 1
      ;;
  esac
}

main "$@"
