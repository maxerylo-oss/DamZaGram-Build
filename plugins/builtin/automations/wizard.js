// Пошаговый редактор правила в окнах dzg.ui.box:
// условия → действия → настройка действий → название и защита.
// Поля в окнах подписаны плейсхолдерами, а сверху в тексте окна —
// нумерованная подсказка: при правке правила поля уже заполнены и
// плейсхолдеров не видно.
// Автор: @wetakush

import * as R from './rules.js';

// Действия с параметрами и их поля — в этом порядке они идут в окне.
const PARAMS = [
  { type: 'notify', key: 'notifyText', multiline: true, label: 'Текст уведомления', placeholder: '🔔 Текст уведомления' },
  { type: 'reply', key: 'replyText', multiline: true, label: 'Текст ответа', placeholder: '↩️ Текст ответа' },
  { type: 'react', key: 'emoji', label: 'Реакция — один эмодзи', placeholder: '👍 Реакция (эмодзи)' },
  { type: 'forward', key: 'forwardTo', label: 'Куда переслать: @username, id или «избранное»', placeholder: '↪️ Куда переслать' },
  { type: 'mute', key: 'muteHours', label: 'На сколько часов выключить звук (можно 0,5)', placeholder: '🔕 Часов без звука' },
  { type: 'webhook', key: 'webhookUrl', label: 'Адрес webhook — https://…', placeholder: '🌐 https://example.com/hook' },
];

export function emptyForm(type) {
  return {
    type,
    chats: '',
    sender: '',
    text: '',
    media: '',
    dirIn: true,
    dirOut: false,
    kPrivate: true,
    kGroup: true,
    kChannel: true,
    when: '09:00',
    weekdays: false,
    actions: { notify: true },
    notifyText: R.defaultNotify(type),
    replyText: '',
    emoji: '👍',
    forwardTo: '',
    muteHours: '8',
    webhookUrl: '',
    name: '',
    limit: '',
    dryRun: false,
    allowBots: false,
    allowChannels: false,
    enabled: true,
  };
}

export const PRESETS = [
  {
    id: 'urgent',
    title: '🔔 Срочные сообщения → уведомление',
    form: {
      type: 'message', text: 'срочно, urgent, asap, важно',
      actions: { notify: true }, notifyText: '🔥 {sender} · {chat}: {text}', name: 'Срочное',
    },
  },
  {
    id: 'voices',
    title: '⭐ Голосовые и кружки из личек → в Избранное',
    form: {
      type: 'message', media: 'голосовое, кружок', kGroup: false, kChannel: false,
      actions: { saved: true }, name: 'Голосовые в Избранное',
    },
  },
  {
    id: 'morning',
    title: '☀️ По будням в 9:00 — напоминание',
    form: {
      type: 'schedule', when: '09:00', weekdays: true,
      actions: { notify: true }, notifyText: 'Доброе утро! Сегодня {date}. Загляни в задачи на день.', name: 'Доброе утро',
    },
  },
  {
    id: 'hello',
    title: '↩️ Автоответ на «привет» в личке (пробный режим)',
    form: {
      type: 'message', text: 'привет, здравствуй, добрый день', kGroup: false, kChannel: false,
      actions: { reply: true }, replyText: 'Привет, {sender}! Увидел сообщение, отвечу чуть позже.',
      dryRun: true, limit: '2', name: 'Автоответ на «привет»',
    },
  },
];

export function presetForm(id) {
  const preset = PRESETS.find((p) => p.id === id);
  if (!preset) return null;
  const form = emptyForm(preset.form.type);
  return Object.assign(form, JSON.parse(JSON.stringify(preset.form)));
}

/**
 * Правило -> поля редактора. id чатов остаются id (они не меняются, в
 * отличие от юзернеймов), а имена подсказывает текст окна.
 */
