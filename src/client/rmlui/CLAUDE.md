# CLAUDE.md — UI2 / RmlUi (src/client/rmlui)

The RmlUi layer and every UI2 panel and window. Loaded when a file under
src/client/rmlui/ is read; the root CLAUDE.md index points here.

### RmlUi UI Layer (Client)
New/custom client panels can be authored in **HTML/CSS-like files** (`.rml` / `.rcss`) instead of
hard-coded C++ draw calls, via **RmlUi 6.2 + FreeType 2.13.3** (`thirdparty/RmlUi-6.2`,
`thirdparty/freetype-2.13.3`, built as x86 `/MT` static libs with `RMLUI_CUSTOM_RTTI`). The client
side is `src/client/rmlui/`; assets live loose in `3ddata/rmlui/`.

Off by default — enable with `[VIDEO] RMLUI=1` in `rose-next.ini` or `ROSE_RMLUI=1`. When enabled,
`/dps` opens the RmlUi damage meter instead of the legacy `CDamageMeterPanel`; both read the same
`CDamageMeter` core, so they A/B in place.

**Scope began as new/custom panels only.** UI2 (below) has since replaced nearly every in-game retail
dialog, the chat input included, one at a time (the login / character-select screens and the few boxes
listed under "Popup sweep" stay classic). `tgamectrl` itself stays: the hidden classic dialogs remain
the models. **IME** is out -- RmlUi has no IME composition handling, and the input boundary is the
fiddliest part of the integration. The purpose is quick, player-editable interfaces, **not** reproducing the original
TSI/atlas workflow: `.rml`/`.rcss` are loaded loose (never via the VFS) so players can edit them.
Textures under `3ddata/rmlui/` resolve **disk first** (a player's file overrides the skin's); **game
art resolves VFS first**, as the classic client does -- a stale loose `icon52.dds` in the launch
folder once drew freshly imported items as empty squares in UI2 only (2026-09-26).

**The shared look lives in `3ddata/rmlui/rose-theme.rcss`** (2026-09-23): the 667 build's "glass"
UI re-expressed as gradients — palette sampled from `GLASSUI_*.DDS`, a colour-neutral `ui-gloss`
`@decorator` laid over `background-color` so one definition makes a glass bar of any colour, and
`ui-window` / `ui-titlebar` / `ui-well` / `ui-btn` / `ui-gauge` component classes. A panel links the
theme first and keeps its own `.rcss` to layout only; the damage meter is the reference. RCSS has no
`var()`, so the palette is a comment block, not tokens. `/uireload` re-reads every stylesheet in
game (styles only — markup still needs a restart). Each gradient is one draw call and the backend
does not batch; measured with the whole HUD converted, batching stays pinned (see `/uistats` under
the performance rules).

**UI2 is the RmlUi remake of the retail HUD, converted one piece at a time** (2026-09-23). The
player picks classic or UI2 with `[VIDEO] UI2=1` (implies RmlUi) or the `/ui2` chat command, which
switches live and saves the choice. `src/client/rmlui/RoseUi2.cpp` holds the whole "converted"
switch: `kReplacedDialogs` (legacy `DLG_TYPE_*` hidden from the outside every frame in
`IT_MGR::Update`, because game code re-shows dialogs freely — no legacy class is edited) and
`kReplacedPieces` (HUD draws that are not dialogs, e.g. `CEndurancePack::Draw`, gated at their
call site). The first two converted were the status panel (`DLG_TYPE_INFO` → `RoseRmlStatusPanel`,
667 compact layout) and the buff strip (`PIECE_BUFF_BAR` → `RoseRmlBuffBar`: buffs, summon/fuel
gauges, worn gear); the sections below cover the rest. Shared helpers: `RoseRmlLayout` (drag position saved in `[UI_LAYOUT]`
as an **anchor**, `minimap=R-250,T0`: the panel's top-left corner hangs from the left/centre/right and top/middle/bottom
point its centre's third of the screen picks, offset in dp, so a layout holds at any resolution and UI scale and can be
shared as text; old `x,y` pixel values are converted on the first run; clamped on screen) and `RoseRmlIcons` (a legacy TSI sprite → `<img src rect>`, loaded through the
VFS — no atlas re-cutting). Things that will bite:

- **RmlUi fires `click` after a drag and has no drag threshold**, so a panel that is both a
  handle and a button must compare press/release positions (the status panel uses 4 px).
- A converted panel must reproduce **every** job of the legacy piece or be deliberately
  documented as dropped: `CEndurancePack::Draw` also drew the summon/fuel gauges and the worn
  gear, not just buffs. The status panel dropped the weapon/ammo slot and two dead buttons.
- Drag-and-drop panels need the bridge (see "Skill bar + drag-and-drop bridge" below):
  `CDragNDropMgr` resolves drop targets by legacy dialog type, so an RmlUi panel is invisible to
  it until it declares a `drop-target`.
- **Stylesheets measure in `dp`, never `px`** (font-effect widths excepted): the UI scale
  (`[VIDEO] UI_SCALE`, 75-200%) is the context's dp ratio, so a `px` length silently ignores it.
  C++ that positions in pixels multiplies by `RoseRmlLayout::GetScaleRatio()`.
- `<handle>` sets `drag: drag` as an **inline** property, so the panel lock (`[VIDEO] UI_LOCK`)
  flips it element by element (`RoseRmlLayout::ApplyLock`); a stylesheet cannot.
