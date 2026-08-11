#!/usr/bin/env node
/**
 * Automated EasyEDA (Standard) export for the AutoPilot PCBs.
 *
 * Replaces the manual File -> Export dance for every board.
 *
 * Every export is fired through callCommand(), the editor's own command
 * dispatcher, using the command ids carried on its menu elements (e.g.
 * 'exportDxf', 'export(altium)', 'pcb_fabrication'). That means no menu
 * clicking, and no dependence on menu wording, layout, or language. The
 * documented extension API is deliberately not used: `api()` exists only
 * inside an extension's own context, not on the page.
 *
 * Usage:
 *   npm run login      once, to sign in (the session is then remembered)
 *   npm run discover   list the projects on the account -> projects.json
 *   npm run inspect    dump the editor's real menu tree -> menu-dump.json
 *   npm run export     export everything in projects.json
 *   npm run export -- --only Wind --dry-run
 *   npm run stale      list export folders left behind by a project rename
 */

import { chromium } from 'playwright';
import { mkdir, writeFile, readFile, readdir, access } from 'node:fs/promises';
import path from 'node:path';
import readline from 'node:readline/promises';

const HERE = import.meta.dirname;
const CIRCUIT = path.resolve(HERE, '..');
const PROFILE = path.join(HERE, '.eda-profile');
const PROJECTS = path.join(HERE, 'projects.json');

/**
 * How long to wait for a generated file. Most exports are near-instant; Gerber
 * and OBJ do real work server-side, so those get their own longer budget via
 * the `slow` flag in EXPORTS.
 */
const EXPORT_TIMEOUT = 45_000;
const SLOW_EXPORT_TIMEOUT = 180_000;

/**
 * The boards live in a team workspace, not the personal one, inside a folder.
 * Discover walks to them by name.
 */
const TEAM = 'Marco Scott';
const FOLDER = 'AutoPilot';

/**
 * Direct link to the team folder holding the boards. Going straight there
 * skips the sidebar tree entirely, which is both faster and far less fragile.
 * Blank this out to fall back to walking the tree by name via TREE_PATH.
 */
const FOLDER_URL = 'https://u.easyeda.com/account/user/projects/team'
  + '?team=0feaeb8e5a094f47bead7ddb562964a7&folder=a17941412c86457385dba807f0e4d429';

/**
 * The sidebar tree path to the boards, used only when FOLDER_URL is blank.
 * Team projects live under "Participated", and each level has to be expanded
 * before the next one exists in the DOM.
 */
const TREE_PATH = ['Participated', TEAM, FOLDER];

/**
 * Filenames are <SCH|PCB>_<board>_<type>.<ext>, where <board> is the document
 * title with spaces turned into hyphens. Every file carries a type segment,
 * including the ones where it repeats the extension (_PNG.png, _DXF.dxf), so
 * the pattern holds without exceptions.
 */
function fileName(prefix, board, type, ext) {
  return `${prefix}_${board}_${type}.${ext}`;
}

/**
 * Every export, in the order it is run.
 *
 * cmds   editor command ids to fire in sequence; the last one triggers the
 *         download, any before it set things up (entering PhotoView, etc.)
 * type   the type segment of the output filename
 * ext    the output extension
 * doc    which document the export belongs to: 'sch' or 'pcb'
 *
 * The ids come from the `cmd` attributes on EasyEDA's own menu items -- run
 * `npm run inspect` to dump the current list.
 */
const EXPORTS = [
  { doc: 'sch', cmds: ['export_easyEDA'], type: 'EasyEDA', ext: 'json' },
  { doc: 'sch', cmds: ['source(SVG)'], type: 'SVG', ext: 'svg', confirm: ['Download'] },
  { doc: 'sch', cmds: ['export(fileImage)'], type: 'PNG', ext: 'png', confirm: ['Export'] },
  // The Altium export gates its Download button behind a disclaimer checkbox.
  { doc: 'sch', cmds: ['export(altium)'], type: 'Altium', ext: 'schdoc', agree: true, confirm: ['Download'] },

  { doc: 'pcb', cmds: ['export_easyEDA'], type: 'EasyEDA', ext: 'json' },
  { doc: 'pcb', cmds: ['source(SVG)'], type: 'SVG', ext: 'svg', confirm: ['Download'] },
  { doc: 'pcb', cmds: ['export(fileImage)'], type: 'PNG', ext: 'png', confirm: ['Export'] },
  // Gerber chains three dialogs: confirm, decline the DRC run, then generate.
  {
    doc: 'pcb', cmds: ['pcb_fabrication'], type: 'Gerber', ext: 'zip', slow: true,
    confirm: ['Yes, Generate Gerber', 'No, Generate Gerber', 'Generate Gerber'],
  },
  { doc: 'pcb', cmds: ['exportFile(dsn)'], type: 'Autorouter', ext: 'dsn' },
  { doc: 'pcb', cmds: ['exportDxf'], type: 'DXF', ext: 'dxf' },
  { doc: 'pcb', cmds: ['export3DModelObj'], type: 'OBJ', ext: 'zip', slow: true },
];

/**
 * PhotoView is a mode, not a dialog: enter it once, then export each side.
 * Kept out of EXPORTS so it is entered a single time -- firing
 * convertToPhotoView again while already in PhotoView would toggle back out.
 */
const PHOTOVIEW = {
  doc: 'pcb',
  enter: 'convertToPhotoView',
  exportCmd: 'source(SVG)',
  confirm: ['Download'],
  ext: 'svg',
  sides: [
    { cmd: 'photoView_changeSide_topSide', label: 'Top Side', type: 'PhotoView-Top' },
    { cmd: 'photoView_changeSide_bottomSide', label: 'Bottom Side', type: 'PhotoView-Bottom' },
  ],
};

/** Fallback confirm labels for exports with no explicit sequence. */
const CONFIRM_LABELS = ['Export', 'Download', 'Generate', 'Confirm', 'OK', 'Yes'];

// ---------------------------------------------------------------- utilities

const log = (...a) => console.log(...a);
const warn = (...a) => console.warn('  !', ...a);

async function exists(p) {
  try { await access(p); return true; } catch { return false; }
}

async function prompt(question) {
  const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
  const answer = await rl.question(question);
  rl.close();
  return answer;
}

/**
 * Launch Chromium against a persistent profile so the EasyEDA login survives
 * between runs. Headed by default -- EasyEDA's canvas work is unreliable
 * headless, and it lets you watch the run.
 */
async function launch({ headless = false } = {}) {
  return chromium.launchPersistentContext(PROFILE, {
    headless,
    acceptDownloads: true,
    viewport: { width: 1600, height: 1000 },
    args: ['--disable-blink-features=AutomationControlled'],
  });
}

// ------------------------------------------------------------ editor access

/** Globals worth describing when working out how to drive the editor. */
const PROBE_GLOBALS = ['api', 'JSAPI', 'easyeda', 'callCommand', 'callByEditor', 'doctype'];

/**
 * Describe globals by name: what they are, and what is callable on them.
 * This is how the real API surface gets identified rather than guessed.
 */
async function describeGlobals(frame, names = PROBE_GLOBALS) {
  return frame.evaluate((wanted) => {
    const out = {};
    for (const name of wanted) {
      let v;
      try { v = window[name]; } catch { continue; }
      if (v === undefined) continue;

      const entry = { type: typeof v };
      if (typeof v === 'function') {
        entry.arity = v.length;
        entry.source = String(v).slice(0, 3000);
        // Statics hanging off the function, e.g. api.getSource
        entry.props = Object.keys(v).slice(0, 60);
      } else if (v && typeof v === 'object') {
        const own = Object.keys(v);
        entry.keys = own.slice(0, 120);
        entry.methods = own.filter((k) => {
          try { return typeof v[k] === 'function'; } catch { return false; }
        }).slice(0, 120);
        const proto = Object.getPrototypeOf(v);
        if (proto && proto !== Object.prototype) {
          entry.protoMethods = Object.getOwnPropertyNames(proto)
            .filter((k) => k !== 'constructor').slice(0, 120);
        }
      }
      out[name] = entry;
    }
    return out;
  }, names).catch(() => ({}));
}

