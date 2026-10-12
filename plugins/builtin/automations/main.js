// Автоматизации: правила «если → то» без кода.
// Когда: новое сообщение (фильтры по чатам, отправителю, тексту, медиа),
// расписание или открытие чата. Что: уведомление, ответ, реакция,
// пересылка, Избранное, прочитать, выключить звук, webhook, «печатает…».
// Защита: лимит срабатываний на правило и общий, общий выключатель
// (/auto off), правило не отвечает на свои сообщения, ботам и в каналах,
// пока это не разрешено, защита от петель и пробный режим.
// Управление — /auto и боковая панель. Правила — в dzg.storage.
// Автор: @wetakush

import * as R from './rules.js';
import * as W from './wizard.js';

const RULES_KEY = 'rules';
const LOG_KEY = 'log';
const SEQ_KEY = 'seq';
// Старые сообщения (догрузка после сна, синхронизация) правила не трогают.
const MAX_AGE_SECONDS = 120;
// После сообщения, которое отправило правило, чат «остывает»: свои
// сообщения там не запускают правила, а входящие не получают ответов.
export const LOOP_GUARD_MS = 10 * 1000;
const TICK_MS = 15 * 1000;
const WINDOW_MS = 60 * 1000;
const MAX_WEBHOOKS = 4;
// Служебные уведомления Telegram (коды входа) — никогда.
const SERVICE_USERS = new Set([777000, 42777, 489000]);
const KIND_ICON = { ok: '✓', dry: '🧪', skip: '⏸', error: '⚠' };

const setting = (key, fallback) => {
  const value = dzg.settings.get(key);
  return (value === undefined || value === null || value === '') ? fallback : value;
};

// Настройки читаются на каждое сообщение: держим копию в памяти.
let cfg = {};
function readCfg() {
  cfg = {
    enabled: setting('enabled', true) !== false,
    defaultLimit: Math.round(R.clamp(setting('defaultLimit', 5), 1, 60, 5)),
    globalLimit: Math.round(R.clamp(setting('globalLimit', 30), 1, 300, 30)),
    silent: !!setting('silent', false),
    logSize: Math.round(R.clamp(setting('logSize', 200), 20, 2000, 200)),
  };
}
readCfg();

const lookup = (id) => {
  try {
    return id ? dzg.chats.get(String(id)) : null;
  } catch (e) {
    return null;
  }
};

// Для того, что пользователь ввёл сам: понимает и id в стиле Bot API
// (-100…, -…), которых dzg.chats.get не знает. Ищет по загруженным чатам.
const lookupChat = (id) => {
  const direct = lookup(id);
  if (direct || !/^-\d+$/.test(String(id || ''))) return direct;
  const tg = Number(String(id).replace(/^-(100)?/, ''));
  try {
    const list = (dzg.chats.list && dzg.chats.list({ limit: 0 })) || [];
    return list.find((c) => Number(c.tgId) === tg) || null;
  } catch (e) {
    return null;
  }
};

const selfPeer = () => {
  try {
    return (dzg.users && dzg.users.self) ? dzg.users.self() : null;
  } catch (e) {
    return null;
  }
};

const errText = (e) => R.clip((e && e.message) ? e.message : String(e), 120);

// --- Хранилище ----------------------------------------------------------------

function nextId() {
  const seq = (Number(dzg.storage.get(SEQ_KEY, 0)) || 0) + 1;
  dzg.storage.set(SEQ_KEY, seq);
  return 'r' + seq;
}

function loadRules() {
  const raw = dzg.storage.get(RULES_KEY, []);
  const list = [];
  const ids = new Set();
  for (const item of Array.isArray(raw) ? raw : []) {
    const rule = R.normalizeRule(item, { defaultLimit: cfg.defaultLimit });
    if (!rule) {
      console.warn('Пропускаю испорченное правило:', JSON.stringify(item).slice(0, 200));
      continue;
    }
    if (!rule.id || ids.has(rule.id)) rule.id = nextId();
    ids.add(rule.id);
    list.push(rule);
  }
  return list;
}

function loadLog() {
  const raw = dzg.storage.get(LOG_KEY, []);
  return (Array.isArray(raw) ? raw : []).filter((e) => e && typeof e === 'object' && e.at).slice(-cfg.logSize);
}

let rules = loadRules();
let log = loadLog();
let persistTimer = 0;

function saveNow() {
  clearTimeout(persistTimer);
  persistTimer = 0;
  dzg.storage.set(RULES_KEY, rules);
  dzg.storage.set(LOG_KEY, log);
}

// Счётчики и журнал меняются часто — пишем их на диск пачкой.
function persistSoon() {
  if (!persistTimer) persistTimer = setTimeout(saveNow, 2000);
}

export function flush() {
  saveNow();
}

function addLog(rule, text, kind) {
  log.push({ at: Date.now(), ruleId: rule ? rule.id : '', rule: rule ? rule.name : '', text: R.clip(text, 300), kind: kind || 'ok' });
  if (log.length > cfg.logSize) log.splice(0, log.length - cfg.logSize);
  persistSoon();
  renderSoon();
}

export const getRules = () => rules;
export const getLog = () => log;

