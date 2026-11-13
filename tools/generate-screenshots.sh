#!/usr/bin/env bash
# Auto-generate showcase screenshots for GoTV into site/
# - Linux GTK+ 3.0 Setup UI (compiled & captured via xvfb-run + ImageMagick)
# - Web UI Desktop & Mobile (captured via headless Chromium against vite preview)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SITE_DIR="${ROOT_DIR}/site"
WEB_DIR="${ROOT_DIR}/web"
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
  if ! command -v xvfb-run >/dev/null 2>&1 || ! command -v import >/dev/null 2>&1; then
    echo "⚠️  Bỏ qua ảnh Linux: thiếu xvfb-run hoặc ImageMagick (import)."
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
  xvfb-run -a -s "-screen 0 1024x768x24" bash -c "
    GTV_DATA_DIR='${ROOT_DIR}/data' '${bin}' &
    PID=\$!
    sleep 2
    import -window root '${out}'
    kill \$PID 2>/dev/null || true
  "

  if command -v convert >/dev/null 2>&1; then
    convert "${out}" -trim +repage "${out}"
  fi
  echo "✅ Đã tạo ${out}"
}

generate_web_screenshots() {
  local browser=""
  if command -v chromium >/dev/null 2>&1; then
    browser="chromium"
  elif command -v google-chrome >/dev/null 2>&1; then
    browser="google-chrome"
  else
    echo "⚠️  Bỏ qua ảnh Web: không tìm thấy chromium hoặc google-chrome."
    return 0
  fi

  if [ ! -d "${WEB_DIR}/dist" ]; then
    echo "⚠️  Chưa có web/dist, đang chạy vite build..."
    (cd "${WEB_DIR}" && npx vite build)
  fi

  echo "🌐 Đang chụp giao diện Web (Desktop & Mobile)..."
  local port=4179
  (cd "${WEB_DIR}" && npx vite preview --port "${port}" --strictPort >/dev/null 2>&1) &
  local server_pid=$!
  trap 'kill ${server_pid} 2>/dev/null || true; cleanup' EXIT

  sleep 2
  "${browser}" --headless --no-sandbox --disable-gpu \
    --window-size=1280,800 \
    --screenshot="${SITE_DIR}/showcase-web-desktop.png" \
    "http://localhost:${port}/gotv/" 2>/dev/null

  "${browser}" --headless --no-sandbox --disable-gpu \
    --window-size=390,844 \
    --screenshot="${SITE_DIR}/showcase-web-mobile.png" \
    "http://localhost:${port}/gotv/" 2>/dev/null

  kill "${server_pid}" 2>/dev/null || true
  trap cleanup EXIT
  echo "✅ Đã tạo ${SITE_DIR}/showcase-web-desktop.png và ${SITE_DIR}/showcase-web-mobile.png"
}

main() {
  local target="${1:-all}"
  case "${target}" in
    linux)
      generate_linux_setup_screenshot
      ;;
    web)
      generate_web_screenshots
      ;;
    all)
      generate_linux_setup_screenshot
      generate_web_screenshots
      ;;
    *)
      echo "Cách dùng: $0 [all|linux|web]" >&2
      exit 1
      ;;
  esac
}

main "$@"