/** Menu items and their command ids, from whichever frame holds the menu bar. */
async function describeMenus(frame) {
  return frame.evaluate(() => {
    const visible = (el) => el.getClientRects().length > 0;
    const describe = (el) => ({
      text: (el.textContent || '').replace(/\s+/g, ' ').trim().slice(0, 60),
      cmd: el.getAttribute('cmd') || el.getAttribute('data-cmd') || undefined,
      id: el.id || undefined,
      cls: el.className?.toString?.().slice(0, 60) || undefined,
      visible: visible(el),
    });

    const withCmd = [...document.querySelectorAll('[cmd],[data-cmd]')].map(describe);
    const menuBar = [...document.querySelectorAll(
      '#menu a, #menu li, [class*="menu"] a, [class*="menu"] li, [class*="dropdown"] a',
    )].map(describe).filter((d) => d.text);
    return { withCmd, menuBar: menuBar.slice(0, 500) };
  }).catch(() => ({ withCmd: [], menuBar: [] }));
}


/**
 * Frames exposing callCommand, the editor's own command dispatcher.
 *
 * The per-document editor frame (editorpage*.html) is preferred: it is the one
 * whose commands act on the open board. The top frame also has callCommand and
 * owns the dialogs.
 */
async function commandFrames(page) {
  const found = [];
  for (const frame of page.frames()) {
    const has = await frame
      .evaluate(() => typeof window.callCommand === 'function').catch(() => false);
    if (has) found.push(frame);
  }
  return found.sort((a, b) =>
    Number(/editorpage/.test(b.url())) - Number(/editorpage/.test(a.url())));
}

/** Wait for a frame that can take commands, once the board has rendered. */
async function waitForCommandFrame(page, timeout = 90_000) {
  const deadline = Date.now() + timeout;
  await waitForEditorReady(page, Math.min(timeout, 60_000));
  while (Date.now() < deadline) {
    const [frame] = await commandFrames(page);
    if (frame) return frame;
    await page.waitForTimeout(1000);
  }
  return null;
}

/**
 * Can this frame actually handle the command?
 *
 * callCommand dispatches from its own `hooks` map, falling back to regex
 * `wildcardHooks`, and simply returns false for anything it doesn't know. The
 * menu commands are registered in the frame that owns the menu bar, which is
 * not the same frame as the board canvas -- so the right frame has to be
 * chosen per command rather than assumed.
 */
async function canHandle(frame, cmd) {
  return frame.evaluate((c) => {
    const dispatch = window.callCommand;
    if (typeof dispatch !== 'function') return false;
    if (dispatch.hooks?.[c]) return true;
    for (const pattern of Object.keys(dispatch.wildcardHooks ?? {})) {
      try { if (new RegExp(pattern).test(c)) return true; } catch { /* bad pattern */ }
    }
    return false;
  }, cmd).catch(() => false);
}

/** Every command id a frame knows about -- the authoritative list. */
async function commandNames(frame) {
  return frame.evaluate(() => ({
    hooks: Object.keys(window.callCommand?.hooks ?? {}),
    wildcards: Object.keys(window.callCommand?.wildcardHooks ?? {}),
  })).catch(() => ({ hooks: [], wildcards: [] }));
}

/**
 * Fire an editor command by id, e.g. 'exportDxf' or 'export(altium)'.
 *
 * The frame is resolved per call rather than cached, for two reasons: commands
 * live in different frames, and commands like convertToPhotoView swap the
 * editor frame underneath us.
 *
 * Throws rather than returning quietly when nothing can handle the command --
 * otherwise the caller sits waiting for a download that was never coming.
 */
async function runCommand(page, cmd, args = null) {
  await waitForCommandFrame(page, 25_000);
  const attempts = [];

  // 1. Click EasyEDA's own menu element for the command. This is exactly what
  //    a user does, handlers and all, and does not depend on knowing how
  //    callCommand dispatches internally. Menu items respond to a click even
  //    while their menu is closed, since the handler is bound to the element.
  for (const frame of page.frames()) {
    const clicked = await frame.evaluate((c) => {
      // Compared as attributes rather than built into a selector, so ids
      // containing punctuation ("export(fileSVG)") need no escaping at all.
      const matches = [...document.querySelectorAll('[cmd],[data-cmd]')]
        .filter((e) => e.getAttribute('cmd') === c || e.getAttribute('data-cmd') === c);
      if (!matches.length) return false;
      // Several elements can share a command (menu bar, toolbar, context menu);
      // a visible one is likeliest to be the live, enabled control.
      const el = matches.find((e) => e.getClientRects().length > 0) ?? matches[0];
      el.click();
      return true;
    }, cmd).catch(() => false);
    if (clicked) return true;
    attempts.push(`no [cmd] element in ${frame.url().slice(0, 60)}`);
  }

  // 2. Fall back to the dispatcher directly, trying the frames that claim to
  //    know the command first. `false` means "not handled here", so keep going.
  const frames = await commandFrames(page);
  const ranked = [];
  for (const frame of frames) ranked.push({ frame, known: await canHandle(frame, cmd) });
  ranked.sort((a, b) => Number(b.known) - Number(a.known));

  for (const { frame } of ranked) {
    const result = await frame.evaluate(
      ([c, a]) => window.callCommand(c, a ?? undefined),
      [cmd, args],
    ).catch((err) => { attempts.push(err.message.split('\n')[0]); return false; });
    if (result !== false) return result;
  }

  throw new Error(`no frame could run "${cmd}" (${attempts.length} attempts)`);
}

/**
 * Wait for the editor to actually have a document open.
 *
 * EasyEDA shows a "Start page" first and swaps in the board a second or two
 * later, so neither 'load' nor the API existing is a reliable ready signal on
 * its own -- the rendered canvas is.
 */
async function waitForEditorReady(page, timeout = 60_000) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    for (const frame of page.frames()) {
      const ready = await frame.evaluate(() =>
        !!document.querySelector('canvas, svg#editor, #canvas, [id*="canvas"], [class*="canvas"]'),
      ).catch(() => false);
      if (ready) return frame;
    }
    await page.waitForTimeout(500);
  }
  return null;
}


/** What the page looks like right now -- used to explain a failed open. */
async function pageDiagnostics(page) {
  const frames = [];
  for (const frame of page.frames()) {
    let hasApi = false;
    let keys = [];
    let functions = [];
    let commandLike = [];
    try {
      hasApi = await frame.evaluate(() => typeof window.callCommand === 'function');
      keys = await frame.evaluate(() =>
        Object.keys(window).filter((k) => /eda|editor|lc|api|doc|sch|pcb/i.test(k)).slice(0, 60));
      // Every global function, and every global object that looks like it
      // carries editor commands -- one of these is the API we need.
      functions = await frame.evaluate(() =>
        Object.keys(window).filter((k) => {
          try { return typeof window[k] === 'function' && !/^(webkit|on)/.test(k); } catch { return false; }
        }).slice(0, 200));
      commandLike = await frame.evaluate(() =>
        Object.keys(window).filter((k) => {
          try {
            const v = window[k];
            return v && typeof v === 'object'
              && ['getSource', 'applySource', 'createShape', 'getSelectedIds']
                .some((m) => typeof v[m] === 'function');
          } catch { return false; }
        }).slice(0, 40));
    } catch { /* cross-origin or gone */ }
    frames.push({
      url: frame.url().slice(0, 200), hasApi,
      interestingGlobals: keys, globalFunctions: functions, commandLikeObjects: commandLike,
    });
  }

  const text = await page.evaluate(() => document.body?.innerText?.slice(0, 600) ?? '').catch(() => '');
  return { url: page.url(), title: await page.title().catch(() => ''), frames, visibleText: text };
}

