# Handoff — September 10, 2026 checkpoint

## Where this stands: the match now starts; it does not yet survive its first seconds

**The port has executed VS match frames.** A run reached
`gm_Scene_Vs_OnFrame` and reported `match_frames=1`, which is the first time
any code inside a Melee match has run in this port. Getting there took two
things: `ftData::x44`, the six bone indices the fighter's ECB is built from
(stop 11.5, the first stop that was not stage-specific), and the resolution
of the stage-param stop, which turned out to be a struct member that does not
exist (stop 9).

**By the end of the session that was 73 frames**, after the camera fix below.
Be precise about what that is and is not: 73 frames is a little over a second,
the test asks for 400, and no run has come close to that. What it does
establish is that the whole chain -- boot, menus, stage load, fighter
creation, scene entry, and then a second of real physics, collision and
rendering -- can complete, and that the remaining work is inside the match
rather than in front of it.

Three stops sit in front of 400 frames right now:

- **A fighter's own position going out of range at ~73 frames.** Same
  `lbvector.c:383` assert as the camera bug, but through
  `Camera_80030CD8`, which passes a subject's position straight in rather
  than deriving one. So this is the fighter, not the camera.
  `pc/tests/fighter_pos_probe.gdb` prints the position with the velocity and
  the previous position, which separates "launched by wrong physics" from
  "went NaN in one frame". Not yet run.
- **The item animation script** (stop 13), the fourth hazard class and the
  first that is not byte order at all. **Deprioritised on the user's
  instruction to leave items for last**, and now diagnosed conclusively
  rather than inferred.
- **`tobj.c:1246`**, an unknown texture format, seen once. A probe is in
  place and has not yet fired.

**The route is still random and that is now the main thing holding the work
back.** It picks a random stage and the CPU picks a random character, so no
two runs are the same experiment. The user has asked for a fixed matchup --
Sheik vs Fox on Pokemon Stadium -- and the groundwork is in
`pc/tests/sss_geometry_probe.gdb`; see "Aiming the route at a named stage and
characters" below for what is measured so far and what is left.

### The old summary, kept because the route description below is still current

`pc/tests/match_smoke.gdb` now takes the game from boot to
`gm_Scene_Vs_OnEnter` without a person touching anything, through the real
menus: title -> main menu -> VS Mode -> Melee -> character select (a character
chosen with the hand cursor, a CPU added on port 2) -> stage select -> match.
Every one of those transitions happens because the game's own menu code acted
on a PADStatus. No scene is skipped, no state is forced, nothing is faked.

VERIFIED: the route reaches `gm_Scene_Vs_OnEnter` and the test reports how far
it got (`css=1 sss=1 in_match=1`). NOT established: a match that runs a single
frame. `match_frames` is still 0 -- the stops are now inside stage and fighter
setup, one schema at a time, which is the same grind that produced the menu.

## Aiming the route at a named stage and characters

The user's target is **Sheik vs Fox on Pokemon Stadium**. The value is not the
matchup, it is that every run becomes the same experiment: at the moment the
route takes a random stage and lets the CPU take a random character, so a fix
and a different draw look identical in the results. That has already cost this
port one wrong "fixed" claim.

What is settled:

- **Pokemon Stadium is slot 18.** `mnStageSel_Scene_OnExit` assigns
  `mnStageSel_803F06D0[slot].xB` straight to `rules.stkind`, and
  `St_Kind_PStadium` is 3. Slot 18's `xB` is 3 and its `x8` is 2, so it is
  selectable with A or Start.
- **The screen has the same open-loop shape as character select.**
  `fn_8025A560` moves the cursor by `0.03 * stick` per frame and clamps it to
  x in [-27, 27], y in [-19, 19], so driving into a corner and counting frames
  out works exactly as it does on the CSS.
- **Sheik is not directly selectable.** It is Zelda plus holding A through the
  load. Fox vs Zelda is the cheaper first target and exercises the same code.

What is measured but not yet usable, and this is the interesting part.
`pc/tests/sss_geometry_probe.gdb` prints every slot's world position after
calling `HSD_JObjSetupMatrix` -- which `lb_8000B1CC`, the function the game
itself hit-tests with, does first, and which two earlier versions of this
probe omitted and got stale root transforms for their trouble. The result:

    slot  0  stkind=0x04  world=34.530056 15.699984
    slot  2  stkind=0x05  world=34.714844 15.699984
    slot  4  stkind=0x0d  world=34.899632 15.699984
    slot  6  stkind=0x08  world=35.084396 15.699984
    slot  8  stkind=0x02  world=35.269184 15.699984
    slot 10  stkind=0x07  world=35.453972 15.699993

**The six columns are 0.1848 apart and the rows are 5.6 apart.** The y spread
is a normal screen layout; the x spread is about fifteen times too small, and
the whole grid sits at x = 34-36 rather than around zero. Whether that is a
real transform bug in this port -- the stage select would then look like a
stack of icons rather than a grid -- or an animation that had not finished at
frame 90, is **not established**, and it is worth knowing which before
anything is built on these numbers.

The way around it that does not need the answer: **measure closed loop**.
Drive the cursor in a known pattern and log `mnStageSel_804D6CAE` each frame,
then read the route off what actually got selected. That needs no coordinate
mapping at all, and it is how the character-select route was built.

Choosing both characters needs one more thing: **a second controller**.
`pc_pad.c` reports only port 0 connected, and a CPU port's character cannot be
set from another port's cursor without dragging its token. Reporting port 1
connected and giving `pc_input_script_poll` a port argument models a perfectly
ordinary hardware setup and turns the character route into two independent
cursors, each doing what the current one already does.

## The route, and why it is worth having

The attract loop wandered: six different endings across runs, so a fix could
never be told from a different path. `pc/src/pc_input_script.c` replaces it
with a fixed route, off unless `PC_INPUT_SCRIPT=vs` is set, so an ordinary run
is untouched.

Two things it needs and how it gets them:

**A clock.** `gm_801A4D34` is called once per SCENE -- the frame loop is
inside it -- so counting its calls held every button down forever and the
opening movie never advanced. The clock is `HSD_PadRenewCopyStatus`, which
`lb_80019900` calls under the same `lb_80019A30(0)` gate that gates the
scene's `on_frame`, one for one. It is also the call that recomputes
`HSD_PadCopyStatus::trigger`, so one tick is exactly one chance for a menu to
see a button edge and exactly one cursor-think's worth of stick movement.
`gm_801A4D34` still identifies the live scene, which is what it is good for.

**Positions at character select.** That screen has no highlighted default:
nothing happens until a hand cursor carries a token onto a portrait, and Start
is ignored until two ports hold a character. The steering is open loop off two
absolute references the screen provides. The cursor clamps to x in [-35, 26]
and y in [-22, 25], so holding a direction parks it on a known edge whatever
it was doing before; from there each frame of full deflection moves it
`0.0002 * (80*80 - 200) = 1.24` units, from `getStickDelta`. Every move drives
into a corner and counts frames out of it.

The geometry was measured out of a running character select rather than read
off the initialisers, because the icon table comes from the disc:

    icon 13                     x -3.40 .. 3.60    y 6.00 .. 13.00   selectable
    port 2 player-kind toggle   x -19.40 .. -13.40 y -4.60 .. 0.20

The token a cursor carries sits at `cursor + (2.7, -2.0)` every frame
(`fn_80262648`) and it is the token, not the cursor, that is hit-tested. So
the hand parks at (-1.52, 10.24) and the token lands at (1.18, 8.24), the
middle of icon 13. Port 2 needs no character chosen for it: its toggle cycles
player kind, with no controller in the port the cycle lands on CPU, and
`mncharsel.c` picks the CPU a character itself.

There is a **one-frame pipeline lag** between the script changing the stick
and `mnCharSel_CursorThink` seeing it, so each direction change gives one
frame to the old direction. Every target above has at least two frames of
margin on each side, which is why it lands anyway. Measured, not assumed:
the trace prints the cursor and token each step.

Stage select opens with the cursor on slot 30, the random stage.
`mnStageSel_80259C28` takes A or Start on an ordinary stage but ONLY Start on
slot 30 -- a run pressing A there produced the rejection sound and sat at
stage select indefinitely, which is how this was found.

### The stage is still random, and that is the next thing to fix about the test

Slot 30 randomises, so each run tests a different stage and a fix cannot be
told from a different stage. Two attempts to make it deterministic failed and
are worth knowing about before a third:

* Reading `mnStageSel_803F06D0[i].x0->mtx[..][3]` twenty frames into the scene
  gave every icon x ~= 34..36, which cannot be right -- the cursor clamps to
  x in [-27, 27]. Either the board is still animating in or those matrices
  were stale.
* Sweeping the cursor's local translate over the whole clamp range on a
  3-unit grid and reading `mnStageSel_804D6CAE` back matched no icon at any
  position. So the hit test's world coordinates are not the local ones the
  clamp is applied to, and the parent transform has to be accounted for.

`fn_8025A310` is the function to read: it clamps a LOCAL translate and then
hit-tests the WORLD position `lb_8000B1CC` returns for it against each icon's
world position. The cursor moves `0.03 * (stick - 30)` per frame, so 1.5 units
per frame at full deflection, with the same corner-clamp trick available.

## Current stops, in the order the route hits them

Each of these is a byte-order schema, and each was found at a real line:

1. **FIXED** `ftdata.c:1674`, "fighter figatree over! c8190000" -- 0x000019c8
   reversed, the length field of a `Fighter_WaitAnimData`. See
   `pc/src/pc_ft_data.c`.
2. **FIXED** `mplib.c:4800` segfault, `groundCollJoint[joint_id]` with
   `joint_id = 256` -- 1, byte-reversed, out of `UnkStageDat::unk8[map].unk20`.
   See the map-entry block in `pc/src/pc_stage_data.c`.
3. **OPEN, and recurring** `particle.c:207`, "psInitDataBanks: unknown
   version", from `grDatFiles_801C6038` -> `psInitDataBankLoad`. Seen on Story,
   Great Bay and Brinstar; other stages convert every bank correctly (a probe
   printed `CONV`/`LOCATE`/`LOAD` with `ver=0042` throughout).

   What the diagnostics have ruled out: the bank IS archive memory, and
   `swap_cmd_bank` neither reported a version it did not recognise nor
   reported missing provenance. What is left is the silent path -- the bank is
   already marked `PC_HSD_PSCMDBANK`, so this port converted it once, and yet
   the version word now reads something else. That means its bytes changed
   after conversion without the DVD path re-marking the range as a fresh
   archive body, so the claim still says "converted" while the contents say
   otherwise. `swap_cmd_bank` now says exactly that when it happens.

   **That paragraph was written about the wrong bank, and the conclusion it
   drew does not follow.** The "marked converted but its version now reads N"
   line fires once, globally, for whichever bank trips it first. Comparing it
   against the panic in the same run shows they are never the same object:

       diagnostic 0x812A8600   panic cmdBank 0x812A9380   (0xD80 apart)
       diagnostic 0x81385FE0   panic cmdBank 0x813860A0   (0xC0 apart)

   So nothing has yet established what is wrong with the bank that actually
   panics, and the ARAM story below was reached by connecting two facts about
   two different objects. It may still be right; it is not evidence.

   `__wrap_psInitDataBankLoad` in `pc/src/pc_hsd_particle.c` now asks at the
   panic's own door, printing the version it is about to read, this port's
   mark on that exact address, and what byte-reversing the version would give.
   The three marks need completely different fixes and one run separates them:
   `0` is untracked memory, `0x80` is a fresh archive body conversion never
   reached, anything else names the schema that claimed it.

   One gap worth knowing about: `psInitDataBank` calls `psInitDataBankLocate`
   from inside particle.c, so `--wrap` does not redirect that one, and the
   same is true of the `psInitDataBankLoad` call on particle.c:332. The
   grdatfiles.c and ground.c call sites do cross a translation unit and are
   wrapped.
4. **FIXED** `ftparts.c:681` segfault, `fp->parts[i].flags8 = 0` past the end
   of a `MAX_FT_PARTS` allocation, because `ftPartsTable[kind]->parts_num`
   came out of PlCo.dat byte-reversed.
5. **FIXED** `ftparts.c:722` segfault in `ftParts_8007506C`, scanning past the
   end of `Fighter_804D6540[kind]->x0` because its count `x4` came out of
   PlCo.dat byte-reversed. The tell was `part=256`: the scan found spurious
   matches in zeroed memory for every part number below that.
6. **FIXED** `aobj.c:47` segfault, `HSD_AObjSetFlags(aobj = 0x3)` reached from
   `grAnime_801C77FC` while Icicle Mountain set itself up. NOT a byte-order
   bug -- see "the third hazard" below.
7. **FIXED** `ftanim.c:1008` "fighter tobj num over!", `ftparts.c:665`
   segfault in the part-visibility walk, `ftcoll.c:3224` "fighter hit num
   over!" -- three counts in the fighter's archive. See `pc/src/pc_ft_data.c`.
8. **MOSTLY FIXED** `ftchangeparam.c:138` segfault, and the same block again
   at `it_26B1.c:207` -- `it_804D6D38[kind - It_Kind_Kuriboh] = article` with
   `kind = 1392508928`, which is an ItemKind read byte-reversed out of the
   attribute block by `ftMr_Init_OnLoad` and its equivalents. Nearly every
   character's `ft<X>_Init_OnLoad` passes an ItemKind from `ext_attr` into
   `it_8026B3F8`, so this is on the load path for most of the roster.

   `ext_attr_words` now takes a size and a list of the members that are NOT
   four-byte fields, and reverses everything else. Two nested descriptors
   account for most of them: **`AbsorbDesc` is uniformly four-byte and needs
   no hole at all**, and **`ReflectDesc` is 0x20 bytes of words then the u8
   `x20_behavior`**, so its hole is the one word that byte sits in.

   Converted: Mario, Fox, Falco, Captain Falcon, Ganondorf, Donkey Kong,
   Kirby, Bowser, Sheik, Ness, Peach, Pikachu, Luigi, Zelda, Dr Mario,
   Pichu and Mr Game & Watch. Dr Mario is in that list although its struct is
   not literally uniform: its odd members are `u8 pad_xN[4]`, four-byte
   padding nothing reads, so reversing it with the rest is inert -- the same
   call ItemCommonData's "filler" words got.

   Still reported and left alone, each for a stated reason: Ice Climbers,
   Jigglypuff and Yoshi have unnamed byte runs; Link and Young Link have a
   `SwordAttrs` and two `UNK_T`; Marth and Roy have a `SwordAttrs`; Samus has
   an `UNK_T`; Mewtwo has nested structs **and a bitfield**, which on this
   target is a second hazard on top of byte order (see stop 13).
