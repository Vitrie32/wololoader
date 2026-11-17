/**
 * GameData - the game's graphics for the UI: palette, decoded SLP files, and icons.
 *
 * Reads through Scriptorium.api (it adds the session token) from the read-only endpoints
 * /api/palette, /api/game-file and /api/game-list. Nothing is written to the game folder.
 * Decoded files and rendered frames are cached for the session; a failed load is not cached,
 * so a later call tries again.
 *
 * Icons (game interface sheets, frame index = icon id from the dat):
 *   units      50730.slp  (unit.icon, buildings excluded)
 *   techs      50729.slp  (tech.icon)
 *   buildings  50705.slp  (culture 0; 50706..50708 hold the other cultures' art, same frame ids)
 * Checked against the game's own files: the frames in these sheets show the units, techs and
 * buildings the dat names (e.g. Barracks = frame 2, Castle = 7, Mill = 19 in 50705..50708).
 * 50791.slp has only 4 frames of 24x24 and is not an icon sheet for these.
 *
 * Usage from the UI:
 *   html += GameIcons.markup(GameIcons.UNIT_SHEET, unit.icon, 'Archer'); // after rendering:
 *   GameData.hydrate(container);                                          // fills the icons
 */
(function () {
  'use strict';

  const INTERFACE_DIR = '_common/drs/interface';
  const UNIT_SHEET = '50730';
  const TECH_SHEET = '50729';
  const BUILDING_SHEET = '50705';

  const cache = new Map();   // key -> Promise (decoded files, frames, lists); removed on failure

  function remember(key, make) {
    if (!cache.has(key)) {
      const p = Promise.resolve().then(make);
      cache.set(key, p);
      p.catch(() => cache.delete(key));
    }
    return cache.get(key);
  }

  function escapeAttr(s) {
    return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;');
  }

  const GameData = {
    UNIT_SHEET, TECH_SHEET, BUILDING_SHEET,

    /** The game palette: 256 entries of [r, g, b]. */
    palette() {
      return remember('palette', async () => {
        const pal = await window.Scriptorium.api('/api/palette');
        if (!Array.isArray(pal) || pal.length !== 256) throw new Error('bad game palette');
        return pal;
      });
    },

    /** Raw bytes of a file under the game's resources folder (relative path, e.g. "_common/drs/graphics/1.slp"). */
    async fetchGameFile(relPath) {
      const res = await window.Scriptorium.api('/api/game-file?path=' + encodeURIComponent(relPath));
      return res.arrayBuffer();
    },

    /** Decoded SLP of a game file (cached). Throws when the file is missing or not an SLP. */
    slp(relPath) {
      return remember('slp:' + relPath, async () => {
        const bytes = await GameData.fetchGameFile(relPath);
        return window.SlpDecode.decode(bytes);
      });
    },

    /** Lists the files of a game folder: { files: [{name, size}], truncated }. */
    list(relDir) {
      return remember('list:' + relDir, () =>
        window.Scriptorium.api('/api/game-list?dir=' + encodeURIComponent(relDir)));
    },

    /**
     * One frame of a decoded SLP as a PNG data URL, or null when the frame does not exist.
     * `decoded` is the result of decode(); `cacheKey` names it for the cache.
     */
    async frameUrl(cacheKey, decoded, frameIndex, player = 0) {
      const frame = decoded.frames[frameIndex];
      if (!frame) return null;
      return remember(`frame:${cacheKey}:${frameIndex}:${player}`, async () => {
        const pal = await GameData.palette();
        const rgba = window.SlpDecode.render(frame, pal, { player, shadow: true });
        const canvas = document.createElement('canvas');
        canvas.width = frame.width;
        canvas.height = frame.height;
        canvas.getContext('2d').putImageData(new ImageData(rgba, frame.width, frame.height), 0, 0);
        return canvas.toDataURL('image/png');
      });
    },

    /** Frame of a game interface sheet as a data URL, or null when the file or frame is missing. */
    interfaceFrameUrl(sheet, frameIndex) {
      if (!Number.isInteger(frameIndex) || frameIndex < 0) return Promise.resolve(null);
      // A failure resolves to null and stays cached: a missing game file is not asked for again
      // on every render (the caller keeps its letter fallback).
      return remember(`icon:${sheet}:${frameIndex}`, async () => {
        try {
          const rel = INTERFACE_DIR + '/' + sheet + '.slp';
          const decoded = await GameData.slp(rel);
          return await GameData.frameUrl(rel, decoded, frameIndex, 0);
        } catch (e) {
          return null;
        }
      });
    },

    /**
     * Fills every [data-game-sheet] element under root with its icon (decoded lazily, once).
     * Elements keep their text fallback when the icon cannot be drawn.
     */
    hydrate(root) {
      if (!root || !root.querySelectorAll) return;
      root.querySelectorAll('[data-game-sheet]:not([data-game-done])').forEach((el) => {
        el.dataset.gameDone = '1';
        const sheet = el.dataset.gameSheet;
        const frame = Number(el.dataset.gameFrame);
        GameData.interfaceFrameUrl(sheet, frame).then((url) => {
          if (!url || !el.isConnected) return;
          const img = document.createElement('img');
          img.className = 'game-icon-img';
          img.alt = '';
          img.src = url;
          el.replaceChildren(img);
          el.classList.add('has-icon');
        });
      });
    },
  };

  /**
   * Markup for an icon placeholder. `fallback` (usually the first letter of the name) shows
   * until the real icon is drawn, or stays when the game file is missing.
   */
  const GameIcons = {
    UNIT_SHEET, TECH_SHEET, BUILDING_SHEET,

    // Frame -1 (unknown icon id) never draws: hydrate() sees no frame and keeps the fallback letter.
    markup(sheet, frame, fallback = '', size = 32) {
      const letter = String(fallback || '?').trim().charAt(0).toUpperCase() || '?';
      return `<span class="game-icon" style="--icon-size:${Number(size) || 32}px" data-game-sheet="${escapeAttr(sheet)}" data-game-frame="${Number(frame)}">` +
        `<span class="game-icon-fallback">${escapeAttr(letter)}</span></span>`;
    },

    /** Icon of a catalog unit: buildings (type 80) use the building sheet, others the unit sheet. */
    unitMarkup(unit, size = 32) {
      if (!unit || !(unit.icon >= 0)) return GameIcons.markup(UNIT_SHEET, -1, unit && unit.name, size);
      const sheet = unit.type === 80 ? BUILDING_SHEET : UNIT_SHEET;
      return GameIcons.markup(sheet, unit.icon, unit.name, size);
    },

    techMarkup(tech, size = 32) {
      if (!tech || !(tech.icon >= 0)) return GameIcons.markup(TECH_SHEET, -1, tech && tech.name, size);
      return GameIcons.markup(TECH_SHEET, tech.icon, tech.name, size);
    },

    hydrate: GameData.hydrate,
  };

  if (typeof window !== 'undefined') {
    window.GameData = GameData;
    window.GameIcons = GameIcons;
  }
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = { GameData, GameIcons };
  }
})();