// --- Сборка правил ------------------------------------------------------------

let compiled = { message: [], schedule: [], chatOpened: [] };
const errors = new Map();

function recompile() {
  compiled = { message: [], schedule: [], chatOpened: [] };
  errors.clear();
  for (const rule of rules) {
    const c = R.compileRule(rule);
    if (c.error) errors.set(rule.id, c.error);
    if (rule.enabled && !c.error) compiled[rule.trigger.type].push(c);
  }
}

function rulesChanged() {
  saveNow();
  recompile();
  armScheduler();
  render();
}

recompile();

// --- Защита -------------------------------------------------------------------

const windows = new Map();     // id правила -> время срабатываний за минуту
let globalWindow = [];
const limitNoted = new Map();  // id правила -> когда записали «лимит» в журнал
const guards = new Map();      // id чата -> до какого времени он «остывает»
let busy = 0;                  // выполняем действия: эхо своих сообщений не трогаем
let webhooks = 0;

function guard(chatId) {
  if (chatId) guards.set(String(chatId), Date.now() + LOOP_GUARD_MS);
}

export function isGuarded(chatId) {
  const until = guards.get(String(chatId));
  if (!until) return false;
  if (until <= Date.now()) {
    guards.delete(String(chatId));
    return false;
  }
  return true;
}

function noteSkip(rule, why, now) {
  rule.stats.skipped++;
  persistSoon();
  if (now - (limitNoted.get(rule.id) || 0) >= WINDOW_MS) {
    limitNoted.set(rule.id, now);
    addLog(rule, why + ' — лишние срабатывания пропускаются', 'skip');
  }
}

// Пропуск из-за защиты от петель — в журнал не чаще раза в минуту на правило.
const guardNoted = new Map();
function noteGuard(rule, chat, now) {
  if (now - (guardNoted.get(rule.id) || 0) < WINDOW_MS) return;
  guardNoted.set(rule.id, now);
  addLog(rule, 'Пропущено: в «' + ((chat && chat.name) || '?') + '» только что писало правило — защита от петель, ' +
    Math.round(LOOP_GUARD_MS / 1000) + ' с', 'skip');
}

function takeSlot(rule, now) {
  const win = (windows.get(rule.id) || []).filter((t) => now - t < WINDOW_MS);
  windows.set(rule.id, win);
  if (win.length >= rule.limit) {
    noteSkip(rule, 'Лимит ' + rule.limit + ' в минуту', now);
    return false;
  }
  globalWindow = globalWindow.filter((t) => now - t < WINDOW_MS);
  if (globalWindow.length >= cfg.globalLimit) {
    noteSkip(rule, 'Общий лимит ' + cfg.globalLimit + ' в минуту', now);
    return false;
  }
  win.push(now);
  globalWindow.push(now);
  return true;
}

// message.new приходит от всех аккаунтов, а действия идут от активного.
function isActiveAccount(m) {
  if (!m.selfId) return true;
  const self = selfPeer();
  return !self || String(self.id) === String(m.selfId);
}

function senderPeer(m) {
  if (!m || !m.fromId || String(m.fromId) === String(m.peerId || (m.chat && m.chat.id))) return m ? m.chat : null;
  return lookup(m.fromId);
}

// Почему действие, которое видит собеседник, здесь делать нельзя ('' — можно).
function blockedReason(rule, chat, ctx) {
  if (!chat) return 'нет чата';
  if (chat.type === 'channel' && !rule.allowChannels) return 'это канал';
  if (chat.type === 'bot' && !rule.allowBots) return 'это бот';
  const m = ctx.message;
  if (m && !m.out) {
    const sender = senderPeer(m);
    if (sender && sender.type === 'bot' && !rule.allowBots) return 'пишет бот';
    if (sender && sender.type === 'channel' && !rule.allowChannels) return 'пишет канал';
  }
  return '';
}

// --- Действия -----------------------------------------------------------------

function varsFor(rule, ctx, chat) {
  const m = ctx.message;
  const now = Date.now();
  return {
    sender: m ? (m.fromName || (m.chat && m.chat.name) || '') : '',
    chat: ((chat || ctx.chat || {}).name) || '',
    text: R.messageText(m),
    time: R.clock(now),
    date: R.dateText(now),
    rule: rule.name,
  };
}

function later(rule, what, start) {
  let promise;
  try {
    promise = Promise.resolve(start());
  } catch (e) {
    promise = Promise.reject(e);
  }
  return promise.then(undefined, (e) => {
    addLog(rule, '⚠ ' + what + ': ' + errText(e), 'error');
    return null;
  });
}

function resolveTarget(to) {
  if (!to || !to.id) return null;
  if (to.id[0] === '@') {
    let peer = null;
    try {
      peer = dzg.chats.find(to.id.slice(1));
    } catch (e) {
      peer = null;
    }
    return peer || null;
  }
  return lookupChat(to.id) || { id: to.id, name: to.name || to.id, type: 'unknown' };
}

const peerJson = (p) => (p ? { id: String(p.id), tgId: p.tgId || 0, name: p.name || '', username: p.username || '', type: p.type || '' } : null);