/**
 * Open a document and wait until the extension API is live. EasyEDA loads the
 * editor asynchronously, so the API appearing is the real ready signal --
 * 'load' fires long before the canvas exists. Returns the frame to talk to.
 */
async function openDoc(page, url) {
  // Editor URLs are #id=<container>|<document>. Which uuid goes where is not
  // documented, so on failure the other arrangements are tried rather than
  // giving up -- a wrong guess costs one reload, not a broken run.
  for (const candidate of urlVariants(url)) {
    await page.goto(candidate, { waitUntil: 'domcontentloaded' });
    const found = await waitForCommandFrame(page, 40_000);
    if (found) {
      if (candidate !== url) log(`    (opened via ${candidate.split('#')[1]})`);
      await page.waitForTimeout(2500);
      return found;
    }
  }

  const diag = await pageDiagnostics(page);
  throw new Error(
    `the editor never became driveable at ${url}\n`
    + `  tried: ${urlVariants(url).map((u) => u.split('#')[1]).join(', ')}\n`
    + `  landed on: ${diag.url}\n`
    + `  page title: ${diag.title || '(none)'}\n`
    + `  frames: ${diag.frames.map((f) => f.url).join(', ') || '(none)'}\n`
    + `  page text: ${(diag.visibleText || '').replace(/\s+/g, ' ').slice(0, 200)}\n`
    + '  run `npm run inspect` for a full dump and a screenshot',
  );
}

/**
 * The forms an editor URL might take, most likely first.
 *
 * A real one looks like #id=<container>|<document>, e.g. a schematic's
 * multi-sheet container and the sheet it opens on. When only one uuid is
 * known, or the pair is the wrong way round, these are the alternatives.
 */
function urlVariants(url) {
  const [base, hash = ''] = url.split('#id=');
  const parts = hash.split('|').filter(Boolean);
  const forms = [hash];
  if (parts.length === 2) {
    forms.push(`${parts[1]}|${parts[0]}`, parts[0], parts[1]);
  }
  return [...new Set(forms)].map((f) => `${base}#id=${f}`);
}


/**
 * Which document the editor currently has open, read from the source itself.
 *
 * The address bar is not a reliable signal: EasyEDA does not rewrite the hash
 * when you switch between a project's schematic and PCB, so the URL looks the
 * same either way. The docType in the source is the ground truth.
 */
async function activeDocKind(page) {
  for (const frame of await commandFrames(page)) {
    const raw = await frame.evaluate(() => String(window.doctype ?? '')).catch(() => '');
    if (!raw) continue;
    if (DOC_TYPES[raw]) return DOC_TYPES[raw];
    if (/pcb/i.test(raw)) return 'pcb';
    if (/sch/i.test(raw)) return 'sch';
  }
  return null;
}

/**
 * Make sure the editor is showing the schematic or the PCB, as asked.
 *
 * Returns the frame to use, or null if the right document could not be
 * reached -- in which case the caller must skip rather than export whatever
 * happens to be on screen, which would silently produce the wrong board's files.
 */
async function ensureDoc(page, wantKind, title) {
  if (await activeDocKind(page) === wantKind) return true;

  if (!title) return false;
  await installTreeHelpers(page);
  const clicked = await page.evaluate((t) => {
    const node = window.__edaFind(t);
    if (!node) return false;
    node.scrollIntoView({ block: 'center' });
    node.click();
    return true;
  }, title).catch(() => false);
  if (!clicked) return false;

  await page.waitForTimeout(3000);
  await waitForCommandFrame(page, 25_000);
  return (await activeDocKind(page)) === wantKind;
}



/** Accepts a Page or a Frame and hands back the owning Page. */
function pageOf(ctx) {
  return typeof ctx.page === 'function' ? ctx.page() : ctx;
}

function escapeRe(s) {
  return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}


/**
 * Run one menu-driven export and save the download straight to its final path.
 * Playwright intercepts the download, so nothing lands in ~/Downloads and there
 * is no filename to untangle afterwards.
 */
/**
 * Click whatever confirms an export dialog, if one opened.
 *
 * Some commands download straight away and some raise a dialog first, so this
 * looks briefly for a confirm button and shrugs if there isn't one.
 */
async function confirmDialog(page, labels = CONFIRM_LABELS, timeout = 20_000) {
  const wanted = Array.isArray(labels) ? labels : [labels];
  const deadline = Date.now() + timeout;

  while (Date.now() < deadline) {
    const clicked = await page.evaluate((want) => {
      // Buttons carry leading tick/cross icons ("√ Export", "✗ No, Generate
      // Gerber"), so those glyphs are stripped before comparing.
      const strip = (s) => (s || '')
        .replace(/[✓✔✕✖✗✘×✕]/g, ' ')
        .replace(/\s+/g, ' ').trim().toLowerCase();

      const buttons = [...document.querySelectorAll(
        'button, a.btn, input[type=button], input[type=submit], [class*="btn"]',
      )].filter((el) => el.getClientRects().length > 0 && !el.disabled);

      const label = (el) => strip(el.value || el.textContent);
      for (const w of want.map(strip)) {
        // Exact first: "Generate Gerber" must not steal "Yes, Generate Gerber".
        const hit = buttons.find((el) => label(el) === w)
          ?? buttons.find((el) => label(el).includes(w));
        if (hit) { hit.click(); return label(hit); }
      }
      return null;
    }, wanted).catch(() => null);

    if (clicked) return clicked;
    await page.waitForTimeout(250);
  }
  return null;
}

/**
 * Reject a download that isn't the file we asked for.
 *
 * EasyEDA names its downloads by type, so the offered filename is a reliable
 * check that the right export ran. Without it a stale dialog quietly saves the
 * previous file under the new name -- an Altium .schdoc that is really an SVG
 * looks fine in a directory listing and is only noticed much later.
 */
function checkDownload(download, wantExt) {
  const suggested = download.suggestedFilename() || '';
  const gotExt = path.extname(suggested).replace('.', '').toLowerCase();
  if (!gotExt || !wantExt) return;
  if (gotExt !== wantExt.toLowerCase()) {
    throw new Error(
      `expected a .${wantExt} but EasyEDA offered "${suggested}" `
      + '-- a dialog from a previous export was probably still open',
    );
  }
}

/**
 * Close any dialog left open, so the next export starts from a clean editor.
 *
 * This matters more than it sounds: a dialog left open from the previous
 * export still has a live Download button, and the next export's confirm click
 * lands on it -- silently saving the previous file under the new name.
 */
async function closeDialogs(page, timeout = 6000) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    const remaining = await page.evaluate(() => {
      const modals = [...document.querySelectorAll(
        '[role=dialog], [class*="modal"], [class*="dialog"], [class*="Modal"], [class*="Dialog"]',
      )].filter((el) => el.getClientRects().length > 0);
      if (!modals.length) return 0;
      for (const modal of modals) {
        const close = modal.querySelector(
          '[class*="close"], .close, button[aria-label*="close" i], [title*="close" i]',
        );
        if (close) close.click();
      }
      return modals.length;
    }).catch(() => 0);

    if (!remaining) return true;
    await page.keyboard.press('Escape').catch(() => {});
    await page.waitForTimeout(400);
  }
  return false;
}

/**
 * Pick the PhotoView board side. The user-facing control is a "Board side"
 * selector; the photoView_changeSide_* commands exist too but did not take
 * effect, which produced two identical renders.
 */