9. **FIXED** "not found stage param" -- `MapCollData::x2C` is an inferred
    struct member that does not exist, and swapping it byte-reversed the
    first word of the stage param table next door. See the section below;
    this one cost more runs than anything else in the port.
10. **DIAGNOSED, fix written, NOT yet observed working.** With Onett the whole
    stage comes up and the stop moves into Onett's own setup: `gronett.c:480`
    passes a NULL item to `grMaterial_801C8E08`, because the cars' spawn
    returned NULL. Breaking on each of the four ways `Item_8026862C` can do
    that named the one in a single run:

        ITEMNULL refused by Item_8026784C: hold=3 kind=160

    `hold_kind` 3 is the `It_PKind_Random` branch, which refuses when
    `Item_804A0C64.x58 >= .x5C` -- a live count against a limit. The count is
    zeroed at init; the limit is copied straight out of `ItemCommonData`,
    which nothing converted. A byte-reversed limit is usually enormous and
    would wave every spawn through, but a real value with bit 7 set reverses
    into a **negative** s32, and then a live count of zero is already "at the
    limit" and every spawn is refused. `pc/src/pc_it_data.c` converts it.

    This one is now **confirmed working, indirectly but decisively**: once
    items could spawn at all, the run stopped one line further on, at the
    first thing that reads a spawned item's data (stop 11). A stop that only
    exists because items exist is proof the items exist.

    `itPublicData` also names three `Article` tables and two more blocks and
    **none of those are converted** -- expect them next.
11. **FIXED** `itcoll.c:1021`, "item hit num over!" -- `ItHurtBoneList::count`
    checked against 2, still big-endian. Converted in
    `pc/src/pc_it_data.c` at `__wrap_it_8027163C`.

    Two details worth carrying forward. The hook is on `it_8027163C` rather
    than on `Item_80267978` where the `Article` is assigned, because
    `Item_80267978` is called from inside its own translation unit and
    `--wrap` only redirects references that cross one -- the same trap that
    cost time on `grDatFiles_801C6228`. And the **list** is claimed, not the
    `Article`: one `Article` serves every item of a kind, so the wrap runs
    many times over the same list, and claiming the object actually converted
    is what makes the second run a no-op.

    Verified 3/3: three consecutive runs reached `gm_Scene_Vs_OnEnter` with
    neither "item hit num over!" nor the schema's own "left byte-reversed"
    diagnostic. That diagnostic is worth keeping -- `pc_hsd_claim` declines
    quietly in two quite different situations (untracked memory, or some
    other schema got there first) and the fix differs completely between
    them, so the log now names which by printing `pc_hsd_kind_at`.

11.5 **FIXED** `lb_00B0.c:102`, `return jobj->parent` with `jobj` a garbage
    pointer (0x1a1a1a1a, 0x565e522f), reached from `Fighter_Create` through
    `mpColl_LoadECB_JObj`. `ft_80081B38` passes
    `bones[ft_data->x44->unk0].joint` and five more like it straight into
    `mpColl_SetECBSource_JObj`, so a byte-reversed s16 index reads a joint
    pointer out of whatever lies past the end of the bone table.

    **This one produced the port's first VS match frame.** Unlike almost
    every stop above it, it is not stage-specific -- it is on the path every
    fighter takes into every match, so nothing in a match could run until it
    was fixed. Its two neighbours went in with it: `ftData::x40` (three Vec4s
    of item-pickup offsets) and `::x50` (a Vec2), both copied wholesale into
    the Fighter by `ftCo_800D105C`, converted for the grGroundParam reason --
    they are all floats, floats do not fault, and they would never have
    announced themselves.
11.6 **FIXED** `ftdynamics.c:96` segfault in `ftCo_8009CF84`,
    `lb_8000FD48(fp->parts[bones->bone_id].joint, ..., bones->dyn_desc.count)`.
    `ftData::x2C->ftDynamicBones->array[]` was never converted, so both the
    bone index and the count were big-endian. This is the fighter-side
    instance of stop 12 below, and the descriptor conversion is now shared
    between them -- `pc_dynamics_desc_to_native` in `pc_stage_data.c`.
12. **FIXED** `lb_00F9.c:171` segfault, `prev->desc.lb_unk0.rotate =
    jobj->rotate` with `jobj` NULL, reached from Hyrule Castle's setup:

        lb_8000FD48 (jobj=0x0, desc=0x80841920, max_count=100663296)

    100663296 is 0x06000000 -- 6 with its bytes the other way round.
    `lb_8000FD48` walks `max_count` bones down a jobj chain and checks for
    NULL only once, on entry, so a count of six became a hundred million and
    the walk ran off the end of the chain on the seventh step. The count comes
    from a `DynamicsDesc` handed out as a `dynamicsdata_*` public symbol.

    `count` is not the only field: `lb_80011710` copies `pos` out of the same
    descriptor and reads `count` rows behind `data`, each 0x3C bytes of float.
    The row array is declared `array[2]` inside a 0x90-byte union but is
    really variable-length in the file, which is why the bound comes from
    `count` and is checked against the archive first. Four symbols across two
    stages use this (`dynamicsdata_flag3/4/6` on Hyrule Castle,
    `dynamicsdata_shipflag` on Rainbow Cruise), so the dispatch matches the
    prefix. See `pc_dynamics_desc_to_native` in `pc/src/pc_stage_data.c`.
13. **OPEN, and a fourth hazard class: bitfield allocation order. PROVEN, not
    inferred** -- the diagnostic added for it printed the command word and the
    opcode read out of it both ways, in one run:

        itanimlist.c:410: item command opcode 34 is outside the sixteen-entry
                          handler table (raw opcode 44)
                     word=1e00002c as-read=44 as-PowerPC-would-read=7

    The word in memory is 0x1e00002c. This build reads the low six bits and
    gets 44, which is off the end of a sixteen-entry table. PowerPC reads the
    top six bits -- 0x1e >> 2 -- and gets **7**, a valid opcode that
    `Command_Execute` handles. Nothing here is a byte-order problem: the word
    is big-endian in memory and that is exactly how MWCC expects to find it.

    **Deprioritised on the user's instruction ("we can leave items for
    last"), and one route change would sidestep it entirely**: the item stop
    only exists because items are on. The VS rules menu can turn them off
    through the real menus -- no scene skipped, no state forced -- which takes
    this whole class out of the way until the readers are rewritten.

    `itanimlist.c:385` jumped to 0x000003e8 -- `it_803F22A8[opcode]` with a
    garbage opcode, reached from Corneria's setup through an item animation
    script. The opcode is a bitfield:

        typedef struct itAnimlistCmdUnk {
            u16 x0_b0 : 6;
            u16 opcode : 8;
            u16 x0_b14 : 2;
            u16 x2;
        } itAnimlistCmdUnk;

    MWCC on PowerPC allocates bitfields from the most significant bit of the
    storage unit, GCC on x86 from the least. So on console `opcode` is bits
    9..2 of that u16 and in this build it is bits 13..6 -- **a different bug
    from byte order, and one no amount of swapping fixes.** The port has met
    this once before (the `StageCallbacks` flags aliases) and the established
    treatment is to reverse the declaration order under the port build while
    leaving the console declaration untouched.

    **This is the next blocker and it is a bigger piece of work than anything
    above, so here is the analysis rather than a guess.** It applies equally
    to fighter subaction scripts (`Fighter_WaitAnimData::xC`), which are the
    same kind of stream and are not converted either.

    What is established:

    - The stream is a sequence of **four-byte units**. `union CmdUnion`'s
      largest member is four bytes, and every handler advances with
      `++cmd->u`.
    - Handlers read those units three different ways: as bitfields
      (`cmd->u->unk0.opcode`), as whole words (`*(u32*) cmd->u` in
      `it_8027978C`), and **as pairs of u16 or s16** (`((u16*) cmd->u)[0]`,
      `0.003906f * ((s16*) cmd->u)[0]` in `it_80278F2C`).

    That third one is what makes this hard. **No single byte swap of the
    stream can be right**, because a 32-bit swap fixes the word reads and
    puts the two u16 halves in the wrong order, while a 16-bit pairwise swap
    does the reverse. A consistent answer has to be a 32-bit swap *plus*
    flipping the u16 index at each of those read sites (file order `[0]` is
    the high half, which after a word swap is `[1]`), *plus* reversing the
    bitfield declaration order in every command struct.

    The obvious alternative is GCC's
    `__attribute__((scalar_storage_order("big-endian")))` on the command
    structs, which handles byte order and bitfield allocation together and
    would need no stream conversion at all. **It does not work here as-is**,
    for two reasons worth writing down so nobody re-derives them: the raw
    casts above bypass it entirely, and `struct Command_07` holds a
    `union CmdUnion*` that the archive's relocation table has already turned
    into a host-order pointer -- an SSO read would byte-swap it a second time.

    There is also a prerequisite either way: **the script's extent is not
    recorded in the file**, so a converting schema would have to walk and
    interpret the stream to find its end, which means the schema becomes a
    second implementation of the command decoder and has to agree with the
    real one.

    The extent problem does have a way out, and it is worth knowing even
    though the conclusion below makes it moot: `pc_hsd_endian.c` keeps a mark
    per aligned word and `pc_hsd_claim` returns true exactly once per address,
    so a helper that claims and swaps **one four-byte unit** is idempotent by
    construction and can be called as the interpreter walks. No length, no
    second decoder.

    **But counting the actual read sites settles the design question, and not
    in favour of converting the data.** `itanimlist.c` alone dereferences
    `cmd->u` 84 times, and those reads are not three widths but four -- there
    are raw byte reads as well:

        hit->x40_b4 = ((u8*) cmd->u)[0];
        hit->x41_b4 = (((u8*) cmd->u)[1] >> 7) & 1;
        ...
        hit->x42_b4 = (((u8*) cmd->u)[2] >> 7) & 1;

    A 32-bit swap of the unit leaves the whole-word reads right and breaks the
    other three kinds: u16 halves come out in the wrong order, single bytes
    come out reversed within the word, and bitfields still need their
    declaration order flipped. There is no swap of these bytes that satisfies
    all four.

    **So the answer is to make the readers endian-aware and leave the stream
    exactly as it came off the disc.** That means byte-order-explicit accessor
    macros at every one of those sites in `itanimlist.c` and `ftcmdscript.c`,
    guarded so the console declarations are untouched -- mechanical, large,
    and checkable, but not subtle. It is a session's work on its own, which is
    why it was written down here rather than started at the end of this one.

    One thing that is cheap and should go in first either way: `it_802799E4`
    indexes `it_803F22A8[opcode - 10]` into a 16-entry table with no bounds
    check, so a bad opcode jumps to a garbage address (0x000003e8, every
    time). A range check that reports the opcode and stops is strictly better
    than that, and it is exactly what the brief asks for -- fail loudly rather
    than silently do something wrong.

### The two stops that are live right now, inside the match

Both of these appear *after* `gm_Scene_Vs_OnFrame` has run, so they are the
first bugs this port has met that are in a running match rather than in front
of one. Neither is diagnosed; both have a probe in place.

**`lbvector.c:383` is FIXED, and it was the adjacent-globals hazard for the
sixth time.** Two probes ran it down in three steps. The first showed the
camera box handed to `Camera_80030CFC` was healthy and that the NaN arrived
through `sp38`. The second printed the camera's own vectors:

    eye_pos                 = nan nan 1000.000000
    interest                = nan nan 0.000000
    game_camera.translation = nan nan

The z components are right (1000, and 0) and only x and y are NaN, which
places the fault in `game_camera.translation` -- the camera's pan -- and
nowhere near a fighter. `Camera_ApplyQuake` computes it, and it opens with:

    struct CameraStaticData {
        CameraModeCallbacks callbacks;
        HSD_WObjDesc interest;
        HSD_WObjDesc eyepos;
        HSD_CameraDescPerspective desc;
    }* data = (struct CameraStaticData*) &cm_803BCB18;

`cm_803BCB18`, `cm_803BCB3C`, `cm_803BCB50` and `cm_803BCB64` are four
separate statics that the console linker placed end to end -- `symbols.txt`
gives their sizes as 0x24, 0x14, 0x14 and 0x38, exactly that struct -- so
MWCC could reach the camera description through the first of them. GCC aligns
each independently and `data->desc` reads whatever follows the callbacks.

The failure that produces is worth understanding, because it is not the usual
garbage-pointer crash. `viewport.xmax - viewport.xmin` came out **zero**, so
both viewport scales were **infinite**, and with no camera-shake input the
products are `0 * inf` -- NaN. It went into the translation, from there into
the eye position and the interest, and finally into `lbVector_WorldToScreen`,
which refused it. Every step in between propagated the NaN silently.

Fixed the way the other five were: `#ifdef MUST_MATCH` keeps the console
declaration and the port reads `cm_803BCB64` directly.

**The stop's own history is the lesson.** It looked like a fighter bug for a
long time -- it is reached from `ftDrawCommon_80080E18` while a fighter is
drawn, and the first thing a probe found was a fighter standing at x = 1601.
That led to a real bug (the joint-pair extent, below) which turned out to be
a different one. What broke it open was asking which *component* was NaN:
x and y wrong with z right cannot come from a fighter's position, because a
fighter's position is not split that way. **Ask which part of a bad value is
bad before asking who wrote it.**