function webhookPayload(rule, ctx) {
  const m = ctx.message;
  let link = null;
  if (m && dzg.messages.link) {
    try {
      link = dzg.messages.link(m.peerId || m.chat.id, m.id);
    } catch (e) {
      link = null;
    }
  }
  return {
    source: 'DamZaGram',
    plugin: 'automations',
    rule: { id: rule.id, name: rule.name },
    trigger: ctx.trigger,
    time: new Date().toISOString(),
    chat: peerJson(ctx.chat),
    chats: ctx.trigger === 'schedule' ? ctx.targets.map(peerJson) : undefined,
    message: m ? {
      id: m.id,
      text: String(m.text || ''),
      date: m.date,
      out: !!m.out,
      fromId: String(m.fromId || ''),
      fromName: m.fromName || '',
      media: m.media || null,
      replyToId: m.replyToId || 0,
      link,
    } : null,
  };
}

const names = (chats) => chats.map((c) => '«' + (c.name || c.id) + '»').join(', ');

// Каждое действие возвращает строку для журнала. dry — только описать.
const RUN = {
  notify(a, rule, ctx, dry) {
    const text = R.clip(R.renderTemplate(a.text, varsFor(rule, ctx)), 600) || rule.name;
    if (!dry) {
      const m = ctx.message;
      const target = ctx.chat || (ctx.targets.length === 1 ? ctx.targets[0] : null);
      dzg.ui.notify({
        title: '⚡ ' + rule.name,
        text,
        silent: cfg.silent,
        onClick: () => {
          if (m) dzg.chats.open(m.peerId || m.chat.id, m.id);
          else if (target) dzg.chats.open(target.id);
          else show();
        },
      });
    }
    return 'уведомление «' + R.clip(text, 50) + '»';
  },

  reply(a, rule, ctx, dry) {
    if (!ctx.targets.length) return 'текст не отправлен: в правиле нет чатов';
    const parts = [];
    for (const chat of ctx.targets) {
      const why = blockedReason(rule, chat, ctx);
      if (why) {
        parts.push('ответ пропущен: ' + why);
        continue;
      }
      const text = R.renderTemplate(a.text, varsFor(rule, ctx, chat)).slice(0, 4096);
      if (!text.trim()) {
        parts.push('пустой текст');
        continue;
      }
      if (!dry) {
        const options = {};
        if (ctx.message) options.replyTo = ctx.message.id;
        if (ctx.message && ctx.message.topicId) options.topicId = ctx.message.topicId;
        guard(chat.id);
        if (!dzg.messages.send(chat.id, text, options)) {
          parts.push('⚠ не отправлено в «' + chat.name + '»');
          continue;
        }
      }
      parts.push((ctx.message ? 'ответ' : 'текст в «' + (chat.name || chat.id) + '»') + ' «' + R.clip(text, 40) + '»');
    }
    return parts.join(', ');
  },

  react(a, rule, ctx, dry) {
    const m = ctx.message;
    if (!m) return '';
    const why = blockedReason(rule, m.chat, ctx);
    if (why) return 'реакция пропущена: ' + why;
    if (!dry) later(rule, 'реакция ' + a.emoji, () => dzg.messages.react(m.peerId || m.chat.id, m.id, a.emoji));
    return 'реакция ' + a.emoji;
  },

  forward(a, rule, ctx, dry) {
    const m = ctx.message;
    if (!m) return '';
    const to = resolveTarget(a.to);
    if (!to) return '⚠ пересылка: чат ' + (a.to.name || a.to.id) + ' не найден';
    if (!dry) {
      guard(to.id);
      later(rule, 'пересылка в «' + (to.name || to.id) + '»', () => dzg.messages.forward(m.peerId || m.chat.id, m.id, to.id));
    }
    return 'переслано в «' + (to.name || to.id) + '»';
  },

  saved(a, rule, ctx, dry) {
    const m = ctx.message;
    if (!m) return '';
    if (m.chat && m.chat.isSelf) return 'уже в Избранном';
    const self = selfPeer();
    if (!self) return '⚠ Избранное недоступно: аккаунт не загружен';
    if (!dry) {
      guard(self.id);
      // Пересылка сохраняет источник. Если переслать нельзя (защищённый
      // чат), кладём в Избранное копию текста.
      later(rule, 'Избранное', () => dzg.messages.forward(m.peerId || m.chat.id, m.id, self.id).then(undefined, () => {
        guard(self.id);
        const head = '⚡ ' + rule.name + ' · ' + (m.chat ? m.chat.name : '') + (m.fromName && m.chat && m.fromName !== m.chat.name ? ' · ' + m.fromName : '');
        if (!dzg.messages.send(self.id, head + '\n' + R.messageText(m))) throw new Error('не удалось сохранить в Избранное');
      }));
    }
    return 'в Избранное';
  },

  read(a, rule, ctx, dry) {
    if (!ctx.targets.length) return 'прочитать: в правиле нет чатов';
    if (!dry) for (const chat of ctx.targets) dzg.messages.markRead(chat.id);
    return 'прочитан ' + names(ctx.targets);
  },

  mute(a, rule, ctx, dry) {
    if (!ctx.targets.length) return 'без звука: в правиле нет чатов';
    if (!dry) for (const chat of ctx.targets) dzg.chats.mute(chat.id, Math.round(a.hours * 3600));
    return 'без звука на ' + R.hoursText(a.hours) + ': ' + names(ctx.targets);
  },

  webhook(a, rule, ctx, dry) {
    const where = 'webhook ' + R.host(a.url);
    if (dry) return where;
    if (webhooks >= MAX_WEBHOOKS) return '⚠ ' + where + ' пропущен: ещё ждём ответа на ' + webhooks + ' запроса';
    const request = dzg.http.fetch(a.url, { method: 'POST', json: webhookPayload(rule, ctx), timeout: 15000 });
    webhooks++;
    Promise.resolve(request).then((response) => {
      if (!response.ok) addLog(rule, '⚠ ' + where + ' ответил ' + response.status, 'error');
    }, (e) => addLog(rule, '⚠ ' + where + ': ' + errText(e), 'error')).then(() => {
      webhooks = Math.max(0, webhooks - 1);
    });
    return where;
  },

  typing(a, rule, ctx, dry) {
    if (!ctx.targets.length) return '«печатает…»: в правиле нет чатов';
    const parts = [];
    for (const chat of ctx.targets) {
      const why = blockedReason(rule, chat, ctx);
      if (why) {
        parts.push('«печатает…» пропущено: ' + why);
        continue;
      }
      if (!dry) dzg.chats.setTyping(chat.id, 'typing');
      parts.push('«печатает…» в «' + (chat.name || chat.id) + '»');
    }
    return parts.join(', ');
  },
};