async function chooseBoardSide(page, label) {
  return page.evaluate((want) => {
    const norm = (s) => (s || '').replace(/\s+/g, ' ').trim().toLowerCase();
    const target = norm(want);

    for (const select of document.querySelectorAll('select')) {
      const option = [...select.options].find((o) => norm(o.textContent) === target);
      if (option) {
        select.value = option.value;
        select.dispatchEvent(new Event('change', { bubbles: true }));
        return 'select';
      }
    }
    const el = [...document.querySelectorAll('a,li,span,div,label,button,option')]
      .find((e) => e.getClientRects().length > 0 && norm(e.textContent) === target);
    if (el) { el.click(); return 'click'; }
    return null;
  }, label).catch(() => null);
}

/**
 * Tick a disclaimer checkbox, e.g. the Altium export's "I have read and
 * agree..." which keeps its Download button disabled until accepted.
 *
 * Only checkboxes whose surrounding text reads like an agreement are touched,
 * so unrelated options ("don't show again") are left alone.
 */
async function acceptAgreements(page, timeout = 12_000) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    const ticked = await page.evaluate(() => {
      const boxes = [...document.querySelectorAll('input[type=checkbox]')]
        .filter((b) => b.getClientRects().length > 0 && !b.checked);
      let n = 0;
      for (const box of boxes) {
        const context = (box.closest('label') ?? box.parentElement ?? box).textContent ?? '';
        if (/agree|have read|accept|terms|disclaim/i.test(context)) { box.click(); n++; }
      }
      return n;
    }).catch(() => 0);
    if (ticked) return ticked;
    await page.waitForTimeout(300);
  }
  return 0;
}

/**
 * Run one export and save the download straight to its final path.
 *
 * Exports are dispatched through the editor's own command ids rather than by
 * clicking menus -- the ids come from the `cmd` attributes in the menu markup,
 * so this does not depend on menu wording, layout, or language.
 *
 * Playwright intercepts the download, so nothing lands in ~/Downloads and
 * there is no filename to untangle afterwards.
 */
async function runExport(page, spec, outPath, { dryRun }) {
  if (dryRun) { log(`    would export -> ${path.basename(outPath)}`); return true; }

  const setup = spec.cmds.slice(0, -1);
  const fire = spec.cmds[spec.cmds.length - 1];

  try {
    // Never start with a dialog still up from the previous export.
    await closeDialogs(page);

    for (const cmd of setup) {
      await runCommand(page, cmd);
      await page.waitForTimeout(2000);
    }

    const [download] = await Promise.all([
      page.waitForEvent('download', { timeout: spec.slow ? SLOW_EXPORT_TIMEOUT : EXPORT_TIMEOUT }),
      (async () => {
        await runCommand(page, fire);
        if (spec.agree) await acceptAgreements(page);
        // An explicit sequence walks the chained dialogs in order (Gerber has
        // three); otherwise fall back to whatever confirm button turns up.
        if (spec.confirm) {
          for (const label of spec.confirm) await confirmDialog(page, [label]);
        } else {
          await confirmDialog(page, CONFIRM_LABELS, 6000);
        }
      })(),
    ]);
    checkDownload(download, spec.ext);
    await download.saveAs(outPath);
    log(`    ${path.basename(outPath)}`);
    return true;
  } catch (err) {
    warn(`${spec.cmds.join(' -> ')} failed: ${err.message.split('\n')[0]}`);
    await page.keyboard.press('Escape').catch(() => {});
    return false;
  }
}

/**
 * Export the PhotoView renders. Enters PhotoView once, then does each side --
 * re-entering per side would toggle straight back out to the PCB.
 */
async function exportPhotoView(page, nameFor, { dryRun }) {
  if (dryRun) {
    for (const side of PHOTOVIEW.sides) log(`    would export -> ${path.basename(nameFor(side.type))}`);
    return { ok: PHOTOVIEW.sides.length, failed: 0 };
  }

  try {
    await closeDialogs(page);
    await runCommand(page, PHOTOVIEW.enter);
    await page.waitForTimeout(4000);
  } catch (err) {
    warn(`could not enter PhotoView: ${err.message.split('\n')[0]}`);
    return { ok: 0, failed: PHOTOVIEW.sides.length };
  }

  let ok = 0;
  let failed = 0;
  for (const side of PHOTOVIEW.sides) {
    const outPath = nameFor(side.type);
    try {
      await closeDialogs(page);
      // Prefer the visible "Board side" control; the command alone left both
      // sides rendering identically.
      const how = await chooseBoardSide(page, side.label);
      if (!how) await runCommand(page, side.cmd);
      await page.waitForTimeout(3000);

      const [download] = await Promise.all([
        page.waitForEvent('download', { timeout: EXPORT_TIMEOUT }),
        (async () => {
          await runCommand(page, PHOTOVIEW.exportCmd);
          await confirmDialog(page, PHOTOVIEW.confirm);
        })(),
      ]);
      checkDownload(download, PHOTOVIEW.ext);
      await download.saveAs(outPath);
      log(`    ${path.basename(outPath)}`);
      ok++;
    } catch (err) {
      warn(`PhotoView ${side.type} failed: ${err.message.split('\n')[0]}`);
      await page.keyboard.press('Escape').catch(() => {});
      failed++;
    }
  }
  return { ok, failed };
}

// -------------------------------------------------------------- the commands

async function cmdLogin() {
  const context = await launch();
  const page = context.pages()[0] ?? await context.newPage();
  await page.goto('https://easyeda.com/editor');
  log('\nA browser window is open. Sign in to EasyEDA there.');
  log('The session is saved to circuit/tools/.eda-profile, so this is a one-time step.');
  await prompt('\nPress Enter once you are signed in and can see your projects... ');
  await context.close();
  log('Saved. You can now run: npm run discover');
}

/**
 * Pull the projects out of an API response: things with a uuid and a name but
 * no docType (that would make them documents). Deliberately loose, since the
 * folder listing's exact shape isn't documented anywhere.
 */
function harvestProjects(node, into = [], seen = new Set()) {
  if (!node || typeof node !== 'object') return into;
  if (Array.isArray(node)) {
    for (const item of node) harvestProjects(item, into, seen);
    return into;
  }
  const uuid = node.uuid ?? node.projectUuid ?? node.project_uuid;
  const title = String(node.title ?? node.name ?? '').trim();
  const isDoc = (node.docType ?? node.doctype) !== undefined;
  if (uuid && title && !isDoc && !seen.has(uuid)) {
    seen.add(uuid);
    into.push({ uuid: String(uuid), title });
  }
  for (const value of Object.values(node)) harvestProjects(value, into, seen);
  return into;
}

/**
 * Attach documents to the project they belong to, when the listing gave us
 * enough to do it. Returns [] to signal that each project has to be opened.
 */
function groupDocsByProject(docs, projects) {
  if (!docs.length) return [];

  const byUuid = new Map(projects.map((p) => [p.uuid, { project: p.title, docs: [] }]));
  let attached = 0;
  for (const doc of docs) {
    const owner = doc.project && byUuid.get(doc.project);
    if (owner) { owner.docs.push(doc); attached++; }
  }
  if (attached) return [...byUuid.values()].filter((p) => p.docs.length);

  // No linkage, but a single project means there is no ambiguity to resolve.
  if (projects.length === 1) return [{ project: projects[0].title, docs }];
  return [];
}

/**
 * Any real editor links on the page. Scraping the href EasyEDA itself uses
 * beats reconstructing the #id=<container>|<document> hash from uuids.
 */
async function collectEditorLinks(page) {
  return page.evaluate(() =>
    [...document.querySelectorAll('a[href*="editor"]')]
      .map((a) => ({ href: a.href, text: a.textContent.trim().slice(0, 80) }))
      .filter((l) => l.href.includes('#id=')),
  ).catch(() => []);
}