The original description of this stop follows.

**`lbvector.c:383`**, `HSD_ASSERT(pos3d->x>-50000.0F&&pos3d->x<50000.0F)`, at
2-3 match frames, reached from a fighter being drawn:

    lbVector_WorldToScreen  <- Camera_80030BBC <- Camera_80030CFC(cam_box, 15)
                            <- ftLib_80086A8C  <- ftDrawCommon_80080E18

`Camera_80030CFC` normalizes a direction and scales it by
`cam_box->ext.v.z + tolerance`, so one large float in the fighter's camera box
produces an astronomical position. The box is built by `ftCamera_80076064`
from three things: `ft_data->x3C` (converted as of this checkpoint),
`Stage_GetCamFixedZoom()` (from grGroundParam, converted) and `fp->cur_pos`.
**`fp->cur_pos` is the one nothing here has looked at** -- the spawn position
comes down from `fn_8016E2BC` into `Fighter_Create`, and where it is read from
the stage has not been traced. That is the next thing to check.

**The probe has run and it answers most of this.** At the refusal:

    pos3d         = nan nan nan
    cam_box       = 0x807edea0
    bone_pos      = 1601.000000 7.050000 0.000000
    pos           = 1601.000000 17.000000 0.000000
    ext.v         = 16.000000 -9.000000 11.000000
    ext.h         = -9.000000 22.000000
    tolerance     = 15.000000
    range         = 26.000000
    sp38          = nan nan nan
    sp20          = nan nan nan

Two things follow, and they retire the guesses above.

**The camera box itself is healthy.** The extents are small sane numbers and
`range` is 26, so `ft_data->x3C` and `Stage_GetCamFixedZoom()` are both being
read correctly -- the x3C conversion added this checkpoint did its job. It is
not a large float being scaled.

**The NaN comes in from `sp38`, which is the camera's own geometry.** `sp38`
is the output of
`lbVector_8000E838(&interest, &eye_pos, &cam_box->bone_pos, &sp38)`, and that
function guards its only division against a near-zero length, so a
zero-length normalize is not the path -- `interest` or `eye_pos` was already
NaN on the way in. Those come from the game camera, not from the fighter.

**And the fighter is at x = 1601.** No Melee stage is a tenth that wide; the
blast zones are in the low hundreds. So the most likely single cause of both
symptoms is a wrong spawn position, with the camera going NaN while trying to
frame one fighter 1601 units away from the other.

**Followed up, and there is a real bug at the end of it: the joint-pair array
is twice as long as its count.** `stage_info.x280` -- the spawn-point jobj
table `Ground_801C2D24` reads a fighter's starting position out of -- is
filled by the walk at `ground.c:1984` from `map_head`'s joint-pair entries:

    count = entry->pair_count;
    pair  = entry->pairs;
    for (j = count; j > 0; j--) {
        target = pair[0];
        ...
        stage_info.x280[pair[1]] = jobj;
        pair += 2;
    }

`pair_count` counts **pairs**, and the loop takes two s16 per iteration. This
port's `map_head` schema converted `pair_count` s16s, not `2 * pair_count`, so
**every second entry stayed big-endian** -- half the joint targets and half
the slot indices. Arbitrary stage joints therefore landed in arbitrary
spawn-point slots, which is exactly how a fighter ends up standing at
x = 1601. Fixed in `pc/src/pc_stage_data.c`; `check_array` and `head_wrote`
now cover the true extent too.

**That fix has been built and run, and the stop survives it.** Four runs
after it: two stopped at the item script (stop 13) and two stopped here again,
at 3 match frames. The joint-pair extent was a real bug and is worth having --
the arithmetic in `ground.c` is unambiguous and half that array really was
left big-endian -- but **it is not the cause of this assert.**

The evidence that always pointed away from the spawn is the second probe
sample: a fighter whose own `bone_pos` was a perfectly sane
`-28.0, 18.6, 0.0`, with the NaN still arriving through `sp38`. `sp38` comes
out of `lbVector_8000E838(&interest, &eye_pos, &cam_box->bone_pos, &sp38)`,
and that function guards its only division against a near-zero length, so
**`interest` or `eye_pos` was already NaN on the way in**. Those are the game
camera's own vectors, and nothing about them depends on where a fighter is
standing.

So the next probe belongs on the camera rather than the fighter: keep the same
negated-assert condition, print `HSD_CObjGetInterest`'s output and the eye
position, and walk back to whatever last wrote them. `Camera_800311EC` and
`fn_800301D0` are the frames above it in every sample taken so far.

`pc/tests/camera_box_probe.gdb` drives the same route and prints the camera
box, the tolerance, the range and both intermediate vectors at the moment of
the refusal. **Its breakpoint carries the assert's own condition, negated**,
and that detail is the whole reason it works: the first version broke on
`lbvector.c:383` unconditionally, and an `HSD_ASSERT` line is evaluated on
every call, so it stopped on a perfectly healthy call from a different caller
whose frame had no `cam_box` in it. A line breakpoint on an assert is not a
breakpoint on the assert failing.

Worth ruling in or out first, because it is cheap: `Ground_801C28CC` fills
`stage_info.xA0` with `((s16*) stage_info.param)[53 + j] * ((s16*) param)[13 + j]`
for 35 j. Both operands are s16 arrays and both **are** converted as s16 --
`GroundParam::x6A` is s16 index 53 exactly, and `StageParam::x1A` is s16 index
13 exactly -- so that path looks right, but it is the obvious place for a
stage-supplied position to come from and the arithmetic is worth confirming
against a real row rather than by reading offsets.

**`tobj.c:1246`**, the `default:` of a switch over `imagedesc->format`, at 3
match frames, from `HSD_MObjSetup` in the fighter display path. The assert
says only "0" -- not the format, not whose image it was, not whether this port
ever saw the descriptor. `__wrap_HSD_TObjSetup` in `pc/src/pc_hsd_swap.c` now
reports all four, including `pc_hsd_kind_at` on the descriptor, which
separates "the schema never reached this" (mark 0x80) from "the schema
converted it and the format is still wrong" (mark PC_HSD_IMAGE). That build
is in; the line has not yet appeared in a run.

### FIXED: the stage-param stop was a struct member that does not exist

**Root cause: `MapCollData::x2C` is not a real field, and this port was
swapping it.** `mp/types.h` marks it `/* inferred */`, nothing in the game
reads it, and the archive proves it is not there. The tripwire caught the
write and then said exactly where it landed:

    the watched stage param word sits 44 bytes into the MapCollData struct
    itself, which starts at 2168153272 and runs 48 bytes
    stage param row 0 at 2168153316 changed from 184549376 to 11 during the
    coll_data struct fields

44 is 0x2C. `grGroundParam`'s `stage_params[0].stkind` **is** the word at
`coll_data + 0x2C`. Two objects in one archive cannot overlap, so
`MapCollData` ends at 0x2C and the next object begins there --
`swap_s32(&d->x2C)` was reaching past the end of its own struct into its
neighbour and byte-reversing the first word of the stage param table.

Everything the stop ever showed follows from that, including the part that
looked most like a mystery: **row 0 was always the row the stage needed**,
because the array begins at 0x2C and row 0 is the row that gets hit. And it
appeared on about half of stages because it needs `coll_data` and
`grGroundParam` to be adjacent in that order, which is a per-file property.

The claim now covers `offsetof(MapCollData, x2C)` rather than `sizeof`, so
the recorded extent matches the object that is really there.

Two lessons worth more than the fix:

- **An inferred struct member is a hazard, not a comment.** On console a
  four-byte over-read of a struct is invisible; a four-byte over-*write* is
  not, and every schema in this port writes. Any `/* inferred */` tail member
  that a schema touches deserves the same scrutiny this one got.
- **A range list only exonerates what it records.** `head_check` answered "in
  none of the ranges" five times running and was right every time, because
  `head_wrote` records the three array walks `pc_map_coll_to_native` makes and
  not the fourteen struct-field swaps that run first. The write was invisible
  to the very diagnostic built to catch it.

### The tripwire, and why it beat five runs of range attribution

`watch_arm` runs when `map_head` is fetched, which is the earliest point the
address is knowable: `grGroundParam` can be looked up through
`__real_HSD_ArchiveGetPublicAddress`, which only reads the archive's symbol
table and converts nothing. It records the word's value; `watch_check` at
each later step reports the first step that changed it.

This is worth keeping as a technique. Attribution after the fact could only
ever say "not this one" about things it had thought to record, and five runs
of that produced five true negatives and no progress. Watching the object
itself answered it on the first run that hit the stop.

### Superseded: the stage-param stop as an open question, and a correction

`grGroundParam` claimed the `GroundParam` but not the `stage_params` array
behind it, so two GroundParams resolving to the same array would each convert
it and the second swap would undo the first. That is a real defect and the
claim was added, because **the rule is right on its own merits: claim the
object you convert, not the object you reached it through.** Anything
reachable from two owners needs its own claim, and an array behind a pointer
is exactly that. The other schemas are worth auditing for the same shape.

**It did not fix the stop, and an earlier version of this file said it had.**
That claim rested on three consecutive clean runs, which turned out to be
luck. The tally since, all on random stages:

    with the claim              3 runs, 0 stage-param failures
    with the claim + ARAM carry 3 runs, 1 (that carry was reverted, below)
    with the claim, later       3 runs, 2 (Temple 18 rows, Inishie1 9 rows)

Two failures in six comparable runs. Before the claim it was roughly half, so
six runs cannot even say whether the claim helped. Treat the stop as open and
the double-conversion story as unproven: it explains the evidence but it has
not been shown to be the cause.

What is still solid, and worth not re-deriving: the DAT files are correct on
disc (dumps below), no schema in this port claims the word before
`pc_ground_param_to_native` runs, no archive body is re-marked over claimed
words, and the post-conversion value is the original file bytes -- an even
number of swaps.

**A hardware watchpoint has now narrowed WHEN.** Set on
`&stage_params[0].stkind` as soon as the address is known -- computed by
walking `archive->public_info` in gdb, not by calling into the inferior, which
matters because a gdb inferior call trips the watchpoint on its own scratch
writes and sends you chasing `HSD_ArchiveGetPublicAddress`, which only reads:

    EARLY archive=0x813a5500 gp=0x8134674c params=813464f4 row0=10000000

`row0=10000000` is a host read of the bytes `00 00 00 10` -- big-endian 16,
St_Kind_Yoster, **unconverted and correct** at the moment `map_head` is
fetched. By the time `pc_ground_param_to_native` runs it reads `00000010`,
i.e. host order. So the swap happens between those two points, and the only
thing that runs between them is `pc_stage_head_to_native` -- this port's own
`map_head` schema, on the `map_head` symbol fetch that precedes
`grGroundParam` in `grDatFiles_801C6038`.

The two writes the watchpoint caught in that window put a pointer
(`0x813...`) and then what looks like a code address (`0x7c4bf2`) into that
word, which is not the shape of a byte swap at all -- so something is writing
through a bad pointer rather than converting. Frames were not reliable enough
to name it.

**That experiment has been run, and it came back negative.**
`pc_stage_head_to_native` now records the address range of every walk it makes
(`head_wrote`), and `pc_ground_param_to_native` asks whether
`stage_params[0]` falls inside any of them (`head_check`). On a failing run --
Inishie1, 9 rows -- it reported nothing, so the word is not inside anything
that schema wrote.

That answer was weak at first, because the check could not distinguish "in
none of the ranges" from "no ranges were recorded" -- the schema returns early
when `map_head` is already claimed, and then the list is empty and the
question was never really asked. So `head_check` was made to print the range
count alongside its answer.

**The count has now come back, and it is comfortably non-zero.** Five runs
that reached the check reported 45, 27, 23, 23 and 13 recorded ranges, and
`stage_params[0]` was inside none of them. The negative is now real evidence:
**`pc_stage_head_to_native` is exonerated.** The `coll_data` walks are
recorded in the same list as of this checkpoint, so the same line now covers
both schemas in that window.

There is a second exoneration worth writing down, because it was cheap and it
removes a suspect that looked likely. `pc_ground_param_to_native` prints the
schema mark on `stage_params[0]` whenever it is not `0x80`, and **that line
has never fired.** The array is unclaimed archive memory at the moment the
schema takes it, so no other schema in this port claimed the word first, and
the array is converted exactly once. Both halves of the double-conversion
story are now ruled out.

What remains, and what the watchpoint already said, is that **something is
writing through a bad pointer** -- a pointer value and then a code address
went into that word, which is not the shape of a byte swap. The next thing
to look at is `grDatFiles_801C6228` and anything else that touches the
archive between `map_head` and `grGroundParam`. The next diagnostic worth
building is the reverse of `head_check`: rather than asking after the fact
whose walk covered the word, watch the word itself from the moment the
archive lands and name the writer at the instant it changes.

The failure's own row dump is now wired up and costs nothing: the breakpoint
on `panicMissingStageParam` in `pc/tests/match_smoke.gdb` prints every row's
`stkind` before the test gives up, so the next failing run says whether one
word is reversed, the whole array is, or the array is intact and the stage
simply is not in it. Those three have completely different causes and the
port has never actually distinguished them.

One process note, learned the hard way: **do not edit the gdb script while
runs are in flight.** Two of three runs in this checkpoint's batch caught the
file mid-write and died with parse errors that looked like real failures
(`Undefined command: "m->stage_params"`).

### OPEN: provenance does not survive an ARAM transfer -- and carrying it made things worse

`ARStartDMA` copies bytes between main RAM and ARAM without going near the
disc, and `pc_hsd_endian.c`'s marks -- one per word of main RAM, saying
whether it is archive data and which schema converted it -- do not move with
them. A buffer refilled from ARAM therefore holds one thing while its marks
describe another. The diagnostic added for it says so in as many words:

    pc_hsd_particle: command bank at 0x8132a0e0 is marked converted but its
    version now reads 30; its bytes changed without the range being re-marked