function perform(rule, ctx, dry) {
  const done = [];
  for (const action of rule.actions) {
    try {
      const text = RUN[action.type](action, rule, ctx, dry);
      if (text) done.push(text);
    } catch (e) {
      done.push('⚠ ' + R.ACTION[action.type].short + ': ' + errText(e));
    }
  }
  return done;
}

function whereText(ctx) {
  if (ctx.message) {
    const m = ctx.message;
    const chat = m.chat ? m.chat.name : '';
    return chat + (m.fromName && m.fromName !== chat ? ' — ' + m.fromName : '');
  }
  if (ctx.chat) return ctx.chat.name;
  return ctx.trigger === 'schedule' ? 'по расписанию' : '';
}

/** Выполнить правило с учётом лимитов. false — не выполнено (лимит). */
export function fire(rule, ctx) {
  const now = Date.now();
  if (!takeSlot(rule, now)) return false;
  const dry = !!rule.dryRun;
  rule.stats.fired++;
  rule.stats.lastAt = now;
  let done;
  busy++;
  try {
    done = perform(rule, ctx, dry);
  } finally {
    busy--;
  }
  const where = whereText(ctx);
  const failed = done.some((d) => d.includes('⚠'));
  addLog(rule, (dry ? 'пробно: ' : '') + (done.join(' · ') || 'нечего делать') + (where ? ' · ' + where : ''), dry ? 'dry' : (failed ? 'error' : 'ok'));
  persistSoon();
  return true;
}

// --- Триггеры -----------------------------------------------------------------

const messageCtx = (m) => ({ trigger: 'message', message: m, chat: m.chat, targets: [m.chat] });
const openedCtx = (chat) => ({ trigger: 'chatOpened', message: null, chat, targets: [chat] });

function scheduleCtx(rule, quiet) {
  const targets = [];
  for (const token of rule.trigger.chats) {
    const peer = resolveTarget({ id: token, name: rule.trigger.names[token] || '' });
    if (peer) targets.push(peer);
    else if (!quiet) addLog(rule, '⚠ чат ' + token + ' не найден среди загруженных', 'error');
  }
  return { trigger: 'schedule', message: null, chat: null, targets };
}

function onMessage(m) {
  if (busy || !m || m.service || !m.chat) return;
  if (!cfg.enabled || !compiled.message.length) return;
  if (m.date && Date.now() / 1000 - m.date > MAX_AGE_SECONDS) return;
  if (SERVICE_USERS.has(Number(m.chat.tgId))) return;
  if (!isActiveAccount(m)) return;
  const peerId = String(m.peerId || m.chat.id);
  const guarded = isGuarded(peerId);
  // Своё сообщение в «остывающем» чате — почти наверняка результат правила.
  if (guarded && m.out) return;
  const env = { lookup };
  for (const c of compiled.message.slice()) {
    if (!R.evaluate(c, m, env)) continue;
    if (guarded && c.chatty) {
      noteGuard(c.rule, m.chat, Date.now());
      continue;
    }
    fire(c.rule, messageCtx(m));
  }
}

function onChatOpened(chat) {
  if (busy || !chat || !chat.id) return;
  if (!cfg.enabled || !compiled.chatOpened.length) return;
  const guarded = isGuarded(chat.id);
  for (const c of compiled.chatOpened.slice()) {
    if (!R.evaluateChat(c, chat)) continue;
    if (guarded && c.chatty) {
      noteGuard(c.rule, chat, Date.now());
      continue;
    }
    fire(c.rule, openedCtx(chat));
  }
}

let tickTimer = 0;