/**
 * Harvest documents from responses that arrived after `before`, waiting for
 * slow ones rather than reading once and giving up. A project whose response
 * lands a little late otherwise looks like a project with no documents.
 */
async function harvestAfter(page, captured, before, timeout = 12_000) {
  const deadline = Date.now() + timeout;
  let docs = harvestDocs(captured.slice(before).map((c) => c.body));
  while (!docs.length && Date.now() < deadline) {
    await page.waitForTimeout(500);
    docs = harvestDocs(captured.slice(before).map((c) => c.body));
  }
  return docs;
}

/**
 * Open a project so the site fetches its documents, then come back.
 *
 * `by` selects how the project is located: 'uuid' is unambiguous and is tried
 * first; 'title' is the fallback for markup that doesn't carry the uuid
 * anywhere. Titles alone are unsafe here because one project's name is often a
 * prefix of another's ("AP - Controller" vs "AP - Controller - 12 volt power"),
 * so a title click can silently land on the wrong row.
 */
async function openProject(page, project, by = 'uuid') {
  await installTreeHelpers(page);
  const before = page.url();

  const clicked = await page.evaluate(({ uuid, title, by: how }) => {
    const node = how === 'uuid' ? window.__edaFindByUuid(uuid) : window.__edaFind(title);
    if (!node) return false;
    node.scrollIntoView({ block: 'center' });
    node.click();
    return true;
  }, { uuid: project.uuid, title: project.title, by });
  if (!clicked) return false;

  await page.waitForLoadState('networkidle').catch(() => {});
  await page.waitForTimeout(2500);

  // Only navigate back if the click actually navigated -- it may just have
  // expanded the row in place, and going back would undo the folder itself.
  if (page.url() !== before) {
    await page.goto(before, { waitUntil: 'domcontentloaded' }).catch(() => {});
    await page.waitForLoadState('networkidle').catch(() => {});
    await page.waitForTimeout(1500);
  }
  return true;
}

/** The real workspace. easyeda.com/page/user is the marketing portal, not this. */
const WORKSPACE = 'https://easyeda.com/account/user/projects/all';

/** EasyEDA docType codes, as seen in the exported JSON headers. */
const DOC_TYPES = { 1: 'sch', 3: 'pcb', 5: 'sch' };

/**
 * Pull every {uuid, title, docType} out of an arbitrary API response. Saves
 * having to know EasyEDA's response shape -- which nests documents differently
 * depending on the endpoint -- and keeps working if they reshape it.
 */
function harvestDocs(node, into = [], seen = new Set()) {
  if (!node || typeof node !== 'object') return into;
  if (Array.isArray(node)) {
    for (const item of node) harvestDocs(item, into, seen);
    return into;
  }
  const uuid = node.uuid ?? node.docUuid ?? node.id;
  const type = node.docType ?? node.doctype;
  if (uuid && type !== undefined && DOC_TYPES[String(type)] && !seen.has(uuid)) {
    seen.add(uuid);
    into.push({
      uuid: String(uuid),
      title: String(node.title ?? node.name ?? '').trim(),
      docType: String(type),
      kind: DOC_TYPES[String(type)],
      project: String(node.projectUuid ?? node.project_uuid ?? node.ownerUuid ?? '').trim() || undefined,
      editorUrl: `https://easyeda.com/editor#id=${uuid}`,
    });
  }
  for (const value of Object.values(node)) harvestDocs(value, into, seen);
  return into;
}

/** Every visible label in the sidebar tree -- the diagnostic when a step misses. */
async function treeLabels(page) {
  return page.evaluate(() =>
    [...document.querySelectorAll('li, a, span, div')]
      .filter((el) => {
        const own = [...el.childNodes]
          .filter((n) => n.nodeType === 3).map((n) => n.textContent.trim()).join('');
        return own && own.length < 40 && el.getClientRects().length > 0;
      })
      .map((el) => el.textContent.trim().slice(0, 40))
      .filter((t, i, a) => t && a.indexOf(t) === i)
      .slice(0, 120),
  );
}

/**
 * Install a tolerant node finder in the page.
 *
 * Tree labels are not clean text: they pick up non-breaking spaces, nested
 * markup and trailing badges ("Marco Scott 3"), so Playwright's exact text
 * matching misses them. This normalises whitespace and, failing an exact hit,
 * accepts a slightly longer containing label. The deepest match wins, so we
 * click the label itself rather than the whole row.
 */
async function installTreeHelpers(page) {
  await page.evaluate(() => {
    // Labels are littered with non-breaking spaces; fold them into plain ones.
    const norm = (s) => (s || '').replace(/\u00a0/g, ' ').replace(/\s+/g, ' ').trim().toLowerCase();
    const depth = (el) => { let d = 0; for (let n = el; n.parentElement; n = n.parentElement) d++; return d; };
    /** Tidied but case-preserving, for labels we hand back to the caller. */
    const norm2 = (s) => (s || '').replace(/\u00a0/g, ' ').replace(/\s+/g, ' ').trim();

    window.__edaFind = (label) => {
      const want = norm(label);
      const all = [...document.querySelectorAll('li,span,div,a,p,label,td')]
        .filter((el) => el.getClientRects().length > 0);
      let hits = all.filter((el) => norm(el.textContent) === want);
      if (!hits.length) {
        hits = all.filter((el) => {
          const t = norm(el.textContent);
          return t.includes(want) && t.length <= want.length + 12;
        });
      }
      return hits.sort((a, b) => depth(b) - depth(a))[0] || null;
    };

    window.__edaVisible = (label) => !!window.__edaFind(label);

    const ROW_SEL = '[role="treeitem"],li,[class*="tree-node"],[class*="treenode"],'
      + '[class*="tree-item"],[class*="treeitem"],[class*="node-item"]';
    const GROUP_SEL = 'ul,[role="group"],[class*="children"],[class*="subtree"],[class*="tree-child"]';

    /**
     * The row element a label sits in. EasyEDA's tree is not ul/li, so this
     * accepts the common tree-row markers and falls back to the label's parent
     * rather than returning null -- callers used to crash on that.
     */
    window.__edaRowOf = (node) => node.closest(ROW_SEL) ?? node.parentElement ?? node;

    /** The container holding a row's children, if the tree nests them. */
    window.__edaGroupOf = (row) => row?.querySelector(GROUP_SEL) ?? null;

    /**
     * The expand toggle on a row. Falls back to "the first thing on the row
     * that isn't the label" -- the usual tree markup -- when the class names
     * are ones we don't recognise.
     */
    window.__edaCaret = (row, labelNode) => {
      if (!row) return null;
      return row.querySelector(
        '[class*="arrow"],[class*="caret"],[class*="toggle"],[class*="switcher"],[class*="expand"]',
      ) ?? [...row.children].find(
        (c) => c !== labelNode && !c.contains(labelNode) && !c.matches(GROUP_SEL),
      ) ?? null;
    };

    /** A row's own label: its longest child text, ignoring any nested children. */
    const rowLabel = (row) => {
      const clone = row.cloneNode(true);
      clone.querySelectorAll(GROUP_SEL).forEach((n) => n.remove());
      const best = [...clone.children]
        .map((c) => norm2(c.textContent)).filter(Boolean)
        .sort((a, b) => b.length - a.length)[0];
      return (best || norm2(clone.textContent)).slice(0, 80);
    };

    /**
     * The labels of a node's child rows, i.e. the projects inside a folder.
     *
     * Handles both tree shapes: children nested inside the parent row, and
     * children as flat following siblings one aria-level deeper.
     */
    window.__edaChildren = (label) => {
      const node = window.__edaFind(label);
      if (!node) return [];
      const row = window.__edaRowOf(node);

      const group = window.__edaGroupOf(row);
      if (group) return [...group.children].map(rowLabel).filter(Boolean);

      const level = Number(row?.getAttribute('aria-level') || 0);
      if (!level) return [];
      const out = [];
      for (let sib = row.nextElementSibling; sib; sib = sib.nextElementSibling) {
        const l = Number(sib.getAttribute('aria-level') || 0);
        if (!l || l <= level) break;
        if (l === level + 1) out.push(rowLabel(sib));
      }
      return out.filter(Boolean);
    };

    /**
     * Open a row, leaving it open. Checks the row's state first -- via
     * aria-expanded, or the child container's visibility -- so an
     * already-expanded row is never toggled shut.
     */
    window.__edaExpandRow = (label) => {
      const node = window.__edaFind(label);
      if (!node) return 'missing';
      const row = window.__edaRowOf(node);
      node.click();

      const expanded = row?.getAttribute('aria-expanded');
      if (expanded === 'true') return 'ok';

      // No child container means the level is unloaded, not that it is open.
      const group = window.__edaGroupOf(row);
      const hidden = !group || getComputedStyle(group).display === 'none';
      if (expanded === 'false' || hidden) {
        const caret = window.__edaCaret(row, node);
        if (caret) caret.click();
      }
      return 'ok';
    };

    /**
     * Find the element for a project by its uuid, wherever the uuid is stashed
     * -- href, data attribute, id. Unambiguous in a way titles are not: one
     * project's name is often a prefix of another's.
     */
    window.__edaFindByUuid = (uuid) => {
      const visible = (el) => el.getClientRects().length > 0;
      const direct = [...document.querySelectorAll(
        `a[href*="${uuid}"],[data-uuid="${uuid}"],[data-id="${uuid}"],[id*="${uuid}"]`,
      )].filter(visible);
      if (direct.length) return direct[0];

      for (const el of document.querySelectorAll('*')) {
        if (!visible(el)) continue;
        for (const attr of el.attributes) {
          if (attr.value.includes(uuid)) return el;
        }
      }
      return null;
    };

    /** The row markup around a label, for working out an unfamiliar tree. */
    window.__edaRowHtml = (label) => {
      const node = window.__edaFind(label);
      if (!node) return null;
      const row = window.__edaRowOf(node);
      return {
        rowTag: row?.tagName, rowClass: row?.className?.toString?.().slice(0, 120),
        ariaExpanded: row?.getAttribute('aria-expanded'),
        ariaLevel: row?.getAttribute('aria-level'),
        html: (row?.parentElement ?? row)?.outerHTML?.slice(0, 4000),
      };
    };

    /** Every visible label that contains the text -- shown when a step misses. */
    window.__edaNear = (label) => {
      const want = norm(label).split(' ')[0];
      return [...new Set([...document.querySelectorAll('li,span,div,a')]
        .filter((el) => el.getClientRects().length > 0 && norm(el.textContent).includes(want))
        .map((el) => el.textContent.trim().slice(0, 60)))].slice(0, 20);
    };
  });
}