**That much is established.** What is not is the fix. Carrying the marks with
the data -- a shadow byte per ARAM word, stored on the way out and restored on
the way in -- was written, built and run three times, and the results were
worse, so it was reverted rather than kept:

    before the ARAM carry:  3 runs, 0 stage-param failures
    with the ARAM carry:    3 runs, 1 stage-param failure, and 2 crashes on a
                            garbage jobj in lb_00B0.c:102, a shape not seen
                            before it

Three runs each is a small sample and the stage is random, so this is
suggestive rather than conclusive -- but it points the same way twice, and an
unverified change that reintroduces a fixed stop is not worth keeping on
reasoning alone. The likely reason is that restoring an "unclaimed archive"
mark over a buffer lets a second conversion run on an object that was already
converted, which is exactly the double-conversion the `stage_params` claim had
just fixed.

If this is picked up again: the diagnostic is still in place and still fires,
so the bug is real and findable. A narrower fix than blanket mark-carrying is
probably what it wants -- for instance forgetting the destination range on an
ARAM read, which turns a silent double-conversion into a loud "no provenance"
report rather than trying to reconstruct what the marks should say.

### The stage-param stop, and what was known about it before the fix

Roughly half of the random stages reach `gm_Scene_Vs_OnEnter` and stop in
`Ground_801C28CC`, which searches `stage_info.param->stage_params[]` for a row
whose `stkind` matches the stage being loaded and finds none. Observed on Mute
City, Rainbow Cruise, Temple, Yoshi's Island (Yoster) and Venom; Onett,
Fountain of Dreams and Yoshi's Story worked.

What the rows look like matters. A ground's table is one row per stage id it
serves, the first being its own VS `StKind` and the rest event and target
stage ids. On a stage that works, every row arrives big-endian and comes out
right:

    Onett, want 9:   raw 09000000 4b000000 69000000 ...  ->  9, 75, 105, ...

On a stage that fails, **row 0 alone is already host order before conversion,
so converting the array leaves it reversed while every other row is right**:

    Yoster, want 16:  [0] 10000000  [1] 0000005f  [2] 00000087  ...

`pc_ground_param_to_native` did run (its trace line is there), the object is
archive memory, and `swap_stage_param` has no per-row claim -- so nothing in
this port converts row 0 twice on purpose. Something else wrote that one word
first.

**Three things ruled out, in this order.** Each was a real hypothesis with a
test, and all three came back negative -- which is most of what is known:

1. *Another schema converted that word first.* `pc_ground_param_to_native`
   reports the schema mark on `&stage_params[0]` when it is anything but
   "unclaimed". On a failing Venom run it said nothing. So no converter in
   this port had touched it. That also kills the specific suspicion that
   `pc_hsd_mobj_flags_to_native` -- the one converter that writes exactly one
   u32 -- had landed there via a stray `unk28[i]`.

2. *The file itself is odd.* It is not. The DATs were read straight off
   `game.iso` and parsed offline (FST -> file -> archive header -> public
   symbol table -> `grGroundParam` -> `+0xB0`/`+0xB4`). Every row is plain
   big-endian and **no row is named by the relocation table**:

       GrVe.dat  grGroundParam @0x110488  stage_params @0x110230  count 6
                 rows 00000016 00000063 0000007a 000000c5 000000e4 0000010c
                 relocated: false for every row; true for grGroundParam+0xB0
       GrMc.dat  count 9,  rows 0a 4a 67 8d a0 ae ...
       GrOt.dat  count 8,  rows 09 4b 69 91 a9 bb ...

   Note Mute City's file says **count 9** where the running game reported
   count 1, and Venom's row 0 is `0x16` = `St_Kind_Venom`, exactly what the
   lookup wanted. So the data is right and the port is wrong.

3. *The archive was converted twice because its provenance marks were reset.*
   `pc_hsd_archive_body` now reports when it marks a range fresh that still
   holds claimed words -- the one way a second conversion could happen. It
   has not fired.

Since the post-conversion word reads as the **original file bytes**, it has
been swapped an even number of times: not zero (this port swaps it once), so
twice. One of those swaps is `swap_stage_param`. The other is still unfound,
and it is not any of the three above. `pc_hsd_archive_body` has exactly one
caller and `pc_hsd_forget_range` one more (`pc_dvd.c`, per read) -- adding the
same "over claimed words" report to `pc_hsd_forget_range` is the obvious next
probe, since clearing a range to "not archive" would let a second conversion
through just as re-marking would.

Note the same shape explains stop 3 above (a particle command bank this port
marked converted whose version word later read wrong), so one cause may
account for both.

### Character attribute blocks (`ftData::ext_attr`)

One layout per character, and **the file does not say how big it is**, so this
cannot be done generically the way the rest of `ftData` was. Each character
needs its struct, and the decomp states only ten: Captain, CrazyHand, Fox,
Kirby, Link, Mario, Peach, Samus, Yoshi, Zelda. Of those, Kirby has an `s16`
mid-struct and a `u8` at the end of a trailing `ReflectDesc`, Link has a
4-byte filler, and Yoshi has three unnamed `char` runs -- so even where a
struct exists, a blanket word swap is only right after checking it.

Kirby is done, because it is what stopped the route. Everything else is
reported once per character (`pc_ft_data: fighter N has no attribute-block
layout here`) and left alone: these are floats a fighter runs on, and a
wrong guess is a match that plays like nothing rather than one that crashes,
which is exactly the failure this port must not produce quietly.

## Fighter data: what is converted and what is not

`pc/src/pc_ft_data.c` hooks `ftData_8008572C` rather than
`HSD_ArchiveGetPublicAddress`, where the stage schemas hook. The symbol name
there would identify the character, but neither move table's length is in the
archive at all: they are compiled into `ftData_Table_Unk0` and
`ftData_UnkIntPairs`, indexed by `FighterKind`. `ftData_8008572C` is handed
that kind, is only called from other translation units so `--wrap` takes, and
is the one function that fills `gFtDataList`.

Converted so far: the common attribute block (`ftCo_DatAttrs` is four-byte
floats and ints from +0x000 to +0x17C, then one byte), and both
`Fighter_WaitAnimData` tables. **Everything else in `ftData` is still
big-endian** -- `ext_attr`, the hurtbox table at +0x30, the dynamics at +0x2C,
the SFX table at +0x4C, and the rest. Expect them in that order as the match
gets further, and expect `ext_attr` to be a separate schema per character.

### PlCo.dat, the tables every fighter shares

`Fighter_LoadCommonData` pulls 23 pointers out of one public symbol,
`ftLoadCommonData`, so the conversion hangs off the same
`HSD_ArchiveGetPublicAddress` wrapper the stage schemas use.

Converted: `pData[0]` `ftCommonData` (four-byte members except
`x6DC_colorsByPlayer` and the four bytes after it), `pData[4]` `ftPartsTable`
(one `parts_num` per kind), `pData[5]` `Fighter_804D6540` (one count per
kind). **The other twenty are untouched** and will surface the same way.
`ftCommonData` is the one worth noticing: it is the deadzones, thresholds and
knockback constants, so a reversed copy never faults -- it just makes a match
that behaves like nothing.

## The adjacent-globals hazard: now six instances

The console linker packed separate globals contiguously and MWCC reached one
through another. GCC aligns each independently, so that arithmetic lands on
unrelated memory. Found so far: `card_host_storage.h`, THPInit's
`__THPLCWork672`, `CSSAllStorage` in `mncharsel.c`, and now two in
`ft_800852B0`:

    (ftData_UnkCountStruct*) &CostumeListsForeachCharacter[FTKIND_MAX]
    (ftData_UnkCountStruct*) ((u8*) CostumeListsForeachCharacter + 5940)
    (ft_8045993C_t*) &gFtDataList[FTKIND_MAX]

`config/GALE01/symbols.txt` says what those really are:
`CostumeListsForeachCharacter` is 0x803C0EC0 size 0x108 with
`ftData_Table_Unk0` at 0x803C0FC8 and `ftData_UnkIntPairs` at 0x803C25F4 --
base + 0x108 and base + 5940 exactly -- and `gFtDataList` is 0x804598B8 size
0x84 with `ft_8045993C` at 0x8045993C. Those three writes were zeroing 0x30
and 0x210 bytes of whatever GCC placed there, on every boot, since long before
this session. The objects are now named and the arithmetic kept under
`MUST_MATCH`.

The sixth is `Camera_ApplyQuake` in `cm/camera.c`, and it is the one that had
been stopping every match:

    struct CameraStaticData {
        CameraModeCallbacks callbacks;   /* cm_803BCB18, 0x24 */
        HSD_WObjDesc        interest;    /* cm_803BCB3C, 0x14 */
        HSD_WObjDesc        eyepos;      /* cm_803BCB50, 0x14 */
        HSD_CameraDescPerspective desc;  /* cm_803BCB64, 0x38 */
    }* data = (struct CameraStaticData*) &cm_803BCB18;

Four separate statics, laid end to end on console, reached through the first.
The sizes in `symbols.txt` match the struct member for member, which is what
makes the diagnosis certain rather than likely.

**This one is also the clearest illustration of why the hazard is dangerous
rather than merely wrong.** It did not crash and it did not produce an
obviously silly number. `data->desc.viewport` read as a zero-width rectangle,
a zero-width viewport made two scale factors infinite, and `0 * inf` made the
camera pan NaN -- which then propagated through the eye position and the
interest into an assert three frames into a match and two files away, in code
that has nothing to do with cameras.

**When a new stop makes no sense, check this first.** The tell is a decomp
expression that indexes past the end of one global, adds a magic byte offset
to one, or -- as here -- casts the address of one global to a struct that
describes several. `config/GALE01/symbols.txt` settles it in one grep.

## The third hazard: a call that only worked because of the register ABI

`grAnime_801C6F50` dispatches a callback by a small type code, and types 0, 4
and 8 called it through `((Event) func)()` -- a pointer taking no arguments.
Every function the tree passes there takes an `HSD_AObj*`: `fn_801C6EE4` and
`fn_801C6F2C`. On PowerPC that is correct by accident, because `aobj` is
already in r3 when the call is made and the callee finds it there. On a host
ABI that passes arguments on the stack, nothing is pushed and the callee reads
whatever was on it -- `HSD_AObjSetFlags` got `aobj = 0x3` and segfaulted while
Icicle Mountain set itself up.

The fix passes `aobj`, which is right on both and harmless to a callee that
ignores it, since the caller cleans up. The original stays under `MUST_MATCH`.

This is a third kind of hazard, distinct from byte order and adjacent globals,
and it will not announce itself as clearly: **the tell is a call through a
cast function pointer whose cast drops arguments the callee declares.**
`sysdolphin`'s own `callbackForeachFunc` (aobj.c:262) does the same dispatch
correctly -- it always passes `aobj` -- so that one is fine, and a grep for
`((Event) func)()` finds only the three in granime.c today.

## Iterating: the build is incremental now

`tools/phase0/linkexe.sh` keeps `build/phase2/obj` between runs and skips a
file whose object is newer than every prerequisite `-MMD` recorded. An
unchanged tree rebuilds 29 objects -- the ones that have never compiled and
are retried every run -- rather than 1175, so a cycle is about a minute
instead of six. `--clean` forces a full rebuild, and the cache is thrown away
by itself whenever the compiler, its flags or the exclusion list change.

It is conservative on purpose: a missing object, a missing or unreadable
dependency list, or a vanished header all mean rebuild. The failure mode is a
wasted compile, never a stale object in a binary a test then believes.

One trap, if this is ever changed: line 1 of a `.d` file is
`<target>: <deps>`, and on Windows the target is an absolute path whose drive
letter has a colon of its own. Cutting at the first colon leaves
`/Users/...o:` behind as a dependency that never exists, and then every file
rebuilds -- silently, and always, so it looks like the cache simply does not
work.

## Previous September 10 checkpoint: the menu renders and navigates

## Where this stands: the menu renders and navigates; a match does not run yet

The main menu draws with its animated background -- the five items and the
selected-item highlight, the streaks, the rotating text ring, the
SmashBrothers wordmarks and the panel -- and the user confirms by hand that it
boots and navigates by keyboard. Over 240 menu frames, draws accepted went
7876 -> 27510 and skips 21179 -> 2299 across this session.

NOT established: a running match, colour accuracy against console, audio,
saves. 2299 menu draws are still discarded.

## Method that produced all of it: rank the rejections, then fix in that order

Rejection reasons used to be logged once each, which notices a gap but cannot
rank one -- a check discarding every draw on screen looked identical to one
discarding a single stray draw. `pc_gx_material_report()` and
`pc_gx_texture_report()` now tally accepted draws and each skip by reason, and
`pc/tests/menu_smoke.gdb` prints both. Keep using this. It chose every fix
below, and twice it stopped work that would have been wasted.

### Fixed this session, in the order the tally chose

1. **Post-transform texture matrices** (20743 skips -> 671). sysdolphin sets an
   identity 2x4 texgen (tobj.c:492) and loads the texture's whole
   scale/rotate/translate as the POST-transform matrix (tobj.c:488). On its own
   this changed nothing visible; nothing textured could be right without it.
2. **Depth attachment** (7033 -> 0). The framebuffer had none at all.
3. **General blend factors** (4280 -> 0).
4. **Palettized textures** (92 CI4 + 92 CI8 -> 0), through the TLUT a
   `GXLoadTlut` wrapper records.
5. **Raster channel 1 via GX lighting** (7600 -> 0). See below.
6. **Shape animation descriptors**, which fighters need and the menu never did.

2 and 3 are pipeline/render-pass state, NOT shader code. That was assumed
otherwise at first, and the assumption would have blocked both behind a
toolchain they never needed. They did need the fixed 32-pipeline enumeration
replaced with a 64-entry cache keyed on GX state, built on demand.

### Channel 1, and the fix that measurement prevented