function armScheduler() {
  clearTimeout(tickTimer);
  tickTimer = 0;
  if (cfg.enabled && compiled.schedule.length) tickTimer = setTimeout(tick, TICK_MS);
}

function tick() {
  tickTimer = 0;
  try {
    runSchedule(Date.now());
  } finally {
    armScheduler();
  }
}

export function runSchedule(now) {
  if (busy || !cfg.enabled) return;
  for (const c of compiled.schedule.slice()) {
    const rule = c.rule;
    const slot = R.dueSlot(rule, now);
    if (!slot) continue;
    rule.state.lastRun = now;
    if (rule.trigger.schedule.kind === 'daily') rule.state.lastSlot = slot;
    persistSoon();
    fire(rule, scheduleCtx(rule));
  }
}

// После паузы правила по расписанию начинают отсчёт заново, а не
// выстреливают всем, что пропустили.
function rearm(rule) {
  rule.state.armedAt = Date.now();
}

// --- Управление правилами -----------------------------------------------------

export function setEnabled(on) {
  const value = !!on;
  if (cfg.enabled === value) {
    dzg.ui.toast(value ? 'Автоматизации и так работают' : 'Автоматизации и так выключены', '⚡ Автоматизации');
    return;
  }
  dzg.settings.set('enabled', value);
  // settings.changed придёт следом; применяем сразу, не дожидаясь его.
  applyEnabled(value);
  dzg.ui.toast(value
    ? 'Правила снова работают. Выключить всё: /auto off'
    : 'Все правила остановлены. Включить: /auto on', value ? '⚡ Автоматизации включены' : '⛔ Автоматизации выключены');
}

function applyEnabled(value) {
  const was = cfg.enabled;
  cfg.enabled = value;
  if (value && !was) {
    for (const rule of rules) if (rule.trigger.type === 'schedule') rearm(rule);
    persistSoon();
  }
  armScheduler();
  render();
}

function findRule(id) {
  return rules.find((r) => r.id === id) || null;
}

export function setRuleEnabled(id, on) {
  const rule = findRule(id);
  if (!rule || rule.enabled === !!on) return;
  rule.enabled = !!on;
  if (rule.enabled) rearm(rule);
  rulesChanged();
}

export async function deleteRule(id) {
  const rule = findRule(id);
  if (!rule) return;
  const ok = await dzg.ui.confirm('Удалить правило «' + rule.name + '»? Его счётчики пропадут, записи в журнале останутся.', {
    title: 'Удалить правило', confirmText: 'Удалить', cancelText: 'Отмена', danger: true,
  });
  if (!ok || !findRule(id)) return;
  rules = rules.filter((r) => r.id !== id);
  windows.delete(id);
  rulesChanged();
  dzg.ui.toast('Правило «' + rule.name + '» удалено');
}

function wizardEnv(editing) {
  return {
    box: (spec) => dzg.ui.box(spec),
    alert: (text, title) => dzg.ui.alert(text, title),
    current: () => (dzg.chats.current ? dzg.chats.current() : null),
    self: selfPeer,
    lookup: lookupChat,
    find: (username) => (dzg.chats.find ? dzg.chats.find(username) : null),
    resolve: (username) => (dzg.chats.resolve ? dzg.chats.resolve(username) : Promise.resolve(null)),
    defaultLimit: cfg.defaultLimit,
    editing: editing ? editing.name : '',
  };
}

let wizardOpen = false;

/** Открыть редактор. existing — правило, которое правим. Возвращает сохранённое правило или null. */
export async function startWizard(form, existing) {
  if (wizardOpen) {
    dzg.ui.toast('Редактор правила уже открыт — закончи или закрой его');
    return null;
  }
  wizardOpen = true;
  try {
    const rule = await W.runWizard(form, wizardEnv(existing));
    if (!rule) return null;
    const now = Date.now();
    const current = existing ? findRule(existing.id) : null;
    if (existing && !current) {
      dzg.ui.toast('Пока окно было открыто, правило удалили — изменения не сохранены');
      return null;
    }
    if (current) {
      rule.id = current.id;
      rule.created = current.created;
      rule.stats = current.stats;
      rule.state = Object.assign({}, current.state, { armedAt: now });
      rules = rules.map((r) => (r.id === current.id ? rule : r));
      windows.delete(rule.id);
    } else {
      if (rules.length >= R.MAX_RULES) {
        dzg.ui.alert('Правил уже ' + rules.length + ' — это максимум. Удали ненужные.', '⚡ Автоматизации');
        return null;
      }
      rule.id = nextId();
      rule.created = now;
      rule.state = { armedAt: now, lastRun: 0, lastSlot: 0 };
      rules = rules.concat([rule]);
    }
    view = 'list';
    rulesChanged();
    dzg.ui.toast((current ? 'Правило обновлено' : 'Правило добавлено') +
      (rule.dryRun ? ' в пробном режиме: действия только пишутся в журнал' : '') +
      (!rule.enabled ? ' (выключено)' : ''), '⚡ ' + rule.name);
    return rule;
  } finally {
    wizardOpen = false;
  }
}

export function editRule(id) {
  const rule = findRule(id);
  if (!rule) return Promise.resolve(null);
  return startWizard(W.ruleToForm(rule, { self: selfPeer, lookup }), rule);
}