export function ruleToForm(rule, env) {
  const t = rule.trigger;
  const form = emptyForm(t.type);
  const self = env && env.self ? env.self() : null;
  const tokenText = (token) => ((self && token === String(self.id)) ? 'избранное' : token);
  form.chats = t.chats.map(tokenText).join(', ');
  if (t.type === 'message') {
    form.sender = t.sender.join(', ');
    form.text = t.text;
    form.media = t.media.map((x) => (x === '*any' ? 'любое' : x === '*none' ? 'нет' : R.MEDIA_LABEL[x] === 'GIF' ? 'гиф' : R.MEDIA_LABEL[x])).join(', ');
    form.dirIn = t.in;
    form.dirOut = t.out;
  }
  if (t.types) {
    form.kPrivate = t.types.private;
    form.kGroup = t.types.group;
    form.kChannel = t.types.channel;
  }
  if (t.type === 'schedule') {
    form.when = R.whenText(t.schedule);
    form.weekdays = t.weekdays;
  }
  form.actions = {};
  for (const a of rule.actions) {
    form.actions[a.type] = true;
    if (a.type === 'notify') form.notifyText = a.text;
    if (a.type === 'reply') form.replyText = a.text;
    if (a.type === 'react') form.emoji = a.emoji;
    if (a.type === 'forward') form.forwardTo = tokenText(a.to.id);
    if (a.type === 'mute') form.muteHours = String(a.hours).replace('.', ',');
    if (a.type === 'webhook') form.webhookUrl = a.url;
  }
  form.name = rule.name;
  form.limit = String(rule.limit);
  form.dryRun = rule.dryRun;
  form.allowBots = rule.allowBots;
  form.allowChannels = rule.allowChannels;
  form.enabled = rule.enabled;
  return form;
}

const available = (type) => R.ACTIONS.filter((a) => a.triggers.includes(type));
const selected = (form) => available(form.type).filter((a) => form.actions[a.type]);
const paramsFor = (form) => PARAMS.filter((p) => form.actions[p.type] && R.ACTION[p.type].triggers.includes(form.type));
const isChatty = (form) => selected(form).some((a) => a.chatty);

function take(form, values, keys) {
  for (const key of keys) {
    if (values && Object.prototype.hasOwnProperty.call(values, key)) {
      const value = values[key];
      form[key] = (typeof form[key] === 'boolean') ? !!value : String(value === undefined || value === null ? '' : value);
    }
  }
}

const BACK = { id: 'back', text: 'Назад' };
const NEXT = { id: 'ok', text: 'Далее' };

function heading(form, ctx, step) {
  const what = ctx.editing ? 'Правило «' + R.clip(ctx.editing, 24) + '»' : 'Новое правило';
  return '⚡ ' + what + ' · ' + step;
}

async function resolveUsername(token, env) {
  const name = token.slice(1);
  let peer = null;
  try {
    peer = env.find ? env.find(name) : null;
    if (!peer && env.resolve) peer = await env.resolve(name);
  } catch (e) {
    peer = null;
  }
  return peer && peer.id ? peer : null;
}

// --- Шаг 1. Условия ---------------------------------------------------------

function namesLine(parsedOrForm, env) {
  const parsed = R.parseChats(parsedOrForm, { current: env.current(), self: env.self(), lookup: env.lookup });
  if (parsed.error || !parsed.tokens.length) return '';
  const known = parsed.tokens.filter((t) => parsed.names[t]).map((t) => parsed.names[t]);
  return known.length ? '\nСейчас в списке: ' + known.join(', ') + '.' : '';
}

