# API v2 contract (internal)

This file fixes the names and shapes of the plugin API v2 so that modules,
built-in plugins and docs can be written in parallel. Implementations must
match it exactly; anything extra is welcome but must be documented in the
module's `docs/plugins/types/<module>.d.ts` and `docs/plugins/api/<module>.md`.

Conventions (all modules):

- Peer ids are strings (`Peer.id`). Every function taking a peer accepts a
  `Peer` object or its id string (use `internal.toPeerId`).
- Message ids are numbers. A message is addressed by `(peer, id)`.
- Async functions return Promises (via `internal.request`), rejecting with
  `Error(message)` on failure. Sync functions never throw for "not found":
  they return `null` / `false` / `[]`.
- Registration functions (`add*`, `on*`, `register*`) return a function that
  undoes the registration. Everything a plugin registered is removed
  automatically when it unloads (modules implement `pluginUnloaded`).
- `dzg.on(event, fn)` is the only event mechanism. New events are listed
  below; payloads are plain objects.
- Natives are prefixed with the module name: `messages_history`, `ui_panelCreate`.

## Core (already implemented, see types/core.d.ts)

`dzg.on/off/once`, `dzg.storage`, `dzg.settings`, `dzg.ui.{toast, box, alert,
confirm, prompt, addMessageMenuItem, addChatMenuItem, mainWindow, focused,
find, create, repaint}`, `dzg.theme.{set, setMany, reset}`,
`dzg.chats.{current, get, find, open}`, `dzg.messages.{send, edit, delete,
get, loaded, addFilter, onBeforeSend, refilter}`, `dzg.commands`,
`dzg.http.fetch`, `dzg.fs`, `dzg.clipboard`, `dzg.openUrl`, `dzg.require`,
timers, console. Module `app`: `dzg.app.{language, isNightMode, setNightMode,
restart}`, event `theme.changed {night}`.

Core events: `ready`, `unload`, `session.ready`, `message.new`,
`message.edited`, `message.deleted`, `chat.opened`, `settings.changed`.

## Module `messages` (api_messages.cpp)

```ts
interface Message {               // fields added to the core Message
  entities: { type: string; offset: number; length: number; url?: string; userId?: string; language?: string }[];
  editDate: number;               // 0 if never edited
  groupedId: string;              // album id, '' if none
  forwardFrom?: { name: string; peerId?: string; date: number };
  mediaInfo?: { fileName?: string; mime?: string; size?: number; duration?: number; width?: number; height?: number };
  buttons?: { text: string; type: 'url' | 'callback' | 'other'; url?: string }[][];
  reactions?: { emoji: string; count: number; chosen: boolean }[];
}
dzg.messages.send(peer, text, {
  replyTo?, silent?, runHooks?,
  parse?: 'markdown' | 'html' | 'none',   // default 'none'
  scheduleDate?: number,                  // unix time
  noWebpage?: boolean,
}): boolean
dzg.messages.history(peer, { limit = 50, offsetId = 0, addOffset = 0 }): Promise<Message[]>   // from server, oldest first
dzg.messages.search(peer | null, query, { limit = 50, from?: peer, filter?: 'photo'|'video'|'document'|'url'|'voice'|'music'|'gif' }): Promise<Message[]>
dzg.messages.forward(fromPeer, ids: number | number[], toPeer, { silent?, dropAuthor?, dropCaptions? }): Promise<void>
dzg.messages.react(peer, id, emoji: string | null): Promise<void>   // null removes our reaction
dzg.messages.pin(peer, id, { silent?, unpin? }): Promise<void>
dzg.messages.markRead(peer): void
dzg.messages.sendFile(peer, path, { caption?, asDocument?, replyTo?, silent? }): boolean
dzg.messages.download(peer, id, path?): Promise<string>   // resolves with the saved path
dzg.messages.link(peer, id): string | null
dzg.messages.clickButton(peer, id, row, column): Promise<{ answer?: string; url?: string }>
```

## Module `chats` (api_chats.cpp) and `users` / `accounts` (api_users.cpp)