// --- Проверка правил ----------------------------------------------------------

function lastMessage(chat) {
  let list = [];
  try {
    list = (dzg.messages.loaded && dzg.messages.loaded(chat.id, 30)) || [];
  } catch (e) {
    list = [];
  }
  let best = null;
  for (const m of list) {
    if (m && !m.service && (!best || m.id > best.id)) best = m;
  }
  if (!best && dzg.chats.entry) {
    try {
      const entry = dzg.chats.entry(chat.id);
      if (entry && entry.lastMessage && !entry.lastMessage.service) best = entry.lastMessage;
    } catch (e) {
      best = null;
    }
  }
  return best;
}

const preview = (m) => (m.out ? 'я' : (m.fromName || '?')) + ': ' + (R.clip(R.messageText(m), 80) || '—');

/** Проверить правило «на сухую»; если подходит — можно выполнить по-настоящему. */
export async function testRule(id) {
  const rule = findRule(id);
  if (!rule) return;
  const title = '🔍 ' + rule.name;
  const c = R.compileRule(rule);
  const type = rule.trigger.type;
  const lines = [];
  let ctx;
  let matched;
  if (type === 'schedule') {
    ctx = scheduleCtx(rule, true);
    matched = true;
    lines.push('Расписание: ' + R.describeSchedule(rule.trigger.schedule, rule.trigger.weekdays) + '.');
    if (rule.enabled && cfg.enabled) lines.push('Следующий запуск: ' + R.formatWhen(R.nextRun(rule, Date.now())) + '.');
    lines.push('Чаты: ' + (ctx.targets.length ? names(ctx.targets) : 'не заданы') + '.');
  } else {
    const chat = dzg.chats.current ? dzg.chats.current() : null;
    if (!chat) {
      await dzg.ui.alert(type === 'message'
        ? 'Открой чат — правило проверится на его последнем сообщении.'
        : 'Открой чат — правило проверится на нём.', title);
      return;
    }
    const checks = [];
    if (type === 'message') {
      const m = lastMessage(chat);
      if (!m) {
        await dzg.ui.alert('В чате «' + chat.name + '» нет загруженных сообщений.', title);
        return;
      }
      lines.push('Последнее сообщение в «' + chat.name + '» — ' + preview(m));
      matched = R.evaluate(c, m, { lookup }, checks);
      ctx = messageCtx(m);
    } else {
      lines.push('Открытый чат: «' + chat.name + '»');
      matched = R.evaluateChat(c, chat, checks);
      ctx = openedCtx(chat);
    }
    lines.push('');
    for (const check of checks) lines.push((check.ok ? '✅ ' : '❌ ') + check.text);
  }
  lines.push('');
  if (matched) {
    lines.push(rule.dryRun ? 'Правило сработает в пробном режиме и запишет в журнал:' : 'Правило сработает и сделает:');
    for (const item of perform(rule, ctx, true)) lines.push('• ' + item);
  } else {
    lines.push('Правило не сработает.');
  }
  if (!rule.enabled) lines.push('\nПравило сейчас выключено.');
  if (!cfg.enabled) lines.push('\nВсе автоматизации остановлены: /auto on.');
  const canRun = matched && cfg.enabled;
  const result = await dzg.ui.box({
    title,
    text: lines.join('\n'),
    buttons: canRun
      ? [{ id: 'run', text: rule.dryRun ? 'Выполнить (пробно)' : 'Выполнить сейчас' }, { id: 'close', text: 'Закрыть' }]
      : [{ id: 'close', text: 'Закрыть' }],
  });
  if (result.button !== 'run' || !findRule(id)) return;
  dzg.ui.toast(fire(rule, ctx)
    ? (rule.dryRun ? 'Записано в журнал (пробный режим)' : 'Правило выполнено')
    : 'Лимит срабатываний — попробуй через минуту', '⚡ ' + rule.name);
}

/** Какие правила сработали бы на это сообщение — для меню сообщения и /auto test. */
export function explainMessage(m) {
  const list = rules.filter((r) => r.trigger.type === 'message');
  if (!list.length) return 'Правил для сообщений нет.';
  const lines = ['Сообщение — ' + preview(m), ''];
  for (const rule of list) {
    const checks = [];
    const ok = R.evaluate(R.compileRule(rule), m, { lookup }, checks);
    const firstFail = checks.find((x) => !x.ok);
    lines.push((ok ? '✅ ' : '❌ ') + rule.name + (rule.enabled ? '' : ' (выключено)') + (ok ? '' : ' — ' + (firstFail ? firstFail.text : 'не подходит')));
  }
  if (!cfg.enabled) lines.push('', 'Все автоматизации сейчас остановлены: /auto on.');
  return lines.join('\n');
}

// --- Экспорт и импорт ---------------------------------------------------------

export function exportRules() {
  if (!rules.length) {
    dzg.ui.toast('Правил пока нет — экспортировать нечего');
    return;
  }
  dzg.clipboard.set(R.exportRules(rules));
  dzg.ui.toast('Скопировано правил: ' + rules.length + '. Вставь JSON в /auto import на другом компьютере.', '⚡ Экспорт');
}