/** Poll until a label shows up. Tree levels load over the network, not instantly. */
async function waitForNode(page, label, timeout = 20_000) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    if (await page.evaluate((l) => window.__edaVisible(l), label)) return true;
    await page.waitForTimeout(400);
  }
  return false;
}

/**
 * Poll until a node's children appear. A folder's contents are fetched when it
 * opens, so a fixed wait is either too short on a slow response or wasted time
 * on a fast one.
 */
async function waitForChildren(page, label, timeout = 25_000) {
  const deadline = Date.now() + timeout;
  let rows = [];
  while (Date.now() < deadline) {
    rows = await page.evaluate((l) => window.__edaChildren(l), label);
    if (rows.length) return rows;
    await page.waitForTimeout(400);
  }
  return rows;
}

/** Click the expand caret on a label's row. Returns false if there isn't one. */
async function clickCaret(page, label) {
  return page.evaluate((l) => {
    const node = window.__edaFind(l);
    if (!node) return false;
    const caret = window.__edaCaret(window.__edaRowOf(node), node);
    if (!caret) return false;
    caret.click();
    return true;
  }, label);
}

/**
 * Walk down the sidebar project tree, expanding as it goes.
 *
 * The boards sit under Participated > <team> > <folder>, and each level is
 * fetched only when its parent opens -- so each step polls for the next label
 * rather than assuming it appears immediately.
 *
 * Clicking a label usually expands it. When it doesn't, the row's caret is
 * tried instead -- but only after the poll has genuinely timed out, and the
 * caret click is undone if it turns out to have collapsed an already-open
 * node rather than opening a closed one.
 */
async function openTreePath(page, labels) {
  await installTreeHelpers(page);

  for (const [i, label] of labels.entries()) {
    if (!(await waitForNode(page, label))) {
      const near = await page.evaluate((l) => window.__edaNear(l), label);
      return { ok: false, failedAt: label, reached: labels.slice(0, i), near };
    }

    // Expand every node, including the last one -- the walk is often split
    // across calls, so the final node of one call is the parent of the next.
    await page.evaluate((l) => window.__edaFind(l).scrollIntoView({ block: 'center' }), label);
    await page.evaluate((l) => window.__edaExpandRow(l), label);

    const next = labels[i + 1];
    if (!next) { log(`  opened "${label}"`); break; }

    // Give the child level time to load before deciding the click failed.
    if (await waitForNode(page, next)) { log(`  opened "${label}"`); continue; }

    // Still nothing: nudge the caret once more, in case the row only became
    // expandable after its contents finished loading.
    if (await clickCaret(page, label)) {
      if (await waitForNode(page, next)) { log(`  opened "${label}" (via caret)`); continue; }
      // That collapsed an already-open node instead. Put it back.
      await clickCaret(page, label);
      if (await waitForNode(page, next, 5000)) { log(`  opened "${label}"`); continue; }
    }

    // Report the truth rather than claiming success: the node was clicked but
    // its children never showed up, so the next step is going to fail.
    warn(`"${label}" did not reveal "${next}"`);
  }

  return { ok: true, reached: labels };
}

/**
 * Read the team's project/document tree.
 *
 * The boards are not in the personal workspace -- they belong to the TEAM,
 * inside the FOLDER. So this switches to the team first, opens that folder,
 * and only then harvests, which keeps unrelated personal projects out.
 *
 * Rather than guessing endpoint names, it watches the JSON the site fetches
 * for itself. Whatever EasyEDA actually calls, we capture it.
 */
