import GLib from 'gi://GLib';
import Shell from 'gi://Shell';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

export default class GoTVFocusExtension extends Extension {
    enable() {
        this._path = GLib.build_filenamev([GLib.get_user_runtime_dir(), 'gotv-focus']);
        this._tracker = Shell.WindowTracker.get_default();
        this._sigId = global.display.connect('notify::focus-window', () => this._sync());
        this._sync();
    }

    disable() {
        if (this._sigId) {
            global.display.disconnect(this._sigId);
            this._sigId = 0;
        }
        if (this._path) {
            GLib.unlink(this._path);
            this._path = null;
        }
        this._tracker = null;
    }

    _sync() {
        const win = global.display.focus_window;
        let id = '';
        if (win) {
            id = win.get_wm_class() || '';
            if (!id && this._tracker) {
                const app = this._tracker.get_window_app(win);
                if (app)
                    id = (app.get_id() || '').replace(/\.desktop$/i, '');
            }
        }
        try {
            GLib.file_set_contents(this._path, id);
        } catch (_e) {
        }
    }
}