export async function importRules() {
  const parsed = R.parseImport(dzg.clipboard.get(), { defaultLimit: cfg.defaultLimit });
  if (parsed.error) {
    await dzg.ui.alert(parsed.error, '⚡ Импорт');
    return 0;
  }
  if (!parsed.rules.length) {
    await dzg.ui.alert('В буфере нет годных правил' + (parsed.invalid ? ' (пропущено: ' + parsed.invalid + ')' : '') + '.', '⚡ Импорт');
    return 0;
  }
  const room = R.MAX_RULES - rules.length;
  const incoming = parsed.rules.slice(0, Math.max(0, room));
  if (!incoming.length) {
    await dzg.ui.alert('Правил уже максимум (' + R.MAX_RULES + '). Удали ненужные.', '⚡ Импорт');
    return 0;
  }
  const ok = await dzg.ui.confirm('Добавить правил: ' + incoming.length +
    (parsed.invalid ? ' (испорченных пропущено: ' + parsed.invalid + ')' : '') +
    '?\n\n' + incoming.slice(0, 8).map((r) => '• ' + r.name).join('\n') + (incoming.length > 8 ? '\n…' : '') +
    '\n\nНовые правила будут выключены: проверь их и включи нужные.', {
    title: '⚡ Импорт правил', confirmText: 'Добавить', cancelText: 'Отмена',
  });
  if (!ok) return 0;
  const now = Date.now();
  const taken = new Set(rules.map((r) => r.name));
  for (const rule of incoming) {
    // Одинаковые названия в списке путают: «Срочное» -> «Срочное (2)».
    const base = rule.name.slice(0, 72);
    for (let n = 2; taken.has(rule.name); ++n) rule.name = base + ' (' + n + ')';
    taken.add(rule.name);
    rule.id = nextId();
    rule.enabled = false;
    rule.created = now;
    rule.state = { armedAt: now, lastRun: 0, lastSlot: 0 };
  }
  rules = rules.concat(incoming);
  rulesChanged();
  dzg.ui.toast('Добавлено правил: ' + incoming.length + '. Они выключены — включи нужные в /auto.', '⚡ Импорт');
  return incoming.length;
}

// --- Панель -------------------------------------------------------------------

let panel = null;
let view = 'list';
let renderTimer = 0;

export function show(nextView) {
  if (nextView) view = nextView;
  if (!dzg.ui.panel) {
    dzg.ui.alert(statusText(), '⚡ Автоматизации');
    return;
  }
  if (!panel || !panel.isOpen()) {
    panel = dzg.ui.panel({ title: '⚡ Автоматизации', width: 380 });
    panel.on('close', () => {
      panel = null;
      view = 'list';
    });
  }
  render();
}

function renderSoon() {
  if (!panel || renderTimer) return;
  renderTimer = setTimeout(() => {
    renderTimer = 0;
    render();
  }, 500);
}

function statusText() {
  const active = rules.filter((r) => r.enabled).length;
  return (cfg.enabled ? 'Работают' : '⛔ Остановлены (/auto on)') + ' · правил: ' + rules.length + (rules.length ? ', включено: ' + active : '');
}

function ruleDetails(rule) {
  const lines = ['Если: ' + R.describeTrigger(rule), 'То: ' + R.describeActions(rule)];
  const s = rule.stats;
  lines.push((s.fired
    ? 'Сработало ' + s.fired + ' ' + R.plural(s.fired, ['раз', 'раза', 'раз']) + ', последний — ' + R.formatWhen(s.lastAt)
    : 'Ещё не срабатывало') +
    ' · лимит ' + rule.limit + '/мин' + (s.skipped ? ' · пропущено ' + s.skipped : ''));
  if (rule.dryRun) lines.push('🧪 Пробный режим: действия только пишутся в журнал');
  if (rule.trigger.type === 'schedule' && rule.enabled && cfg.enabled) {
    lines.push('Следующий запуск: ' + R.formatWhen(R.nextRun(rule, Date.now())));
  }
  if (errors.has(rule.id)) lines.push('⚠ ' + errors.get(rule.id));
  return lines.join('\n');
}

const TEST_LABEL = {
  message: 'Проверить на последнем сообщении',
  schedule: 'Проверить и выполнить сейчас',
  chatOpened: 'Проверить на открытом чате',
};