The obvious reading -- "channel 1 is a second vertex colour, decode
`GX_VA_CLR1`" -- is WRONG, and `pc_gx_fifo.c` already read and discarded that
attribute, so it looked like a small change. Instrumenting the vertex
descriptor at each rejection gave `with a vertex CLR1: 0, without: 5920`. Not
one such draw supplies one. Channel 1 comes from GX lighting, and tallying
rejected draws by the channel state in force gave ONE configuration for all of
them: lit, both sources from registers, lights 2 and 3, `GX_DF_CLAMP`,
`GX_AF_SPEC`.

GX lights per VERTEX, so the equation is C in `pc/src/pc_gx_light.h` beside the
vertex expansion, where `pc/tests/gx_light_test.c` checks it against answers
worked out by hand. It is a port of encounter/aurora's `lighting_func`
(lib/gx/shader.cpp, MIT), credited in the header.

### Three of my own bugs worth remembering

Each of these had already put a wrong statement in this file or a wrong value
on screen, and none would have been caught by looking at the picture:

- `GXSetChanMatColor`/`GXSetChanAmbColor` were recorded only for channel ids
  below 4, missing `GX_COLOR0A0`/`GX_COLOR1A1` -- the ids sysdolphin uses. It
  reported channel 1's material colour as black; it is white.
- A light's `Color` is a packed u32 `(r<<24)|(g<<16)|(b<<8)|a`
  (GXLight.c:291), not four bytes; reading it as bytes made a blue light yellow.
- `GXLoadNrmMtxImm` was wrapped in `pc_gx_fifo.c` but never added to
  linkexe.sh's wrap list, so `__real_` got a generated placeholder and
  `__wrap_` was never called -- the normal matrix sat at identity for every lit
  vertex. **The placeholder count caught this, not the eye: it went 34 to 35.**
  Watch that number; a rise means a symbol is being silently stubbed.

## Current blocker: getting a match to run

`pc/tests/match_smoke.gdb` is the target. **Its current form is UNVERIFIED** --
it was rewritten to drive the menus with real PADStatus input (title -> Start
-> main menu -> VS Mode -> character select -> stage select -> match) and the
one run of it produced no output before the session ended. Debug it before
trusting a FAIL from it.

It was rewritten because the attract-mode demo picks a different route every
run. Observed endings, all different, all real:

    pobj.c:842        vertex_buffer_size >= nb_vertex_index   (fixed: shape anim)
    particle.c:207    psInitDataBanks: unknown version        (fixed: preload path)
    mnevent.c:737     lb_80011E24 returns joint 0x53faa000, outside game RAM
    mncharsel.c:2482  and :2517, character select cursor
    cobj.c:559        HSD_CObjSetInterest with a NULL cobj, from gmtoulib.c:2643
    pc_gx_render.c    native texture source of unknown extent (see below)

Driving the menus makes the route repeatable; chasing the attract loop does
not. The three unfixed entries above are each a separate investigation and are
probably each a descriptor schema, the same shape as every fix before them.

### One change that needs re-examining

`__wrap_GXLoadTexObj` used to `pc_sys_exit(1)` on "unknown native texture
source": a texobj whose source is a host pointer in neither game RAM nor the
font atlas, so its extent cannot be bounded. Adding CI format support made
that path reachable -- previously CI returned 0 from `pc_texture_source_size`
and fell through -- and it killed the run at character select. It now reports
once and leaves the slot unbound, so the draw renders untextured and loudly,
matching what `pc_gx_texture.c` already does for a format it cannot decode.

That is a deliberate trade and should be revisited: the better answer is to
know the real extent of those sources rather than to skip them. Find out which
textures they are first.

### What still gets discarded in the menu

     869  texcoord index beyond enabled texgen count
     640  non-identity texgen
     630  more than four TEV stages
     130  logic/subtract blending

No dominant cause remains. `non-identity texgen` is the residue the
post-transform work did not cover -- narrow it by recording which
configurations those are, exactly as the first 20743 were narrowed.

Tests: gx_light_test, texture_decode_test, thp_kernel_test, stage_data_test,
hsd_archive_test all pass. `pc/tests/bmp_to_png.py` converts a capture for
viewing; captures stay under gitignored `build/`.

## Latest checkpoint: main menu runs for 240 frames; rendering incomplete

This section supersedes all checkpoints below. Opening-movie playback was
already working; this session fixes the normal Start-to-menu path after it.
No movie decoder or GX renderer runtime implementation changed in this session.

### Three diagnosed and fixed failures

1. Menu light descriptors were still big-endian. The ambient light arrived as
   flags `0x2400`, and both point lights as `0x0e00`, so `HSD_LObjLoadDesc`
   classified all three as ambient. The menu's point-light search then walked
   off the list at `mnmain.c:1789`. Added provenance-checked conversion for
   light flags, attenuation descriptors, and WObj positions. Also wrapped
   `HSD_WObjLoadDesc`: it calls the class load method directly, bypassing the
   existing WObjInit wrapper. Runtime now has flags `0x24/0x0e/0x0e`, valid
   point-light positions and attenuation, and passes this lookup.
2. Once light loading worked, `mn_8022BE34_OnEnter` overwrote its saved stack
   frame/return address. Its console-specific expression writes a Vec3 20
   bytes beyond the local Vec3; GCC returned to address zero, with camera Z
   (`0x424c0000`, 51.0) visible on the corrupted stack. The native-only branch
   now passes `&pos`; the console build retains the original expression.
3. Menu initialization then completed, but drawing asserted at `tobj.c:1246`
   within a few frames. Texture animation selected an unconverted image:
   width `0x4000`, height `0x3000`, format `0x02000000` (actually 64x48 IA4).
   Added texture-animation id/count conversion and image/TLUT table traversal
   through the existing shared-descriptor converters. Both TObjAddAnim and
   TObjAddAnimAll are wrapped because the same-TU call bypasses linker wrapping.
   Pointer tables and image/palette payload bytes stay untouched. Invalid
   archive table extents still abort loudly; provenance rules are not relaxed.

### Verification and honest visual status

- `pc/tests/run_renderer_tests.ps1 -Gpu`: all eight CPU executables and the
  Vulkan TEV GPU readback test pass. New regression cases cover light types,
  shared attenuation/positions, texture-animation chains and shared image/TLUT
  descriptors, repeat conversion, and native descriptors remaining unchanged.
- Native 32-bit incremental compilation of `pc_hsd_swap.c` and `mnmain.c`,
  followed by relinking with the new wrappers, passed. No clean full rebuild
  was performed in this session. Full build invocation remains below.
- New repeatable integration test, run from the repository root:
  `gdb -batch -x pc/tests/menu_smoke.gdb build/phase2/melee_host.exe`.
  It deliberately skips the intro and pulses Start through PADRead, with no
  scene override. It reached `mnMain_Scene_OnFrame`, `cur_menu=0`,
  `hovered_selection=0`, and stopped successfully after 240 menu frames.
  The debugger then intentionally terminates its test process.
- Inspected ignored `build/menu-smoke.bmp`, captured after menu frame 120:
  "Solo Smash!" is visible near the bottom, but the rest is black. This proves
  menu code runs and some text draws, NOT a complete or playable menu.
- Existing diagnostics still reject missing depth attachments, non-identity
  texgen/texture order, unsupported palette/depth/copy formats, more than four
  TEV stages, and unsupported blend modes/factors. Do not silence these or
  replace missing visuals with mocks. Logs/captures/binaries remain ignored.
- Full movie completion, interactive submenu navigation, and gameplay were
  not retested this session. Earlier frame-1000 movie evidence is historical.

### Next work

Prioritize the mostly missing menu rendering: inspect rejected menu draw state
and identify which unsupported GX feature blocks its actual panel/background
geometry. Use the repeatable menu test/capture to compare real rendering.
Also test normal keyboard navigation and submenu transitions before claiming
the menu is usable. Enter is Start (required at the title); J is A, K is B,
WASD is the analog stick. Announce deliberate movie skips and test stops before
launching visible tests so they are not mistaken for playback regressions.
The separate randomized attract-mode stage stops below have not been rerun
with these changes; texture-animation conversion may affect the Stadium stop,
but that has not been verified.

## Historical September 9 checkpoint: stage fixes; main menu light crash

This section supersedes the historical checkpoints below. Full opening-movie
playback was already working before this session; no movie decoder or renderer
runtime code was changed here. A fresh no-skip regression run reached movie frame
1000 in `lbMthp_8001F67C` / `__wrap_GXInitTexObj`, without an assert or crash,
then ended at an intentional debugger breakpoint. That is a bounded regression
check, not a new full-movie-completion claim.

### Changes in this checkpoint

- Fixed the native `StageCallbacks` flags aliases under `PC_GX_RENDERER`.
  Tables initialize a numerical u32 mask such as `0xc0000000`, but the old
  byte bitfields read the wrong bits on little-endian GCC. `flags_b0/b1` now
  map to bits 31/30, preserving the 20-byte structure and offset-16 flags.
  Ground now selects the archive light entry instead of the native fallback
  that caused the `ground.c:2526` assert. Console declarations are unchanged.
- Restored `map_ptcl` / `map_texg` conversion through the public-symbol hook.
  Preloaded stage archives call `psInitDataBankLoad` without Locate, so those
  banks need conversion before the public pointer reaches the caller. Existing
  provenance bookkeeping prevents double conversion on the normal Locate path.
- Diagnosed the conflicting-schema abort independently of the particle change.
  Kongo and Corneria requested `PC_HSD_MOBJ` (6) at an address previously marked
  `PC_HSD_STAGEENTRY` (22). `UnkStageDat::unk28` entries are shallow views of
  material descriptors: `unk4` aliases `HSD_MObjDesc::rendermode`. Stage setup
  converts and modifies that word before MObj loading. Both consumers now use
  one field-level `PC_HSD_MOBJ_FLAGS` claim, preserving the stage mutation and
  preventing a second swap. The full descriptor still gets its normal MObj
  claim; conflicting-schema rejection has NOT been weakened. Diagnostics now
  report the address and both schema kinds.
- Added flag-layout and both-material-load-order regression tests, public-symbol
  dispatch checks, and integrated stage/archive/particle/endian tests into
  `pc/tests/run_renderer_tests.ps1`.

### Priority blocker: actual main menu, not the attract-mode match

The title screen accepts **Start**, mapped to **Enter**, for the normal menu.
J maps to A: it can skip the opening, but at retail DbLevel 0 it does not enter
the normal menu from the title. Without Start, the title times out into the
attract-mode demo. Previous stage tests intentionally pressed A after movie
frame 10 to reach this demo quickly; this was the reason the user saw the
movie disappear, not evidence of a playback regression. Announce deliberate
skips and debugger stops before launching visible tests.

A targeted PADRead test sent Start pulses after title entry, through the game's
normal input path (no scene override). It reached `mnMain_Scene_OnEnter`, then:

```
SIGSEGV mn_8022C068(lobj=NULL, unused=0, div=0) mnmain.c:1789
  while (!(lobj->flags & LOBJ_POINT))
  mn_8022DDA8_inline -> mnMain_Scene_OnEnter (mnmain.c:2939)
  gm_801A4014 -> runGameMode(mode_kind=1) -> main
```

The point-light search reaches NULL. Next inspect `MenMain_lights`,
`lb_80011AC4`, and `HSD_LObjLoadDesc`: distinguish an empty list from incorrect
descriptor flags or a missing expected point light. No fix for this crash is
implemented or verified yet; do not hide it with a null guard. Menu rendering
also remains unverified. Runtime diagnostics still report unsupported depth,
texgen/texture formats, blending, and more-than-four-TEV-stage configurations.

### Separate, later attract-mode stage stops

With all three stage fixes above, Castle reached `grCastle_801CD658` and stopped
in `lb_8000FD48(jobj=NULL, max_count=100663296)` at `lb_00F9.c:171`, called by
`grLib_801C9B20`. The count is `0x06000000`, suggesting an unconverted big-endian
6 in `arg1->count`; this hypothesis is not yet fixed or proven.

An earlier flags-only test reached Stadium initialization and stopped in
`HSD_TlutLoadDesc(tlutdesc=0x09000000)` from `HSD_TObjAddAnim`, via
`grAnime_801C8138`. Audit the texture-animation descriptor/table/count conversion
if this recurs. This Stadium run preceded the shared-material-field fix.

Attract-mode stages are randomized. These observations show progress past the
previous stops, not that every stage loads or gameplay works.

### Verification and build

`powershell -File pc/tests/run_renderer_tests.ps1 -Gpu` passes the eight CPU
test executables plus the Vulkan TEV GPU readback test. Runtime checks above
used an incremental rebuild of the changed host objects and `ground.c`, then
relinked the native executable; no clean full rebuild was performed this session.

Full build from MSYS, from the repository root:

```sh
export PATH="/c/Users/Owner/msys64/mingw32/bin:$PATH"
CC=/c/Users/Owner/msys64/mingw32/bin/gcc.exe bash tools/phase0/linkexe.sh -m32 --renderer
```

Run `build/phase2/melee_host.exe` from the repository root. The user's ignored
`game.iso` must be there. Keep native recovered C, 32-bit pointers, real GX
interpretation, and strict unsupported-feature diagnostics. Do not commit game
assets, extracted data, binaries, or framebuffer captures. Do not run concurrent
builds against `build/phase2`.

## Historical checkpoint: two known stops in VS stage setup, plus one open question

Runs are NOT deterministic -- the attract-mode demo picks a stage at random,
and different stages take different paths -- so a single clean run proves
nothing. Sample several. Across the last set, three outcomes appeared:

**1. `ground.c:2526`, the most common.** `Ground_801C43C4` cannot find a
natively declared `HSD_LightAnim` in the stage's shadow-entry array:

```
HSD_ASSERT(3652, 0)  Ground_801C43C4 (arg0=Ground_803E069C) ground.c:2526
  Ground_801C466C () ground.c:2726 -> Ground_801C0800 ground.c:508
  Stage_8022524C () stage.c:520 -> fn_8016E730 gm_16AE.c:2007
```