async function stepConditions(form, env, ctx) {
  const type = form.type;
  let spec;
  if (type === 'message') {
    spec = {
      title: heading(form, ctx, 'условия'),
      text: 'Правило сработает на сообщение, которое подходит под все заполненные поля. Пустое поле — без ограничения.\n\n' +
        '1. Чаты: @username или id через запятую; «текущий» — открытый сейчас чат, «избранное» — Избранное.\n' +
        '2. Отправитель: @username, id или часть имени.\n' +
        '3. Текст содержит одно из слов (через запятую) или /регулярное выражение/.\n' +
        '4. Медиа: фото, видео, голосовое, кружок, стикер, гиф, файл, аудио, опрос; «любое» или «нет» — только текст.' +
        namesLine(form.chats, env),
      inputs: [
        { id: 'chats', placeholder: '1. Чаты — пусто: все', value: form.chats },
        { id: 'sender', placeholder: '2. Отправитель — пусто: любой', value: form.sender },
        { id: 'text', placeholder: '3. Текст содержит — пусто: любой', value: form.text },
        { id: 'media', placeholder: '4. Медиа — пусто: неважно', value: form.media },
      ],
      toggles: [
        { id: 'dirIn', text: 'Входящие сообщения', checked: form.dirIn },
        { id: 'dirOut', text: 'Мои исходящие сообщения', checked: form.dirOut },
        { id: 'kPrivate', text: 'Личные чаты и боты', checked: form.kPrivate },
        { id: 'kGroup', text: 'Группы', checked: form.kGroup },
        { id: 'kChannel', text: 'Каналы', checked: form.kChannel },
      ],
    };
  } else if (type === 'schedule') {
    spec = {
      title: heading(form, ctx, 'расписание'),
      text: 'Когда запускать правило:\n' +
        '• 09:00 — каждый день в 9:00, можно несколько: 09:00, 18:30;\n' +
        '• 30m — каждые 30 минут, 2h — каждые 2 часа.\n\n' +
        'Чаты — куда отправлять текст, где показывать «печатает…», читать или выключать звук: @username или id через запятую, «текущий», «избранное». Уведомлению и webhook чаты не нужны.' +
        namesLine(form.chats, env),
      inputs: [
        { id: 'when', placeholder: 'Когда: 09:00 или 30m', value: form.when },
        { id: 'chats', placeholder: 'Чаты — можно оставить пустым', value: form.chats },
      ],
      toggles: [{ id: 'weekdays', text: 'Только по будням (пн–пт)', checked: form.weekdays }],
    };
  } else {
    spec = {
      title: heading(form, ctx, 'какой чат'),
      text: 'Правило сработает, когда ты открываешь подходящий чат.\n\n' +
        'Чаты: @username или id через запятую, «текущий» — открытый сейчас. Пусто — любой чат выбранных типов.' +
        namesLine(form.chats, env),
      inputs: [{ id: 'chats', placeholder: 'Чаты — пусто: любые', value: form.chats }],
      toggles: [
        { id: 'kPrivate', text: 'Личные чаты и боты', checked: form.kPrivate },
        { id: 'kGroup', text: 'Группы', checked: form.kGroup },
        { id: 'kChannel', text: 'Каналы', checked: form.kChannel },
      ],
    };
  }
  spec.buttons = [NEXT, { id: 'cancel', text: 'Отмена' }];
  const result = await env.box(spec);
  take(form, result.values, ['chats', 'sender', 'text', 'media', 'when', 'dirIn', 'dirOut', 'kPrivate', 'kGroup', 'kChannel', 'weekdays']);
  if (result.button !== 'ok') return 'cancel';

  const chats = R.parseChats(form.chats, { current: env.current(), self: env.self(), lookup: env.lookup });
  if (chats.error) return fail(env, chats.error);
  if (type === 'schedule') {
    // Сюда правило будет писать само: юзернеймы превращаем в чаты сразу.
    for (let i = 0; i < chats.tokens.length; ++i) {
      const token = chats.tokens[i];
      if (token[0] !== '@') continue;
      const peer = await resolveUsername(token, env);
      if (!peer) return fail(env, 'Не нашёл чат ' + token + '. Проверь юзернейм или укажи id.');
      chats.tokens[i] = String(peer.id);
      chats.names[String(peer.id)] = peer.name;
    }
    // «@damzachat, 200» — один и тот же чат: писать в него дважды незачем.
    chats.tokens = Array.from(new Set(chats.tokens));
    const when = R.parseWhen(form.when);
    if (when.error) return fail(env, when.error);
    ctx.schedule = when.schedule;
  } else {
    if (!form.kPrivate && !form.kGroup && !form.kChannel) return fail(env, 'Выбери хотя бы один тип чатов: личные, группы или каналы.');
  }
  if (type === 'message') {
    if (!form.dirIn && !form.dirOut) return fail(env, 'Выбери хотя бы одно: входящие или мои исходящие сообщения.');
    const senders = R.parseSenders(form.sender);
    if (senders.error) return fail(env, senders.error);
    const pattern = R.parsePattern(form.text);
    if (pattern.error) return fail(env, pattern.error);
    const media = R.parseMedia(form.media);
    if (media.error) return fail(env, media.error);
    ctx.senders = senders.list;
    ctx.media = media.list;
  }
  ctx.chats = chats;
  return 'next';
}