- **Never bind an empty string into `data-style-*`**: it parses as `color: ;`, logs a syntax
  warning and the RmlUi debugger pops its event log open at startup. Initialise bound style
  strings to a valid value.
- Data models in one context share a type register; a struct used by two panels
  (`RoseRmlBuffBar::BuffVM`) must be registered once (`RegisterBuffStruct` guards it).

**Skill bar + drag-and-drop bridge** (2026-09-24). `RoseRmlSkillBar` replaces both quickbars as
a *view*: the legacy `CQuickBAR`s stay alive hidden and keep the F-key hotkeys
(`CQuickBAR::Process` has no visibility check, unlike `CTDialog::Process`), the page and the hot-icon observers. The bridge is thin because every quickbar
drop command ends in `Send_cli_SET_HOTICON` and asks `CQuickBAR::GetMouseClickSlot` for the slot:
a row declares `drop-target` (its legacy dialog type), `RoseRmlUi` ends a legacy drag there
(`DragEnd`), and `GetMouseClickSlot` asks `SkillBarSlotAt` while UI2 is on. A release over any
other UI2 area now **cancels** (`CDragNDropMgr::DragCancel`) — before, it was eaten and left the
icon glued to the cursor. Icons report `GetSprite` / `GetCooldown` / `GetStackCount` (virtuals on
`CIcon`; item reload logic shared with `Draw` via `CIconItem::GetUseItemDelay`). Two blur traps:
**skill sprites are 39x39** (items 40x40) — never force an icon's size, let the `rect` size it and
pin it top-left; and **position panels on whole pixels** (`RoseRmlLayout::SetPosition` floors) —
centring an odd width put the bar at x.5 and blurred every icon on it.

**UI2 windows** (2026-09-24): replaced dialogs the player opens/closes (`kReplacedWindows` in
`RoseUi2.cpp`, first: the skill window `RoseRmlSkillWindow`) are routed at the source —
`IT_MGR::OpenDialog` / `CloseDialog` / `IsDlgOpened` defer to `RoseUi2::OpenWindow` & co, so every
opener (menu, hotkey, menu shortcut) and the Escape close-all/reopen work unchanged. UI2 panels are
designed fresh in the 667 style around the jobs the legacy dialog did, not copied from its layout.
A scroll area must itself be positioned (`.ui-well` is) or it does not clip its absolutely
positioned descendants.

**Character window** (2026-09-25, `RoseRmlCharacterWindow`, second `kReplacedWindows` entry):
Stats and Union tabs under a shared identity/EXP/stamina header; the Union tab the classic XML
hid is back. Stat hover text is shared with the classic window (`interface/StatDescriptions.*`).
The party frames (`RoseRmlPartyFrames`, `DLG_TYPE_PARTY`) followed: one card per other member,
Lead/Kick for the leader; the hidden `CPartyDlg` still writes the party chat lines. Then the rest
of the party set: options (`RoseRmlPartyOptions`, `DLG_TYPE_PARTYOPTION`, applies on click, rules
leader-only), the invitation prompt (`RoseRmlPartyInvite`, routed in `Recv_gsv_PARTY_REQ`,
auto-declines after 30 s) and **`RoseRmlMessageBox`** — the UI2 stand-in for `CMsgBox`, *opt-in per
call site*: `RoseRmlUi::ConfirmBox` / `NoticeBox` return false when UI2 cannot show the box and
the caller falls back to `IT_MGR::OpenMsgBox`; commands run through `AddTCommand(0, …)` exactly as
`CMsgBox` runs them.