async function cmdDiscover() {
  const context = await launch();
  const page = context.pages()[0] ?? await context.newPage();

  const captured = [];
  page.on('response', async (res) => {
    const url = res.url();
    if (!/\/api\//.test(url) || !res.ok()) return;
    if (!(res.headers()['content-type'] || '').includes('json')) return;
    try { captured.push({ url, body: await res.json() }); } catch { /* not json after all */ }
  });

  // Straight to the folder when we have its URL; otherwise fall back to
  // finding it by name in the sidebar tree.
  let trail = { ok: true, reached: ['(direct url)'] };
  if (FOLDER_URL) {
    log(`Opening ${FOLDER_URL}`);
    await page.goto(FOLDER_URL, { waitUntil: 'domcontentloaded' });
    await page.waitForLoadState('networkidle').catch(() => {});
    await page.waitForTimeout(4000);
  } else {
    log(`Opening ${WORKSPACE}`);
    await page.goto(WORKSPACE, { waitUntil: 'domcontentloaded' });
    await page.waitForLoadState('networkidle').catch(() => {});
    await page.waitForTimeout(4000);

    trail = await openTreePath(page, TREE_PATH);
    if (!trail.ok) {
      warn(`stopped at "${trail.failedAt}" -- it was not visible in the tree`);
      warn(`got as far as: ${trail.reached.join(' > ') || '(nothing)'}`);
    }
    await waitForChildren(page, FOLDER);
  }

  const afterNav = page.url();

  // Landing on the folder page loads its contents, so everything captured so
  // far belongs to this folder -- no need to filter by when it arrived.
  const folderDocs = harvestDocs(captured.map((c) => c.body));
  const projects = harvestProjects(captured.map((c) => c.body));
  log(`  ${projects.length} project(s), ${folderDocs.length} document(s) in the folder listing`);

  // If the listing already carries each project's documents, we are done.
  // Otherwise open each project so the site fetches its documents for us.
  let perProject = groupDocsByProject(folderDocs, projects);

  if (!perProject.length) {
    log('  listing has no documents; opening each project');
    perProject = [];
    for (const project of projects) {
      // Try by uuid, then by title. A project that comes back with nothing is
      // usually one whose click landed on the wrong row, not one with no
      // documents -- so retry rather than accepting the zero.
      let docs = [];
      let used = null;
      for (const by of ['uuid', 'title']) {
        const before = captured.length;
        if (!(await openProject(page, project, by))) continue;
        docs = await harvestAfter(page, captured, before);
        used = by;
        if (docs.length) break;
      }

      // Real editor links beat any URL we construct, so grab them while the
      // project is open.
      const links = await collectEditorLinks(page);
      perProject.push({ project: project.title, docs, links, foundBy: used });
      const how = used && docs.length ? ` (by ${used})` : '';
      log(`    ${docs.length ? '' : '! '}${project.title} -- ${docs.length} document(s)${how}`);
    }
  }

  const docs = perProject.flatMap((p) => p.docs);
  const draft = buildDraftProjects(perProject);

  await writeFile(path.join(HERE, 'discover-dump.json'), JSON.stringify({
    source: FOLDER_URL || WORKSPACE,
    usedDirectUrl: Boolean(FOLDER_URL),
    treeWalk: trail,
    urlAfterNavigation: afterNav,
    endpointsSeen: [...new Set(captured.map((c) => c.url.replace(/[?#].*$/, '')))],
    projects,
    perProject: perProject.map((p) => ({ project: p.project, docs: p.docs.length, foundBy: p.foundBy })),
    docs,
    captured,
  }, null, 2));
  await writeFile(path.join(HERE, 'projects.draft.json'), JSON.stringify(draft, null, 2));

  log(`\nWrote discover-dump.json  (${captured.length} API responses, ${docs.length} documents)`);
  log(`Wrote projects.draft.json (${draft.length} boards)`);
  if (!docs.length) {
    warn('no documents found -- send discover-dump.json so the endpoint can be identified');
  } else {
    log('\nCheck projects.draft.json, then rename it to projects.json.');
  }
  await context.close();
}

/** Every file a full export of one board produces, in write order. */
function plannedFiles(project) {
  const files = [];
  for (const kind of ['sch', 'pcb']) {
    if (!project[kind]) continue;
    const prefix = kind === 'sch' ? 'SCH' : 'PCB';
    for (const spec of EXPORTS.filter((e) => e.doc === kind)) {
      files.push(fileName(prefix, project.board, spec.type, spec.ext));
    }
    if (kind === 'pcb') {
      for (const side of PHOTOVIEW.sides) {
        files.push(fileName(prefix, project.board, side.type, PHOTOVIEW.ext));
      }
    }
  }
  return files;
}

/**
 * Turn {project, docs} pairs into a projects.json skeleton.
 *
 * The board name comes from the project's own row in the tree, so it matches
 * what is on screen, and it doubles as the output directory -- there is
 * nothing to fill in by hand.
 */
function buildDraftProjects(perProject) {
  return perProject
    .map(({ project, docs, links = [] }) => ({
      // "AP - Sensor - Wind" -> "AP-Sensor-Wind".
      board: project.replace(/[\s-]+/g, '-').replace(/^-|-$/g, ''),
      sch: docUrl(docs, 'sch', links),
      pcb: docUrl(docs, 'pcb', links),
      // Used to switch documents inside the editor when the URL alone does not
      // select one -- EasyEDA keeps the same hash for a project's schematic
      // and PCB.
      schTitle: mainDoc(docs, 'sch')?.title ?? '',
      pcbTitle: mainDoc(docs, 'pcb')?.title ?? '',
    }))
    .filter((p) => p.sch || p.pcb);
}

/**
 * The document that represents a board's schematic or PCB. For schematics
 * docType 5 is the whole multi-sheet schematic and 1 is a single sheet --
 * prefer 5, so a multi-sheet board exports as one document, not just sheet one.
 */
function mainDoc(docs, kind) {
  const of = docs.filter((d) => d.kind === kind);
  return of.find((d) => d.docType === '5') ?? of[0] ?? null;
}

/**
 * The editor URL for a board's schematic or PCB.
 *
 * A real link scraped off the page always wins. Otherwise the URL is built as
 * #id=<container>|<document>: for a schematic that is the multi-sheet
 * container (docType 5) and the sheet it opens on (docType 1). openDoc retries
 * the other arrangements if this one turns out to be wrong.
 */
function docUrl(docs, kind, links) {
  const of = docs.filter((d) => d.kind === kind);
  const main = mainDoc(docs, kind);
  if (!main) return '';

  const real = links.find((l) => l.href.includes(main.uuid));
  if (real) return real.href;

  const sheet = kind === 'sch' ? of.find((d) => d.docType === '1' && d.uuid !== main.uuid) : null;
  const hash = sheet ? `${main.uuid}|${sheet.uuid}` : main.uuid;
  return `https://easyeda.com/editor#id=${hash}`;
}

/**
 * Dump the editor's real menu tree. This is what turns the guessed labels in
 * EXPORTS into verified ones.
 */
async function cmdInspect() {
  const projects = await loadProjects();
  const first = projects[0];
  if (!first) throw new Error('projects.json has no entries yet -- run discover first');

  const context = await launch();
  const page = context.pages()[0] ?? await context.newPage();

  const url = first.pcb || first.sch;
  log(`Opening ${url}`);
  await page.goto(url, { waitUntil: 'domcontentloaded' });

  // Deliberately does not use openDoc: when the editor fails to come up, the
  // whole point of inspect is to report why rather than throw.
  const cmdFrame = await waitForCommandFrame(page, 60_000);
  const diag = await pageDiagnostics(page);
  const shot = path.join(HERE, 'inspect-screenshot.png');
  await page.screenshot({ path: shot, fullPage: false }).catch(() => {});

  log(`  landed on:  ${diag.url}`);
  log(`  page title: ${diag.title || '(none)'}`);
  log(`  frames:     ${diag.frames.length}`);
  for (const f of diag.frames) log(`    callCommand=${f.hasApi}  ${f.url}`);

  // Check every command EXPORTS depends on against the editor's own registry.
  // A wrong id otherwise shows up as a download that never arrives.
  const wanted = [...new Set(EXPORTS.flatMap((e) => e.cmds))];
  const frames = await commandFrames(page);
  log('\n  command check:');
  const commandCheck = [];
  for (const cmd of wanted) {
    const owners = [];
    for (const frame of frames) if (await canHandle(frame, cmd)) owners.push(frame.url());
    commandCheck.push({ cmd, owners });
    log(`    ${owners.length ? 'ok  ' : 'MISSING'} ${cmd}`
      + (owners.length ? `  (${owners.length} frame${owners.length > 1 ? 's' : ''})` : ''));
  }

  // Describe the command surface and the menus in every frame that has
  // anything. The command ids in `withCmd` are what EXPORTS is built from.
  const perFrame = [];
  for (const frame of page.frames()) {
    const globals = await describeGlobals(frame);
    const menus = await describeMenus(frame);
    const registry = await commandNames(frame);
    if (!Object.keys(globals).length && !menus.withCmd.length && !registry.hooks.length) continue;
    perFrame.push({ url: frame.url().slice(0, 200), globals, registry, ...menus });
  }

  await writeFile(path.join(HERE, 'menu-dump.json'), JSON.stringify({
    opened: url,
    commandFrame: cmdFrame?.url() ?? null,
    ...diag,
    commandCheck,
    frameDetail: perFrame,
  }, null, 2));

  log('');
  if (cmdFrame) log(`  command frame: ${cmdFrame.url()}`);
  else warn('no frame exposing callCommand -- the editor did not finish loading');
  for (const f of perFrame) {
    const names = Object.keys(f.globals);
    if (!names.length && !f.withCmd.length) continue;
    log(`  ${f.url}`);
    for (const [name, g] of Object.entries(f.globals)) {
      const detail = g.type === 'function'
        ? `function(${g.arity} args)`
        : `${g.type}, ${(g.methods ?? []).length} methods`;
      log(`    window.${name}: ${detail}`);
    }
    if (f.withCmd.length) log(`    elements with a cmd attribute: ${f.withCmd.length}`);
    if (f.registry?.hooks?.length) log(`    callCommand knows ${f.registry.hooks.length} commands`);
  }

  log(`\nWrote circuit/tools/menu-dump.json and inspect-screenshot.png`);
  log('\nLeaving the browser open so the File > Export menu can be opened by hand.');
  await prompt('Press Enter to close... ');
  await context.close();
}

async function cmdExport({ only, dryRun }) {
  let projects = await loadProjects();
  if (only) {
    const want = only.toLowerCase();
    // An exact board name wins over a substring, so `--only Display` means the
    // Display board and not also Display-LCD and Display-Button. Substring
    // matching still works for picking a group, or for typing less.
    const exact = projects.filter((p) => p.board.toLowerCase() === want);
    projects = exact.length ? exact : projects.filter((p) => p.board.toLowerCase().includes(want));
    if (!projects.length) throw new Error(`no project matches --only ${only}`);
    if (projects.length > 1) log(`--only ${only} matched: ${projects.map((p) => p.board).join(', ')}\n`);
  }

  // A dry run is a pure filename preview -- no browser, no login, no network.
  // Useful for checking the naming convention before committing to a real run.
  if (dryRun) {
    for (const project of projects) {
      log(`\ncircuit/${project.board}/`);
      for (const file of plannedFiles(project)) log(`  ${file}`);
    }
    log(`\n${projects.length} board(s), ${projects.reduce((n, p) => n + plannedFiles(p).length, 0)} files. Nothing written.`);
    return;
  }

  const context = await launch();
  const page = context.pages()[0] ?? await context.newPage();
  const summary = [];

  for (const project of projects) {
    // One directory per board, all at the same level under circuit/.
    const outDir = path.join(CIRCUIT, project.board);
    log(`\n${project.board}  ->  circuit/${project.board}`);
    if (!dryRun) await mkdir(outDir, { recursive: true });

    const name = (prefix, type, ext) => path.join(outDir, fileName(prefix, project.board, type, ext));
    let ok = 0, failed = 0;

    for (const kind of ['sch', 'pcb']) {
      const url = project[kind];
      if (!url) { warn(`no ${kind} url configured, skipping`); continue; }

      log(`  ${kind === 'sch' ? 'schematic' : 'pcb'}`);
      try {
        await openDoc(page, url);
      } catch (err) {
        warn(`could not open ${kind}: ${err.message.split('\n')[0]}`);
        failed++;
        continue;
      }

      // The URL alone does not prove which document loaded, so confirm it and
      // switch if needed. Exporting the wrong one would fail silently.
      if (!(await ensureDoc(page, kind, project[`${kind}Title`]))) {
        warn(`the ${kind} document did not come up (editor is showing `
          + `${await activeDocKind(page) ?? 'nothing'}) -- skipping to avoid wrong files`);
        failed++;
        continue;
      }

      const prefix = kind === 'sch' ? 'SCH' : 'PCB';

      for (const spec of EXPORTS.filter((e) => e.doc === kind)) {
        const done = await runExport(page, spec, name(prefix, spec.type, spec.ext), { dryRun });
        done ? ok++ : failed++;
      }

      if (kind === 'pcb') {
        const photo = await exportPhotoView(
          page, (type) => name(prefix, type, PHOTOVIEW.ext), { dryRun });
        ok += photo.ok;
        failed += photo.failed;
      }
    }

    summary.push({ board: project.board, ok, failed });
  }

  await context.close();

  log('\n' + '-'.repeat(48));
  for (const s of summary) {
    log(`  ${s.board.padEnd(24)} ${String(s.ok).padStart(2)} ok  ${s.failed ? `${s.failed} failed` : ''}`);
  }
  const totalFailed = summary.reduce((n, s) => n + s.failed, 0);
  if (totalFailed) log(`\n${totalFailed} export(s) failed -- see the warnings above.`);
}

/**
 * Board directories under circuit/ that no longer match projects.json --
 * what a project rename in EasyEDA leaves behind.
 */
async function cmdStale({ dryRun }) {
  const projects = await loadProjects();
  const current = new Set(projects.map((p) => p.board));
  const entries = await readdir(CIRCUIT, { withFileTypes: true });

  // Only consider directories this tool would itself have produced, so
  // hand-made folders and tools/ are never proposed for deletion.
  const stale = [];
  for (const entry of entries) {
    if (!entry.isDirectory() || entry.name === 'tools' || current.has(entry.name)) continue;
    const files = await readdir(path.join(CIRCUIT, entry.name)).catch(() => []);
    const ours = files.filter((f) => /^(SCH|PCB)_.+_(EasyEDA|SVG|PNG|Altium|Gerber|Autorouter|DXF|OBJ|PhotoView-\w+)\./.test(f));
    if (ours.length) stale.push({ dir: entry.name, files: ours.length, total: files.length });
  }

  if (!stale.length) { log('No stale export directories.'); return; }

  log('Directories holding exports for boards not in projects.json:\n');
  for (const s of stale) log(`  circuit/${s.dir}  (${s.files} exported file(s), ${s.total} total)`);
  log(`\nCurrent boards: ${[...current].join(', ')}`);
  log('\nNothing deleted. Remove them yourself once the new exports look right:');
  for (const s of stale) log(`  rm -rf circuit/${s.dir}`);
}

async function loadProjects() {
  if (!(await exists(PROJECTS))) {
    throw new Error('circuit/tools/projects.json not found -- copy projects.example.json and fill it in');
  }
  return JSON.parse(await readFile(PROJECTS, 'utf8'));
}

// ------------------------------------------------------------------- driver

const [, , command, ...rest] = process.argv;
const flags = {
  only: rest.includes('--only') ? rest[rest.indexOf('--only') + 1] : undefined,
  dryRun: rest.includes('--dry-run'),
};

const commands = {
  login: cmdLogin,
  discover: cmdDiscover,
  inspect: cmdInspect,
  export: () => cmdExport(flags),
  stale: () => cmdStale(flags),
};

// Only drive the CLI when run directly, so the helpers above stay importable
// (and therefore testable) from other scripts.
if (process.argv[1] === new URL(import.meta.url).pathname) {
  const run = commands[command];
  if (!run) {
    console.error(`usage: node eda.mjs <${Object.keys(commands).join('|')}> [--only NAME] [--dry-run]`);
    process.exit(1);
  }

  run().catch((err) => {
    console.error(`\nerror: ${err.message}`);
    process.exit(1);
  });
}

export {
  openDoc, runCommand, runExport, exportPhotoView, confirmDialog, acceptAgreements,
  closeDialogs, chooseBoardSide, checkDownload, launch,
  harvestDocs, harvestProjects, groupDocsByProject, buildDraftProjects,
  plannedFiles, openTreePath, treeLabels, waitForChildren, urlVariants, docUrl,
};