async function fail(env, message) {
  await env.alert(message, 'Проверь правило');
  return 'stay';
}

// --- Шаг 2. Действия --------------------------------------------------------

async function stepActions(form, env, ctx) {
  const list = available(form.type);
  const result = await env.box({
    title: heading(form, ctx, 'действия'),
    text: 'Что сделать, когда правило сработает? Можно выбрать несколько — они выполнятся по порядку.' +
      (form.type === 'message' ? '\n\nОтвет, реакция и «печатает…» не уходят ботам и в каналы, пока ты не разрешишь это на последнем шаге.' : '') +
      (form.type === 'schedule' ? '\n\nОтправлять текст по расписанию можно не чаще раза в ' + R.MIN_SEND_INTERVAL + ' минут.' : ''),
    toggles: list.map((a) => ({ id: 'a_' + a.type, text: a.icon + ' ' + R.actionTitle(a.type, form.type), checked: !!form.actions[a.type] })),
    buttons: [NEXT, BACK],
  });
  const values = result.values || {};
  for (const a of list) {
    if (Object.prototype.hasOwnProperty.call(values, 'a_' + a.type)) form.actions[a.type] = !!values['a_' + a.type];
  }
  if (result.button === 'back') return 'back';
  if (result.button !== 'ok') return 'cancel';
  if (!selected(form).length) return fail(env, 'Выбери хотя бы одно действие.');
  const needChats = selected(form).filter((a) => ['reply', 'read', 'mute', 'typing'].includes(a.type));
  if (form.type === 'schedule' && needChats.length && !(ctx.chats && ctx.chats.tokens.length)) {
    return fail(env, 'Для действий «' + needChats.map((a) => R.actionTitle(a.type, form.type)).join('», «') +
      '» нужны чаты. Вернись назад и укажи их на первом шаге — или оставь уведомление и webhook.');
  }
  if (form.type === 'schedule' && form.actions.reply && ctx.schedule && ctx.schedule.kind === 'every' &&
    ctx.schedule.minutes < R.MIN_SEND_INTERVAL) {
    return fail(env, 'Отправлять сообщения по расписанию можно не чаще раза в ' + R.MIN_SEND_INTERVAL +
      ' минут. Вернись назад и увеличь интервал — или оставь только уведомление.');
  }
  return 'next';
}

// --- Шаг 3. Настройка действий ----------------------------------------------