**Inventory** (2026-09-25, `RoseRmlInventory`, in phases): a *view* over the hidden `CItemDlg`,
which stays the model — its slots receive the item events and hold the per-PC arrangement
(`GetVirtualInventory` / `ApplySavedVirtualInventory`). Drags start from `CItemDlg`'s own drag
items, so every drop target keeps working; the window is `drop-target` `DLG_TYPE_ITEM`, and
`CItemDlg::IsInsideInven` / `IsInsideEquip` / `GetEquipSlot` / `GetInvenSlot` /
`is_costume_tab_open` answer from UI2 while it is on (**`is_costume_tab_open` decides costume vs
normal equipping for every equip**). Repair/appraisal clicks go through
`CItemDlg::HandleStateClick`. Phase 1 = bag, money, weight; phase 2 = the paper doll and ammo;
phase 3 = PAT (the mounted-stats preview is requested by whoever shows it:
`CItemDlg::DriveTuningPreview`, called by UI2 while its PAT section is on screen); phase 4 =
costume (six wearable slots -- `equip_costume` refuses weapons -- and `is_costume_tab_open` is
true only while that section is *on screen*, unlike the classic dialog's sticky tab). Accepted
losses: positioned opens (trade accept) and the desktop "iconize" shortcut. Also: **a bare text node inside a flex
container does not draw** — wrap it in a `<span>`.

**RmlUi has no default stylesheet: a `div` is `display: inline` unless told otherwise**, and an
inline box ignores width and height. `rose-theme.rcss` now declares `div { display: block; }`
(2026-09-25). Before it, every nested bar broke — a track drew as a 2 px sliver of border, an
in-flow fill had no width to take a percentage of, and an absolute fill skipped its unpositioned
inline parent and painted the whole skill list gold — while bars that were direct children of a
flex container worked, because flex blockifies its items. That pattern read as three different
traps across three panels; it was always this one.

**Quest journal** (2026-09-25, `RoseRmlQuestJournal`, `DLG_TYPE_QUEST`): reads `m_Quests`
every frame (none of `CQuestDlg`'s refresh calls needed); Abandon goes through the UI2 message box
and is refused while an NPC dialog is open. **Game text is in the ANSI code page and RmlUi reads
UTF-8** -- `RoseRmlText::FromGame` converts (ASCII passes through); the older panels do not use it
yet, which is harmless until a string carries a non-ASCII character.

**Minimap** (2026-09-26, `RoseRmlMinimap`, `DLG_TYPE_MINIMAP`): a view over the hidden
`CMinimapDLG`, which still loads each zone's map (`SetMinimap`) and keeps the scripts' indicators;
M / L are handed over, and clan zones close it through the window routing. The previous zone's
texture is released on each change (`Rml::ReleaseTexture`). Traps: **`size` is a reserved data
name** (arrays' `.size`) -- binding it fails and the expression prints as text; **RmlUi only
scissors an element whose content overflows its *scroll* size**, and content panned to negative
offsets does not count -- a map view needs `clip: always`; **classic drawing happens before the
RmlUi pass**, so anything over a UI2 panel must be an RmlUi element -- except `CToolTipMgr`'s
tooltip and a legacy drag, which UI2 draws after the pass (`CGameState::render_dev_ui`); panels
place tooltips at the cursor like the classic slots (`RoseRmlUi::PlaceTooltipAtCursor`, 2026-09-26);
and labels need
`pointer-events: none` and whole-pixel placement (a `translate(-50%)` centring blurs odd widths).
**CSS `transform` works now**: `RoseRmlRenderer::SetTransform` (world = translation x transform;
Rml's column-major data read as-is is the D3D row-vector matrix).

**Conversation** (2026-09-26, `RoseRmlConversation`): one window for the three conversation
dialogs (`DLG_TYPE_DIALOG` / `SELECTEVENT` / `EVENTDIALOG`, three looks). **Not a view over the
classic dialogs**: their `Hide()` wipes text and answers (callbacks included). The engine
(`CEvent`) reaches the UI only through `IT_MGR::OpenQueryDLG` / `QueryDLG_AppendExam` /
`CloseQueryDlg`; the first two branch to UI2, the third already routes. A click calls the
answer's own callback (`CEvent::Click_ITEM`), so every script action is unchanged. It closes on a
zone change: the zone load deletes every `CEvent`, and a held answer would dangle. Classic text
markup (`{FC=n}` / `{B}` / `{BR}`) becomes RML via `data-rml`. Also: **a `data-for` whose rows
also read a shared variable can be evaluated past the end when the array shrinks in the same
frame that variable changes** ("Data array index out of bounds") -- keep such arrays grow-only
and hide unused rows (the minimap's marks). Root cause found 2026-10-13 and patched in the vendored
RmlUi (`DataViewDefault.cpp`, `DataViewFor` bias -1000): a row clone is a *sibling* of its
`data-for` element, so a view on the row itself (`data-class-ui-blink="b.expiring"` in the buff
strip and target frame) tied with the for-view in sort order and the unstable sort sometimes ran it
first after a shrink. The grow-only lists are now belt and braces, not required.

**Shop** (2026-09-26, `RoseRmlShop`, `DLG_TYPE_STORE` + `DLG_TYPE_DEAL` in one window): a view
over the hidden `CStoreDLG` / `CDealDLG`, whose slots, drag items and commands it uses; the shelf
side is `drop-target` STORE (a bag item sells now), the basket side DEAL (onto the selling list).
Closing empties the basket -- `CDealDLG::Hide`'s job, which a routed close skips -- and
`IT_MGR::Close_store` hands the whole close to UI2. **The quantity popup had to ship with it**:
classic windows draw under RmlUi, so any popup a UI2 window can raise must be UI2 too.
`RoseRmlNumberInput` replaces `CNumberInputDlg` (`DLG_TYPE_N_INPUT`) everywhere; the hidden dialog
keeps the command (`Submit` / `Cancel`), `RoseUi2::OpenWindow` never toggles it (a second question
used to close the first), and it takes the keyboard through `RoseRmlUi::ProcessWndMsg` (digits,
Backspace, Enter, Escape -- the Enter/Escape `WM_CHAR` is swallowed or it opens the chat). For
the same reason `IT_MGR::OpenMsgBox` sends a plain notice (OK only, no command, no type, no
invoker) to the UI2 message box while UI2 is on. The popup and the message box stay on top with
`body { z-index }`: a click pulls its document to the front.

**Trade** (2026-09-26, `RoseRmlTrade`, `DLG_TYPE_EXCHANGE`): a view over the hidden
`CExchangeDLG` (both offers' slots, drag items, commands). **Its `Hide()` ends the trade**, so the
classic dialog must never be shown, and the UI2 window ends it itself on every close; Cancel/X also
sends `RESULT_TRADE_CANCEL`. The request prompt (`RoseRmlTradeInvite`) replaces the *typed*
`MSGTYPE_RECV_TRADE_REQ` box -- typed boxes stay classic under the plain-notice routing -- and
declines after 30 s. Two changes from classic: Ready can be taken back (`RESULT_TRADE_UNCHECK_READY`,
which the server always had), and when the other side changes its offer while you are ready your
Ready is taken back for you (classic only warned; the server lets the unready side keep editing).
`citstatenormal.cpp` blocked world clicks during a trade by testing the classic dialog's
`IsVision()`, always false once UI2 hides it -- **any classic check of a replaced dialog's
visibility must go through `IT_MGR::IsDlgOpened`**. Also: a panel's hover/point lookups must stay
in its own document (`RoseRmlLayout::HoverIn`) -- `slot-kind` is used by three windows, and the
inventory read an empty trade slot as bag slot 0.

**RmlUi performance rules** (measured 2026-09-26; the shop's first open went from a 93 ms frame
to 12 ms, a tab switch from 12-25 ms to nothing measurable):

- **No auto-sized flex nested in flex.** RmlUi formats an auto-sized flex item up to three times
  (base size, hypothetical cross size, final), multiplied at every nesting level, and it
  re-lays out the **whole document** on any change. The shelf's icons sat five flex levels deep.
  Grids are fixed-size floats in a `display: flow-root` box, windows get a definite `width`, and
  flex is kept to small rows (tabs, totals, buttons). Shop, inventory and skill bar follow this.
- **Anything that changes every frame must not force layout.** `width` / `height` / text do;
  `transform` and `visibility` do not (the forces-layout flag in `StyleSheetSpecification.cpp`).
  Cooldown curtains are `scaleY` with `data-visible`, not a height with `data-if`.
- The renderer keeps geometry CPU-side and draws with `DrawIndexedPrimitiveUP` (a D3D vertex +
  index buffer per piece made a window's first draw ~50 ms), loads file textures in their own
  format (DXT stays compressed; texture stage 1 premultiplies at draw time -- decompressing cost
  ~7 ms a sheet), and decodes them in `BeginFrame` under a 5 ms budget, not all in one frame.
- `rmlui.vcxproj`'s release build lacked `NDEBUG`, so `RMLUI_DEBUG` (asserts, debug checks) was
  compiled in while the client's headers had it off. Fixed; it was not the layout cost.
- `[rmlui] slow UI frame:` in `client-*.log` names the panels and phases whenever UI2 takes more
  than 8 ms of a frame, plus what the renderer had to build. Measure with it before guessing.
- `/uistats` (2026-09-26) measures the next 3 s -- UI update and render time per frame, the whole
  frame, draw calls split into gradients / textured / texture switches, the open documents -- and
  writes one line to `client.log`; nothing is recorded until it is typed. Measure with `vsync=0`.
  First reading: the UI2 HUD alone costs 0.73 ms a frame (the whole frame ran faster than the classic
  HUD's), seven windows 1.4 ms and 542 draws; texture switches ~ draws (text, icons and shapes
  alternate in paint order), so draw-call batching would save ~0.5 ms and stays pinned until the UI
  passes ~2 ms. Also seen: with UI2 off the panels' updates still cost 0.37 ms a frame.

**Storage** (2026-09-26, `RoseRmlStorage`, `DLG_TYPE_BANK`): a view over the hidden `CBankDlg`.
**The deposit command reads the tab from the classic dialog** (`CTCmdMoveItemInv2Bank` ->
`CBankDlg::GetCurrentTab()` decides a Platinum deposit), so the UI2 window keeps it in step
(`SetCurrentTab`) -- the same shape as the trade's movement lock: classic code that asks a hidden
dialog for its state must get UI2's answer. The walk-away close ran only while the classic dialog
was visible and is UI2's now. Zuly goes through the UI2 quantity popup; `CBankWindowDlg` is never
shown. Double-click takes an item out (the classic icon had no command).

**Personal shops** (2026-09-26): visiting one (`RoseRmlAvatarStore`, `DLG_TYPE_AVATARSTORE`) and
your own (`RoseRmlPrivateStore`, `DLG_TYPE_PRIVATESTORE`, with the price form `RoseRmlGoodsForm`,
`DLG_TYPE_GOODS`) are views over the hidden classic dialogs, whose `Hide()` wipes the visited shop
or **closes your own** -- so UI2 calls their split halves (`CAvatarStoreDlg::Prepare/Reset`,
`CPrivateStoreDlg::Prepare/Teardown`) and never shows or hides them; `citstatedead` closes the
own shop through `IT_MGR::CloseDialog`. The bag's drop command reads the own shop's tab from the
classic dialog (`SetTab` keeps it in step), as the storage deposit does. **The classic Wanted list
is two steps**: a dropped item only joins the wish list (unpriced, never sent to visitors) until it
is selected and priced -- which read as "listed at 0, invisible" and was not a bug; under UI2 a drop
on Wanted asks the price at once. The sell list allowed 31 items (`MAX_P_STORE_ITEM_SLOT`) into a
30-slot dialog; capped at 30. The visited shop and the price form never toggle.

**Crafting windows** (2026-09-26): **a hidden classic dialog does nothing per
frame** -- `CTDialog::Update` (and most dialogs' own `Update`/`Process`) returns while
`!IsVision()`, which also stops its command queue. Upgrading and crafting play their result
animation in that per-frame code and only there apply the result to the bag
(`CUpgradeDlgStateResult::Leave` -> `ApplyResultItemSet`, `CMakeStateResult::Update` ->
`Add_ITEM`); `Recv_gsv_CREATE_ITEM_REPLY` drops the reply unless `CMakeDLG` is visible, and the
crafting result boxes are invoked by `DLG_TYPE_MAKE` with a command that runs from its queue. So
those two cannot be a pure view: UI2 drives start/wait/result itself and keeps the classic dialogs
for slots and drag items. Break down (`RoseRmlSeparate`, `DLG_TYPE_SEPARATE`) is done: a view over
`CSeparateDlg` (`Start()` holds the button's checks), the decomposition result's icon box
(`OpenMsgBox2`, classic only) is `RoseRmlUi::ItemsBox`, and `CSeparate::RemoveItem` now clears the
wait flag -- closing mid-request left breaking down stuck until relog. **RmlUi puts a block that
follows a float *below* it, not beside it**: an icon beside text is pinned `position: absolute`
in a positioned row, never floated (floats are fine for grids of equal cells). Upgrade
(`RoseRmlUpgrade`, `DLG_TYPE_UPGRADE`) is done: `CUpgradeDlg` keeps slots, drag items and the state
machine (`GetState`, `Start()`); UI2 plays the result bar itself and opens the classic result box
(routed to UI2), whose OK (`CTCmdChangeStateUpgradeDlg`) leaves the result state and applies the
result -- so the window refuses to close (and the NPC walk-away waits) while a request or result is
pending. The bar is a **roll, not progress**: it stops where it lands (short of the success mark on
a failure), eased and with the mark labelled. Crafting (`RoseRmlCraft`, `DLG_TYPE_MAKE`) completes
the set: `Recv_gsv_CREATE_ITEM_REPLY` asks `IT_MGR::IsDlgOpened` under UI2 (it dropped the answer
while the classic dialog was hidden), `CMakeDLG::RecvResult` skips the classic result state's
`Init` under UI2 (its boxes are invoked by the hidden dialog and their OK never runs), and UI2 plays
the bars, applies the result (`SubItemsAfterRecvResult` + `Add_ITEM`), reports it and puts the
dialog back to NORMAL; a request with no answer frees the window after 20 s. Kinds and recipes are
grow-only lists (rows hidden, never removed).

**Menu bar and system menu** (2026-09-26): `RoseRmlMenuBar` replaces the classic pop-up menu
(`DLG_TYPE_MENU`) with an always-shown bar (Char, Bag, Skills, Quests, Community, Clan, Options,
System -- Help and Game Info dropped); the routing makes opening the menu a no-op and its close (every
world click) harmless. **The tutorial blinks menu buttons** (`SC_SetButtonBlink(DLG_TYPE_MENU,
MENU_BTN_*)`): on a replaced dialog that would blink the hidden classic button, so
`SC_SetButtonBlink` forwards to `RoseRmlUi::BlinkButton`. `RoseRmlSystemMenu` replaces
`CSystemDLG` (`DLG_TYPE_SYSTEM`) through the shared `CSystemDLG::RequestLeave`. Also: a
`.ui-btn`'s 1dp border is outside its width unless `box-sizing: border-box` -- a row of fixed-width
buttons wrapped on it.

**Community** (2026-09-26; mail/memos deliberately not ported): `RoseRmlCommunity` (Friends and
Rooms tabs, `DLG_TYPE_COMMUNITY` + `ADDFRIEND`), `RoseRmlMessages` (one tabbed window for the
private chats, `DLG_TYPE_PRIVATECHAT`) and `RoseRmlChatRoom` (`DLG_TYPE_CHATROOM`). **There is no
friends-list model**: the list is `CCommDlg`'s list-box items, which the messenger packets update
directly -- UI2 reads them (`GetFriendAt`). **Classic private chats and the room window show
themselves** (`CTCmdOpenPrivateChatDlg` / the `CChatRoom` activated event call `Show()`, not
`OpenDialog`) and **their `Hide()` destroys the chat / leaves the room** -- so under UI2
`IT_MGR::OpenPrivateChatDlg`, `Recv_wsv_MESSENGER_CHAT` and `Recv_wsv_CHATROOM_MSG` branch to UI2,
and the room dialog opens and closes through `IT_MGR` (which also closes the window when kicked).
Room messages are named from the hidden `CChatRoomDlg`'s members (`GetMemberName`). The client
never requested the room list (`Send_cli_CHAT_ROOM_LIST` had no caller): the Rooms tab asks, and
`Recv_wsv_CHATROOM` asks for the next 15 on a non-final page.

**Clan** (2026-09-27): `RoseRmlClan` (`DLG_TYPE_CLAN` + `CLAN_NOTICE`, the notice box is the
Notice tab editing in place) and `RoseRmlClanOrganize` (`DLG_TYPE_CLAN_ORGANIZE`). **The member
list is the hidden `CClanDlg`'s**: `CClan` keeps only names and ranks, while contribution,
channel, level and job live on the classic list items (`GetMemberAt` / `FindMember`); an offline
member's level and job are never set, so show them only while online. The buttons are shared
statics (`CClanDlg::Request*`, `CClanOrganizeDlg::RequestOrganize`), so classic and UI2 run the
same checks; Leave now asks first in both (`CTCmdQuitClan`). Clan skills drag to either bar row
(`CTCmdDragClanSkill2QuickBar` takes the bar type). The custom mark (`Mark.bmp`, and the
downloaded `clanmark\<server>_<clan>.bmp`) is loaded by **full path from disk** -- the renderer
now falls back to disk when the VFS has no such file -- and released (`Rml::ReleaseTexture`) when
the file's write time changes, or RmlUi keeps drawing the cached old mark. A `<textarea>` keeps
the keyboard on Enter (a new line), like a `keep-focus` field.

**Skill tree** (2026-09-27, `RoseRmlSkillTree`, `DLG_TYPE_SKILLTREE`): **generated from
`LIST_SKILL`, nothing hand-placed** -- no XML coordinates, no painted DDS pages. Skills = rank-1
rows whose class set (col 35 -> `LIST_CLASS`) names a job of your family (class-less ones stay
out); connectors = the prerequisites (cols 39-44, skill + rank), which hold every line the classic
art drew plus the second prerequisites it left out; lanes = skills with no prerequisite that
others need (lone ones share a last lane); column = prerequisite depth; lane = the first
prerequisite's. The classic art had never drawn 15 skills (every Cleric buff, Vanish, two Dealer
crafts). **A new skill row shows up in its place with its lines on its own** -- the recipe in
`doc/skill-tree-art.md` is classic-only now. Learned / ready (requirements met) / locked read at
three brightness levels; a filter dims the other second job. Ranks are still raised in the skill
window, by decision, and the tree does not say where books are sold (discovery).

**Text fields** (2026-09-26): an RmlUi `<input type="text" class="ui-field">` with the focus owns
the keyboard -- `RoseRmlUi::ProcessWndMsg` hands it every key and character (printable ASCII only:
plain English by decision, no IME) and none reaches the game; `numeric="1"` takes digits only.
Enter arrives as a `change` event with `linebreak`, Escape as a `keydown` (`KI_ESCAPE`); both, a
world click, or the window closing end typing, and their `WM_CHAR` is swallowed (it would open the
chat); a field marked `keep-focus="1"` (a conversation's) keeps the keyboard on Enter. Focusing a
field unfocuses the classic chat box. Read a field's text from its `value`
attribute rather than a `data-value` binding. Avoid `&&` in markup expressions (XML would need
`&amp;&amp;`); compute such flags in the view model.

**Popup sweep** (2026-09-26): classic popups draw under the UI2 windows, so while UI2 is on
`IT_MGR::OpenMsgBox` routes every box **without a message type or an invoking dialog** to
`RoseRmlMessageBox` -- OK-only = a self-closing notice, OK + command = one button that waits, OK/Cancel
= a question. Typed boxes stay classic unless routed by name (something looks them up): party,
trade, friend, clan and cart-ride requests have UI2 prompts that decline after 30 s (the cart one
also once the carts drift apart, as `CMsgBox_CartRide` did). The death window (`DLG_TYPE_RESTART`)
is a routed window backed by a typed message-box entry (`kMsgTypeRestart`), the logout countdown
another (`kMsgTypeLogout`, re-worded every second), both *urgent* (front of the queue); the
tutorial's `SC_ShowNotifyMessage` renders its markup. Still classic: the crafting/break-down result
boxes (their windows are classic), the GM announcement/help banner, and the dead name-input dialog.
`.ui-btn.on` (ember at rest) is for a *selected* tab/toggle only -- action buttons turn ember on hover.

**Mouse ownership** (2026-09-26): a button pressed outside the UI2 panels keeps the mouse for the
world until it comes back up (`g_uWorldButtons` in `RoseRmlUi::ProcessWndMsg`) -- moves and that
release go to the world wherever the cursor is, and RmlUi is not told. Before, a right-drag camera
crossing a panel had its moves eaten there: the camera froze, then jumped when the cursor came out.
A legacy icon drag is checked first: it can start in a classic window (a world press) and still end
on a UI2 panel. **The right button is always the world's** (2026-09-27), even pressed over a panel:
the camera anchors its drag on `WM_RBUTTONDOWN` (`m_PosRButtonClick`), so a panel that ate the press
left the camera frozen, then jumping the whole distance once the cursor left the panel. No UI2 panel
uses right-click; one that ever does must take this back. **`IT_MGR::IsMouseOnInterface` counts
UI2 panels** (`RoseRmlUi::IsMouseOverUi`, false during a world press): the per-frame cursor choice
(`CGameStateMain::UpdateCheckFrame`) forces the arrow only over "interface", so an NPC's talk bubble
stayed on over UI2 windows and world hover labels showed through them.

**UI2 sounds** come from the classic XML: a UI2 window plays its replaced dialog's `SHOWSID` /
`HIDESID` (`RoseUi2::PlayWindowSound`), and a context-wide listener plays the classic `CLICKSID`
(8) for any `.ui-btn` or `.ui-click` (`.ui-static` opts out). `CTDialog::Hide` always plays its
close sound, so `HideReplacedDialogs` hides silently. Skill bar slots use on **double-click**, as
the classic quickbar.

**Summon cards** (2026-10-16, `RoseRmlSummons`, `RoseUi2::PIECE_SUMMON_PANEL`, replacing
`CSummonInfoPanel`): one card per owned summon under a title bar carrying the summon gauge --
name, level, eased HP, the summon-time ATK/DEF/RES from `SummonMobInfo`, abilities, status
effects (`RoseRmlBuffBar::CollectBuffs`); a click targets the summon. **A summon's abilities are
its AI file's `AIACT_24` (cast a skill) actions** -- no table lists them: LIST_NPC col 16 ->
`g_AI_LIST.CollectCastSkills` (read-only walk added to `CAI_FILE`/`CAI_PATTERN`/`CAI_EVENT`,
cached per NPC row). Most summons (the monster summons, Phantom Sword, Elemental, mercenaries)
cast nothing and show no ability row. The tooltip is written from the skill row (kind from
`SKILL_TYPE`, reach/area, power, HP/MP restored, states with duration and chance, then the STL
description) because most monster skills have no description, and the classic
`CIconSkill::GetToolTip` lists learning requirements. Ability icons are the 39 px sprite shrunk
to 24dp (native was too big, the user's call), pinned top-left; the summon's own life-timer state
(`ING_DEC_LIFE_TIME`, every summon has it) is left out of its status icons
(`CollectBuffs`'s `iSkipIng`), though the target frame still shows it. Cards are **not positioned**: a `.ui-tip` takes the nearest
positioned ancestor as its width cap. The default spot is `right:`-anchored with a definite
width, the first top-level panel to do so; a saved `left` wins.

Also UI2: the target frame (`RoseRmlTargetFrame`, new — retail showed the target only overhead),
and, in the classic options, Play > "Use new interface (UI2)" (checkbox ID 49, loose `DlgOption.xml`).

**Options** (2026-09-27, `RoseRmlOptions`, `DLG_TYPE_OPTION`): four tabs -- Graphics, Sound,
Gameplay (incl. the social "accept" toggles and the chat keyboard mode), Interface (UI2 on/off,
scale, lock, skill bar, reset positions -- the old `/ui` Interface window, removed; `/ui` and the
status panel's UI button open this tab). The classic edit model, by decision: OK applies **every**
tab and saves (classic applied only the tab on screen and dropped the rest), Cancel drops the form,
Defaults resets the tab on screen. Clearer names: LIST_CAMERA's three presets are Korean
"low/mid/high" (printed as mojibake) and differ only by fog -- "View distance: Near/Normal/Far";
the "Seven Hearts" input style is the *default* "Act on click", the other one "Select, then act"
(first click selects, arrow keys walk and turn). Antialiasing is offered but applies on restart
(the device is built with it; unsupported levels fall back to none). Left in the ini on purpose:
VSync, the frame cap, exclusive fullscreen, background rendering, every diagnostic knob. The
window-size change is shared with classic (`COptionDlg::ApplyResolution`). **`CClientStorage::Save`
writes a key only when the file holds something else** (`WriteIniIfChanged`): each
`WritePrivateProfileString` rewrites the whole ini, and ~60 of them stuttered the game for most of
a second on every OK; the log line `[options] saved rose-next.ini in X ms` measures it. Also:
`size` is a reserved data-binding name (the window size printed `{{size}}`).

**Chat** (2026-09-27, `RoseRmlChat`, `DLG_TYPE_CHAT` + `CHATFILTER`): system strip, log, five tabs
under it (All/Whisper/Trade/Party/Clan -- the alliance channel is dropped), input; moved by its top
bar, resized from its top-right corner (grows up; `[UI_LAYOUT] chat_size`, `RoseRmlLayout::SavePosition`).
Filters: the Filter button at the end of the tab row (the tab's own channel locked on, whispers
always shown), saved to `[UI_CHAT] TAB<n>`. **Sending is `CChatDLG::SendLine`**, split out of the
classic `SendChatMsg`, which is now a wrapper: every command, prefix (`! @ $ # &`), spam/shout
limit, GM chat block and item-link token rule is shared; it reports what the input holds next
(`@name ` after a whisper). `IT_MGR::AppendChatMsg` feeds UI2 every line (`RoseRmlUi::ChatAppend`)
even while the classic chat shows; `CChatDLG::AddItemLinkToInput` inserts into UI2's input when it is
live. **Log lines are DOM elements added by C++** (`SetInnerRML`, oldest removed past a cap), not a
data binding -- a busy chat must not re-render the log per message. Keyboard: Enter with no field
focused takes the chat input (`ProcessWndMsg`); "Always typing" mode sets `keep-focus` and
`esc-to-game` on it and refocuses it whenever nothing else is typing; **F1-F12 pass through every
UI2 text field** (the skill bar while typing). **The chat body has `z-index: 0`**: a document with an
auto z-index is pulled to the front whenever something in it takes focus, and the auto-refocus
would drag the chat over every window. **An RmlUi text input keeps its default width when pinned
left/right** -- wrap it in a full-width box and give it `width: 100%`.

**Tutorial tip buttons** (`RoseRmlNotifyButtons`, `RoseUi2::PIECE_NOTIFY_BUTTONS`): the blinking
buttons the tutorial adds above the chat; `CNotifyButtonDlg` stays the model (`Activate` is the click
for both UI2 and classic), drawn with the classic normal/blink/over sprites and no frame. The tip
text itself was already UI2 (`SC_ShowNotifyMessage`). Not converted: the GM announcement banner
(`SYSTEMMSG`) -- unused on this server; the logout countdown (`MSGBOX_SPECIAL`) was already UI2.
`CTargetMenu` / `CTargetObjectDLG` are dead code (never constructed).

Linear gradients are implemented in the D3D9 backend without a shader, and `border-radius` needs no
renderer support, so skins need no image files at all. Radial/conic gradients, blurred `box-shadow`
and `filter` are **not** implemented and will warn or do nothing (`transform` is, see Minimap). Full
design notes, phase history, the authoring palette and the traps are in
[doc/rmlui-evaluation.md](../../../doc/rmlui-evaluation.md); the integration specifics (device,
input, rendering, assets) are in the last section below.

## RmlUi UI Layer (`rmlui/`)

CSS-authored panels, off by default (`[VIDEO] RMLUI=1` or `ROSE_RMLUI=1`). `/dps` then opens the
RmlUi damage meter instead of `CDamageMeterPanel`; both consume the same `CDamageMeter` snapshot, so
the data core is untouched and the two views A/B in place. Design notes and phase history:
[doc/rmlui-evaluation.md](../../../doc/rmlui-evaluation.md).

| File | Role |
|---|---|
| `RoseRmlRenderer.*` | `Rml::RenderInterface` on D3D9(Ex) — one draw per `RenderGeometry` (no batching), CPU-side geometry, file textures decoded in `BeginFrame` under a 5 ms budget |
| `RoseRmlSystem.*` | clock, logging, clipboard, `JoinPath` |
| `RoseRmlUi.*` | context ownership, device lifetime, input bridge |
| `RoseRmlDamageMeter.*` | the meter view + data-model bindings |
| `RoseUi2.*` | the UI2 switch (`kReplacedDialogs` / `kReplacedWindows` / `kReplacedPieces`) |
| `RoseRmlLayout.*` | panel positions, UI scale, lock |
| `RoseRmlIcons.*` / `RoseRmlText.h` | TSI sprites as `<img>`; ANSI -> UTF-8 |
| `RoseRmlMessageBox.*` | UI2 message box / prompts |
| `RoseRml<Panel>.*` | one per UI2 panel or window |

Hooks: `CGame::GameLoop` (init/shutdown), `CGameState::render_dev_ui` (the shared in-scene overlay
point every state already calls — must be **outside** `beginSprite()/endSprite()`, the state guard
assumes it owns the device), `CGameState::ProcWndMsgInstant` (input, first refusal),
`CApplication::ApplyWindowedClientResize` + `ResizeWindowByClientSize` (device rebuild), and an
`RmlUi: draws=` debug HUD line.

**Device lifetime — the trap.** `resetScreen()` does **not** `Reset()` the device: `cleanup()` calls
`SAFE_RELEASE(d3d_device)` and `initialize()` creates a *new* one, so a cached `IDirect3DDevice9*`
goes stale. There are seven `resetScreen()` call sites (five in `CApplication`, two in
`coptiondlg.cpp`) plus `resetScreenIfNeeded()` on entering the game (`CGameStatePrepareMain`), and
hooking them individually was got wrong twice. Correctness therefore rests on
`Update()` comparing the engine's current device against ours each frame and rebuilding on change;
the explicit hooks are only an optimisation. Both paths log. Note RmlUi does **not** re-request
compiled geometry or shaders after a reset; geometry lives CPU-side only (drawn with
`DrawIndexedPrimitiveUP`, nothing to rebuild) and the renderer rebuilds gradient ramps and textures
itself in `CreateDeviceObjects`; `Rml::ReleaseTextures()` covers only textures.

**Input arbitration — the other trap.** Consumption is decided by **panel geometry**, never by
`Context::GetHoverElement()`. Each document's `<body>` must span the viewport (or `ElementHandle`
cannot drag a panel — it clamps to the move target's containing block), which makes the hover element
a screen-wide hit target. Every top-level child of every visible document counts as solid UI;
everything else is transparent to the game. A left-press on a panel latches a dragging flag until
release so a drag keeps tracking off-panel, and `WM_MOUSEWHEEL` carries **screen** coordinates unlike
the other mouse messages, so it needs `ScreenToClient` before hit-testing.

**Rendering notes.** RmlUi 6 vertices are **premultiplied alpha** (blend is `ONE`/`INVSRCALPHA`) and
RGBA-ordered where D3D9 wants BGRA. `D3DPOOL_DEFAULT` textures cannot be locked, so both texture
paths go via a `SYSTEMMEM` staging texture + `UpdateTexture`. Linear gradients are implemented
without a shader: the gradient decorator sets each vertex's `tex_coord` to its element-local pixel
position, so a `D3DTTFF_COUNT2` texture-coordinate transform onto a baked 256×1 ramp reproduces one
exactly. Radial/conic gradients, blurred `box-shadow` and `filter` are unimplemented and warn or
no-op; `transform` goes through `SetTransform` (world = translation x transform); `border-radius` needs no renderer support (RmlUi tessellates it).

**Assets.** `.rml`/`.rcss` load loose via `fopen` relative to the launch dir — the VFS bake never
covers them, same as `UI_strID.ID`. That is deliberate: players edit them. Texture loading resolves
**disk first, VFS second** under `3ddata/rmlui/` only, so a player's file beats the skin's; game art
resolves **VFS first**, disk second (run-time files such as clan marks), as the classic client does.
`RoseRmlSystem::JoinPath` passes
game-root paths (`3DDATA/...`) through untouched instead of resolving them against the document
folder. Keep the RmlUi debugger visible (or watch `[rmlui]` warnings in `client-*.log`) while
authoring — on screen a discarded declaration (e.g. `transition: … linear`, which is invalid; RmlUi has only
`linear-in`/`-out`/`-in-out`) looks exactly like one that works.