Read this one before writing a schema for it, because it may not be one.
`arg0` is `Ground_803E069C`, a native `HSD_LightAnim[]` in ground.c reached
through the native fallback light list `Ground_803E06C8`. `Ground_801C466C`
picks that fallback only when no entry in `stage_datas[grkind]->callbacks` has
`flags_b0 == 1`, which is native game data, not archive data. So the question
is whether that selection is correct, and whether `UnkStageDat::unk24` -- the
shadow-entry count the search runs over, which only became a real number with
the map_head schema -- is right for this stage. A native fallback pointer will
never be in an archive-sourced array, so if both are correct then the assert
is reachable on console too and something further upstream is wrong.

**2. `particle.c:207`, "psInitDataBanks: unknown version".** A real gap, with a
known fix that is NOT in this commit. `grdatfiles.c` has two branches: the
normal one calls `psInitDataBank`, which calls `psInitDataBankLocate` (wrapped,
so the banks get converted) and then `psInitDataBankLoad`. The preloaded-
archive branch calls `psInitDataBankLoad` DIRECTLY, so the banks were never
converted and Load panics on a byte-reversed version.

Routing `map_ptcl` and `map_texg` through the `HSD_ArchiveGetPublicAddress`
symbol hook fixes it -- two `else if` arms calling
`pc_hsd_particle_banks_to_native(p, NULL, NULL)` and `(NULL, p, NULL)` -- and
that was tried and did remove this panic. It was reverted before committing
because it introduced item 3 below and there was no time to tell whether the
two are related. Redo it, then diagnose 3 with it in place.

**3. `pc_hsd_endian: descriptor outside archive or conflicting schema`.**
Appeared only in runs with the item-2 change applied, so it is probably caused
by it, but that is not established -- the run-to-run variation is large enough
that it could have been present already. This abort comes from `pc_hsd_claim`
finding a first word already claimed under a DIFFERENT schema kind, which
means two schemas disagree about what lives at one address. Get a backtrace on
the abort and print the address and both kinds before changing anything; do
not widen the claim rules to make it go away.

## What is verified this session

Archive conversion moved from DVD-read time to consumption time
(`pc/src/pc_hsd_archive.c`). This is the fix for the title staging blocker
recorded in the previous handoff, and it removes three separate defects that
all came from converting too early:

- A large archive arrives in several reads. Only the first saw `rel == 0`, and
  since its length was far below the archive's stated size the body-size check
  tripped and the relocation, public and extern tables were never converted at
  all. The recurring `pc_dvd: truncated HSD archive body` line was exactly
  this. It no longer appears in a full run.
- `devcom.c` does not read into final memory: it stages DVD chunks through two
  16 KB relay buffers and ARAM before copying them where the archive will live.
  Conversion at read time therefore ran against a relay buffer, and
  `pc_hsd_archive_body` correctly refused an address outside the game's RAM
  window. That refusal was the abort entering `gm_Scene_Title_OnEnter`. The
  buffer address was never the bug; the timing was.
- Provenance recorded against a staging buffer does not follow the bytes to
  their destination, so descriptor schemas would not have applied even if the
  address had been accepted.

An archive is whole, contiguous and final in exactly one place: when a consumer
parses it. `HSD_ArchiveParse` and `lbArchiveRelocate` are those places, and
every `.dat` consumer reaches one of them — `lbArchive_InitializeDAT`,
`efAsync_OnLoad` and `grDatFiles_801C5FC0` all funnel into the first. Both are
now wrapped.

Doing it exactly once needs no extra bookkeeping, because both callees already
compare the archive's own `file_size` field against the caller's size as a
byte-order check. An archive whose stated size already equals the caller's is
in host order and is left alone, so re-parsing a converted buffer — or
relocating a copy of one, which `ftdata.c` does to fighter animation data — is
a no-op rather than a second swap. That same property is why the DVD-time path
had to be removed rather than kept as a fast path for small files: a partially
converted header passes this test while its tables are still big-endian.

`__DVDLongFileNameFlag` is now published by `pc_os.c`'s `OSInit`, as the real
`OSInit` does unconditionally (OS.c:168). `dvdfs.c` enforces 8.3 filenames when
it is zero, and Melee ships names that do not fit — `PlKbNrCpDk.dat` among
them — so the SDK's own diagnostic panic was the stopping point the first time
Kirby's copy-ability data was opened. Same class as the `__OSBusClock` fix: a
console-init global nothing on the host was writing. Worth grepping for others.

The movie's frame-size guard moved from `fn_8001E910` to the two
`HSD_DevComRequest` calls that actually use the value. It read the next frame's
size out of the front of the buffer just filled, and at the end of the movie —
or once `lbMthp_8001F800` clears `unk_70` — there is no next frame, so the word
is whatever the last frame left behind. The game never uses it in that case.
Observed firing at `unk_70 = 0`, i.e. during shutdown, on a value that was
about to be discarded. Validating at the point of use keeps the real
protection: an oversized value there would overrun a frame buffer.

## Tests

`pc/tests/hsd_archive_test.c` — conversion happens exactly once and only when
it should: header, tables and only relocation-named body words converted;
`version[4]` and symbol strings left as bytes; a second call a no-op; a size
matching neither byte order leaving the archive completely untouched; a short
(partial) read refused rather than half-converted; and provenance recorded on
the body, at the parsed address, and forgotten on re-read.

```
gcc -m32 -w -O0 -g -std=gnu17 -include tools/phase0/compat.h \
    -I pc/src -I src -I extern/dolphin/include \
    pc/tests/hsd_archive_test.c pc/src/pc_hsd_archive.c \
    pc/src/pc_hsd_endian.c -Wl,--large-address-aware -o t.exe
```

Note for every test here: `-I extern/dolphin/include/libc` must be left out.
Its `assert.h` shadows the host's and defines no `assert`.

## Still not done — do not claim any of this

- No VS match, gameplay, title visuals, audio or saves. Reaching
  `gm_Scene_Vs_OnEnter` is a scene entry point, not a playable match.
- Audio remains entirely unimplemented and is independent of this track:
  `pc_ai` reports playback disabled and `pc_dsp` completes tasks immediately.
  HPS stream metadata converts correctly, but nothing decodes or outputs.
- Renderer still reports these during the opening scene, each skipping a draw:
  depth comparison without a depth attachment, texture order or non-identity
  texgen, more than four TEV stages, logic/subtract blending, and
  palette/depth/copy texture formats.
- The run is timing-dependent. Two runs from the same binary stopped in
  different places, so a single clean run is not evidence a path is fixed.

---

# Earlier handoffs — history, not current status

## Latest verification: movie frame 1000; next blocker is title asset staging

The longer run reached frame=1000, counter=2001, buffered=31, alarm.period=675000
without a crash/assert. This is ~33 seconds of source movie content, not full
intro completion. PADStatus was held neutral by GDB after MoviePlayer.power
became nonzero to prevent test-time button presses from skipping the movie.
The normal executable retains its keyboard input; there is no scene bypass.

Clean renderer rebuild linked successfully: 1141 objects, 34 existing placeholder
symbols, 92MB EXE. Renderer CPU/GPU, texture CPU/GPU, THP kernel and HSD descriptor
tests all pass. The unresolved placeholders are still a port-completeness limit,
not game-code recompilation or evidence of a finished executable.

Skipping normally with confirm input enters gm_Scene_Title_OnEnter and aborts:
`pc_hsd_archive_body(data=HSD_DevCom_804C6330_bufs+32, size=6960)`.
Call chain: gm_PreloadTitleDemo -> lbDvd_80018254 -> DVD completion ->
DVDLowRead -> swap_contents("PlKbAJ.dat", rel=0, length=16384) ->
swap_hsd_archive -> pc_hsd_archive_body. The two 16KB relay buffers are native
static storage outside 0x80000000..0x81800000, not malformed archive pointers.

Do NOT merely expand the provenance bounds or drop the assertion. devcom.c
stages DVD chunks through these buffers into ARAM, then copies them to final
memory. The current DVD-read-time archive conversion assumes a whole archive
in its final RAM location: it both converts too early and loses provenance
across transfers; larger split archives also leave tables unswapped. Audit
moving archive conversion to HSD_ArchiveParse / lbArchiveRelocate consumption
or explicitly tracking byte-order/provenance across staged transfers. Preserve
header-only consumers and ensure relocation words are converted exactly once.
This title-path fix is not implemented. Full opening completion, title visuals,
gameplay, sound and saves are still unverified/unimplemented as noted below.

---

## Latest renderer milestone: real opening-movie frame in Vulkan

The game-issued movie quad now renders its three decoded I8 planes through the
actual four-stage TEV program from sobjlib.c. An application framebuffer capture
at movie frame 90 visibly shows the blue sky/trophy scene. The user also saw the
intro in the window. The short appearance ended because the GDB capture test
finished, not because that test crashed. No external video player, fake frame,
scene bypass or preconverted game asset is involved.

Renderer changes: up to four ADD/SUB TEV stages, PREV/REG0/1/2 inputs/outputs,
per-stage konst selectors with separate konst storage, eight sampler bindings,
208-byte std140 material snapshots per draw, and front/back/all culling pipeline
variants. Texgens may select any generated coordinate that is identity TEX0;
arbitrary texture matrices and independent TEX1+ vertex coordinates still fail.
RGB565/RGBA4/RGBA6 vertex colors now decode rather than substituting white.

`pc/tests/run_renderer_tests.ps1 -Gpu` runs clocks/alarms, material/BP state,
immediate/packed-color decoding, and actual Vulkan readback of four synthetic
YUV triples plus same-frame material/texture lifetime and sprite culling tests.
GPU values agree with the TEV arithmetic reference within 1 byte for those
samples. Arithmetic remains floating-point, not bit-exact GX fixed-point;
rounding, unclamped register feedback/wrapping and broader materials need audit.
No Vulkan validation-layer coverage is claimed (layer unavailable here).

Next: longer uninterrupted opening playback (frame-90 capture is the current
verified endpoint), then remaining depth attachments/state and nonidentity
texgen support. An earlier run was interrupted around an archive-provenance
abort after user input; if this recurs, capture the full caller and archive
address before changing provenance bounds. No title-screen/gameplay/audio/saves
claim. All movie dumps and framebuffer captures remain ignored under build/.

---

## Latest: clock initialization fixed; content-bearing movie frames verified

`pc_bootinfo_init` now publishes the IPL bus/core clock words (162/486 MHz).
`pc_clock.h` shares those values with the host timebase. The nanosecond-to-tick
conversion also no longer overflows after ~455 seconds, and its epoch handles
a monotonic source initially returning zero.

Real runs reached decoded frames 10 and 90. GDB at the movie's GXInitTexObj:
alarm.period=675000, handler=fn_8001F2A4, frame=90, counter=180, buffered=31.
The user provided confirm input during these runs; no scene-state override.
Frame 90's dumped Y/U/V planes were untiled and converted for diagnostic viewing:
recognizable blue sky/trophy imagery, not a flat frame. This confirms real
content decoding, NOT bit-exact agreement with console output. Dumps/previews
stay in ignored build/ and are not committed. Still no movie pixels on-screen:
the next task is interpreting the four-stage TEV program in sobjlib.c.

`pc/tests/os_time_test.c` passes startup-word/SDK conversion tests, a simulated
24-hour timebase, repeated 60 Hz periodic callbacks, catch-up, cancellation and
one-shot delivery. Link pc_bootinfo.c and pc_os_time.c with the usual 32-bit host
test flags (no SDK libc include), compat.h, and --large-address-aware.
Clock consumers audited include lb_0195.c frame/pad pacing, lbtime.c, gmopening.c
performance reporting, and SDK device timeouts. Calendar dates are still the
existing approximate host implementation; not fixed or claimed here.

---

## Previous handoff (clock blocker below is now resolved)

## Current blocker: the movie decodes but never advances past frame 0