async function stepParams(form, env, ctx) {
  const params = paramsFor(form);
  const label = (p) => {
    if (p.type === 'reply' && form.type !== 'message') return 'Текст сообщения';
    if (p.type === 'forward') {
      const peer = /^-?\d+$/.test(form.forwardTo.trim()) && env.lookup ? env.lookup(form.forwardTo.trim()) : null;
      if (peer) return p.label + ' (сейчас: ' + peer.name + ')';
    }
    return p.label;
  };
  const legend = params.map((p, i) => (i + 1) + '. ' + label(p)).join('\n');
  const usesTemplates = params.some((p) => p.type === 'notify' || p.type === 'reply');
  const result = await env.box({
    title: heading(form, ctx, 'настройка действий'),
    text: legend + (usesTemplates ? '\n\nВ тексте можно подставлять: ' + R.TEMPLATE_HELP + '.' : ''),
    inputs: params.map((p, i) => ({ id: p.key, placeholder: (i + 1) + '. ' + p.placeholder, value: form[p.key], multiline: !!p.multiline })),
    buttons: [NEXT, BACK],
  });
  take(form, result.values, params.map((p) => p.key));
  if (result.button === 'back') return 'back';
  if (result.button !== 'ok') return 'cancel';

  if (form.actions.reply && !form.replyText.trim()) return fail(env, 'Напиши текст ответа.');
  if (form.actions.reply && form.replyText.length > 4096) return fail(env, 'Текст ответа длиннее 4096 символов.');
  if (form.actions.react) {
    const emoji = form.emoji.trim();
    if (!emoji || /\s/.test(emoji) || emoji.length > 64) return fail(env, 'Реакция — это один эмодзи, например 👍 или 🔥.');
  }
  if (form.actions.forward && R.ACTION.forward.triggers.includes(form.type)) {
    const parsed = R.parseChats(form.forwardTo, { current: env.current(), self: env.self(), lookup: env.lookup });
    if (parsed.error) return fail(env, parsed.error);
    if (parsed.tokens.length !== 1) return fail(env, 'Укажи один чат, куда пересылать: @username, id или «избранное».');
    let token = parsed.tokens[0];
    let name = parsed.names[token] || '';
    if (token[0] === '@') {
      const peer = await resolveUsername(token, env);
      if (!peer) return fail(env, 'Не нашёл чат ' + token + '. Проверь юзернейм или укажи id.');
      token = String(peer.id);
      name = peer.name;
    }
    ctx.forward = { id: token, name };
  }
  if (form.actions.mute) {
    const hours = Number(String(form.muteHours).replace(',', '.'));
    if (!(hours > 0) || hours > 8760) return fail(env, 'Звук выключается на срок от нескольких минут (0,1 ч) до года (8760 ч).');
  }
  if (form.actions.webhook && !/^https?:\/\/[^\s/?#]+\S*$/i.test(form.webhookUrl.trim())) {
    return fail(env, 'Адрес webhook должен начинаться с https:// (или http://).');
  }
  return 'next';
}

// --- Шаг 4. Название и защита -----------------------------------------------

export function formToRule(form, ctx) {
  const trigger = { type: form.type, chats: ctx.chats ? ctx.chats.tokens : [], names: ctx.chats ? ctx.chats.names : {} };
  if (form.type === 'message') {
    trigger.sender = ctx.senders || [];
    trigger.text = form.text.trim();
    trigger.media = ctx.media || [];
    trigger.in = form.dirIn;
    trigger.out = form.dirOut;
  }
  if (form.type === 'schedule') {
    trigger.schedule = ctx.schedule;
    trigger.weekdays = form.weekdays;
  } else {
    trigger.types = { private: form.kPrivate, group: form.kGroup, channel: form.kChannel };
  }
  const actions = selected(form).map((a) => {
    switch (a.type) {
      case 'notify': return { type: 'notify', text: form.notifyText };
      case 'reply': return { type: 'reply', text: form.replyText };
      case 'react': return { type: 'react', emoji: form.emoji.trim() };
      case 'forward': return { type: 'forward', to: ctx.forward };
      case 'mute': return { type: 'mute', hours: Number(String(form.muteHours).replace(',', '.')) };
      case 'webhook': return { type: 'webhook', url: form.webhookUrl.trim() };
      default: return { type: a.type };
    }
  });
  return {
    name: form.name,
    enabled: form.enabled,
    dryRun: form.dryRun,
    limit: Number(form.limit),
    allowBots: form.allowBots,
    allowChannels: form.allowChannels,
    trigger,
    actions,
  };
}

async function stepFinal(form, env, ctx) {
  const draft = R.normalizeRule(Object.assign(formToRule(form, ctx), { name: '' }), { defaultLimit: env.defaultLimit });
  const chatty = isChatty(form);
  // Подписи чекбоксов в окне — в одну строку: длинное объясняем в тексте.
  const toggles = [{ id: 'dryRun', text: '🧪 Пробный режим', checked: form.dryRun }];
  if (chatty) {
    toggles.push({ id: 'allowBots', text: '🤖 Разрешить отвечать ботам', checked: form.allowBots });
    toggles.push({ id: 'allowChannels', text: '📢 Разрешить ответы в каналах', checked: form.allowChannels });
  }
  toggles.push({ id: 'enabled', text: 'Правило включено', checked: form.enabled });
  const result = await env.box({
    title: heading(form, ctx, 'название и защита'),
    text: (draft ? 'Если: ' + R.describeTrigger(draft) + '\nТо: ' + R.describeActions(draft) + '\n\n' : '') +
      '1. Название — видно в списке и в уведомлениях' + (draft ? '. Пусто — «' + draft.name + '»' : '') + '.\n' +
      '2. Лимит — сколько раз в минуту правило может сработать (1–60). Лишние срабатывания пропускаются.\n\n' +
      '🧪 Пробный режим — правило ничего не делает, а только пишет в журнал, что сделало бы. Удобно, чтобы проверить условия.' +
      (chatty ? '\nБотам и в каналы ответ, реакция и «печатает…» уходят, только если это разрешено ниже.' : ''),
    inputs: [
      { id: 'name', placeholder: '1. Название правила', value: form.name },
      { id: 'limit', placeholder: '2. Лимит в минуту (1–60)', value: form.limit || String(env.defaultLimit) },
    ],
    toggles,
    buttons: [{ id: 'ok', text: 'Сохранить' }, BACK],
  });
  take(form, result.values, ['name', 'limit', 'dryRun', 'allowBots', 'allowChannels', 'enabled']);
  if (result.button === 'back') return 'back';
  if (result.button !== 'ok') return 'cancel';
  const limit = Number(String(form.limit).trim() || env.defaultLimit);
  if (!Number.isInteger(limit) || limit < 1 || limit > 60) return fail(env, 'Лимит — целое число от 1 до 60 срабатываний в минуту.');
  form.limit = String(limit);
  if (!chatty) {
    form.allowBots = false;
    form.allowChannels = false;
  }
  const rule = R.normalizeRule(formToRule(form, ctx), { defaultLimit: env.defaultLimit });
  if (!rule) return fail(env, 'Правило не получилось собрать: проверь условия и действия.');
  ctx.rule = rule;
  return 'done';
}

const STEPS = { conditions: stepConditions, actions: stepActions, params: stepParams, final: stepFinal };

/**
 * Провести пользователя по шагам. Возвращает готовое (нормализованное)
 * правило без id и счётчиков или null, если редактор закрыли.
 * env: { box, alert, current(), self(), lookup(id), find(username),
 *        resolve(username), defaultLimit, editing?: имя правила }
 */
export async function runWizard(form, env) {
  const ctx = { editing: env.editing || '' };
  const steps = () => ['conditions', 'actions'].concat(paramsFor(form).length ? ['params'] : [], ['final']);
  let index = 0;
  // Ограничение на число окон подряд — на случай бесконечных «Проверь правило».
  for (let shown = 0; shown < 100; ++shown) {
    const list = steps();
    const step = list[Math.min(index, list.length - 1)];
    const result = await STEPS[step](form, env, ctx);
    if (result === 'cancel') return null;
    if (result === 'done') return ctx.rule;
    if (result === 'back') index = Math.max(0, list.indexOf(step) - 1);
    else if (result === 'next') index = list.indexOf(step) + 1;
  }
  return null;
}