```ts
interface ChatListEntry extends Peer { unread: number; mentions: number; pinned: boolean; muted: boolean; archived: boolean; lastMessage: Message | null }
dzg.chats.list({ folder?: number | 'archive', limit = 100 }): ChatListEntry[]   // what the client has loaded
dzg.chats.folders(): { id: number; title: string }[]
dzg.chats.resolve(username): Promise<Peer | null>     // asks the server
dzg.chats.join(usernameOrInviteLink): Promise<Peer>
dzg.chats.leave(peer): Promise<void>
dzg.chats.mute(peer, seconds: number | false): void  // true/0 = forever; false = unmute
dzg.chats.archive(peer, archived: boolean): void
dzg.chats.pin(peer, pinned: boolean): void
dzg.chats.markUnread(peer, unread: boolean): void
dzg.chats.setTyping(peer, action: 'typing' | 'cancel' | 'record_voice' | 'upload_photo' | 'upload_document' | 'choose_sticker'): void
dzg.chats.members(peer, { limit = 200, query? }): Promise<Peer[]>
dzg.chats.info(peer): Promise<{ peer: Peer; about: string; membersCount: number; onlineCount: number; inviteLink: string; linkedChat: Peer | null; slowmodeSeconds: number }>

dzg.users.self(): Peer
dzg.users.info(peer): Promise<{ peer: Peer; bio: string; phone: string; isContact: boolean; isPremium: boolean; isBot: boolean; isVerified: boolean; commonChats: number; status: { online: boolean; lastSeen: number } }>
dzg.users.contacts(): Peer[]
dzg.users.block(peer, blocked: boolean): Promise<void>

dzg.accounts.list(): { index: number; self: Peer; active: boolean }[]
dzg.accounts.switchTo(index): void
```

Events: `user.status {user: Peer, online: boolean, lastSeen: number}`,
`chat.typing {chat: Peer, user: Peer, action: string}`,
`chat.unread {chat: Peer, unread: number}`.

## Module `api` — raw MTProto (api_mtproto.cpp, damza/plugins/tl/)

```ts
dzg.api.call(method: string, params?: object, { timeout? }): Promise<any>
// method: 'messages.getHistory'; params use the field names from api.tl.
// Constructors are objects with `_`: { _: 'inputPeerSelf' }. 64-bit ints
// (long) are decimal strings, bytes are base64 strings, Bool is boolean,
// flags fields are computed automatically from present optional fields.
// Results come back the same way.
dzg.api.inputPeer(peer): object        // InputPeer for a Peer (uses known access hashes)
dzg.api.inputUser(peer): object
dzg.api.inputChannel(peer): object
dzg.api.onUpdate(type: string | '*', fn: (update: object) => void): () => void  // raw Update objects, e.g. 'updateUserTyping'
dzg.api.schema(name): { kind: 'method' | 'constructor'; id: number; params: { name: string; type: string }[]; type: string } | null
dzg.api.layer: number
```

## Module `ui` extensions (api_ui.cpp + client hooks)

```ts
dzg.ui.icons(): string[]       // names usable as `icon` below
dzg.ui.addTopBarButton({ icon?: string, text?: string, tooltip?: string, when?(chat): boolean, onClick(chat) }): () => void
dzg.ui.addMainMenuItem({ title, icon?: string, onClick() }): () => void
dzg.ui.addShortcut(keys: string, fn: () => void): () => void      // 'Ctrl+Shift+K', Qt key sequence syntax
dzg.ui.notify({ title, text, onClick?: () => void, silent?: boolean }): void
dzg.ui.openLink(url): void     // https://t.me/..., tg://... handled by the client
dzg.ui.compose: {
  get(): { chat: Peer; text: string } | null
  set(text): boolean
  insert(text): boolean
  focus(): void
  addButton({ icon?: string, text?: string, tooltip?: string, onClick(chat) }): () => void
}
dzg.ui.panel({ title, width = 340 }): Panel   // a panel docked to the right of the main window
interface Panel {
  readonly widget: Widget;
  setTitle(title): void;
  clear(): void;
  addLabel(text, { bold?, muted? }?): Widget;
  addButton(text, onClick, { style?: 'light' }?): Widget;
  addInput({ placeholder?, value?, multiline?, onSubmit?(text), onChange?(text) }): Widget;
  addToggle(text, checked, onChange(checked)): Widget;
  addSeparator(): void;
  addSkip(px?): void;
  close(): void;                 // also fires panel.on('close')
  on(event: 'close', fn): Panel;
  isOpen(): boolean;
}
dzg.messages.addRenderTransform(fn: (message: Message) => string | null | undefined): () => void
// returns the text to display instead of message.text (display only)
```

Events: `app.focus {active: boolean}`, `compose.changed {chat: Peer, text: string}` (throttled).

## Built-in plugins (plugins/builtin/<id>/)

Shipped inside the client and installed into `tdata/plugins/` on start
(version-aware, `"builtin": true` in manifest). Enabled by default unless
noted: `command-palette`, `text-tools`, `deleted-alerts`, `reminders`,
`chat-stats`, `ai-digest` (needs API key), `keyword-alerts`, `damza-theme`,
`anti-ads`. Disabled by default: `auto-reply`.