`__OSBusClock` at 0x800000F8 is zero. Nothing in the host layer ever writes it
(the console's bootrom does), so `OS_TIMER_CLOCK` — defined as
`__OSBusClock / 4` — is zero too, and every `OSSecondsToTicks(x)` returns 0.
`lbMthp_8001F410` arms the movie's frame-pacing alarm with
`OSSetPeriodicAlarm(&alarm, OSSecondsToTicks(1.0f/60), OSSecondsToTicks(1.0f/60),
fn_8001F2A4)`, so it arrives with `period == 0`. `pc_alarm_poll` treats a zero
period as one-shot: it fires the handler once and calls `OSCancelAlarm`.

Verified in gdb at `lbmthp.c:637`: `__OSBusClock=0`, `alarm.period=0`,
`alarm.fire=0`, `alarm.handler=(nil)`, `MoviePlayer.unk_80=1` (so the handler
did run exactly once), `unk_78=0` (the frame index never advanced),
`unk_108=31` — thirty-one frames are buffered and ready, so this is not a
streaming or DVD problem. Dumping the decoded planes at the 91st call of
`lbMthp_8001F67C` gives a plane byte-identical to the first: the decoder is
re-decoding frame 0 forever.

Fix `__OSBusClock` (and `__OSCoreClock` beside it at 0xF8/0xFC) at host init —
GameCube values are a 162 MHz bus and a 486 MHz core, giving the 40.5 MHz timer
the whole OSTime layer already assumes. Audit what else silently divides by
`OS_TIMER_CLOCK`: every `OSTicksToSeconds`/`OSMillisecondsToTicks` user has been
running against zero, so other timing that looked "close enough" may also be
wrong rather than merely coarse. Do NOT special-case a zero period inside
`pc_alarm_poll` — that hides the real defect and leaves the rest of the timing
layer broken.

**A black screen is currently the correct output and not evidence of anything
working.** Frame 0 of MvOpen.mth genuinely decodes to black, and nothing draws
YUV to the framebuffer yet (see "still not done" below).

## What is verified this session

Native THP decoding, replacing kernels that existed only as MWCC assembly:

- `pc/src/pc_thp_kernels.h` holds native C for the Huffman/receive entropy
  decoder and the AAN inverse DCT. `extern/dolphin/src/dolphin/thp/THPDec.c`
  calls them from `#else` branches of the same `#ifdef __MWERKS__` guards that
  select the assembly, so the MWCC paths are byte-for-byte unchanged.
- The IDCT is libjpeg-6b's `jidctflt.c`. That is not a design choice — reading
  the paired-single kernel out shows identical AAN constants and an identical
  `__THPAANScaleFactor` table. libjpeg's 1/8 is absent from the quantisation
  table because the quantised store recovers it (GQR6 = 0x3D04, type u8, scale
  −3), which is also where the level shift lands: the column pass adds 1024 and
  1024/8 is JPEG's +128.
- The IDCT writes GX_TF_I8 tile order directly (8x4 tiles; rows 8 bytes apart;
  a 32-byte group every 8 columns). That is why `lbMthp_8001F67C` can hand the
  plane straight to `GXInitTexObj` as `GX_TF_I8` — nothing re-tiles it.

Three further defects were in the way, each of which alone produces a
plausible-looking failure:

- **Bitstream byte order.** The decoder caches a 32-bit *big-endian* word in
  `info->currByte` and consumes it MSB first; every console refill is a `lwz`,
  and `__THPPrepBitStream`'s plain `info->currByte = *ptr` compiled to a
  host-order load. A correct Huffman decoder alone would still have decoded
  every code from reversed bytes. Audited the rest of the THP path: markers,
  quantisation tables and Huffman bit counts are all read a byte at a time and
  were already correct either way, so this word cache was the only exposure.
- **Adjacent globals.** `THPInit` writes `__THPLC` and `__THPLCWork672` through
  one `struct THPInitWork`, which works only because the console linker placed
  them next to each other — `work672` lands past the end of `__THPLC`. A native
  linker orders independent globals freely, so that store went somewhere else
  and `__THPLCWork672`, the base every decoded MCU row is written through,
  stayed null. Now declared as one object, the same fix `card_host_storage.h`
  makes for the memory-card context. The locked cache itself became ordinary
  memory rather than the hardcoded 0xE0000000.
- **`LCStoreData` was a no-op**, on the grounds that x86 caches are coherent.
  It is a DMA, not cache maintenance: `__THPDecompressiMCURow*` decodes a whole
  MCU row into locked-cache scratch and stores it into the frame plane, so the
  stub discarded every decoded pixel. It and the other LC transfers now copy.

Against the real `MvOpen.mth`, with no controlled input: the decoder runs to
completion, `__THPBitStreamFailed` never fires, and execution reaches
`GXInitTexObj`. Frame 0 decodes to Y=16, U=V=128 — black — with a Y=0 border
whose edges fall on rows 19 and 462. Those rows are **not** tile-aligned, so
those pixels came from decoded coefficients rather than unwritten memory. That
is the strongest evidence available that the decode is real, and it is not the
same thing as having seen a correct picture: no frame with actual content has
been decoded yet, because of the pacing blocker above.

Material and texture descriptor byte order (`pc_hsd_swap.c`):

- `DObjLoad` panicked at `dobj.c:312` on `rendermode & 0x60000000` hitting
  `default` with 0x31001060, which byte-swaps to a valid 0x60100031. New
  schemas convert `HSD_MObjDesc`, `HSD_Material`, `HSD_TObjDesc`,
  `HSD_ImageDesc`, `HSD_TlutDesc`, `HSD_TexLODDesc` and `HSD_TObjTevDesc`,
  wrapped at `HSD_MObjLoadDesc` and `HSD_TObjLoadDesc` (both, because texture
  animation reaches the latter without a material; `pc_hsd_claim` makes
  whichever runs second a no-op).
- Byte fields are deliberately untouched: `HSD_PEDesc`, the repeat flags, every
  `GXColor`, and all of `HSD_TObjTevDesc` except `active` have no byte order to
  get wrong. Pointer fields were already converted by `pc_dvd.c`'s relocation
  pass.
- The panic is gone and the opening scene now runs continuously for at least 90
  seconds without halting.

## Tests

- `pc/tests/thp_kernel_test.c` — standalone, 32-bit, known answers throughout:
  big-endian word loads, MSB-first receive across word boundaries, sign
  extension against JPEG Table F.2, codes of 1..7 bits through both the quick
  table and the canonical fallback, a code starting at every bit offset from 26
  to 32 so each word-boundary branch of the console decoder is covered, an
  unmatchable code reported rather than guessed, zig-zag placement and DC
  prediction, GX I8 tile addressing, and the IDCT against a double-precision
  textbook transform (exact — worst deviation 0 counts over 64 random blocks).
  It found a real bug: the canonical search terminated on the `maxCode[17]`
  sentinel and then indexed `Vij` with a 17-bit code.

  ```
  gcc -m32 -std=c11 -Wall -Wextra -Werror -O2 -I pc/src \
      -include tools/phase0/compat.h pc/tests/thp_kernel_test.c -o t.exe
  ```

- `pc/tests/hsd_endian_test.c` — extended with the material graph, including
  the real 0x31001060 rendermode, and asserting that byte and pointer fields
  come through untouched. Note the include list: `-I extern/dolphin/include/libc`
  must be **left out**, because its `assert.h` shadows the host's and defines no
  `assert`.

  ```
  gcc -m32 -w -O0 -g -fgnu89-inline -std=gnu17 \
      -Wno-error=implicit-function-declaration \
      -Wno-error=incompatible-pointer-types -include tools/phase0/compat.h \
      -I src -I extern/dolphin/include -I extern/dolphin/src -I pc/src \
      -DVERSION_GALE01 -DBUILD_VERSION=0 -DPC_GX_RENDERER \
      pc/tests/hsd_endian_test.c pc/src/pc_hsd_endian.c pc/src/pc_hsd_swap.c \
      -Wl,--large-address-aware -o t.exe
  ```

- `pc/tests/thp_plane_dump.py` — untiles a plane dumped from a running process
  and reports its histogram, row means and optionally a PPM. Diagnostic only.
  Dump with gdb into `build/` (gitignored); never commit a decoded frame.

## Still not done — do not claim any of this

- **No movie pixels reach the screen.** `lbMthp_8001F67C` binds three
  `GX_TF_I8` texobjs — Y at full resolution on TEXMAP0, U and V at half on
  TEXMAP1/TEXMAP2 — and the renderer has no YUV→RGB path across three texmaps.
  This is the third task from the original brief and is still entirely ahead.
- No frame with real picture content has been decoded, so the decoder is
  verified against synthetic known answers and against frame 0's structure,
  **not** against a photograph. Re-dump a mid-movie frame once pacing works and
  check it is not flat before trusting it.
- The renderer still reports these unsupported cases during the opening scene,
  each of which skips a draw: face culling, RGB565/RGBA4/RGBA6 vertex colour
  formats, TEV PREV/REG1/REG2/konst inputs, multiple TEV stages, logic/subtract
  blending, and palette/depth/copy texture formats.
- `pc_dvd: truncated HSD archive body` still appears three times during the
  opening scene. Unaudited.
- No audio, no saves, no gameplay, no title screen.

## Recent additions

The window title now carries the presented frame rate, counted from real
`vkQueuePresentKHR` calls rather than the game's internal frame counter.
`tools/phase0/linkcheck.sh` and `survey.sh` gained `-I pc/src` so they still
compile `THPDec.c`.

---

# Earlier handoffs — history, not current status

## Latest: HPS stream metadata fixed; opening movie header is next

Audio metadata now converts at the three stream-header consumption callbacks.
Observed raw HPS header magic was " HALPST", rate=32000, channels=2 (the earlier
one-channel hypothesis below was not correct). Native code had truncated
0x02000000 to an 8-bit voice_count=0. Only header u32 fields and AX 16-bit
records are swapped; ADPCM payloads remain raw. `hps_endian_test.c` passes.
The controlled input run now gets beyond AXSetVoiceMix into MvOpen.mth loading.

Next crash: lbmthp.c fn_8001ECF4 receives NULL allocation because movie header
width/height/buffer sizes are still big-endian. Convert the MTH metadata and
frame-size words with validated bounds. THPDec.c also contains MWCC-only
paired-single assembly with missing native paths: audit before treating decoded
movie output as valid. Do not silently substitute blank/video placeholders.

---

## Beyond the text prompt: card-context layout fixed

Controlled PADStatus A-button pulses through the real menu code now reach
`gm_Scene_Opening_OnEnter`. This is not physical-keyboard verification or a
game-state bypass. Keyboard mappings: J=A, K=B, Enter=Start, WASD=stick,
arrows=D-pad, L=X, I=Y, U=Z, Space=L-trigger. Run from repo root:
`./build/phase2/melee_host.exe` (PowerShell uses `.\build\phase2\melee_host.exe`).

The next crash after confirming no-card was card-context storage, not card I/O:
the code assumes three independently declared globals occupy one contiguous
0x1510-byte context. The native linker put the command array BEFORE its header,
so base+0x10 read unrelated `hsd_804D799C=2` as a command with a NULL state.
`card_host_storage.h` now defines a single aggregate with asserted offsets
0x10 and 0x1210, retaining the original console definitions outside renderer
builds. `card_storage_test.c` exercises all 128 command positions.

Next observed crash is opening-scene streaming audio: HSD_SynthPStreamHeaderCallback
reads raw big-endian HPS metadata as native (`voice_count` becomes zero from
0x01000000; sample ratio becomes 256). A later AXSetVoiceMix receives NULL voice[1].
Fix the stream metadata schema, not a NULL-voice guard or fake audio output.
Some `.dat` header-only reads also log "truncated HSD archive body"; audit partial
archive reads separately. The initial text remains visually verified, but no
opening scene or gameplay is verified yet.

---

## Latest VERIFIED checkpoint: readable native menu text

The SIS endian blocker below is fixed. A Vulkan framebuffer readback was
visually inspected and displays **"There is no Memory Card in Slot A."** in
the actual game's font, with transparent glyph backgrounds and readable
two-line layout. This is game-issued geometry and runtime-loaded font data,
not mock visuals. The fourth frame-end breakpoint reached the normal memory-card
menu loop without the invalid-source exit. No gameplay or broader menus verified.

`sis_byteorder.h` provides renderer-host big-endian reads while retaining the
original console dereferences. All 18 SIS 16-bit loads in hsd_3A76.c now use
these readers: glyph rendering AND measurement, scales, signed positions and
spacing, delays and saved-state restoration. Saved pointers are read in the
same big-endian byte order that the existing stack writer emits. Archive-
relocated jump pointers remain native and are deliberately not swapped again.
Producer audit: hsd_3A64.c writes coordinates/scales explicitly high byte first;
hsd_3A76.c does the same for saved values/pointers. Texture bytes stay raw.

Regression: `pc/tests/sis_byteorder_test.c` covers odd alignment, glyph 0x201d,
negative spacing, fixed-point scale and saved-pointer round trips. Compile with
32-bit GCC, `-DPC_GX_RENDERER`, compat.h and normal host/game include paths.

Next work: test continuation/input through the actual memory-card prompt and
audit newly encountered material/descriptor state. Do not assume correct
gameplay from a correctly rendered text message. Texture/TEV subset limitations
documented below still apply. No extracted assets or frame captures committed.

---

## Latest WIP: texture/shader wiring; current blocker supersedes status below

New code loads the native font atlas from the user's GALE01 revision-2 disc
DATA section only (no executable-code translation or extraction). It preserves
bounded native GXTexObj sources, decoded UVs, immutable per-draw descriptors,
and BP register state. The new shader implements a validated single-stage TEV
subset, alpha comparison and source-alpha blending/write masks. Regenerate
SPIR-V and embedded headers with `pc/shaders/regenerate.ps1`. No assets committed.

**Readable text is NOT verified.** The latest build now fails explicitly at
`pc_gx_render: unknown native texture source`, called from
`HSD_SisLib_803A84BC`, hsd_3A76.c:896. GDB: glyph_idx=0x1d20,
tex_offset=0xfffffd20, source=font_base+0x1fa4000. At line 830,
`glyph_idx = *(u16*)sis_cursor` reads the SIS byte stream in native little-endian;
the expected glyph ID is 0x201d. Do NOT widen source bounds to accept this pointer.

Next: audit SIS reader AND producer byte order, including text measurement/layout
and inline signed/unsigned 16-bit parameters; fix consistently, then rebuild and
capture the actual menu. Avoid fixing only one glyph read while leaving spacing
and text-control parameters backward. The earlier white boxes remain the last
visually confirmed checkpoint, not evidence that this shader already works.

Clean renderer build linked: 1141 objects, 34 placeholders. Startup confirmed
`font atlas loaded from user disc data`, then the source-bounds failure above.
Texture CPU/GPU readback, direct-UV decoder, and material-state tests pass.
The material-state guards were tightened/unit-tested after the full build.
Vulkan validation layer unavailable. New `gx_material_test.c` tests BP masks,
konst/TEV register separation, menu state and unsupported-state rejection.

This is NOT complete/bit-exact GX: extra TEV stages, PREV/REG1/REG2/konst inputs,
compare ops, nonidentity texgen/swap, fog and most blend/depth states remain
unsupported and diagnosed. Preserve this checkpoint before further changes.

---

Checkout: `C:\Users\Owner\melee`, branch `claude/sync-branch-update-pr033u`.
The texture branch `codex/gx-texture-upload` was merged in `08c5ba7b7`.
The older handoff below is retained as history, **not current blocker/status**.

## Current evidence

- Native Windows EXE initializes Vulkan and reaches the memory-card menu loop.
  The former unresolved-joint assertion and subsequent GX finish wait are fixed.
- Real menu GX immediate geometry now reaches Vulkan. Application framebuffer
  readback was visually inspected: white glyph-sized rectangles on black. These
  are game-issued geometry, **not legible text, a correct menu, or gameplay**.
- At the fourth frame-end breakpoint, three frames had been submitted and the
  current frame contained 162 expanded vertices. Stack:
  `HSD_VICopyXFBAsync -> gm_801A4D34 -> runGameMode`.
- Texture CPU decode and Vulkan upload/readback tests pass for I4/I8/IA4/IA8,
  RGB565/RGB5A3/RGBA8/CMPR, four mip levels, cache and rebinding lifetime.
  The Vulkan validation layer was unavailable; do not claim a validation pass.
- The basic shader still uses position/color only. Texture upload is implemented
  but texture sampling/TEV/depth state are not. First draw logs this limitation.
- No gameplay, audio, or correct textured rendering has been verified.

## Changes since the previous handoff

`6db4637ea`: tiled texture decoders, immutable Vulkan image uploads/cache/samplers,
tests, `pc/TEXTURES.md`, MIT reference license under `pc/licenses`.

`7b5c87fc5`: archive-provenance/once-per-schema endian conversion for PObj chains,
vertex metadata, envelope weights, joint trees and matrices. The missing joint
was a misread union: observed flags `0x01a0` swap to `0xa001` (envelope), with an
envelope pointer array at the supposed joint address. Relocated pointers must
NOT be swapped again. The next stall was GXWaitDrawDone; native completion now
waits for the real Vulkan queue and delivers SDK interrupt 19, preserving the
SDK FinishQueue/callback mechanism.

Latest checkpoint:

- Renderer-only hooks capture release GX inline vertex MMIO stores and feed
  their actual values into the shared display-list decoder.
- Draws use separate vertex-buffer ranges until the frame fence, so later
  uploads cannot overwrite earlier queued geometry.
- Corrected color enums and GX-to-Vulkan clip-space conversion; converted
  archive WObj camera positions once.
- Restored existing MSL float/math tables to the Windows build and called the
  retained trig initializer. Excluding them had created zero-filled stubs,
  collapsing geometry and producing NaN projection values. Projection is now
  finite, and real first-quad positions span approximately (-13.921, 9, -64)
  to (-11.393, 6.472, -64).
- Optional application-only GPU readback:
  `PC_CAPTURE_FRAME=build/phase2/first-frame.bmp ./build/phase2/melee_host.exe`
  from repo root in MSYS2. Captures the first frame containing a draw, skipping
  clear-only frames. Uses Win32 I/O to avoid game MSL/host CRT stdio conflicts.
  Do not commit captures or other game data. Full builds recreate build/phase2.

## Next work

1. Preserve texture coordinates in decoded vertices and bind uploaded images
   and samplers to shaders. Implement the GX/TEV state actually issued by the
   menu, not a hardcoded appearance or mock visuals.
2. Resolve `GXLoadTexObj: source outside RAM`: native font symbols currently use
   placeholder data when generated headers are absent, and GX packed physical
   addresses cannot round-trip arbitrary host pointers. Load required font data
   from the user's disc at runtime and retain valid pointer mappings; never
   commit extracted assets.
3. Implement viewport/scissor, blend/alpha/depth/cull, matrix/texgen and broader
   TEV support with tests and actual frame checks. Keep unsupported cases loud.
4. Audit remaining material/image/animation descriptor byte order and provenance
   invalidation on freed/reused archive memory. One menu loop does not establish
   that all game modes work.

Build invocation and constraints below still apply. Use mandatory `-m32`; run
from repo root. Never run two linkexe builds against this checkout at once.
A timeout in the menu loop is not by itself a boot failure: inspect logs and
backtrace. Do not trust stale incremental object lists after source inclusion
changes. No recompilation/emulation runtime is permitted.

Tests: `pc/tests/run_texture_tests.ps1 -Gpu`, plus standalone
`pc/tests/hsd_endian_test.c` (link pc_hsd_endian.c, pc_hsd_swap.c and
`--large-address-aware`) and `pc/tests/gx_immediate_test.c` (link Vulkan).
Use 32-bit GCC, normal game include paths, `-fgnu89-inline`, and compat.h.
Synthetic decoder tests do not substitute visuals in the running game.

---

# Historical handoff — superseded status below

Branch: `claude/sync-branch-update-pr033u` · last commit `a276d7fbd`

## Goal (unchanged)

Replace the trace-only GX layer with a real native Vulkan renderer, compiling
the recovered doldecomp C source directly. No DolRecomp, no static
recompilation, no CPU emulation, no Dolphin as a runtime, no generated
PowerPC-to-C game code. No game data committed — the user supplies their own
legally dumped GALE01 1.02 image at runtime (`game.iso` in the repo root, now
gitignored).

## Build and run

The toolchain is MSYS2's **32-bit** MinGW gcc. It is not on `PATH` by default,
and its location is not the usual `C:\msys64`:

```sh
export PATH="/c/Users/Owner/msys64/mingw32/bin:$PATH"
cd ~/melee
CC=/c/Users/Owner/msys64/mingw32/bin/gcc.exe \
  bash tools/phase0/linkexe.sh -m32 --renderer
```

`-m32` is mandatory. The script defaults to `-m64`, `mingw32`'s cc1 has no
64-bit codegen, and the port must stay 32-bit regardless: the decomp's
`ASSERT_SIZE`-checked structs contain pointers and have to match GameCube's
32-bit layout.

**Run from the repo root**, not from `build/phase2` — `pc_dvd.c` looks up
`game.iso` by relative path, and running elsewhere silently mounts nothing and
fails much later with a confusing `DVDReadAsync` range assert:

```sh
cd ~/melee && ./build/phase2/melee_host.exe
# or, for a backtrace:
cd ~/melee && gdb -batch -ex run -ex "bt full" build/phase2/melee_host.exe
```

To inspect a live spin rather than a crash, launch the exe in the background,
find its pid with `tasklist`, and `gdb -batch -ex "thread apply all bt" -p
<pid>`. `gdb`'s own `-ex interrupt` does not work here — it breaks into a
Windows debug-break thread, not the game thread.

## Where boot currently stops

```
assertion "pobj->u.jobj" failed in src/sysdolphin/baselib/pobj.c on line 411.
```

`HSD_PObjResolveRefs` takes the `POBJ_SKIN` branch and calls
`HSD_IDGetData((u32) pdesc->u.joint, NULL)`, which returns NULL, so the
assert fires. Established so far:

- The failing descriptor is at `0x80308790` with `u.joint == 0x80308780`,
  `flags == 128 (0x0080)`, `n_display == 256 (0x0100)`.
- Joints register themselves in the ID table from `JObjLoad`
  (`src/sysdolphin/baselib/jobj.c:662`, `HSD_IDInsertToTable(NULL, (u32) joint,
  jobj)`), keyed by the joint's own address. The lookup misses, so *that*
  joint was never passed through `JObjLoad`.
- A breakpoint conditioned on `id == 0x80308780` in `HSD_IDInsertToTable`
  never fires, confirming the miss rather than a hash/table bug.

**Two live hypotheses, not yet distinguished** — this is exactly where to pick
up:

1. **Byte order.** `n_display == 256` byte-swaps to 1, which is a far more
   plausible display-list count than 256, and `flags == 0x0080` swaps to
   `0x8000`. Both `pobj_type()` readings happen to yield `POBJ_SKIN`
   (`flags & 0x3000 == 0`) either way, so the type is not proof of correct
   order. If `HSD_PObjDesc` bodies are still big-endian, `u.joint` may be a
   relocated-but-misread pointer. Note `u.joint` (`0x80308780`) is only 16
   bytes below the descriptor itself — too close for a real `HSD_Joint` to fit
   without overlapping, which is *suggestive* of a bad pointer but not
   conclusive, since the joint could legitimately live elsewhere in the
   archive.
   The fix, if this is it, follows the established pattern in
   `pc/src/pc_hsd_swap.c`: add a `swap_pobj_desc()` with a self-consistency
   guard (as `swap_cobj_desc()` uses `projection_type`'s valid range) and wrap
   `HSD_PObjLoadDesc`. Add the `--wrap` flag next to the existing
   `HSD_CObjInit`/`HSD_CObjLoadDesc`/`HSD_PadGetRawQueueCount` ones in
   `tools/phase0/linkexe.sh` (~line 188).

2. **Load ordering.** `HSD_JObjLoadJoint` loads the whole joint tree (which
   registers every joint) *before* `HSD_JObjResolveRefsAll`. If this
   particular pobj is resolved from a different entry point, its joint may
   genuinely not be loaded yet. Get the backtrace at the assert to see which
   top-level call this resolve sits under.

The fastest way to separate the two: dump memory at `0x80308780` and check
whether it looks like a plausible `HSD_Joint` in host order, in big-endian
order, or neither. A gdb Python script over `default_table` (in
`src/sysdolphin/baselib/id.c`) enumerating the registered ids and comparing
their range against the target will also say quickly whether the target is
merely absent or wildly out of range. An attempt at this script is in
`/tmp/dumpids.py`; it was written but its output was never successfully
captured, so re-run it rather than trusting it.

## What was fixed this session (all in `a276d7fbd`)

Three distinct host-layer gaps, each blocking boot before any GX drawing call:

1. **OSAlarms were never delivered.** `pc_os_time.c` recorded them and relied
   on a decrementer interrupt that has no host equivalent. Added
   `pc_alarm_poll()`, called from `pc_vi_tick()` in `pc_os_thread.c` alongside
   the ARAM completions already delivered there. `OSSetPeriodicAlarm` now
   walks its first fire past `now` by whole periods, matching the real
   `InsertAlarm`.

2. **The pad queue had no writer.** `gm_801A4D34`'s boot loop spins on
   `HSD_PadGetRawQueueCount()` without ever sleeping or yielding, so no frame
   tick runs during it. The count's only writer, `HSD_PadRenewRawStatus`, is
   driven on hardware by the SI poll-complete interrupt — and
   `dolphin/pad/pad.c` is excluded from this build while `pc_pad.c`'s
   `PADRead` is synchronous, so neither driver existed. New file
   `pc/src/pc_pad_alarm.c` wraps the entry point and renews raw status there.
   Verified via gdb that `lb_80019628`'s alarm never arms on this path, so
   fixing the alarm queue alone was *not* sufficient — worth knowing before
   assuming (1) covers (2).

3. **`sqrtf` recursed into itself.** At `-O0` GCC lowers
   `__builtin_sqrt(f)` to a *call* to the libc function of that name rather
   than to an instruction; since `pc_libc.c` defines that function, it called
   itself until the stack died. Replaced with inline `fsqrt`; `floor` moved to
   portable C for the same reason (`frndint` follows the FPU rounding-control
   word, not round-toward-negative-infinity).

The general shape — a deferred-completion or interrupt-driven mechanism that
the host layer records but never delivers — has now accounted for the ARAM,
DVD, shared-guard, alarm, and pad-queue blockers. It is the first thing to
suspect on the next stall (as opposed to crash).

## Renderer status

Built and infrastructure-validated, but **not yet visually confirmed against
real game content**, because boot has never reached a drawing call.

- `pc/src/pc_vulkan.c` — Win32 window + instance/device/swapchain/render
  pass/framebuffers/command buffer/sync. Validated standalone (120 presented
  frames, no validation errors) and confirmed initializing during real boot
  (`pc_vulkan: window and swapchain ready` prints every run).
- `pc/src/pc_gx_render.c` — the `__wrap_GX*` entry points; opens a frame
  lazily, clears to the game's `GXSetCopyClear` color, presents on
  `GXCopyDisp`.
- `pc/src/pc_gx_fifo.c` — display-list decoder: CP/XF/BP register loads,
  vertex descriptors and attribute formats, direct and indexed position and
  color0, into a fixed position+vertex-color Vulkan pipeline. Unsupported
  cases log once each rather than guessing or crashing.
- `pc/GX_RENDERER.md` — the design note; read this before extending any of
  the above.

Only `__wrap_GXCopyDisp` (from `HSD_VIInit`'s initial framebuffer clear) has
ever fired. Breakpoints on `__wrap_GXBegin`, `__wrap_GXCallDisplayList` and
`pc_gx_fifo_exec` confirm none of them are reached before the pobj assert.
**The renderer is not the bottleneck; boot progress is.**

## Remaining plan

Steps 6–11 of the original brief, in order: finish GX state tracking
(viewport/scissor/depth/blend/cull — the pipeline currently hardcodes
`VK_CULL_MODE_NONE` and has no depth test), then texture upload and texcoord
generation, then TEV combiner stages as shaders, then fog/alpha test/render
targets.

Texture upload (step 8) is the one piece that is genuinely independent of the
boot blocker and can be worked in parallel — GameCube texture format decoding
(I4/I8/IA4/IA8/RGB565/RGB5A3/RGBA8/CMPR) into Vulkan images behind
`__wrap_GXLoadTexObj`, which currently only passes through. If two agents work
at once, use a separate branch or `git worktree`, and do not run
`tools/phase0/linkexe.sh` concurrently — it writes to a shared `build/phase2`.

Shaders are compiled ahead of time and embedded as C arrays (`pc/shaders/`,
`pc/src/gx_basic_*_spv.h`) because no 32-bit `glslc`/`shaderc` exists. Whether
TEV becomes runtime-compiled shaders or a pre-generated permutation set is
still an open decision.

## Constraints to keep

- Never commit or distribute the ISO, DOL, extracted assets, fonts, textures,
  or any copyrighted game data. `*.iso`/`*.gcm` are gitignored as of this
  commit; do not add exceptions.
- Do not replace the game's rendering with mock visuals — interpret the actual
  GX commands and state the game issues.
- Unsupported GX features must fail visibly and diagnostically, never silently
  produce wrong output. `pc_gx_fifo.c`'s one-shot `static int warned` logging
  is the established pattern.
- Aurora (`encounter/aurora`, MIT) is an algorithmic and format reference
  only, credited in `pc_gx_fifo.c`'s header. It is never linked — it needs
  Dawn/WebGPU, which has no 32-bit build.
