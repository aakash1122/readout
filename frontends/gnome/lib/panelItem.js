// One panel item: a PanelMenu.Button showing one module, or several (combined), with a popup.
//
// The popup has the Stats frame (docs/visual-spec.md 1.2 and 6.4): a header with the system
// monitor button, the centered title and the settings button, then the body. A combined item
// shows compact summaries first; "›" opens a module's full page and the header shows a back
// button. Only the visible full page runs at the detail tier.

import Clutter from 'gi://Clutter';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import Shell from 'gi://Shell';
import St from 'gi://St';

import {gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';

import {MODULES} from './catalog.js';
import {ClientState} from './dbusClient.js';
import {iconButton, setText} from './components.js';
import {createPanelWidget, panelScale} from './panelWidgets.js';
import {createPage} from './popups.js';
import {createSummary} from './summaries.js';
import * as Values from './values.js';

const TOOLTIP_DELAY_MS = 400;
const CONNECTIVITY_CELLS = 90;
const OVERVIEW = 'overview';

const SYSTEM_MONITOR_APPS = ['org.gnome.SystemMonitor.desktop', 'gnome-system-monitor.desktop'];
const CALENDAR_APPS = ['org.gnome.Calendar.desktop', 'gnome-calendar.desktop'];

function findApp(ids) {
    const appSystem = Shell.AppSystem.get_default();
    for (const id of ids) {
        const app = appSystem.lookup_app(id);
        if (app)
            return app;
    }
    return null;
}

export const ReadoutButton = GObject.registerClass(
class ReadoutButton extends PanelMenu.Button {
    /**
     * @param {string} owner - unique key for the client demand
     * @param {object} item - normalized panel item options
     * @param {object} context - {client, icon(name), processIcon(name), openPreferences(), popupLook,
     *   popupHeights (Map of "owner/view" to px), savePopupHeights()}
     */
    _init(owner, item, context) {
        const title = item.modules.map(module => _(MODULES[module].title)).join(', ');
        super._init(0.5, _('Readout: %s').format(title), false);
        this._owner = owner;
        this._item = item;
        this._client = context.client;
        this._context = {...context, closeMenu: () => this.menu.close()};
        this._combined = item.modules.length > 1;
        this._daemonModules = item.modules.filter(module => MODULES[module].daemon);
        this._payloads = {};
        this._pages = new Map();
        this._summaries = new Map();
        this._view = this._combined ? OVERVIEW : item.modules[0];
        // Per-item state that lives while the popup is closed (visual-spec.md 6.5).
        this._shared = {connectivity: [], unpluggedAt: null, externalPower: null};
        this._tooltip = null;
        this._tooltipTimeoutId = 0;
        this._settling = false;
        this.add_style_class_name('readout-button');

        this._box = new St.BoxLayout({style_class: 'panel-status-indicators-box readout-panel-box'});
        this.add_child(this._box);
        this._widgets = [];
        this._buildWidgets();
        this._warningIcon = new St.Icon({
            icon_name: 'dialog-warning-symbolic', style_class: 'system-status-icon',
            visible: false,
        });
        this._box.add_child(this._warningIcon);
        // Widget geometry follows the panel height (visual-spec.md 6.1).
        this._panelHeightId = Main.panel.connect('notify::height', () => this._buildWidgets());

        this.menu.box.add_style_class_name('readout-menu');
        // The menu frame is cheap and must exist up front (PopupMenu refuses to open while
        // empty); the pages inside it are built on first use.
        this._buildMenu();
        this.menu.connect('open-state-changed', (_menu, open) => this._onOpenStateChanged(open));
        this.connect('notify::hover', () => this._onHoverChanged());

        this._removeUpdateListener = this._client.addUpdateListener(modules => this._onUpdate(modules));
        this._removeStateListener = this._client.addStateListener(state => this._onClientState(state));
        this._setTier();
        this._onClientState(this._client.state);
    }

    _buildWidgets() {
        const scale = panelScale();
        if (scale === this._widgetScale)
            return;
        this._widgetScale = scale;
        for (const {widget} of this._widgets)
            widget.destroy();
        this._widgets = this._item.modules.map((module, index) => {
            const widget = createPanelWidget(module, this._item, this._context, scale);
            this._box.insert_child_at_index(widget.actor, index);
            if (this._payloads[module])
                widget.update(this._payloads[module]);
            return {module, widget};
        });
    }

    _setTier() {
        if (this._daemonModules.length === 0)
            return;
        const open = this.menu.isOpen;
        const modules = {};
        for (const module of this._daemonModules)
            modules[module] = open && this._view === module ? 'detail' : 'panel';
        const sensors = this._daemonModules.includes('sensors') ? this._item.sensors : [];
        const cores = this._daemonModules.includes('cpu') && this._item.styles.cpu === 'bars';
        this._client.setDemand(this._owner, modules, sensors, cores);
    }

    _onUpdate(modules) {
        let changed = false;
        for (const {module, widget} of this._widgets) {
            const payload = modules[module];
            if (payload === undefined)
                continue;
            this._payloads[module] = payload;
            widget.update(payload);
            changed = true;
        }
        if (!changed)
            return;
        this._updateShared(modules);
        if (this.menu.isOpen) {
            if (this._view === OVERVIEW) {
                for (const [module, summary] of this._summaries) {
                    if (modules[module] !== undefined)
                        summary.update(modules[module]);
                }
            } else if (modules[this._view] !== undefined) {
                this._pages.get(this._view)?.update(this._pagePayload(this._view));
                if (this._settling && this._client.dataTier(this._view) === 'detail')
                    this._finishSettling();
                else
                    this._rememberHeight();
            }
        }
        // Screen readers read the button's accessible name, so it carries the values too.
        this._updateTooltipText();
    }

    _updateShared(modules) {
        const {network} = modules;
        if (network) {
            const online = network.connectivity === undefined || network.connectivity === 'unknown'
                ? Boolean(network.connected) : network.connectivity === 'full';
            this._shared.connectivity.push(online);
            if (this._shared.connectivity.length > CONNECTIVITY_CELLS)
                this._shared.connectivity.shift();
        }
        const {battery} = modules;
        if (battery && battery.externalPower !== undefined) {
            if (this._shared.externalPower === true && !battery.externalPower)
                this._shared.unpluggedAt = GLib.get_monotonic_time();
            this._shared.externalPower = battery.externalPower;
        }
    }

    _onClientState(state) {
        const missing = state === ClientState.MISSING && this._daemonModules.length > 0;
        this._warningIcon.visible = missing;
        if (this._statusLabel) {
            // Without the service the module pages would only show dashes.
            this._scroll.visible = !missing || this._daemonModules.length < this._item.modules.length;
            this._statusLabel.visible = missing || state === ClientState.ERROR;
            this._statusLabel.text = missing
                ? _('The Readout system service (readoutd) is not installed. Install the “readout” package from your distribution to see system statistics.')
                : _('Cannot reach the Readout system service. Retrying…');
        }
    }

    // ---- Popup -------------------------------------------------------------------------

    _buildMenu() {
        const container = new St.BoxLayout({
            orientation: Clutter.Orientation.VERTICAL,
            style_class: 'readout-menu-content',
        });
        this._menuContent = container;
        this.setPopupLook(this._context.popupLook);

        const header = new St.BoxLayout({style_class: 'readout-header', x_expand: true});
        this._header = header;
        const clock = this._item.modules.length === 1 && this._item.modules[0] === 'clock';
        this._launcherApp = findApp(clock ? CALENDAR_APPS : SYSTEM_MONITOR_APPS);
        this._launchButton = iconButton(clock ? 'x-office-calendar-symbolic' : 'utilities-system-monitor-symbolic',
            clock ? _('Open Calendar') : _('Open System Monitor'), () => {
                this.menu.close();
                this._launcherApp?.activate();
            }, 'readout-header-button');
        this._launchButton.reactive = Boolean(this._launcherApp);
        this._launchButton.opacity = this._launcherApp ? 255 : 0;
        this._backButton = iconButton('go-previous-symbolic', _('Back'), () => this._showView(OVERVIEW),
            'readout-header-button');
        this._backButton.visible = false;
        header.add_child(this._launchButton);
        header.add_child(this._backButton);
        this._title = new St.Label({
            style_class: 'readout-header-title', x_expand: true,
            x_align: Clutter.ActorAlign.CENTER, y_align: Clutter.ActorAlign.CENTER,
        });
        header.add_child(this._title);
        header.add_child(iconButton('emblem-system-symbolic', _('Readout Settings'), () => {
            this.menu.close();
            this._context.openPreferences();
        }, 'readout-header-button'));
        container.add_child(header);

        this._statusLabel = new St.Label({style_class: 'readout-status-message', visible: false});
        this._statusLabel.clutter_text.line_wrap = true;
        container.add_child(this._statusLabel);

        this._body = new St.BoxLayout({orientation: Clutter.Orientation.VERTICAL, x_expand: true});
        this._scroll = new St.ScrollView({
            style_class: 'readout-scroll',
            hscrollbar_policy: St.PolicyType.NEVER,
            vscrollbar_policy: St.PolicyType.AUTOMATIC,
            overlay_scrollbars: false,
            child: this._body,
        });
        container.add_child(this._scroll);
        this.menu.box.add_child(container);
    }

    /**
     * Applies the popup look settings shared by all items (docs/frontend-spec.md, "Popup look").
     *
     * @param {object} look - {textScale (percent), spacing, margin (points), width}
     */
    setPopupLook(look) {
        const content = this._menuContent;
        for (const name of ['compact', 'comfortable'])
            content.remove_style_class_name(`readout-spacing-${name}`);
        for (const name of ['narrow', 'wide'])
            content.remove_style_class_name(`readout-width-${name}`);
        if (look.spacing !== 'normal')
            content.add_style_class_name(`readout-spacing-${look.spacing}`);
        if (look.width !== 'normal')
            content.add_style_class_name(`readout-width-${look.width}`);
        // Em lengths here are relative to this box's own (scaled) font, and one Stats point is
        // 1em / 13, so the margin is divided by the text scale to stay in shell-font points.
        const textFactor = look.textScale / 100;
        const margin = (look.margin / 13 / textFactor).toFixed(3);
        content.style = `font-size: ${textFactor.toFixed(2)}em; padding: 0 ${margin}em ${margin}em ${margin}em;`;
    }

    _ensureOverview() {
        if (!this._overview) {
            this._overview = new St.BoxLayout({
                orientation: Clutter.Orientation.VERTICAL, style_class: 'readout-overview', x_expand: true,
            });
            for (const module of this._item.modules) {
                const summary = createSummary(module, this._item, this._context, name => this._showView(name));
                this._summaries.set(module, summary);
                this._overview.add_child(summary.actor);
            }
            this._body.add_child(this._overview);
        }
        return this._overview;
    }

    _ensurePage(module) {
        let page = this._pages.get(module);
        if (!page) {
            page = createPage(module, this._item, this._context, this._shared);
            this._pages.set(module, page);
            this._body.add_child(page.actor);
        }
        return page;
    }

    _showView(view) {
        this._closeVisible();
        this._view = view;
        if (this._overview)
            this._overview.visible = view === OVERVIEW;
        for (const [module, page] of this._pages)
            page.actor.visible = module === view;
        if (view === OVERVIEW) {
            this._ensureOverview().visible = true;
            setText(this._title, _('Readout'));
            for (const [module, summary] of this._summaries) {
                if (this._payloads[module])
                    summary.update(this._payloads[module]);
                summary.opened();
            }
        } else {
            const page = this._ensurePage(view);
            page.actor.visible = true;
            setText(this._title, page.title);
            const payload = this._pagePayload(view);
            if (payload)
                page.update(payload);
            page.opened();
        }
        this._backButton.visible = this._combined && view !== OVERVIEW;
        this._launchButton.visible = !this._backButton.visible;
        this._scroll.vadjustment.value = 0;
        if (this.menu.isOpen)
            this._setTier();
        this._startSettling();
    }

    // A page has only panel-tier data until the first detail Update (about 500 ms after it
    // opens, so rates have a baseline) and is shorter until then. Meanwhile the body keeps the
    // height the page had last time, so the popup opens at its final size instead of growing.
    _startSettling() {
        const page = this._view !== OVERVIEW && MODULES[this._view].daemon;
        this._settling = Boolean(this.menu.isOpen && page && this._client.dataTier(this._view) !== 'detail');
        const height = this._settling ? this._context.popupHeights.get(this._heightKey) ?? 0 : 0;
        this._body.style = height > 0 ? `min-height: ${height}px;` : null;
    }

    _finishSettling() {
        this._settling = false;
        this._body.style = null;
        this._rememberHeight();
    }

    get _heightKey() {
        return `${this._owner}/${this._view}`;
    }

    // Records the open page's natural height, in stylesheet px.
    _rememberHeight() {
        if (!this.menu.isOpen || this._settling || this._view === OVERVIEW)
            return;
        const [, naturalHeight] = this._body.get_preferred_height(-1);
        const scaleFactor = St.ThemeContext.get_for_stage(global.stage).scale_factor || 1;
        const height = Math.ceil(naturalHeight / scaleFactor);
        if (height > 0)
            this._context.popupHeights.set(this._heightKey, height);
    }

    // What a page shows: the latest payload, and until the page's own first detail Update the
    // last detail payload fills in what the panel tier lacks (frequencies, processes, per-core
    // values), so nothing appears half a second after opening. Fresh panel values win.
    _pagePayload(view) {
        const payload = this._payloads[view];
        if (this._client.dataTier(view) === 'detail')
            return payload;
        const cached = this._client.detailPayload(view);
        return cached ? {...cached, ...payload} : payload;
    }

    _closeVisible() {
        if (this._view === OVERVIEW) {
            for (const summary of this._summaries.values())
                summary.closed();
        } else {
            this._pages.get(this._view)?.closed();
        }
    }

    _onOpenStateChanged(open) {
        this._hideTooltip();
        if (open) {
            this._fitToScreen();
            this._showView(this._combined ? OVERVIEW : this._item.modules[0]);
            this.add_style_pseudo_class('checked');
        } else {
            this._closeVisible();
            this._settling = false;
            this._body.style = null;
            this.remove_style_pseudo_class('checked');
            this._context.savePopupHeights();
        }
        this._setTier();
    }

    // The popup is exactly as tall as its content, up to 70% of the monitor's work area; only
    // above that does the body scroll (docs/visual-spec.md 6.4).
    _fitToScreen() {
        const monitor = Main.layoutManager.findMonitorForActor(this) ?? Main.layoutManager.primaryMonitor;
        if (!monitor)
            return;
        const workArea = Main.layoutManager.getWorkAreaForMonitor(monitor.index);
        const [, headerHeight] = this._header.get_preferred_height(-1);
        const padding = this._menuContent.get_theme_node().get_vertical_padding();
        const scaleFactor = St.ThemeContext.get_for_stage(global.stage).scale_factor || 1;
        // Stylesheet px lengths are multiplied by the scale factor; actor sizes already are.
        const available = Math.floor((workArea.height * 0.7 - headerHeight - padding) / scaleFactor);
        const style = `max-height: ${Math.max(100, available)}px;`;
        if (this._scroll.style !== style)
            this._scroll.style = style;
    }

    // ---- Tooltip -----------------------------------------------------------------------

    _onHoverChanged() {
        if (this.hover && !this.menu.isOpen) {
            if (!this._tooltipTimeoutId) {
                this._tooltipTimeoutId = GLib.timeout_add(GLib.PRIORITY_DEFAULT, TOOLTIP_DELAY_MS, () => {
                    this._tooltipTimeoutId = 0;
                    this._showTooltip();
                    return GLib.SOURCE_REMOVE;
                });
            }
        } else {
            this._hideTooltip();
        }
    }

    _tooltipText() {
        const lines = this._item.modules.map(module => {
            const title = _(MODULES[module].title);
            if (module === 'clock')
                return title;
            const summary = Values.summary(module, this._payloads[module], this._item);
            return summary ? `${title}  ${summary}` : title;
        });
        if (this._client.state === ClientState.MISSING && this._daemonModules.length > 0)
            lines.push(_('readoutd is not installed'));
        return lines.join('\n');
    }

    _updateTooltipText() {
        const text = this._tooltipText();
        if (this._tooltip)
            setText(this._tooltip, text);
        const accessibleName = text.replace(/\n/g, ', ');
        if (this.accessible_name !== accessibleName)
            this.accessible_name = accessibleName;
    }

    _showTooltip() {
        if (this._tooltip || this.menu.isOpen)
            return;
        this._tooltip = new St.Label({style_class: 'dash-label readout-tooltip', opacity: 0});
        Main.layoutManager.uiGroup.add_child(this._tooltip);
        this._updateTooltipText();
        const [x, y] = this.get_transformed_position();
        const [width, height] = this.get_transformed_size();
        const [, tooltipWidth] = this._tooltip.get_preferred_width(-1);
        const monitor = Main.layoutManager.findMonitorForActor(this);
        let left = Math.round(x + width / 2 - tooltipWidth / 2);
        if (monitor)
            left = Math.max(monitor.x + 4, Math.min(left, monitor.x + monitor.width - tooltipWidth - 4));
        this._tooltip.set_position(left, Math.round(y + height + 6));
        this._tooltip.ease({opacity: 255, duration: 120, mode: Clutter.AnimationMode.EASE_OUT_QUAD});
    }

    _hideTooltip() {
        if (this._tooltipTimeoutId) {
            GLib.source_remove(this._tooltipTimeoutId);
            this._tooltipTimeoutId = 0;
        }
        if (this._tooltip) {
            this._tooltip.destroy();
            this._tooltip = null;
        }
    }

    destroy() {
        this._hideTooltip();
        if (this._panelHeightId) {
            Main.panel.disconnect(this._panelHeightId);
            this._panelHeightId = 0;
        }
        this._removeUpdateListener?.();
        this._removeStateListener?.();
        this._client.removeDemand(this._owner);
        for (const summary of this._summaries.values())
            summary.destroy();
        this._summaries.clear();
        for (const page of this._pages.values())
            page.destroy();
        this._pages.clear();
        for (const {widget} of this._widgets)
            widget.destroy();
        this._widgets = [];
        super.destroy();
    }
});