function renderList() {
  panel.addToggle('Автоматизации работают', cfg.enabled, (on) => setEnabled(on));
  panel.addLabel(statusText() + '\nВыключить всё одной командой: /auto off', { muted: true });
  panel.addButton('＋ Добавить правило', () => show('add'));
  panel.addSeparator();
  if (!rules.length) {
    panel.addLabel('Правил пока нет. Нажми «Добавить правило» или возьми готовый шаблон — например, уведомление о слове «срочно» или голосовые в Избранное.', { muted: true });
  }
  for (const rule of rules) {
    const id = rule.id;
    panel.addToggle(R.TRIGGERS[rule.trigger.type].icon + ' ' + rule.name, rule.enabled, (on) => setRuleEnabled(id, on));
    panel.addLabel(ruleDetails(rule), { muted: true });
    panel.addButton(TEST_LABEL[rule.trigger.type], () => testRule(id), { icon: 'search' });
    panel.addButton('Изменить', () => editRule(id), { icon: 'edit' });
    panel.addButton('Удалить', () => deleteRule(id), { icon: 'delete' });
    panel.addSkip(8);
  }
  panel.addSeparator();
  panel.addLabel('Журнал', { bold: true });
  if (!log.length) {
    panel.addLabel('Пока пусто. Здесь появятся срабатывания правил, пропуски по лимиту и ошибки.', { muted: true });
  }
  for (const entry of log.slice(-15).reverse()) {
    panel.addLabel((KIND_ICON[entry.kind] || '•') + ' ' + R.formatWhen(entry.at) + ' · ' + entry.rule + '\n' + entry.text);
  }
  if (log.length) {
    panel.addButton('Очистить журнал', () => {
      log = [];
      saveNow();
      render();
    }, { style: 'light' });
  }
  panel.addSeparator();
  panel.addButton('Скопировать все правила (экспорт)', exportRules, { icon: 'copy' });
  panel.addButton('Добавить правила из буфера (импорт)', importRules, { icon: 'download' });
}

function renderAdd() {
  panel.addLabel('Новое правило — когда срабатывать?', { bold: true });
  panel.addButton('На новое сообщение', () => startWizard(W.emptyForm('message')), { icon: 'chat' });
  panel.addButton('По расписанию', () => startWizard(W.emptyForm('schedule')), { icon: 'schedule' });
  panel.addButton('При открытии чата', () => startWizard(W.emptyForm('chatOpened')), { icon: 'folder' });
  panel.addLabel('Дальше откроются окна: условия → действия → их настройка → название и защита.', { muted: true });
  panel.addSeparator();
  panel.addLabel('Готовые шаблоны', { bold: true });
  panel.addLabel('Шаблон заполнит окна редактора — останется проверить и сохранить.', { muted: true });
  for (const preset of W.PRESETS) {
    panel.addButton(preset.title, () => startWizard(W.presetForm(preset.id)), { style: 'light' });
  }
  panel.addSeparator();
  panel.addButton('← Назад к списку правил', () => show('list'), { style: 'light' });
}

function render() {
  clearTimeout(renderTimer);
  renderTimer = 0;
  if (!panel || !panel.isOpen()) return;
  panel.clear();
  if (view === 'add') renderAdd();
  else renderList();
}

// --- Команды и меню -----------------------------------------------------------

async function testAllOnLast(chat) {
  const target = chat || (dzg.chats.current ? dzg.chats.current() : null);
  if (!target) {
    dzg.ui.toast('Открой чат: правила проверятся на его последнем сообщении');
    return;
  }
  const m = lastMessage(target);
  if (!m) {
    dzg.ui.toast('В чате «' + target.name + '» нет загруженных сообщений');
    return;
  }
  await dzg.ui.alert(explainMessage(m), '⚡ Какие правила сработают');
}

dzg.commands.register('auto', {
  description: 'Автоматизации: /auto — правила · /auto on|off · /auto test · /auto export|import',
  run: ({ args, chat }) => {
    const action = (args[0] || '').toLowerCase();
    if (action === 'on' || action === 'вкл' || action === 'start') setEnabled(true);
    else if (action === 'off' || action === 'выкл' || action === 'stop' || action === 'стоп') setEnabled(false);
    else if (action === 'test' || action === 'проверить') return testAllOnLast(chat);
    else if (action === 'export' || action === 'экспорт') exportRules();
    else if (action === 'import' || action === 'импорт') return importRules();
    else if (action === 'add' || action === 'new' || action === 'добавить') show('add');
    else show('list');
    return undefined;
  },
});

dzg.ui.addMessageMenuItem({
  title: '⚡ Какие правила сработают?',
  when: () => rules.some((r) => r.trigger.type === 'message'),
  onClick: (m) => dzg.ui.alert(explainMessage(m), '⚡ Автоматизации'),
});

dzg.ui.addChatMenuItem({
  title: '⚡ Правило для этого чата…',
  onClick: (chat) => {
    const form = W.emptyForm('message');
    form.chats = chat.isSelf ? 'избранное' : String(chat.id);
    return startWizard(form);
  },
});

if (dzg.ui.addMainMenuItem) {
  dzg.ui.addMainMenuItem({ title: 'Автоматизации', icon: 'bot', onClick: () => show('list') });
}

// --- События ------------------------------------------------------------------

dzg.on('message.new', onMessage);
dzg.on('chat.opened', onChatOpened);

dzg.on('settings.changed', ({ key }) => {
  const was = cfg.enabled;
  readCfg();
  const now = cfg.enabled;
  cfg.enabled = was;
  if (key === 'enabled' || was !== now) applyEnabled(now);
  if (log.length > cfg.logSize) {
    log.splice(0, log.length - cfg.logSize);
    persistSoon();
  }
  renderSoon();
});

armScheduler();

dzg.on('unload', () => {
  clearTimeout(tickTimer);
  clearTimeout(renderTimer);
  tickTimer = 0;
  renderTimer = 0;
  saveNow();
  if (panel) panel.close();
});
